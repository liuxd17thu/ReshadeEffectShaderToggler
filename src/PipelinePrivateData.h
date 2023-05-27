#pragma once

#include <vector>
#include <unordered_map>
#include <tuple>
#include "CDataFile.h"
#include "ToggleGroup.h"
#include "PipelineStateTracker.h"

struct __declspec(novtable) ShaderData final {
    uint32_t activeShaderHash = -1;
    std::unordered_map<std::string, std::tuple<const ShaderToggler::ToggleGroup*, uint32_t, reshade::api::resource_view>> bindingsToUpdate;
    std::unordered_set<const ShaderToggler::ToggleGroup*> constantBuffersToUpdate;
    std::unordered_map<std::string, std::tuple<const ShaderToggler::ToggleGroup*, uint32_t, reshade::api::resource_view>> techniquesToRender;
    std::unordered_set<const ShaderToggler::ToggleGroup*> srvToUpdate;
    const std::vector<ShaderToggler::ToggleGroup*>* blockedShaderGroups = nullptr;
    uint32_t id = 0;

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
    uint32_t commandQueue = 0;
    StateTracker::PipelineStateTracker stateTracker;
    ShaderData ps;
    ShaderData vs;

    void Reset()
    {
        ps.Reset();
        vs.Reset();
        stateTracker.Reset();

        commandQueue = 0;
    }
};

struct __declspec(uuid("C63E95B1-4E2F-46D6-A276-E8B4612C069A")) DeviceDataContainer {
    reshade::api::effect_runtime* current_runtime = nullptr;
    atomic_bool rendered_effects = false;
    std::unordered_map<std::string, bool> allEnabledTechniques;
    std::unordered_map<std::string, std::tuple<reshade::api::resource, reshade::api::format, reshade::api::resource_view, reshade::api::resource_view, uint32_t, uint32_t, bool>> bindingMap;
    std::unordered_set<std::string> bindingsUpdated;
    std::unordered_set<const ShaderToggler::ToggleGroup*> constantsUpdated;
    std::unordered_set<const ShaderToggler::ToggleGroup*> srvUpdated;
    std::unordered_map<uint64_t, std::vector<bool>> transient_mask;
};