#pragma once

#include "RenderingManager.h"

namespace Rendering
{
    class __declspec(novtable) RenderingPreviewManager final
    {
    public:
        RenderingPreviewManager(AddonImGui::AddonUIData& data, ResourceManager& rManager);
        ~RenderingPreviewManager();

        void UpdatePreview(reshade::api::command_list* cmd_list, uint64_t callLocation, uint64_t invocation);
    private:
        AddonImGui::AddonUIData& uiData;
        ResourceManager& resourceManager;
    };
}