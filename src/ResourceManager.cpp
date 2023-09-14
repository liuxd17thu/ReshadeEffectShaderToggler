#include <format>
#include "ResourceManager.h"

using namespace Rendering;
using namespace reshade::api;
using namespace Shim::Resources;
using namespace std;

ResourceShimType ResourceManager::ResolveResourceShimType(const string& stype)
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
            const auto& [oldbackBufferView, oldbackBufferViewSRGB] = entry->second;

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
    //InitBackbuffer(swapchain);
}

void ResourceManager::OnDestroySwapchain(reshade::api::swapchain* swapchain)
{
    //ClearBackbuffer(swapchain);
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

    if (rShim != nullptr)
    {
        rShim->OnInitResource(device, desc, initData, usage, handle);
    }
}

void ResourceManager::DisposeView(device* device, uint64_t handle)
{
    const auto it = s_sRGBResourceViews.find(handle);

    if (it != s_sRGBResourceViews.end())
    {
        auto& [view,srgbView] = it->second;

        if (view != 0)
            device->destroy_resource_view(view);
        if (srgbView != 0)
            device->destroy_resource_view(srgbView);

        s_sRGBResourceViews.erase(it);
    }

    const auto sit = s_SRVs.find(handle);

    if (sit != s_SRVs.end())
    {
        auto& [srv1, srv2] = sit->second;

        if (srv1 != 0)
            device->destroy_resource_view(srv1);
        if (srv2 != 0)
            device->destroy_resource_view(srv2);

        s_SRVs.erase(sit);
    }

    const auto rIt = _resourceViewRefCount.find(handle);
    if (rIt != _resourceViewRefCount.end())
    {
        _resourceViewRefCount.erase(rIt);
    }
}

void ResourceManager::OnDestroyResource(device* device, resource res)
{
    if (rShim != nullptr)
    {
        rShim->OnDestroyResource(device, res);
    }

    if (view_mutex.try_lock())
    {
        if (resource_mutex.try_lock())
        {
            DisposeView(device, res.handle);
            resource_mutex.unlock();
        }
    
        view_mutex.unlock();
    }
}

void ResourceManager::OnDestroyDevice(device* device)
{
    //unique_lock<shared_mutex> lock(resource_mutex);
    //
    //for (auto it = s_sRGBResourceViews.begin(); it != s_sRGBResourceViews.end();)
    //{
    //    auto& views = it->second;
    //
    //    if (views.first != 0)
    //        device->destroy_resource_view(views.first);
    //    if (views.second != 0)
    //        device->destroy_resource_view(views.second);
    //
    //    it = s_sRGBResourceViews.erase(it);
    //}
    //
    //unique_lock<shared_mutex> vlock(view_mutex);
    //_resourceViewRefCount.clear();
    //_resourceViewRef.clear();
}


bool ResourceManager::OnCreateResourceView(device* device, resource resource, resource_usage usage_type, resource_view_desc& desc)
{
    if (rShim != nullptr)
    {
        return rShim->OnCreateResourceView(device, resource, usage_type, desc);
    }

    return false;
}

void ResourceManager::OnInitResourceView(device* device, resource resource, resource_usage usage_type, const resource_view_desc& desc, resource_view view)
{
    resource_desc rdesc = device->get_resource_desc(resource);
    
    if (static_cast<uint32_t>(rdesc.usage & resource_usage::render_target) && rdesc.type == resource_type::texture_2d)
    {
        unique_lock<shared_mutex> vlock(view_mutex);

        const auto vRef = _resourceViewRef.find(view.handle);
        if (vRef != _resourceViewRef.end())
        {
            if (vRef->second != resource.handle)
            {
                auto curCount = _resourceViewRefCount.find(vRef->second);

                if (curCount != _resourceViewRefCount.end())
                {
                    if (curCount->second > 1)
                    {
                        curCount->second--;
                    }
                    else
                    {
                        DisposeView(device, vRef->second);
                    }
                }
            }
        }

        const auto& cRef = _resourceViewRefCount.find(resource.handle);
        if (cRef == _resourceViewRefCount.end())
        {
            unique_lock<shared_mutex> lock(resource_mutex);
        
            resource_view view_non_srgb = { 0 };
            resource_view view_srgb = { 0 };

            resource_view srv_non_srgb = { 0 };
            resource_view srv_srgb = { 0 };
            
            reshade::api::format format_non_srgb = format_to_default_typed(rdesc.texture.format, 0);
            reshade::api::format format_srgb = format_to_default_typed(rdesc.texture.format, 1);
            
            device->create_resource_view(resource, resource_usage::render_target,
                resource_view_desc(format_non_srgb), &view_non_srgb);
            
            device->create_resource_view(resource, resource_usage::render_target,
                resource_view_desc(format_srgb), &view_srgb);

            device->create_resource_view(resource, resource_usage::shader_resource,
                resource_view_desc(format_non_srgb), &srv_non_srgb);

            device->create_resource_view(resource, resource_usage::shader_resource,
                resource_view_desc(format_srgb), &srv_srgb);
            
            s_sRGBResourceViews.emplace(resource.handle, make_pair(view_non_srgb, view_srgb));
            s_SRVs.emplace(resource.handle, make_pair(srv_non_srgb, srv_srgb));
        }

        _resourceViewRefCount[resource.handle]++;
        _resourceViewRef[view.handle] = resource.handle;
    }
}

void ResourceManager::OnDestroyResourceView(device* device, resource_view view)
{
    unique_lock<shared_mutex> lock(view_mutex);

    const auto& vRef = _resourceViewRef.find(view.handle);
    if (vRef != _resourceViewRef.end())
    {
        auto curCount = _resourceViewRefCount.find(vRef->second);

        if (curCount != _resourceViewRefCount.end())
        {
            if (curCount->second > 1)
            {
                curCount->second--;
            }
            else
            {
                DisposeView(device, vRef->second);
            }
        }
    
        _resourceViewRef.erase(vRef);
    }
}

void ResourceManager::SetResourceViewHandles(uint64_t handle, reshade::api::resource_view* non_srgb_view, reshade::api::resource_view* srgb_view)
{
    const auto& it = s_sRGBResourceViews.find(handle);
    if (it != s_sRGBResourceViews.end())
    {
        std::tie(*non_srgb_view, *srgb_view) = it->second;
    }
}

void ResourceManager::SetShaderResourceViewHandles(uint64_t handle, reshade::api::resource_view* non_srgb_view, reshade::api::resource_view* srgb_view)
{
    const auto& it = s_SRVs.find(handle);
    if (it != s_SRVs.end())
    {
        std::tie(*non_srgb_view, *srgb_view) = it->second;
    }
}


void ResourceManager::DisposePreview(reshade::api::effect_runtime* runtime)
{
    if (preview_res == 0)
        return;

    runtime->get_command_queue()->wait_idle();

    if (preview_res != 0)
    {
        runtime->get_device()->destroy_resource(preview_res);
    }

    if (preview_srv != 0)
    {
        runtime->get_device()->destroy_resource_view(preview_srv);
    }

    if (preview_rtv != 0)
    {
        runtime->get_device()->destroy_resource_view(preview_rtv);
    }

    preview_res = resource{ 0 };
    preview_srv = resource_view{ 0 };
    preview_rtv = resource_view{ 0 };
}

void ResourceManager::CreatePreview(reshade::api::effect_runtime* runtime, reshade::api::resource originalRes)
{
    if (originalRes == 0)
        return;

    resource_desc desc = runtime->get_device()->get_resource_desc(originalRes);

    // don't recreate in case format matches current preview RT format
    if (preview_res != 0)
    {
        resource_desc pdesc = runtime->get_device()->get_resource_desc(preview_res);

        if (pdesc.texture.format == desc.texture.format && pdesc.texture.width == desc.texture.width && pdesc.texture.height == desc.texture.height)
        {
            return;
        }
    }

    DisposePreview(runtime);

    if (!runtime->get_device()->create_resource(
        resource_desc(desc.texture.width, desc.texture.height, 1, 1, format_to_typeless(desc.texture.format), 1, memory_heap::gpu_only, resource_usage::copy_dest | resource_usage::shader_resource),
        nullptr, resource_usage::shader_resource, &preview_res))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create preview resource!");
    }

    if (!runtime->get_device()->create_resource_view(preview_res, resource_usage::shader_resource, resource_view_desc(format_to_default_typed(desc.texture.format, 0)), &preview_srv))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create preview view!");
    }

    if (!runtime->get_device()->create_resource_view(preview_res, resource_usage::render_target, resource_view_desc(format_to_default_typed(desc.texture.format, 0)), &preview_rtv))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create preview view!");
    }
}

void ResourceManager::SetPreviewViewHandles(reshade::api::resource* res, reshade::api::resource_view* rtv, reshade::api::resource_view* srv)
{
    if (preview_res != 0)
    {
        if(res != nullptr)
            *res = preview_res;
        if(rtv != nullptr)
            *rtv = preview_rtv;
        if(srv != nullptr)
            *srv = preview_srv;
    }
}