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
    constexpr uint64_t CALL_DRAW = 0;
    constexpr uint64_t CALL_BIND_PIPELINE = 1;
    constexpr uint64_t CALL_BIND_RENDER_TARGET = 2;

    constexpr uint64_t MATCH_NONE       = 0b000000000000;
    constexpr uint64_t MATCH_EFFECT_PS  = 0b000000000001; // 0
    constexpr uint64_t MATCH_EFFECT_VS  = 0b000000000010; // 1
    constexpr uint64_t MATCH_EFFECT_CS  = 0b000000000100; // 2
    constexpr uint64_t MATCH_BINDING_PS = 0b000000001000; // 3
    constexpr uint64_t MATCH_BINDING_VS = 0b000000010000; // 4
    constexpr uint64_t MATCH_BINDING_CS = 0b000000100000; // 5
    constexpr uint64_t MATCH_CONST_PS   = 0b000001000000; // 6
    constexpr uint64_t MATCH_CONST_VS   = 0b000010000000; // 7
    constexpr uint64_t MATCH_CONST_CS   = 0b000100000000; // 8
    constexpr uint64_t MATCH_PREVIEW_PS = 0b001000000000; // 9
    constexpr uint64_t MATCH_PREVIEW_VS = 0b010000000000; // 10
    constexpr uint64_t MATCH_PREVIEW_CS = 0b100000000000; // 11

    constexpr uint64_t MATCH_ALL        = 0b111111111111;
    constexpr uint64_t MATCH_EFFECT     = 0b000000000111;
    constexpr uint64_t MATCH_BINDING    = 0b000000111000;
    constexpr uint64_t MATCH_CONST      = 0b000111000000;
    constexpr uint64_t MATCH_PREVIEW    = 0b111000000000;
    constexpr uint64_t MATCH_PS         = 0b001001001001;
    constexpr uint64_t MATCH_VS         = 0b010010010010;
    constexpr uint64_t MATCH_CS         = 0b100100100100;
    constexpr uint64_t MATCH_DELIMITER  = 12;

    constexpr uint64_t CHECK_MATCH_DRAW         = MATCH_ALL << (CALL_DRAW * MATCH_DELIMITER);
    constexpr uint64_t CHECK_MATCH_DRAW_EFFECT  = MATCH_EFFECT << (CALL_DRAW * MATCH_DELIMITER);
    constexpr uint64_t CHECK_MATCH_DRAW_BINDING = MATCH_BINDING << (CALL_DRAW * MATCH_DELIMITER);
    constexpr uint64_t CHECK_MATCH_DRAW_PREVIEW = MATCH_PREVIEW << (CALL_DRAW * MATCH_DELIMITER);

    constexpr uint64_t CHECK_MATCH_BIND_PIPELINE         = MATCH_ALL << (CALL_BIND_PIPELINE * MATCH_DELIMITER);
    constexpr uint64_t CHECK_MATCH_BIND_PIPELINE_EFFECT  = MATCH_EFFECT << (CALL_BIND_PIPELINE * MATCH_DELIMITER);
    constexpr uint64_t CHECK_MATCH_BIND_PIPELINE_BINDING = MATCH_BINDING << (CALL_BIND_PIPELINE * MATCH_DELIMITER);
    constexpr uint64_t CHECK_MATCH_BIND_PIPELINE_PREVIEW = MATCH_PREVIEW << (CALL_BIND_PIPELINE * MATCH_DELIMITER);

    constexpr uint64_t CHECK_MATCH_BIND_RENDERTARGET         = MATCH_ALL << (CALL_BIND_RENDER_TARGET * MATCH_DELIMITER);
    constexpr uint64_t CHECK_MATCH_BIND_RENDERTARGET_EFFECT  = MATCH_EFFECT << (CALL_BIND_RENDER_TARGET * MATCH_DELIMITER);
    constexpr uint64_t CHECK_MATCH_BIND_RENDERTARGET_BINDING = MATCH_BINDING << (CALL_BIND_RENDER_TARGET * MATCH_DELIMITER);
    constexpr uint64_t CHECK_MATCH_BIND_RENDERTARGET_PREVIEW = MATCH_PREVIEW << (CALL_BIND_RENDER_TARGET * MATCH_DELIMITER);

    class __declspec(novtable) RenderingManager final
    {
    public:
        RenderingManager(AddonImGui::AddonUIData& data, ResourceManager& rManager);
        ~RenderingManager();

        const reshade::api::resource_view GetCurrentResourceView(reshade::api::command_list* cmd_list, DeviceDataContainer& deviceData, ShaderToggler::ToggleGroup* group, CommandListDataContainer& commandListData, uint32_t descIndex, uint64_t action);
        const reshade::api::resource_view GetCurrentPreviewResourceView(reshade::api::command_list* cmd_list, DeviceDataContainer& deviceData, const ShaderToggler::ToggleGroup* group, CommandListDataContainer& commandListData, uint32_t descIndex, uint64_t action);
        void UpdatePreview(reshade::api::command_list* cmd_list, uint64_t callLocation, uint64_t invocation);
        void RenderEffects(reshade::api::command_list* cmd_list, uint64_t callLocation = CALL_DRAW, uint64_t invocation = MATCH_NONE);
        bool RenderRemainingEffects(reshade::api::effect_runtime* runtime);

        bool CreateTextureBinding(reshade::api::effect_runtime* runtime, reshade::api::resource* res, reshade::api::resource_view* srv, reshade::api::resource_view* rtv, const resource_desc& desc);
        bool CreateTextureBinding(reshade::api::effect_runtime* runtime, reshade::api::resource* res, reshade::api::resource_view* srv, reshade::api::resource_view* rtv, reshade::api::format format);
        uint32_t UpdateTextureBinding(reshade::api::effect_runtime* runtime, const std::string& binding, const resource_desc& desc);
        void DestroyTextureBinding(reshade::api::effect_runtime* runtime, const std::string& binding);
        void InitTextureBingings(reshade::api::effect_runtime* runtime);
        void DisposeTextureBindings(reshade::api::effect_runtime* runtime);
        void UpdateTextureBindings(reshade::api::command_list* cmd_list, uint64_t callLocation = CALL_DRAW, uint64_t invocation = MATCH_NONE);
        void ClearUnmatchedTextureBindings(reshade::api::command_list* cmd_list);

        void _CheckCallForCommandList(ShaderData& sData, CommandListDataContainer& commandListData, DeviceDataContainer& deviceData) const;
        void CheckCallForCommandList(reshade::api::command_list* commandList);

        void RescheduleGroups(CommandListDataContainer& commandListData, DeviceDataContainer& deviceData);

        void ClearQueue(CommandListDataContainer& commandListData, const uint64_t pipelineChange) const;

        static void EnumerateTechniques(reshade::api::effect_runtime* runtime, std::function<void(reshade::api::effect_runtime*, reshade::api::effect_technique, std::string&)> func);
    private:
        bool _RenderEffects(
            reshade::api::command_list* cmd_list,
            DeviceDataContainer& deviceData,
            const std::unordered_map<std::string, std::tuple<ShaderToggler::ToggleGroup*, uint64_t, reshade::api::resource_view>>& techniquesToRender,
            std::vector<std::string>& removalList,
            const std::unordered_set<std::string>& toRenderNames);
        void _UpdateTextureBindings(reshade::api::command_list* cmd_list,
            DeviceDataContainer& deviceData,
            const std::unordered_map<std::string, std::tuple<ShaderToggler::ToggleGroup*, uint64_t, reshade::api::resource_view>>& bindingsToUpdate,
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
            std::unordered_map<std::string, std::tuple<ShaderToggler::ToggleGroup*, uint64_t, reshade::api::resource_view>>& queue,
            std::unordered_set<std::string>& immediateQueue,
            uint64_t callLocation,
            uint32_t layoutIndex,
            uint64_t action);
        void _RescheduleGroups(ShaderData& sData, CommandListDataContainer& commandListData, DeviceDataContainer& deviceData);

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