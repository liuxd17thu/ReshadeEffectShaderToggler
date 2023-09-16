#include "RenderingPreviewManager.h"
#include "StateTracking.h"

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;
using namespace std;

RenderingPreviewManager::RenderingPreviewManager(AddonImGui::AddonUIData& data, ResourceManager& rManager) : uiData(data), resourceManager(rManager)
{
}

RenderingPreviewManager::~RenderingPreviewManager()
{

}



void RenderingPreviewManager::UpdatePreview(command_list* cmd_list, uint64_t callLocation, uint64_t invocation)
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
            active_rtv = RenderingManager::GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 0, invocation & MATCH_PREVIEW_PS);
        }
        else if (invocation & MATCH_PREVIEW_VS)
        {
            active_rtv = RenderingManager::GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 1, invocation & MATCH_PREVIEW_VS);
        }
        else if (invocation & MATCH_PREVIEW_CS)
        {
            active_rtv = RenderingManager::GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 2, invocation & MATCH_PREVIEW_CS);
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