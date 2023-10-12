#pragma once

#include "RenderingManager.h"

namespace Rendering
{
    class __declspec(novtable) RenderingBindingManager final
    {
    public:
        RenderingBindingManager(AddonImGui::AddonUIData& data, ResourceManager& rManager);
        ~RenderingBindingManager();

        bool CreateTextureBinding(reshade::api::effect_runtime* runtime, reshade::api::resource* res, reshade::api::resource_view* srv, const reshade::api::resource_desc& desc);
        bool CreateTextureBinding(reshade::api::effect_runtime* runtime, reshade::api::resource* res, reshade::api::resource_view* srv, reshade::api::format format);
        uint32_t UpdateTextureBinding(reshade::api::effect_runtime* runtime, const std::string& binding, const reshade::api::resource_desc& desc);
        void DestroyTextureBinding(reshade::api::effect_runtime* runtime, const std::string& binding);
        void InitTextureBingings(reshade::api::effect_runtime* runtime);
        void DisposeTextureBindings(reshade::api::effect_runtime* runtime);
        void UpdateTextureBindings(reshade::api::command_list* cmd_list, uint64_t callLocation = CALL_DRAW, uint64_t invocation = MATCH_NONE);
        void ClearUnmatchedTextureBindings(reshade::api::command_list* cmd_list);
    private:
        AddonImGui::AddonUIData& uiData;
        ResourceManager& resourceManager;

        reshade::api::resource empty_res = { 0 };
        reshade::api::resource_view empty_srv = { 0 };

        void _UpdateTextureBindings(reshade::api::command_list* cmd_list,
            DeviceDataContainer& deviceData,
            const std::unordered_map<std::string, std::tuple<ShaderToggler::ToggleGroup*, uint64_t, reshade::api::resource>>& bindingsToUpdate,
            std::vector<std::string>& removalList,
            const std::unordered_set<std::string>& toUpdateBindings);
        bool _CreateTextureBinding(reshade::api::effect_runtime* runtime,
            reshade::api::resource* res,
            reshade::api::resource_view* srv,
            reshade::api::format format,
            uint32_t width,
            uint32_t height,
            uint16_t levels);
    };
}