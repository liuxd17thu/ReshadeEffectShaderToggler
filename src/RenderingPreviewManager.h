#pragma once

#include "RenderingManager.h"

namespace Rendering
{
    class __declspec(novtable) RenderingPreviewManager final
    {
    public:
        RenderingPreviewManager(AddonImGui::AddonUIData& data, ResourceManager& rManager);
        ~RenderingPreviewManager();

        void OnInitDevice(reshade::api::device* device);
        void OnDestroyDevice(reshade::api::device* device);
        void CopyResource(reshade::api::command_list* cmd_list, reshade::api::resource_view srv_src, reshade::api::resource_view rtv_dst, uint32_t width, uint32_t height);
        void UpdatePreview(reshade::api::command_list* cmd_list, uint64_t callLocation, uint64_t invocation);
        const std::tuple<reshade::api::resource_view, bool> GetCurrentPreviewResourceView(reshade::api::command_list* cmd_list, DeviceDataContainer& deviceData, const ShaderToggler::ToggleGroup* group, CommandListDataContainer& commandListData, uint32_t descIndex, uint64_t action);

    private:
        AddonImGui::AddonUIData& uiData;
        ResourceManager& resourceManager;

        reshade::api::pipeline copyPipeline;
        reshade::api::pipeline_layout copyPipelineLayout;
        reshade::api::sampler copyPipelineSampler;
    };
}