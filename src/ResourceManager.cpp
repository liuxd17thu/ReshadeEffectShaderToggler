#include <format>
#include "ResourceManager.h"

using namespace Rendering;
using namespace reshade::api;
using namespace Shim::Resources;

ResourceShimType ResourceManager::ResolveResourceShimType(const std::string& stype)
{
    if (stype == "none")
        return ResourceShimType::Resource_Shim_None;
    else if (stype == "srgb")
        return ResourceShimType::Resource_Shim_SRGB;
    else if (stype == "ffxiv")
        return ResourceShimType::Resource_Shim_FFXIV;

    return ResourceShimType::Resource_Shim_None;
}

void ResourceManager::Init()
{

    switch (_shimType)
    {
    case Resource_Shim_None:
        rShim = nullptr;
        break;
    case Resource_Shim_SRGB:
    {
        static ResourceShimSRGB srgbShim;
        rShim = &srgbShim;
    }
        break;
    case Resource_Shim_FFXIV:
    {
        static ResourceShimFFXIV ffxivShim;
        rShim = &ffxivShim;
    }
        break;
    default:
        rShim = nullptr;
        break;
    }

    if (rShim != nullptr && rShim->Init())
    {
        reshade::log_message(reshade::log_level::info, std::format("Resource shim initialized").c_str());
    }
    else
    {
        reshade::log_message(reshade::log_level::info, std::format("No resource shim initialized").c_str());
    }
}

void ResourceManager::InitBackbuffer(swapchain* runtime)
{
    // Create backbuffer resource views
    device* dev = runtime->get_device();
    uint32_t count = runtime->get_back_buffer_count();

    resource_desc desc = dev->get_resource_desc(runtime->get_back_buffer(0));

    for (uint32_t i = 0; i < count; ++i)
    {
        resource backBuffer = runtime->get_back_buffer(i);

        resource_view backBufferView = { 0 };
        resource_view backBufferViewSRGB = { 0 };

        reshade::api::format viewFormat = format_to_default_typed(desc.texture.format, 0);
        reshade::api::format viewFormatSRGB = format_to_default_typed(desc.texture.format, 1);

        dev->create_resource_view(backBuffer, resource_usage::render_target,
            resource_view_desc(viewFormat), &backBufferView);
        dev->create_resource_view(backBuffer, resource_usage::render_target,
            resource_view_desc(viewFormatSRGB), &backBufferViewSRGB);

        s_sRGBResourceViews.emplace(backBuffer.handle, make_pair(backBufferView, backBufferViewSRGB));
    }
}

void ResourceManager::ClearBackbuffer(reshade::api::swapchain* runtime)
{
    device* dev = runtime->get_device();

    uint32_t count = runtime->get_back_buffer_count();

    for (uint32_t i = 0; i < count; ++i)
    {
        resource backBuffer = runtime->get_back_buffer(i);

        const auto& entry = s_sRGBResourceViews.find(backBuffer.handle);

        // Back buffer resource got probably resized, clear old views and reinitialize
        if (entry != s_sRGBResourceViews.end())
        {
            resource_view oldbackBufferView = entry->second.first;
            resource_view oldbackBufferViewSRGB = entry->second.second;

            if (oldbackBufferView != 0)
            {
                runtime->get_device()->destroy_resource_view(oldbackBufferView);
            }

            if (oldbackBufferViewSRGB != 0)
            {
                runtime->get_device()->destroy_resource_view(oldbackBufferViewSRGB);
            }
        }

        s_sRGBResourceViews.erase(backBuffer.handle);
    }
}

bool ResourceManager::OnCreateSwapchain(reshade::api::swapchain_desc& desc, void* hwnd)
{
    return false;
}

void ResourceManager::OnInitSwapchain(reshade::api::swapchain* swapchain)
{
    InitBackbuffer(swapchain);
}

void ResourceManager::OnDestroySwapchain(reshade::api::swapchain* swapchain)
{
    ClearBackbuffer(swapchain);
}

bool ResourceManager::OnCreateResource(device* device, resource_desc& desc, subresource_data* initial_data, resource_usage initial_state)
{
    if (rShim != nullptr)
    {
        return rShim->OnCreateResource(device, desc, initial_data, initial_state);
    }
    
    return false;
}

void ResourceManager::OnInitResource(device* device, const resource_desc& desc, const subresource_data* initData, resource_usage usage, reshade::api::resource handle)
{
    auto& data = device->get_private_data<DeviceDataContainer>();

    std::unique_lock<shared_mutex> lock(resource_mutex);

    if (rShim != nullptr)
    {
        rShim->OnInitResource(device, desc, initData, usage, handle);
    }

    if (static_cast<uint32_t>(desc.usage & resource_usage::render_target) && desc.type == resource_type::texture_2d)
    {
        resource_view view_non_srgb = { 0 };
        resource_view view_srgb = { 0 };

        reshade::api::format format_non_srgb = format_to_default_typed(desc.texture.format, 0);
        reshade::api::format format_srgb = format_to_default_typed(desc.texture.format, 1);

        device->create_resource_view(handle, resource_usage::render_target,
            resource_view_desc(format_non_srgb), &view_non_srgb);

        device->create_resource_view(handle, resource_usage::render_target,
            resource_view_desc(format_srgb), &view_srgb);

        s_sRGBResourceViews.emplace(handle.handle, make_pair(view_non_srgb, view_srgb));
    }
}


void ResourceManager::OnDestroyResource(device* device, resource res)
{
    if (!resource_mutex.try_lock())
        return;

    if (rShim != nullptr)
    {
        rShim->OnDestroyResource(device, res);
    }

    const auto& it = s_sRGBResourceViews.find(res.handle);

    if (it != s_sRGBResourceViews.end())
    {
        auto& views = it->second;

        if (views.first != 0)
            device->destroy_resource_view(views.first);
        if (views.second != 0)
            device->destroy_resource_view(views.second);

        s_sRGBResourceViews.erase(it);
    }

    resource_mutex.unlock();
}

void ResourceManager::OnDestroyDevice(device* device)
{
    std::unique_lock<shared_mutex> lock(resource_mutex);

    for (auto it = s_sRGBResourceViews.begin(); it != s_sRGBResourceViews.end();)
    {
        auto& views = it->second;

        if (views.first != 0)
            device->destroy_resource_view(views.first);
        if (views.second != 0)
            device->destroy_resource_view(views.second);

        it = s_sRGBResourceViews.erase(it);
    }
}


bool ResourceManager::OnCreateResourceView(device* device, resource resource, resource_usage usage_type, resource_view_desc& desc)
{
    if (rShim != nullptr)
    {
        return rShim->OnCreateResourceView(device, resource, usage_type, desc);
    }

    return false;
}

void ResourceManager::SetResourceViewHandles(uint64_t handle, reshade::api::resource_view* non_srgb_view, reshade::api::resource_view* srgb_view)
{
    const auto& it = s_sRGBResourceViews.find(handle);
    if (it != s_sRGBResourceViews.end())
    {
        *non_srgb_view = it->second.first;
        *srgb_view = it->second.second;
        return;
    }
}