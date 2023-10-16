#pragma once

#include "RenderingManager.h"

namespace Rendering
{
    class __declspec(novtable) RenderingEffectManager final
    {
    public:
        RenderingEffectManager(AddonImGui::AddonUIData& data, ResourceManager& rManager);
        ~RenderingEffectManager();

        void RenderEffects(reshade::api::command_list* cmd_list, uint64_t callLocation = CALL_DRAW, uint64_t invocation = MATCH_NONE);
        bool RenderRemainingEffects(reshade::api::effect_runtime* runtime);
    private:
        AddonImGui::AddonUIData& uiData;
        ResourceManager& resourceManager;

        bool _RenderEffects(
            reshade::api::command_list* cmd_list,
            DeviceDataContainer& deviceData,
            const std::unordered_map<std::string, std::tuple<ShaderToggler::ToggleGroup*, uint64_t, reshade::api::resource>>& techniquesToRender,
            std::vector<std::string>& removalList,
            const std::unordered_set<std::string>& toRenderNames);
    };
}