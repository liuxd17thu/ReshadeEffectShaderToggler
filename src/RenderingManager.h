#pragma once

#include <reshade.hpp>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include "ToggleGroup.h"
#include "PipelinePrivateData.h"
#include "AddonUIData.h"
#include "ResourceManager.h"

namespace Rendering
{
    static constexpr uint32_t CALL_DRAW = 0;
    static constexpr uint32_t CALL_BIND_PIPELINE = 1;
    static constexpr uint32_t CALL_BIND_RENDER_TARGET = 2;

    static constexpr uint32_t MATCH_NONE       = 0b00000000;
    static constexpr uint32_t MATCH_EFFECT_PS  = 0b00000001; // 0
    static constexpr uint32_t MATCH_EFFECT_VS  = 0b00000010; // 1
    static constexpr uint32_t MATCH_BINDING_PS = 0b00000100; // 2
    static constexpr uint32_t MATCH_BINDING_VS = 0b00001000; // 3
    static constexpr uint32_t MATCH_CONST_PS   = 0b00010000; // 4
    static constexpr uint32_t MATCH_CONST_VS   = 0b00100000; // 5
    static constexpr uint32_t MATCH_PREVIEW_PS = 0b01000000; // 6
    static constexpr uint32_t MATCH_PREVIEW_VS = 0b10000000; // 7

    static constexpr uint32_t MATCH_ALL        = 0b11111111;
    static constexpr uint32_t MATCH_EFFECT     = 0b00000011;
    static constexpr uint32_t MATCH_BINDING    = 0b00001100;
    static constexpr uint32_t MATCH_CONST      = 0b00110000;
    static constexpr uint32_t MATCH_PREVIEW    = 0b11000000;
    static constexpr uint32_t MATCH_PS         = 0b01010101;
    static constexpr uint32_t MATCH_VS         = 0b10101010;
    static constexpr uint32_t MATCH_DELIMITER  = 8;

    static constexpr uint32_t CHECK_MATCH_DRAW         = MATCH_ALL << (CALL_DRAW * MATCH_DELIMITER);
    static constexpr uint32_t CHECK_MATCH_DRAW_EFFECT  = MATCH_EFFECT << (CALL_DRAW * MATCH_DELIMITER);
    static constexpr uint32_t CHECK_MATCH_DRAW_BINDING = MATCH_BINDING << (CALL_DRAW * MATCH_DELIMITER);
    static constexpr uint32_t CHECK_MATCH_DRAW_PREVIEW = MATCH_PREVIEW << (CALL_DRAW * MATCH_DELIMITER);

    static constexpr uint32_t CHECK_MATCH_BIND_PIPELINE         = MATCH_ALL << (CALL_BIND_PIPELINE * MATCH_DELIMITER);
    static constexpr uint32_t CHECK_MATCH_BIND_PIPELINE_EFFECT  = MATCH_EFFECT << (CALL_BIND_PIPELINE * MATCH_DELIMITER);
    static constexpr uint32_t CHECK_MATCH_BIND_PIPELINE_BINDING = MATCH_BINDING << (CALL_BIND_PIPELINE * MATCH_DELIMITER);
    static constexpr uint32_t CHECK_MATCH_BIND_PIPELINE_PREVIEW = MATCH_PREVIEW << (CALL_BIND_PIPELINE * MATCH_DELIMITER);

    static constexpr uint32_t CHECK_MATCH_BIND_RENDERTARGET         = MATCH_ALL << (CALL_BIND_RENDER_TARGET * MATCH_DELIMITER);
    static constexpr uint32_t CHECK_MATCH_BIND_RENDERTARGET_EFFECT  = MATCH_EFFECT << (CALL_BIND_RENDER_TARGET * MATCH_DELIMITER);
    static constexpr uint32_t CHECK_MATCH_BIND_RENDERTARGET_BINDING = MATCH_BINDING << (CALL_BIND_RENDER_TARGET * MATCH_DELIMITER);
    static constexpr uint32_t CHECK_MATCH_BIND_RENDERTARGET_PREVIEW = MATCH_PREVIEW << (CALL_BIND_RENDER_TARGET * MATCH_DELIMITER);

    class __declspec(novtable) RenderingManager final
    {
    public:
        RenderingManager(AddonImGui::AddonUIData& data, ResourceManager& rManager);
        ~RenderingManager();

        const reshade::api::resource_view GetCurrentResourceView(reshade::api::command_list* cmd_list, DeviceDataContainer& deviceData, ShaderToggler::ToggleGroup* group, CommandListDataContainer& commandListData, uint32_t descIndex, uint32_t action);
        const reshade::api::resource_view GetCurrentPreviewResourceView(reshade::api::command_list* cmd_list, DeviceDataContainer& deviceData, const ShaderToggler::ToggleGroup* group, CommandListDataContainer& commandListData, uint32_t descIndex, uint32_t action);
        void UpdatePreview(reshade::api::command_list* cmd_list, uint32_t callLocation, uint32_t invocation);
        void RenderEffects(reshade::api::command_list* cmd_list, uint32_t callLocation = CALL_DRAW, uint32_t invocation = MATCH_NONE);
        bool RenderRemainingEffects(reshade::api::effect_runtime* runtime);

        bool CreateTextureBinding(reshade::api::effect_runtime* runtime, reshade::api::resource* res, reshade::api::resource_view* srv, reshade::api::resource_view* rtv, const resource_desc& desc);
        bool CreateTextureBinding(reshade::api::effect_runtime* runtime, reshade::api::resource* res, reshade::api::resource_view* srv, reshade::api::resource_view* rtv, reshade::api::format format);
        uint32_t UpdateTextureBinding(reshade::api::effect_runtime* runtime, const std::string& binding, const resource_desc& desc);
        void DestroyTextureBinding(reshade::api::effect_runtime* runtime, const std::string& binding);
        void InitTextureBingings(reshade::api::effect_runtime* runtime);
        void DisposeTextureBindings(reshade::api::effect_runtime* runtime);
        void UpdateTextureBindings(reshade::api::command_list* cmd_list, uint32_t callLocation = CALL_DRAW, uint32_t invocation = MATCH_NONE);
        void ClearUnmatchedTextureBindings(reshade::api::command_list* cmd_list);

        void _CheckCallForCommandList(ShaderData& sData, CommandListDataContainer& commandListData, DeviceDataContainer& deviceData) const;
        void CheckCallForCommandList(reshade::api::command_list* commandList);

        void ClearQueue2(CommandListDataContainer& commandListData, const uint32_t location0, const uint32_t location1) const;

        static void EnumerateTechniques(reshade::api::effect_runtime* runtime, std::function<void(reshade::api::effect_runtime*, reshade::api::effect_technique, std::string&)> func);
    private:
        bool _RenderEffects(
            reshade::api::command_list* cmd_list,
            DeviceDataContainer& deviceData,
            const std::unordered_map<std::string, std::tuple<ShaderToggler::ToggleGroup*, uint32_t, reshade::api::resource_view>>& techniquesToRender,
            std::vector<std::string>& removalList,
            const std::unordered_set<std::string>& toRenderNames);
        void _UpdateTextureBindings(reshade::api::command_list* cmd_list,
            DeviceDataContainer& deviceData,
            const std::unordered_map<std::string, std::tuple<ShaderToggler::ToggleGroup*, uint32_t, reshade::api::resource_view>>& bindingsToUpdate,
            std::vector<std::string>& removalList,
            const std::unordered_set<std::string>& toUpdateBindings);
        bool _CreateTextureBinding(reshade::api::effect_runtime* runtime,
            reshade::api::resource* res,
            reshade::api::resource_view* srv,
            reshade::api::resource_view* rtv,
            reshade::api::format format,
            uint32_t width,
            uint32_t height);
        void _QueueOrDequeue(
            command_list* cmd_list,
            DeviceDataContainer& deviceData,
            CommandListDataContainer& commandListData,
            std::unordered_map<std::string, std::tuple<ShaderToggler::ToggleGroup*, uint32_t, reshade::api::resource_view>>& queue,
            std::unordered_set<std::string>& immediateQueue,
            uint32_t callLocation,
            uint32_t layoutIndex,
            uint32_t action);

        AddonImGui::AddonUIData& uiData;
        ResourceManager& resourceManager;

        std::shared_mutex render_mutex;
        std::shared_mutex binding_mutex;

        reshade::api::resource empty_res = { 0 };
        reshade::api::resource_view empty_rtv = { 0 };
        reshade::api::resource_view empty_srv = { 0 };

        static constexpr size_t CHAR_BUFFER_SIZE = 256;
        static size_t g_charBufferSize;
        static char g_charBuffer[CHAR_BUFFER_SIZE];
    };
}