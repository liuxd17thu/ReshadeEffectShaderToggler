#pragma once

#include <vector>
#include <unordered_map>
#include <tuple>
#include "CDataFile.h"
#include "ToggleGroup.h"
#include "PipelineStateTracker.h"

using effect_queue = std::unordered_map<std::string, std::tuple<ShaderToggler::ToggleGroup*, uint64_t, reshade::api::resource_view>>;

struct __declspec(novtable) ShaderData final {
    uint32_t activeShaderHash = -1;
    effect_queue bindingsToUpdate;
    std::unordered_set<ShaderToggler::ToggleGroup*> constantBuffersToUpdate;
    effect_queue techniquesToRender;
    std::unordered_set<ShaderToggler::ToggleGroup*> srvToUpdate;
    const std::vector<ShaderToggler::ToggleGroup*>* blockedShaderGroups = nullptr;
    uint32_t id = 0;

    ShaderData(uint32_t _id) : id(_id) { }

    void Reset()
    {
        activeShaderHash = -1;
        bindingsToUpdate.clear();
        constantBuffersToUpdate.clear();
        techniquesToRender.clear();
        srvToUpdate.clear();
        blockedShaderGroups = nullptr;
    }
};

struct __declspec(uuid("222F7169-3C09-40DB-9BC9-EC53842CE537")) CommandListDataContainer {
    uint64_t commandQueue = 0;
    StateTracker::PipelineStateTracker stateTracker;
    ShaderData ps{ 0 };
    ShaderData vs{ 1 };
    ShaderData cs{ 2 };

    void Reset()
    {
        ps.Reset();
        vs.Reset();
        cs.Reset();
        stateTracker.Reset();

        commandQueue = 0;
    }
};

struct __declspec(novtable) TextureBindingData final
{
    reshade::api::resource res;
    reshade::api::format format;
    reshade::api::resource_view rtv;
    reshade::api::resource_view srv;
    uint32_t width;
    uint32_t height;
    bool enabled_reset_on_miss;
    bool copy;
    bool reset = false;
};

struct __declspec(novtable) HuntPreview final
{
    resource_view target_rtv = resource_view{ 0 };
    bool matched = false;
    uint64_t target_invocation_location = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    reshade::api::format format = reshade::api::format::unknown;
    uint32_t targets_sum = 0;

    void Reset()
    {
        matched = false;
        target_rtv = resource_view{ 0 };
        target_invocation_location = 0;
        width = 0;
        height = 0;
        format = reshade::api::format::unknown;
        targets_sum = 0;
    }
};

struct __declspec(uuid("C63E95B1-4E2F-46D6-A276-E8B4612C069A")) DeviceDataContainer {
    reshade::api::effect_runtime* current_runtime = nullptr;
    atomic_bool rendered_effects = false;
    std::unordered_map<std::string, bool> allEnabledTechniques;
    std::unordered_map<std::string, TextureBindingData> bindingMap;
    std::unordered_set<std::string> bindingsUpdated;
    std::unordered_set<const ShaderToggler::ToggleGroup*> constantsUpdated;
    std::unordered_set<const ShaderToggler::ToggleGroup*> srvUpdated;
    std::unordered_map<uint64_t, std::vector<bool>> transient_mask;
    bool reload_bindings = false;
    HuntPreview huntPreview;
};