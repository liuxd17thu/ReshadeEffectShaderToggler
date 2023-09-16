#include "RenderingManager.h"
#include "PipelinePrivateData.h"
#include "StateTracking.h"

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;
using namespace std;

size_t RenderingManager::g_charBufferSize = CHAR_BUFFER_SIZE;
char RenderingManager::g_charBuffer[CHAR_BUFFER_SIZE];

RenderingManager::RenderingManager(AddonImGui::AddonUIData& data, ResourceManager& rManager) : uiData(data), resourceManager(rManager)
{
}

RenderingManager::~RenderingManager()
{

}

void RenderingManager::EnumerateTechniques(effect_runtime* runtime, function<void(effect_runtime*, effect_technique, string&)> func)
{
    runtime->enumerate_techniques(nullptr, [func](effect_runtime* rt, effect_technique technique) {
        g_charBufferSize = CHAR_BUFFER_SIZE;
        rt->get_technique_name(technique, g_charBuffer, &g_charBufferSize);
        string name(g_charBuffer);
        func(rt, technique, name);
        });
}

void RenderingManager::_CheckCallForCommandList(ShaderData& sData, CommandListDataContainer& commandListData, DeviceDataContainer& deviceData) const
{
    // Masks which checks to perform. Note that we will always schedule a draw call check for binding and effect updates,
    // this serves the purpose of assigning the resource_view to perform the update later on if needed.
    uint64_t queue_mask = MATCH_NONE;

    // Shift in case of VS using data id
    const uint64_t match_effect = MATCH_EFFECT_PS << sData.id;
    const uint64_t match_binding = MATCH_BINDING_PS << sData.id;
    const uint64_t match_const = MATCH_CONST_PS << sData.id;
    const uint64_t match_preview = MATCH_PREVIEW_PS << sData.id;

    if (sData.blockedShaderGroups != nullptr)
    {
        for (auto group : *sData.blockedShaderGroups)
        {
            if (group->isActive())
            {
                if (group->getExtractConstants() && !deviceData.constantsUpdated.contains(group))
                {
                    if (!sData.constantBuffersToUpdate.contains(group))
                    {
                        sData.constantBuffersToUpdate.emplace(group);
                        queue_mask |= match_const;
                    }
                }

                if (group->getId() == uiData.GetToggleGroupIdShaderEditing() && !deviceData.huntPreview.matched)
                {
                    if(uiData.GetCurrentTabType() == AddonImGui::TAB_RENDER_TARGET)
                    {
                        queue_mask |= (match_preview << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_preview << (CALL_DRAW * MATCH_DELIMITER));
                        deviceData.huntPreview.target_invocation_location = group->getInvocationLocation();
                    }
                }

                if (group->isProvidingTextureBinding() && !deviceData.bindingsUpdated.contains(group->getTextureBindingName()))
                {
                    if (!sData.bindingsToUpdate.contains(group->getTextureBindingName()))
                    {
                        if (!group->getCopyTextureBinding() || group->getExtractResourceViews())
                        {
                            sData.bindingsToUpdate.emplace(group->getTextureBindingName(), std::make_tuple(group, CALL_DRAW, resource_view{ 0 }));
                            queue_mask |= (match_binding << CALL_DRAW * MATCH_DELIMITER);
                        }
                        else
                        {
                            sData.bindingsToUpdate.emplace(group->getTextureBindingName(), std::make_tuple(group, group->getBindingInvocationLocation(), resource_view{ 0 }));
                            queue_mask |= (match_binding << (group->getBindingInvocationLocation() * MATCH_DELIMITER)) | (match_binding << (CALL_DRAW * MATCH_DELIMITER));
                        }
                    }
                }

                if (group->getAllowAllTechniques())
                {
                    for (const auto& [techName, techEnabled] : deviceData.allEnabledTechniques)
                    {
                        if (group->getHasTechniqueExceptions() && group->preferredTechniques().contains(techName))
                        {
                            continue;
                        }

                        if (!techEnabled)
                        {
                            if (!sData.techniquesToRender.contains(techName))
                            {
                                sData.techniquesToRender.emplace(techName, std::make_tuple(group, group->getInvocationLocation(), resource_view{ 0 }));
                                queue_mask |= (match_effect << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_effect << (CALL_DRAW * MATCH_DELIMITER));
                            }
                        }
                    }
                }
                else if (group->preferredTechniques().size() > 0) {
                    for (auto& techName : group->preferredTechniques())
                    {
                        if (deviceData.allEnabledTechniques.contains(techName) && !deviceData.allEnabledTechniques.at(techName))
                        {
                            if (!sData.techniquesToRender.contains(techName))
                            {
                                sData.techniquesToRender.emplace(techName, std::make_tuple(group, group->getInvocationLocation(), resource_view{ 0 }));
                                queue_mask |= (match_effect << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_effect << (CALL_DRAW * MATCH_DELIMITER));
                            }
                        }
                    }
                }
            }
        }
    }

    commandListData.commandQueue |= queue_mask;
}

void RenderingManager::CheckCallForCommandList(reshade::api::command_list* commandList)
{
    if (nullptr == commandList)
    {
        return;
    }

    CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = commandList->get_device()->get_private_data<DeviceDataContainer>();

    shared_lock<shared_mutex> r_mutex(render_mutex);
    shared_lock<shared_mutex> b_mutex(binding_mutex);

    _CheckCallForCommandList(commandListData.ps, commandListData, deviceData);
    _CheckCallForCommandList(commandListData.vs, commandListData, deviceData);
    _CheckCallForCommandList(commandListData.cs, commandListData, deviceData);

    b_mutex.unlock();
    r_mutex.unlock();
}

void RenderingManager::_RescheduleGroups(ShaderData& sData, CommandListDataContainer& commandListData, DeviceDataContainer& deviceData)
{
    const uint32_t match_effect = MATCH_EFFECT_PS << sData.id;
    const uint32_t match_binding = MATCH_BINDING_PS << sData.id;
    const uint32_t match_const = MATCH_CONST_PS << sData.id;
    const uint32_t match_preview = MATCH_PREVIEW_PS << sData.id;

    uint32_t queue_mask = MATCH_NONE;

    for (const auto& tech : sData.techniquesToRender)
    {
        const ToggleGroup* group = std::get<0>(tech.second);
        const resource_view view = std::get<2>(tech.second);

        if (view == 0 && group->getRequeueAfterRTMatchingFailure())
        {
            queue_mask |= (match_effect << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_effect << (CALL_DRAW * MATCH_DELIMITER));

            if (group->getId() == uiData.GetToggleGroupIdShaderEditing() && !deviceData.huntPreview.matched && deviceData.huntPreview.target_rtv == 0)
            {
                if (uiData.GetCurrentTabType() == AddonImGui::TAB_RENDER_TARGET)
                {
                    queue_mask |= (match_preview << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_preview << (CALL_DRAW * MATCH_DELIMITER));

                    deviceData.huntPreview.target_invocation_location = group->getInvocationLocation();
                }
            }
        }
    }

    for (const auto& tech : sData.bindingsToUpdate)
    {
        const ToggleGroup* group = std::get<0>(tech.second);
        const resource_view view = std::get<2>(tech.second);

        if (view == 0 && group->getRequeueAfterRTMatchingFailure())
        {
            queue_mask |= (match_binding << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_binding << (CALL_DRAW * MATCH_DELIMITER));

            if (group->getId() == uiData.GetToggleGroupIdShaderEditing() && !deviceData.huntPreview.matched && deviceData.huntPreview.target_rtv == 0)
            {
                if (uiData.GetCurrentTabType() == AddonImGui::TAB_RENDER_TARGET)
                {
                    queue_mask |= (match_preview << (group->getInvocationLocation() * MATCH_DELIMITER)) | (match_preview << (CALL_DRAW * MATCH_DELIMITER));

                    deviceData.huntPreview.target_invocation_location = group->getInvocationLocation();
                }
            }
        }
    }


    commandListData.commandQueue |= queue_mask;
}

void RenderingManager::RescheduleGroups(CommandListDataContainer& commandListData, DeviceDataContainer& deviceData)
{
    std::shared_lock<std::shared_mutex> r_mutex(render_mutex);
    std::shared_lock<std::shared_mutex> b_mutex(binding_mutex);

    if (commandListData.ps.techniquesToRender.size() > 0 || commandListData.ps.bindingsToUpdate.size() > 0)
    {
        _RescheduleGroups(commandListData.ps, commandListData, deviceData);
    }

    if (commandListData.vs.techniquesToRender.size() > 0 || commandListData.vs.bindingsToUpdate.size() > 0)
    {
        _RescheduleGroups(commandListData.vs, commandListData, deviceData);
    }

    if (commandListData.cs.techniquesToRender.size() > 0 || commandListData.cs.bindingsToUpdate.size() > 0)
    {
        _RescheduleGroups(commandListData.cs, commandListData, deviceData);
    }

    b_mutex.unlock();
    r_mutex.unlock();
}

static inline bool IsColorBuffer(reshade::api::format value)
{
    switch (value)
    {
    default:
        return false;
    case reshade::api::format::b5g6r5_unorm:
    case reshade::api::format::b5g5r5a1_unorm:
    case reshade::api::format::b5g5r5x1_unorm:
    case reshade::api::format::r8g8b8a8_typeless:
    case reshade::api::format::r8g8b8a8_unorm:
    case reshade::api::format::r8g8b8a8_unorm_srgb:
    case reshade::api::format::r8g8b8x8_unorm:
    case reshade::api::format::r8g8b8x8_unorm_srgb:
    case reshade::api::format::b8g8r8a8_typeless:
    case reshade::api::format::b8g8r8a8_unorm:
    case reshade::api::format::b8g8r8a8_unorm_srgb:
    case reshade::api::format::b8g8r8x8_typeless:
    case reshade::api::format::b8g8r8x8_unorm:
    case reshade::api::format::b8g8r8x8_unorm_srgb:
    case reshade::api::format::r10g10b10a2_typeless:
    case reshade::api::format::r10g10b10a2_unorm:
    case reshade::api::format::r10g10b10a2_xr_bias:
    case reshade::api::format::b10g10r10a2_typeless:
    case reshade::api::format::b10g10r10a2_unorm:
    case reshade::api::format::r11g11b10_float:
    case reshade::api::format::r16g16b16a16_typeless:
    case reshade::api::format::r16g16b16a16_float:
    case reshade::api::format::r16g16b16a16_unorm:
    case reshade::api::format::r32g32b32_typeless:
    case reshade::api::format::r32g32b32_float:
    case reshade::api::format::r32g32b32a32_typeless:
    case reshade::api::format::r32g32b32a32_float:
        return true;
    }
}

// Checks whether the aspect ratio of the two sets of dimensions is similar or not, stolen from ReShade's generic_depth addon
static bool check_aspect_ratio(float width_to_check, float height_to_check, uint32_t width, uint32_t height, uint32_t matchingMode)
{
    if (width_to_check == 0.0f || height_to_check == 0.0f)
        return true;

    const float w = static_cast<float>(width);
    float w_ratio = w / width_to_check;
    const float h = static_cast<float>(height);
    float h_ratio = h / height_to_check;
    const float aspect_ratio = (w / h) - (width_to_check / height_to_check);

    // Accept if dimensions are similar in value or almost exact multiples
    return std::fabs(aspect_ratio) <= 0.1f && ((w_ratio <= 1.85f && w_ratio >= 0.5f && h_ratio <= 1.85f && h_ratio >= 0.5f) || (matchingMode == ShaderToggler::SWAPCHAIN_MATCH_MODE_EXTENDED_ASPECT_RATIO && std::modf(w_ratio, &w_ratio) <= 0.02f && std::modf(h_ratio, &h_ratio) <= 0.02f));
}

const resource_view RenderingManager::GetCurrentResourceView(command_list* cmd_list, DeviceDataContainer& deviceData, ToggleGroup* group, CommandListDataContainer& commandListData, uint32_t descIndex, uint64_t action)
{
    resource_view active_rtv = { 0 };

    if (deviceData.current_runtime == nullptr)
    {
        return active_rtv;
    }

    device* device = deviceData.current_runtime->get_device();

    state_tracking& state = cmd_list->get_private_data<state_tracking>();
    const vector<resource_view>& rtvs = state.render_targets;
    const auto& [_, current_srv] = state.descriptors[shader_stage::all_graphics];

    size_t index = group->getRenderTargetIndex();
    index = std::min(index, rtvs.size() - 1);

    size_t bindingRTindex = group->getBindingRenderTargetIndex();
    bindingRTindex = std::min(bindingRTindex, rtvs.size() - 1);

    // Only return SRVs in case of bindings
    if(action & MATCH_BINDING && group->getExtractResourceViews())
    { 
        uint32_t slot_size = static_cast<uint32_t>(current_srv.size());
        uint32_t slot = std::min(group->getBindingSRVSlotIndex(), slot_size - 1);

        if (slot_size == 0)
            return active_rtv;

        uint32_t desc_size = static_cast<uint32_t>(current_srv[slot].size());
        uint32_t desc = std::min(group->getBindingSRVDescriptorIndex(), desc_size - 1);

        if (desc_size == 0)
            return active_rtv;

        descriptor_tracking::descriptor_data buf = current_srv[slot][desc];

        DescriptorCycle cycle = group->consumeSRVCycle();
        if (cycle != CYCLE_NONE)
        {
            if (cycle == CYCLE_UP)
            {
                uint32_t newDescIndex = std::min(++desc, desc_size - 1);
                buf = current_srv[slot][newDescIndex];

                while (buf.view == 0 && desc < desc_size - 2)
                {
                    buf = current_srv[slot][++desc];
                }
            }
            else
            {
                uint32_t newDescIndex = desc > 0 ? --desc : 0;
                buf = current_srv[slot][newDescIndex];

                while (buf.view == 0 && desc > 0)
                {
                    buf = current_srv[slot][--desc];
                }
            }

            if (buf.view != 0)
            {
                group->setBindingSRVDescriptorIndex(desc);
            }
        }

        active_rtv = buf.view;
    }
    else if(action & MATCH_BINDING && !group->getExtractResourceViews() && rtvs.size() > 0 && rtvs[bindingRTindex] != 0)
    {
        resource rs = device->get_resource_from_view(rtvs[bindingRTindex]);

        if (rs == 0)
        {
            // Render targets may not have a resource bound in D3D12, in which case writes to them are discarded
            return active_rtv;
        }

        resource_desc desc = device->get_resource_desc(rs);

        if (group->getBindingMatchSwapchainResolution() < ShaderToggler::SWAPCHAIN_MATCH_MODE_NONE)
        {
            uint32_t width, height;
            deviceData.current_runtime->get_screenshot_width_and_height(&width, &height);

            if ((group->getBindingMatchSwapchainResolution() >= ShaderToggler::SWAPCHAIN_MATCH_MODE_ASPECT_RATIO &&
                !check_aspect_ratio(static_cast<float>(desc.texture.width), static_cast<float>(desc.texture.height), width, height, group->getBindingMatchSwapchainResolution())) ||
                (group->getBindingMatchSwapchainResolution() == ShaderToggler::SWAPCHAIN_MATCH_MODE_RESOLUTION &&
                    (width != desc.texture.width || height != desc.texture.height)))
            {
                return active_rtv;
            }
        }

        active_rtv = rtvs[bindingRTindex];
    }
    else if (action & MATCH_EFFECT && rtvs.size() > 0 && rtvs[index] != 0)
    {
        resource rs = device->get_resource_from_view(rtvs[index]);

        if (rs == 0)
        {
            // Render targets may not have a resource bound in D3D12, in which case writes to them are discarded
            return active_rtv;
        }

        // Don't apply effects to non-RGB buffers
        resource_desc desc = device->get_resource_desc(rs);
        if (!IsColorBuffer(desc.texture.format))
        {
            return active_rtv;
        }

        // Make sure our target matches swap buffer dimensions when applying effects or it's explicitly requested
        if (group->getMatchSwapchainResolution() < ShaderToggler::SWAPCHAIN_MATCH_MODE_NONE)
        {
            uint32_t width, height;
            deviceData.current_runtime->get_screenshot_width_and_height(&width, &height);

            if ((group->getMatchSwapchainResolution() >= ShaderToggler::SWAPCHAIN_MATCH_MODE_ASPECT_RATIO &&
                !check_aspect_ratio(static_cast<float>(desc.texture.width), static_cast<float>(desc.texture.height), width, height, group->getMatchSwapchainResolution())) ||
                (group->getMatchSwapchainResolution() == ShaderToggler::SWAPCHAIN_MATCH_MODE_RESOLUTION &&
                    (width != desc.texture.width || height != desc.texture.height)))
            {
                return active_rtv;
            }
        }

        active_rtv = rtvs[index];
    }

    return active_rtv;
}

const resource_view RenderingManager::GetCurrentPreviewResourceView(command_list* cmd_list, DeviceDataContainer& deviceData, const ToggleGroup* group, CommandListDataContainer& commandListData, uint32_t descIndex, uint64_t action)
{
    resource_view active_rtv = { 0 };

    if (deviceData.current_runtime == nullptr)
    {
        return active_rtv;
    }

    device* device = deviceData.current_runtime->get_device();

    state_tracking& state = cmd_list->get_private_data<state_tracking>();
    const vector<resource_view>& rtvs = state.render_targets;

    size_t index = group->getRenderTargetIndex();
    index = std::min(index, rtvs.size() - 1);

    if (rtvs.size() > 0 && rtvs[index] != 0)
    {
        resource rs = device->get_resource_from_view(rtvs[index]);

        if (rs == 0)
        {
            // Render targets may not have a resource bound in D3D12, in which case writes to them are discarded
            return active_rtv;
        }

        // Don't apply effects to non-RGB buffers
        resource_desc desc = device->get_resource_desc(rs);

        // Make sure our target matches swap buffer dimensions when applying effects or it's explicitly requested
        if (group->getMatchSwapchainResolution() < ShaderToggler::SWAPCHAIN_MATCH_MODE_NONE)
        {
            uint32_t width, height;
            deviceData.current_runtime->get_screenshot_width_and_height(&width, &height);

            if ((group->getMatchSwapchainResolution() >= ShaderToggler::SWAPCHAIN_MATCH_MODE_ASPECT_RATIO &&
                !check_aspect_ratio(static_cast<float>(desc.texture.width), static_cast<float>(desc.texture.height), width, height, group->getMatchSwapchainResolution())) ||
                (group->getMatchSwapchainResolution() == ShaderToggler::SWAPCHAIN_MATCH_MODE_RESOLUTION && (width != desc.texture.width || height != desc.texture.height)))
            {
                return active_rtv;
            }
        }

        active_rtv = rtvs[index];
    }

    return active_rtv;
}

bool RenderingManager::RenderRemainingEffects(effect_runtime* runtime)
{
    if (runtime == nullptr || runtime->get_device() == nullptr)
    {
        return false;
    }

    command_list* cmd_list = runtime->get_command_queue()->get_immediate_command_list();
    device* device = runtime->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();
    bool rendered = false;

    resource res = runtime->get_current_back_buffer();
    resource_view active_rtv = { 0 };
    resource_view active_rtv_srgb = { 0 };

    resourceManager.SetResourceViewHandles(res.handle, &active_rtv, &active_rtv_srgb);
    
    if (deviceData.current_runtime == nullptr || active_rtv == 0 || !deviceData.rendered_effects) {
        return false;
    }
    
    EnumerateTechniques(deviceData.current_runtime, [&deviceData, &commandListData, &cmd_list, &device, &active_rtv, &active_rtv_srgb, &rendered, &res](effect_runtime* runtime, effect_technique technique, string& name) {
        if (deviceData.allEnabledTechniques.contains(name) && !deviceData.allEnabledTechniques[name])
        {
            runtime->render_technique(technique, cmd_list, active_rtv, active_rtv_srgb);
    
            deviceData.allEnabledTechniques[name] = true;
            rendered = true;
        }
        });

    return rendered;
}

bool RenderingManager::_RenderEffects(
    command_list* cmd_list,
    DeviceDataContainer& deviceData,
    const unordered_map<string, tuple<ToggleGroup*, uint64_t, resource_view>>& techniquesToRender,
    vector<string>& removalList,
    const unordered_set<string>& toRenderNames)
{
    bool rendered = false;
    CommandListDataContainer& cmdData = cmd_list->get_private_data<CommandListDataContainer>();

    EnumerateTechniques(deviceData.current_runtime, [&cmdData, &deviceData, &techniquesToRender, &cmd_list, &rendered, &removalList, &toRenderNames, this](effect_runtime* runtime, effect_technique technique, string& name) {

        if (toRenderNames.find(name) != toRenderNames.end())
        {
            auto tech = techniquesToRender.find(name);

            if (tech != techniquesToRender.end() && !deviceData.allEnabledTechniques.at(name))
            {
                auto& [techName, techData] = *tech;
                const auto& [group, _, active_rtv] = techData;

                if (active_rtv == 0)
                {
                    return;
                }

                resource res = runtime->get_device()->get_resource_from_view(active_rtv);

                resource_view view_non_srgb = active_rtv;
                resource_view view_srgb = active_rtv;

                resourceManager.SetResourceViewHandles(res.handle, &view_non_srgb, &view_srgb);

                if (view_non_srgb == 0)
                {
                    return;
                }

                deviceData.rendered_effects = true;

                runtime->render_technique(technique, cmd_list, view_non_srgb, view_srgb);

                resource_desc resDesc = runtime->get_device()->get_resource_desc(res);
                removalList.push_back(name);

                deviceData.allEnabledTechniques[name] = true;
                rendered = true;
            }
        }
        });

    return rendered;
}

void RenderingManager::_QueueOrDequeue(
    command_list* cmd_list,
    DeviceDataContainer& deviceData,
    CommandListDataContainer& commandListData,
    unordered_map<string, tuple<ShaderToggler::ToggleGroup*, uint64_t, reshade::api::resource_view>>& queue,
    unordered_set<string>& immediateQueue,
    uint64_t callLocation,
    uint32_t layoutIndex,
    uint64_t action)
{
    for (auto it = queue.begin(); it != queue.end();)
    {
        auto& [name, data] = *it;
        auto& [group, loc, view] = data;
        // Set views during draw call since we can be sure the correct ones are bound at that point
        if (!callLocation && view == 0)
        {
            resource_view active_rtv = GetCurrentResourceView(cmd_list, deviceData, group, commandListData, layoutIndex, action);

            if (active_rtv != 0)
            {
                view = active_rtv;
            }
            else if(group->getRequeueAfterRTMatchingFailure())
            {
                // Leave loaded up in the effect/bind list and re-issue command on RT change
                it++;
                continue;
            }
            else
            {
                it = queue.erase(it);
                continue;
            }
        }

        // Queue updates depending on the place their supposed to be called at
        if (view != 0 && (!callLocation && !loc || callLocation & loc))
        {
            immediateQueue.insert(name);
        }

        it++;
    }
}

void RenderingManager::RenderEffects(command_list* cmd_list, uint64_t callLocation, uint64_t invocation)
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

    if (deviceData.current_runtime == nullptr || (commandListData.ps.techniquesToRender.size() == 0 && commandListData.vs.techniquesToRender.size() == 0 && commandListData.cs.techniquesToRender.size() == 0)) {
        return;
    }

    bool toRender = false;
    unordered_set<string> psToRenderNames;
    unordered_set<string> vsToRenderNames;
    unordered_set<string> csToRenderNames;

    if (invocation & MATCH_EFFECT_PS)
    {
        _QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.ps.techniquesToRender, psToRenderNames, callLocation, 0, MATCH_EFFECT_PS);
    }

    if (invocation & MATCH_EFFECT_VS)
    {
        _QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.vs.techniquesToRender, vsToRenderNames, callLocation, 1, MATCH_EFFECT_VS);
    }

    if (invocation & MATCH_EFFECT_CS)
    {
        _QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.cs.techniquesToRender, csToRenderNames, callLocation, 2, MATCH_EFFECT_CS);
    }

    bool rendered = false;
    vector<string> psRemovalList;
    vector<string> vsRemovalList;
    vector<string> csRemovalList;

    if (psToRenderNames.size() == 0 && vsToRenderNames.size() == 0)
    {
        return;
    }

    deviceData.current_runtime->render_effects(cmd_list, resource_view{ 0 }, resource_view{ 0 });

    unique_lock<shared_mutex> dev_mutex(render_mutex);
    rendered =
        (psToRenderNames.size() > 0) && _RenderEffects(cmd_list, deviceData, commandListData.ps.techniquesToRender, psRemovalList, psToRenderNames) ||
        (vsToRenderNames.size() > 0) && _RenderEffects(cmd_list, deviceData, commandListData.vs.techniquesToRender, vsRemovalList, vsToRenderNames) ||
        (csToRenderNames.size() > 0) && _RenderEffects(cmd_list, deviceData, commandListData.cs.techniquesToRender, csRemovalList, csToRenderNames);
    dev_mutex.unlock();

    for (auto& g : psRemovalList)
    {
        commandListData.ps.techniquesToRender.erase(g);
    }

    for (auto& g : vsRemovalList)
    {
        commandListData.vs.techniquesToRender.erase(g);
    }

    for (auto& g : csRemovalList)
    {
        commandListData.cs.techniquesToRender.erase(g);
    }

    if (rendered)
    {
        cmd_list->get_private_data<state_tracking>().apply(cmd_list);
    }
}


void RenderingManager::UpdatePreview(command_list* cmd_list, uint64_t callLocation, uint64_t invocation)
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

    if (deviceData.current_runtime == nullptr || uiData.GetToggleGroupIdShaderEditing() < 0) {
        return;
    }

    const ToggleGroup& group = uiData.GetToggleGroups().at(uiData.GetToggleGroupIdShaderEditing());

    // Set views during draw call since we can be sure the correct ones are bound at that point
    if (!callLocation && deviceData.huntPreview.target_rtv == 0)
    {
        resource_view active_rtv = resource_view{ 0 };

        if (invocation & MATCH_PREVIEW_PS)
        {
            active_rtv = GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 0, invocation & MATCH_PREVIEW_PS);
        }
        else if(invocation & MATCH_PREVIEW_VS)
        {
            active_rtv = GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 1, invocation & MATCH_PREVIEW_VS);
        }
        else if (invocation & MATCH_PREVIEW_CS)
        {
            active_rtv = GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 2, invocation & MATCH_PREVIEW_CS);
        }

        if (active_rtv != 0)
        {
            resource res = device->get_resource_from_view(active_rtv);
            resource_desc desc = device->get_resource_desc(res);

            deviceData.huntPreview.target_rtv = active_rtv;
            deviceData.huntPreview.format = desc.texture.format;
            deviceData.huntPreview.width = desc.texture.width;
            deviceData.huntPreview.height = desc.texture.height;
        }
        else
        {
            return;
        }
    }

    if (deviceData.huntPreview.target_rtv == 0 || !(!callLocation && !deviceData.huntPreview.target_invocation_location || callLocation & deviceData.huntPreview.target_invocation_location))
    {
        return;
    }
    
    if (group.getId() == uiData.GetToggleGroupIdShaderEditing() && !deviceData.huntPreview.matched)
    {
        resource rs = device->get_resource_from_view(deviceData.huntPreview.target_rtv);

        if (rs == 0)
        {
            return;
        }

        if (!resourceManager.IsCompatibleWithPreviewFormat(deviceData.current_runtime, rs))
        {
            deviceData.huntPreview.target_desc = cmd_list->get_device()->get_resource_desc(rs);
            deviceData.huntPreview.recreate_preview = true;
        }
        else
        {
            resource previewRes = resource{ 0 };
            resourceManager.SetPreviewViewHandles(&previewRes, nullptr, nullptr);

            if (previewRes != 0)
            {
                cmd_list->copy_resource(rs, previewRes);
            }
        }

        deviceData.huntPreview.matched = true;
    }
}

void RenderingManager::InitTextureBingings(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    // Init empty texture
    CreateTextureBinding(runtime, &empty_res, &empty_srv, &empty_rtv, reshade::api::format::r8g8b8a8_unorm);

    // Initialize texture bindings with default format
    for (auto& [_,group] : uiData.GetToggleGroups())
    {
        if (group.isProvidingTextureBinding() && group.getTextureBindingName().length() > 0)
        {
            resource res = { 0 };
            resource_view srv = { 0 };
            resource_view rtv = { 0 };

            unique_lock<shared_mutex> lock(binding_mutex);
            if (group.getCopyTextureBinding() && CreateTextureBinding(runtime, &res, &srv, &rtv, reshade::api::format::r8g8b8a8_unorm))
            {
                data.bindingMap[group.getTextureBindingName()] = TextureBindingData{ res, reshade::api::format::r8g8b8a8_unorm, rtv, srv, 0, 0, group.getClearBindings(), group.getCopyTextureBinding(), false };
                runtime->update_texture_bindings(group.getTextureBindingName().c_str(), srv);
            }
            else if (!group.getCopyTextureBinding())
            {
                data.bindingMap[group.getTextureBindingName()] = TextureBindingData{ resource { 0 }, format::unknown, resource_view { 0 }, resource_view { 0 }, 0, 0, group.getClearBindings(), group.getCopyTextureBinding(), false};
                runtime->update_texture_bindings(group.getTextureBindingName().c_str(), resource_view{ 0 }, resource_view{ 0 });
            }
        }
    }
}

void RenderingManager::DisposeTextureBindings(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    unique_lock<shared_mutex> lock(binding_mutex);

    if (empty_res != 0)
    {
        runtime->get_device()->destroy_resource(empty_res);
    }

    if (empty_srv != 0)
    {
        runtime->get_device()->destroy_resource_view(empty_srv);
    }

    if (empty_rtv != 0)
    {
        runtime->get_device()->destroy_resource_view(empty_rtv);
    }

    for (auto& [bindingName,_] : data.bindingMap)
    {
        DestroyTextureBinding(runtime, bindingName);
    }

    data.bindingMap.clear();
}

bool RenderingManager::_CreateTextureBinding(reshade::api::effect_runtime* runtime,
    reshade::api::resource* res,
    reshade::api::resource_view* srv,
    reshade::api::resource_view* rtv,
    reshade::api::format format,
    uint32_t width,
    uint32_t height)
{
    runtime->get_command_queue()->wait_idle();

    if (!runtime->get_device()->create_resource(
        resource_desc(width, height, 1, 1, format_to_typeless(format), 1, memory_heap::gpu_only, resource_usage::copy_dest | resource_usage::shader_resource | resource_usage::render_target),
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

    if (!runtime->get_device()->create_resource_view(*res, resource_usage::render_target, resource_view_desc(format_to_default_typed(format, 0)), rtv))
    {
        reshade::log_message(reshade::log_level::error, "Failed to create texture binding resource view!");
        return false;
    }

    return true;
}

bool RenderingManager::CreateTextureBinding(effect_runtime* runtime, resource* res, resource_view* srv, resource_view* rtv, const resource_desc& desc)
{
    reshade::api::format format = desc.texture.format;

    uint32_t frame_width, frame_height;
    frame_height = desc.texture.height;
    frame_width = desc.texture.width;

    return _CreateTextureBinding(runtime, res, srv, rtv, format, frame_width, frame_height);
}

bool RenderingManager::CreateTextureBinding(effect_runtime* runtime, resource* res, resource_view* srv, resource_view* rtv, reshade::api::format format)
{
    uint32_t frame_width, frame_height;
    runtime->get_screenshot_width_and_height(&frame_width, &frame_height);

    return _CreateTextureBinding(runtime, res, srv, rtv, format, frame_width, frame_height);
}

void RenderingManager::DestroyTextureBinding(effect_runtime* runtime, const string& binding)
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
        bindingData.rtv = { 0 };
        bindingData.srv = { 0 };
        bindingData.format = format::unknown;
        bindingData.width = 0;
        bindingData.height = 0;
    }
}


uint32_t RenderingManager::UpdateTextureBinding(effect_runtime* runtime, const string& binding, const resource_desc& desc)
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

        if (format != oldFormat || oldWidth != width || oldHeight != height)
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


void RenderingManager::_UpdateTextureBindings(command_list* cmd_list,
    DeviceDataContainer& deviceData,
    const unordered_map<string, tuple<ToggleGroup*, uint64_t, resource_view>>& bindingsToUpdate,
    vector<string>& removalList,
    const unordered_set<string>& toUpdateBindings)
{
    for (const auto& [bindingName,bindingData] : bindingsToUpdate)
    {
        if (toUpdateBindings.contains(bindingName) && !deviceData.bindingsUpdated.contains(bindingName))
        {
            effect_runtime* runtime = deviceData.current_runtime;

            resource_view active_rtv = std::get<2>(bindingData);

            if (active_rtv == 0)
            {
                continue;
            }

            auto it = deviceData.bindingMap.find(bindingName);

            if (it != deviceData.bindingMap.end())
            {
                auto& [bindingName, bindingData] = *it;
                resource res = runtime->get_device()->get_resource_from_view(active_rtv);

                if (res == 0)
                {
                    continue;
                }

                if (!bindingData.copy)
                {
                    resource_view view_non_srgb = { 0 };
                    resource_view view_srgb = { 0 };

                    resourceManager.SetShaderResourceViewHandles(res.handle, &view_non_srgb, &view_srgb);

                    if (view_non_srgb == 0)
                    {
                        return;
                    }

                    resource_desc resDesc = runtime->get_device()->get_resource_desc(res);

                    resource target_res = bindingData.res;

                    if (target_res != res)
                    {
                        runtime->update_texture_bindings(bindingName.c_str(), view_non_srgb, view_srgb);

                        bindingData.res = res;
                        bindingData.format = resDesc.texture.format;
                        bindingData.srv = view_non_srgb;
                        bindingData.rtv = { 0 };
                        bindingData.width = resDesc.texture.width;
                        bindingData.height = resDesc.texture.height;
                    }

                    bindingData.reset = false;
                }
                else
                {
                    resource_desc resDesc = runtime->get_device()->get_resource_desc(res);

                    uint32_t retUpdate = UpdateTextureBinding(runtime, bindingName, resDesc);

                    resource target_res = bindingData.res;

                    if (retUpdate && target_res != 0)
                    {
                        cmd_list->copy_resource(res, target_res);
                        bindingData.reset = false;
                    }
                }

                deviceData.bindingsUpdated.emplace(bindingName);
                removalList.push_back(bindingName);
            }
        }
    }
}

void RenderingManager::UpdateTextureBindings(command_list* cmd_list, uint64_t callLocation, uint64_t invocation)
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
        _QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.ps.bindingsToUpdate, psToUpdateBindings, callLocation, 0, MATCH_BINDING_PS);
    }

    if (invocation & MATCH_BINDING_VS)
    {
        _QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.vs.bindingsToUpdate, vsToUpdateBindings, callLocation, 1, MATCH_BINDING_VS);
    }

    if (invocation & MATCH_BINDING_CS)
    {
        _QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.cs.bindingsToUpdate, csToUpdateBindings, callLocation, 2, MATCH_BINDING_CS);
    }

    if (psToUpdateBindings.size() == 0 && vsToUpdateBindings.size() == 0 && csToUpdateBindings.size() == 0)
    {
        return;
    }

    vector<string> psRemovalList;
    vector<string> vsRemovalList;
    vector<string> csRemovalList;

    unique_lock<shared_mutex> mtx(binding_mutex);
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

void RenderingManager::ClearUnmatchedTextureBindings(reshade::api::command_list* cmd_list)
{
    DeviceDataContainer& data = cmd_list->get_device()->get_private_data<DeviceDataContainer>();

    shared_lock<shared_mutex> mtx(binding_mutex);
    if (data.bindingMap.size() == 0)
    {
        return;
    }

    static const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    for (auto& [bindingName,bindingData] : data.bindingMap)
    {
        if (data.bindingsUpdated.contains(bindingName) || !bindingData.enabled_reset_on_miss || bindingData.reset)
        {
            continue;
        }

        if (!bindingData.copy)
        {
            data.current_runtime->update_texture_bindings(bindingName.c_str(), empty_srv);

            bindingData.res = { 0 };
            bindingData.srv = { 0 };
            bindingData.rtv = { 0 };
            bindingData.width = 0;
            bindingData.height = 0;
        }
        else
        {
            resource_view rtv = bindingData.rtv;

            if (rtv != 0)
            {
                cmd_list->clear_render_target_view(rtv, clearColor);
            }
        }

        bindingData.reset = true;
    }

    if (!data.huntPreview.matched && uiData.GetToggleGroupIdShaderEditing() >= 0)
    {
        resource_view rtv = resource_view{ 0 };
        resourceManager.SetPreviewViewHandles(nullptr, &rtv, nullptr);
        if (rtv != 0)
        {
            cmd_list->clear_render_target_view(rtv, clearColor);
        }
    }
}

static void clearStage(CommandListDataContainer& commandListData, effect_queue& queuedTasks, uint64_t pipelineChange, uint64_t clearFlag, uint64_t location)
{
    if (queuedTasks.size() > 0 && (pipelineChange & clearFlag))
    {
        for (auto it = queuedTasks.begin(); it != queuedTasks.end();)
        {
            uint64_t callLocation = std::get<1>(it->second);
            if (callLocation == location)
            {
                it = queuedTasks.erase(it);
                continue;
            }
            it++;
        }
    }
}

void RenderingManager::ClearQueue(CommandListDataContainer& commandListData, const uint64_t pipelineChange, const uint64_t location) const
{
    const uint64_t qloc = pipelineChange << (location * Rendering::MATCH_DELIMITER);

    if (commandListData.commandQueue & qloc)
    {
        commandListData.commandQueue &= ~qloc;

        clearStage(commandListData, commandListData.ps.techniquesToRender, pipelineChange, MATCH_EFFECT_PS, location);
        clearStage(commandListData, commandListData.vs.techniquesToRender, pipelineChange, MATCH_EFFECT_VS, location);
        clearStage(commandListData, commandListData.cs.techniquesToRender, pipelineChange, MATCH_EFFECT_CS, location);

        clearStage(commandListData, commandListData.ps.bindingsToUpdate, pipelineChange, MATCH_BINDING_PS, location);
        clearStage(commandListData, commandListData.vs.bindingsToUpdate, pipelineChange, MATCH_BINDING_VS, location);
        clearStage(commandListData, commandListData.cs.bindingsToUpdate, pipelineChange, MATCH_BINDING_CS, location);
    }
}