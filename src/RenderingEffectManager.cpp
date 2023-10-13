#include "RenderingEffectManager.h"
#include "StateTracking.h"

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;
using namespace std;

RenderingEffectManager::RenderingEffectManager(AddonImGui::AddonUIData& data, ResourceManager& rManager) : uiData(data), resourceManager(rManager)
{
}

RenderingEffectManager::~RenderingEffectManager()
{

}

bool RenderingEffectManager::RenderRemainingEffects(effect_runtime* runtime)
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

    RenderingManager::EnumerateTechniques(deviceData.current_runtime, [&deviceData, &commandListData, &cmd_list, &device, &active_rtv, &active_rtv_srgb, &rendered, &res](effect_runtime* runtime, effect_technique technique, string& name) {
        if (deviceData.allEnabledTechniques.contains(name) && !deviceData.allEnabledTechniques[name].rendered)
        {
            runtime->render_technique(technique, cmd_list, active_rtv, active_rtv_srgb);

            deviceData.allEnabledTechniques[name].rendered = true;
            rendered = true;
        }
        });

    return rendered;
}

bool RenderingEffectManager::_RenderEffects(
    command_list* cmd_list,
    DeviceDataContainer& deviceData,
    const unordered_map<string, tuple<ToggleGroup*, uint64_t, resource>>& techniquesToRender,
    vector<string>& removalList,
    const unordered_set<string>& toRenderNames)
{
    bool rendered = false;
    CommandListDataContainer& cmdData = cmd_list->get_private_data<CommandListDataContainer>();

    RenderingManager::EnumerateTechniques(deviceData.current_runtime, [&cmdData, &deviceData, &techniquesToRender, &cmd_list, &rendered, &removalList, &toRenderNames, this](effect_runtime* runtime, effect_technique technique, string& name) {

        if (toRenderNames.find(name) != toRenderNames.end())
        {
            const auto& tech = techniquesToRender.find(name);
            const auto& techState = deviceData.allEnabledTechniques.find(name);

            if (tech != techniquesToRender.end() && !techState->second.rendered)
            {
                const auto& [techName, techData] = *tech;
                const auto& [group, _, active_resource] = techData;

                if (active_resource == 0)
                {
                    return;
                }

                resource_view view_non_srgb = {};
                resource_view view_srgb = {};

                resourceManager.SetResourceViewHandles(active_resource.handle, &view_non_srgb, &view_srgb);

                if (view_non_srgb == 0)
                {
                    return;
                }

                deviceData.rendered_effects = true;

                runtime->render_technique(technique, cmd_list, view_non_srgb, view_srgb);

                resource_desc resDesc = runtime->get_device()->get_resource_desc(active_resource);
                removalList.push_back(name);

                deviceData.allEnabledTechniques[name].rendered = true;
                rendered = true;
            }
        }
        });

    return rendered;
}

void RenderingEffectManager::RenderEffects(command_list* cmd_list, uint64_t callLocation, uint64_t invocation)
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
        RenderingManager::QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.ps.techniquesToRender, psToRenderNames, callLocation, 0, MATCH_EFFECT_PS);
    }

    if (invocation & MATCH_EFFECT_VS)
    {
        RenderingManager::QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.vs.techniquesToRender, vsToRenderNames, callLocation, 1, MATCH_EFFECT_VS);
    }

    if (invocation & MATCH_EFFECT_CS)
    {
        RenderingManager::QueueOrDequeue(cmd_list, deviceData, commandListData, commandListData.cs.techniquesToRender, csToRenderNames, callLocation, 2, MATCH_EFFECT_CS);
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

    unique_lock<shared_mutex> dev_mutex(deviceData.render_mutex);
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
