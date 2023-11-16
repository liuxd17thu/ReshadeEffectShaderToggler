#include "RenderingBindingManager.h"

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;
using namespace std;

RenderingBindingManager::RenderingBindingManager(AddonImGui::AddonUIData& data, ResourceManager& rManager) : uiData(data), resourceManager(rManager)
{
}

RenderingBindingManager::~RenderingBindingManager()
{

}

void RenderingBindingManager::InitTextureBingings(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    // Init empty texture
    CreateTextureBinding(runtime, &empty_res, &empty_srv, &empty_rtv, reshade::api::format::r8g8b8a8_unorm);

    // Initialize texture bindings with default format
    for (auto& [_, group] : uiData.GetToggleGroups())
    {
        if (group.isProvidingTextureBinding() && group.getTextureBindingName().length() > 0)
        {
            resource res = { 0 };
            resource_view srv = { 0 };
            resource_view rtv = { 0 };

            unique_lock<shared_mutex> lock(data.binding_mutex);
            if (group.getCopyTextureBinding() && CreateTextureBinding(runtime, &res, &srv, &rtv, reshade::api::format::r8g8b8a8_unorm))
            {
                data.bindingMap[group.getTextureBindingName()] = TextureBindingData{ res, reshade::api::format::r8g8b8a8_unorm, srv, rtv, 0, 0, 1, group.getClearBindings(), group.getCopyTextureBinding(), false };
                runtime->update_texture_bindings(group.getTextureBindingName().c_str(), srv);
            }
            else if (!group.getCopyTextureBinding())
            {
                data.bindingMap[group.getTextureBindingName()] = TextureBindingData{ resource { 0 }, format::unknown, resource_view { 0 }, resource_view { 0 }, 0, 0, 1, group.getClearBindings(), group.getCopyTextureBinding(), false };
                runtime->update_texture_bindings(group.getTextureBindingName().c_str(), resource_view{ 0 }, resource_view{ 0 });
            }
        }
    }
}

void RenderingBindingManager::DisposeTextureBindings(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    unique_lock<shared_mutex> lock(data.binding_mutex);

    if (empty_res != 0)
    {
        runtime->get_device()->destroy_resource(empty_res);
    }

    if (empty_srv != 0)
    {
        runtime->get_device()->destroy_resource_view(empty_srv);
    }

    for (auto& [bindingName, _] : data.bindingMap)
    {
        DestroyTextureBinding(runtime, bindingName);
    }

    data.bindingMap.clear();
}

static bool isValidRenderTarget(reshade::api::format format)
{
    switch (format)
    {
    case reshade::api::format::r1_unorm:
    case reshade::api::format::l8_unorm:
    case reshade::api::format::a8_unorm:
    case reshade::api::format::r8_typeless:
    case reshade::api::format::r8_uint:
    case reshade::api::format::r8_sint:
    case reshade::api::format::r8_unorm:
    case reshade::api::format::r8_snorm:
    case reshade::api::format::l8a8_unorm:
    case reshade::api::format::r8g8_typeless:
    case reshade::api::format::r8g8_uint:
    case reshade::api::format::r8g8_sint:
    case reshade::api::format::r8g8_unorm:
    case reshade::api::format::r8g8_snorm:
    case reshade::api::format::r8g8b8a8_typeless:
    case reshade::api::format::r8g8b8a8_uint:
    case reshade::api::format::r8g8b8a8_sint:
    case reshade::api::format::r8g8b8a8_unorm:
    case reshade::api::format::r8g8b8a8_unorm_srgb:
    case reshade::api::format::r8g8b8a8_snorm:
    case reshade::api::format::r8g8b8x8_unorm:
    case reshade::api::format::r8g8b8x8_unorm_srgb:
    case reshade::api::format::b8g8r8a8_typeless:
    case reshade::api::format::b8g8r8a8_unorm:
    case reshade::api::format::b8g8r8a8_unorm_srgb:
    case reshade::api::format::b8g8r8x8_typeless:
    case reshade::api::format::b8g8r8x8_unorm:
    case reshade::api::format::b8g8r8x8_unorm_srgb:
    case reshade::api::format::r10g10b10a2_typeless:
    case reshade::api::format::r10g10b10a2_uint:
    case reshade::api::format::r10g10b10a2_unorm:
    case reshade::api::format::r10g10b10a2_xr_bias:
    case reshade::api::format::b10g10r10a2_typeless:
    case reshade::api::format::b10g10r10a2_uint:
    case reshade::api::format::b10g10r10a2_unorm:
    case reshade::api::format::l16_unorm:
    case reshade::api::format::r16_typeless:
    case reshade::api::format::r16_uint:
    case reshade::api::format::r16_sint:
    case reshade::api::format::r16_unorm:
    case reshade::api::format::r16_snorm:
    case reshade::api::format::r16_float:
    case reshade::api::format::l16a16_unorm:
    case reshade::api::format::r16g16_typeless:
    case reshade::api::format::r16g16_uint:
    case reshade::api::format::r16g16_sint:
    case reshade::api::format::r16g16_unorm:
    case reshade::api::format::r16g16_snorm:
    case reshade::api::format::r16g16_float:
    case reshade::api::format::r16g16b16a16_typeless:
    case reshade::api::format::r16g16b16a16_uint:
    case reshade::api::format::r16g16b16a16_sint:
    case reshade::api::format::r16g16b16a16_unorm:
    case reshade::api::format::r16g16b16a16_snorm:
    case reshade::api::format::r16g16b16a16_float:
    case reshade::api::format::r32_typeless:
    case reshade::api::format::r32_uint:
    case reshade::api::format::r32_sint:
    case reshade::api::format::r32_float:
    case reshade::api::format::r32g32_typeless:
    case reshade::api::format::r32g32_uint:
    case reshade::api::format::r32g32_sint:
    case reshade::api::format::r32g32_float:
    case reshade::api::format::r32g32b32_typeless:
    case reshade::api::format::r32g32b32_uint:
    case reshade::api::format::r32g32b32_sint:
    case reshade::api::format::r32g32b32_float:
    case reshade::api::format::r32g32b32a32_typeless:
    case reshade::api::format::r32g32b32a32_uint:
    case reshade::api::format::r32g32b32a32_sint:
    case reshade::api::format::r32g32b32a32_float:
    case reshade::api::format::r9g9b9e5:
    case reshade::api::format::r11g11b10_float:
    case reshade::api::format::b5g6r5_unorm:
    case reshade::api::format::b5g5r5a1_unorm:
    case reshade::api::format::b5g5r5x1_unorm:
    case reshade::api::format::b4g4r4a4_unorm:
    case reshade::api::format::a4b4g4r4_unorm:
        return true;
    default:
        return false;
    }

    return false;
}

bool RenderingBindingManager::_CreateTextureBinding(reshade::api::effect_runtime* runtime,
    reshade::api::resource* res,
    reshade::api::resource_view* srv,
    reshade::api::resource_view* rtv,
    reshade::api::format format,
    uint32_t width,
    uint32_t height,
    uint16_t levels)
{
    runtime->get_command_queue()->wait_idle();

    reshade::api::resource_usage res_usage = resource_usage::copy_dest | resource_usage::shader_resource;

    bool validRT = isValidRenderTarget(format);
    if (validRT)
    {
        res_usage |= resource_usage::render_target;
    }

    if (!runtime->get_device()->create_resource(
        resource_desc(width, height, 1, levels, format_to_typeless(format), 1, memory_heap::gpu_only, res_usage),
        nullptr, resource_usage::shader_resource, res))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource!");
        return false;
    }

    if (!runtime->get_device()->create_resource_view(*res, resource_usage::shader_resource, resource_view_desc(format_to_default_typed(format, 0)), srv))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource view!");
        return false;
    }

    if (validRT && !runtime->get_device()->create_resource_view(*res, resource_usage::render_target, resource_view_desc(format_to_default_typed(format, 0)), rtv))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource view!");
        return false;
    }

    return true;
}

bool RenderingBindingManager::CreateTextureBinding(effect_runtime* runtime, resource* res, resource_view* srv, resource_view* rtv, const resource_desc& desc)
{
    return _CreateTextureBinding(runtime, res, srv, rtv, desc.texture.format, desc.texture.width, desc.texture.height, desc.texture.levels);
}

bool RenderingBindingManager::CreateTextureBinding(effect_runtime* runtime, resource* res, resource_view* srv, resource_view* rtv, reshade::api::format format)
{
    uint32_t frame_width, frame_height;
    runtime->get_screenshot_width_and_height(&frame_width, &frame_height);

    return _CreateTextureBinding(runtime, res, srv, rtv, format, frame_width, frame_height, 1);
}

void RenderingBindingManager::DestroyTextureBinding(effect_runtime* runtime, const string& binding)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    auto it = data.bindingMap.find(binding);

    if (it != data.bindingMap.end())
    {
        auto& [bindingName, bindingData] = *it;
        // Destroy copy resource if copy option is enabled, otherwise just reset the binding
        if (bindingData.copy)
        {
            resource res = { 0 };
            resource_view srv = { 0 };
            resource_view rtv = { 0 };

            runtime->get_command_queue()->wait_idle();

            res = bindingData.res;
            if (res != 0)
            {
                runtime->get_device()->destroy_resource(res);
            }

            srv = bindingData.srv;
            if (srv != 0)
            {
                runtime->get_device()->destroy_resource_view(srv);
            }

            rtv = bindingData.rtv;
            if (rtv != 0)
            {
                runtime->get_device()->destroy_resource_view(rtv);
            }
        }

        runtime->update_texture_bindings(binding.c_str(), resource_view{ 0 }, resource_view{ 0 });

        bindingData.res = { 0 };
        bindingData.srv = { 0 };
        bindingData.rtv = { 0 };
        bindingData.format = format::unknown;
        bindingData.width = 0;
        bindingData.height = 0;
        bindingData.levels = 1;
    }
}


uint32_t RenderingBindingManager::UpdateTextureBinding(effect_runtime* runtime, const string& binding, const resource_desc& desc)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    auto it = data.bindingMap.find(binding);

    if (it != data.bindingMap.end())
    {
        auto& [bindingName, bindingData] = *it;
        reshade::api::format oldFormat = bindingData.format;
        reshade::api::format format = desc.texture.format;
        uint32_t oldWidth = bindingData.width;
        uint32_t width = desc.texture.width;
        uint32_t oldHeight = bindingData.height;
        uint32_t height = desc.texture.height;
        uint32_t oldLevels = bindingData.levels;
        uint32_t levels = desc.texture.levels;

        if (format != oldFormat || oldWidth != width || oldHeight != height || oldLevels != levels)
        {
            DestroyTextureBinding(runtime, binding);

            resource res = {};
            resource_view srv = {};
            resource_view rtv = {};

            if (CreateTextureBinding(runtime, &res, &srv, &rtv, desc))
            {
                bindingData.res = res;
                bindingData.srv = srv;
                bindingData.rtv = rtv;
                bindingData.width = desc.texture.width;
                bindingData.height = desc.texture.height;
                bindingData.format = desc.texture.format;
                bindingData.levels = desc.texture.levels;

                runtime->update_texture_bindings(binding.c_str(), srv);
            }
            else
            {
                return 0;
            }

            return 2;
        }
    }
    else
    {
        return 0;
    }

    return 1;
}


void RenderingBindingManager::_UpdateTextureBindings(command_list* cmd_list,
    DeviceDataContainer& deviceData,
    const unordered_map<string, tuple<ToggleGroup*, uint64_t, resource>>& bindingsToUpdate,
    vector<string>& removalList,
    const unordered_set<string>& toUpdateBindings)
{
    for (const auto& [bindingName, bindingData] : bindingsToUpdate)
    {
        if (toUpdateBindings.contains(bindingName) && !deviceData.bindingsUpdated.contains(bindingName))
        {
            effect_runtime* runtime = deviceData.current_runtime;

            resource active_resource = std::get<2>(bindingData);
            ToggleGroup* group = std::get<0>(bindingData);

            if (active_resource == 0)
            {
                continue;
            }

            auto it = deviceData.bindingMap.find(bindingName);

            if (it != deviceData.bindingMap.end())
            {
                auto& [bindingName, bindingData] = *it;

                if (!bindingData.copy)
                {
                    resource_view view_non_srgb = { 0 };
                    resource_view view_srgb = { 0 };

                    resourceManager.SetShaderResourceViewHandles(active_resource.handle, &view_non_srgb, &view_srgb);

                    if (view_non_srgb == 0)
                    {
                        return;
                    }

                    resource_desc resDesc = runtime->get_device()->get_resource_desc(active_resource);

                    resource target_res = bindingData.res;

                    if (target_res != active_resource)
                    {
                        runtime->update_texture_bindings(bindingName.c_str(), view_non_srgb, view_srgb);

                        bindingData.res = active_resource;
                        bindingData.format = resDesc.texture.format;
                        bindingData.srv = view_non_srgb;
                        bindingData.width = resDesc.texture.width;
                        bindingData.height = resDesc.texture.height;
                        bindingData.levels = resDesc.texture.levels;
                    }

                    bindingData.reset = false;
                }
                else
                {
                    resource_desc resDesc = runtime->get_device()->get_resource_desc(active_resource);

                    uint32_t retUpdate = UpdateTextureBinding(runtime, bindingName, resDesc);

                    resource target_res = bindingData.res;

                    if (retUpdate && target_res != 0)
                    {
                        cmd_list->copy_resource(active_resource, target_res);

                        if (group->getFlipBufferBinding() && bindingData.rtv != 0 && deviceData.specialEffects.flip.technique != 0)
                        {
                            runtime->render_technique(deviceData.specialEffects.flip.technique, cmd_list, bindingData.rtv);
                        }

                        bindingData.reset = false;
                    }
                }

                deviceData.bindingsUpdated.emplace(bindingName);
                removalList.push_back(bindingName);
            }
        }
    }
}

void RenderingBindingManager::UpdateTextureBindings(command_list* cmd_list, uint64_t callLocation, uint64_t invocation)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    // Remove call location from queue
    commandListData.commandQueue &= ~(invocation << (callLocation * MATCH_DELIMITER));

    if (deviceData.current_runtime == nullptr || (commandListData.ps.bindingsToUpdate.size() == 0 && commandListData.vs.bindingsToUpdate.size() == 0 && commandListData.cs.bindingsToUpdate.size() == 0)) {
        return;
    }

    unordered_set<string> psToUpdateBindings;
    unordered_set<string> vsToUpdateBindings;
    unordered_set<string> csToUpdateBindings;

    if (invocation & MATCH_BINDING_PS)
    {
        RenderingManager::QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.ps.bindingsToUpdate, psToUpdateBindings, callLocation, 0, MATCH_BINDING_PS);
    }

    if (invocation & MATCH_BINDING_VS)
    {
        RenderingManager::QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.vs.bindingsToUpdate, vsToUpdateBindings, callLocation, 1, MATCH_BINDING_VS);
    }

    if (invocation & MATCH_BINDING_CS)
    {
        RenderingManager::QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.cs.bindingsToUpdate, csToUpdateBindings, callLocation, 2, MATCH_BINDING_CS);
    }

    if (psToUpdateBindings.size() == 0 && vsToUpdateBindings.size() == 0 && csToUpdateBindings.size() == 0)
    {
        return;
    }

    vector<string> psRemovalList;
    vector<string> vsRemovalList;
    vector<string> csRemovalList;

    unique_lock<shared_mutex> mtx(deviceData.binding_mutex);
    if (psToUpdateBindings.size() > 0)
    {
        _UpdateTextureBindings(cmd_list, deviceData, commandListData.ps.bindingsToUpdate, psRemovalList, psToUpdateBindings);
    }
    if (vsToUpdateBindings.size() > 0)
    {
        _UpdateTextureBindings(cmd_list, deviceData, commandListData.vs.bindingsToUpdate, vsRemovalList, vsToUpdateBindings);
    }
    if (csToUpdateBindings.size() > 0)
    {
        _UpdateTextureBindings(cmd_list, deviceData, commandListData.cs.bindingsToUpdate, csRemovalList, csToUpdateBindings);
    }
    mtx.unlock();

    for (auto& g : psRemovalList)
    {
        commandListData.ps.bindingsToUpdate.erase(g);
    }

    for (auto& g : vsRemovalList)
    {
        commandListData.vs.bindingsToUpdate.erase(g);
    }

    for (auto& g : csRemovalList)
    {
        commandListData.cs.bindingsToUpdate.erase(g);
    }

}

void RenderingBindingManager::ClearUnmatchedTextureBindings(reshade::api::command_list* cmd_list)
{
    DeviceDataContainer& data = cmd_list->get_device()->get_private_data<DeviceDataContainer>();

    shared_lock<shared_mutex> mtx(data.binding_mutex);
    if (data.bindingMap.size() == 0)
    {
        return;
    }

    static const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    for (auto& [bindingName, bindingData] : data.bindingMap)
    {
        if (data.bindingsUpdated.contains(bindingName) || !bindingData.enabled_reset_on_miss || bindingData.reset)
        {
            continue;
        }

        data.current_runtime->update_texture_bindings(bindingName.c_str(), empty_srv);

        bindingData.res = { 0 };
        bindingData.srv = { 0 };
        bindingData.rtv = { 0 };
        bindingData.width = 0;
        bindingData.height = 0;
        bindingData.levels = 1;
        bindingData.reset = true;
    }

    if (!data.huntPreview.matched && uiData.GetToggleGroupIdShaderEditing() >= 0)
    {
        resource_view rtv_ping = resource_view{ 0 };
        resource_view rtv_pong = resource_view{ 0 };

        resourceManager.SetPingPreviewHandles(nullptr, &rtv_ping, nullptr);
        resourceManager.SetPongPreviewHandles(nullptr, &rtv_pong, nullptr);

        if (rtv_ping != 0)
        {
            cmd_list->clear_render_target_view(rtv_ping, clearColor);
        }
        if (rtv_pong != 0)
        {
            cmd_list->clear_render_target_view(rtv_pong, clearColor);
        }
    }
}