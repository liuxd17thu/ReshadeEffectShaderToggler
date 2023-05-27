///////////////////////////////////////////////////////////////////////
//
// Part of ShaderToggler, a shader toggler add on for Reshade 5+ which allows you
// to define groups of shaders to toggle them on/off with one key press
// 
// (c) Frans 'Otis_Inf' Bouma.
//
// All rights reserved.
// https://github.com/FransBouma/ShaderToggler
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/////////////////////////////////////////////////////////////////////////
#include <imgui.h>
#include <reshade.hpp>
#include <vector>
#include <format>
#include <unordered_map>
#include <set>
#include <functional>
#include <tuple>
#include <chrono>
#include <filesystem>
#include <MinHook.h>
#include "crc32_hash.hpp"
#include "ShaderManager.h"
#include "CDataFile.h"
#include "ToggleGroup.h"
#include "AddonUIData.h"
#include "AddonUIDisplay.h"
#include "ConstantManager.h"
#include "PipelineStateTracker.h"
#include "PipelinePrivateData.h"
#include "ResourceManager.h"
#include "RenderingManager.h"

using namespace reshade::api;
using namespace ShaderToggler;
using namespace AddonImGui;
using namespace Shim::Constants;
using namespace StateTracker;

extern "C" __declspec(dllexport) const char* NAME = "Reshade Effect Shader Toggler";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "又名REST，允许你定义接受ReShade效果渲染的游戏着色器分组。";

constexpr auto MAX_EFFECT_HANDLES = 128;
constexpr auto REST_VAR_ANNOTATION = "source";

static filesystem::path g_dllPath;
static filesystem::path g_basePath;

static ShaderToggler::ShaderManager g_pixelShaderManager;
static ShaderToggler::ShaderManager g_vertexShaderManager;

static ConstantManager constantManager;
static ConstantHandlerBase* constantHandler = nullptr;
static ConstantCopyBase* constantCopy = nullptr;
static bool constantHandlerHooked = false;

static atomic_uint32_t g_activeCollectorFrameCounter = 0;
static vector<string> allTechniques;
static AddonUIData g_addonUIData(&g_pixelShaderManager, &g_vertexShaderManager, constantHandler, &g_activeCollectorFrameCounter, &allTechniques);

static Rendering::ResourceManager resourceManager;
static Rendering::RenderingManager renderingManager(g_addonUIData, resourceManager);

static std::shared_mutex pipeline_layout_mutex;
// TODO: was this needed? might need to re-check
//static std::shared_mutex render_mutex;
static char g_charBuffer[CHAR_BUFFER_SIZE];
static size_t g_charBufferSize = CHAR_BUFFER_SIZE;

// TODO: actually implement ability to turn off srgb-view generation
static std::vector<effect_runtime*> runtimes;

/// <summary>
/// Calculates a crc32 hash from the passed in shader bytecode. The hash is used to identity the shader in future runs.
/// </summary>
/// <param name="shaderData"></param>
/// <returns></returns>
static uint32_t calculateShaderHash(void* shaderData)
{
    if (nullptr == shaderData)
    {
        return 0;
    }

    const auto shaderDesc = *static_cast<shader_desc*>(shaderData);
    return compute_crc32(static_cast<const uint8_t*>(shaderDesc.code), shaderDesc.code_size);
}

static void onInitDevice(device* device)
{
    device->create_private_data<DeviceDataContainer>();
}


static void onDestroyDevice(device* device)
{
    resourceManager.OnDestroyDevice(device);

    device->destroy_private_data<DeviceDataContainer>();
}


static void onInitCommandList(command_list* commandList)
{
    commandList->create_private_data<CommandListDataContainer>();
    CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
    commandListData.ps.id = 1;
    commandListData.vs.id = 2;
}


static void onDestroyCommandList(command_list* commandList)
{
    commandList->destroy_private_data<CommandListDataContainer>();
}

static void onResetCommandList(command_list* commandList)
{
    CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
    commandListData.Reset();
}


static void onInitSwapchain(reshade::api::swapchain* swapchain)
{
    resourceManager.OnInitSwapchain(swapchain);
}


static void onDestroySwapchain(reshade::api::swapchain* swapchain)
{
    resourceManager.OnDestroySwapchain(swapchain);
}


static bool onCreateResource(device* device, resource_desc& desc, subresource_data* initial_data, resource_usage initial_state)
{
    return resourceManager.OnCreateResource(device, desc, initial_data, initial_state);
}


static void onInitResource(device* device, const resource_desc& desc, const subresource_data* initData, resource_usage usage, reshade::api::resource handle)
{
    resourceManager.OnInitResource(device, desc, initData, usage, handle);
    
    if (constantCopy != nullptr)
        constantCopy->OnInitResource(device, desc, initData, usage, handle);
}


static void onDestroyResource(device* device, resource res)
{
    resourceManager.OnDestroyResource(device, res);
    
    if (constantCopy != nullptr)
        constantCopy->OnDestroyResource(device, res);
}


static bool onCreateResourceView(device* device, resource resource, resource_usage usage_type, resource_view_desc& desc)
{
    return resourceManager.OnCreateResourceView(device, resource, usage_type, desc);
}


static void onReshadeReloadedEffects(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();
    data.allEnabledTechniques.clear();
    allTechniques.clear();

    Rendering::RenderingManager::EnumerateTechniques(runtime, [&data](effect_runtime* runtime, effect_technique technique, string& name) {
        allTechniques.push_back(name);
        bool enabled = runtime->get_technique_state(technique);
    
        if (enabled)
        {
            data.allEnabledTechniques.emplace(name, false);
        }
        });

    if (constantHandler != nullptr)
    {
        constantHandler->OnReshadeReloadedEffects(runtime, static_cast<int32_t>(data.allEnabledTechniques.size()));
    }
}


static bool onReshadeSetTechniqueState(effect_runtime* runtime, effect_technique technique, bool enabled)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();
    g_charBufferSize = CHAR_BUFFER_SIZE;
    runtime->get_technique_name(technique, g_charBuffer, &g_charBufferSize);
    string techName(g_charBuffer);
    
    if (!enabled)
    {
        if (data.allEnabledTechniques.contains(techName))
        {
            data.allEnabledTechniques.erase(techName);
        }
    }
    else
    {
        if (data.allEnabledTechniques.find(techName) == data.allEnabledTechniques.end())
        {
            data.allEnabledTechniques.emplace(techName, false);
        }
    }

    if (constantHandler != nullptr)
    {
        constantHandler->OnReshadeSetTechniqueState(runtime, static_cast<int32_t>(data.allEnabledTechniques.size()));
    }
    
    return false;
}


static void onInitEffectRuntime(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    // Dispose of texture bindings created from the runtime below
    if (data.current_runtime != nullptr)
    {
        renderingManager.DisposeTextureBindings(data.current_runtime);
    }

    // Push new runtime on top
    runtimes.push_back(runtime);
    data.current_runtime = runtime;

    renderingManager.InitTextureBingings(runtime);

    if (constantHandler != nullptr)
    {
        constantHandler->ReloadConstantVariables(runtime);
    }
}


static void onDestroyEffectRuntime(effect_runtime* runtime)
{
    DeviceDataContainer& data = runtime->get_device()->get_private_data<DeviceDataContainer>();

    // Remove runtime from stack
    for (auto it = runtimes.begin(); it != runtimes.end();)
    {
        if (runtime == *it)
        {
            it = runtimes.erase(it);
            continue;
        }

        it++;
    }

    // Pick the runtime on top of our stack if there are any
    if (runtime == data.current_runtime)
    {
        renderingManager.DisposeTextureBindings(runtime);

        if (runtimes.size() > 0)
        {
            data.current_runtime = runtimes[runtimes.size() - 1];

            renderingManager.InitTextureBingings(data.current_runtime);

            if (constantHandler != nullptr)
            {
                constantHandler->ReloadConstantVariables(data.current_runtime);
            }
        }
        else
        {
            data.current_runtime = nullptr;

            if (constantHandler != nullptr)
            {
                constantHandler->ClearConstantVariables();
            }
        }
    }
}


static void onInitPipeline(device* device, pipeline_layout, uint32_t subobjectCount, const pipeline_subobject* subobjects, pipeline pipelineHandle)
{
    // shader has been created, we will now create a hash and store it with the handle we got.
    for (uint32_t i = 0; i < subobjectCount; ++i)
    {
        switch (subobjects[i].type)
        {
        case pipeline_subobject_type::vertex_shader:
        {
            g_vertexShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
        }
        break;
        case pipeline_subobject_type::pixel_shader:
        {
            g_pixelShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
        }
        break;
        }
    }
}


static void onDestroyPipeline(device* device, pipeline pipelineHandle)
{
    g_pixelShaderManager.removeHandle(pipelineHandle.handle);
    g_vertexShaderManager.removeHandle(pipelineHandle.handle);
}


static void onBindPipeline(command_list* commandList, pipeline_stage stages, pipeline pipelineHandle)
{
    if (nullptr == commandList || pipelineHandle.handle == 0 || !((uint32_t)(stages & pipeline_stage::pixel_shader) || (uint32_t)(stages & pipeline_stage::vertex_shader)))
    {
        return;
    }

    const uint32_t handleHasPixelShaderAttached = (uint32_t)(stages & pipeline_stage::pixel_shader) ? g_pixelShaderManager.safeGetShaderHash(pipelineHandle.handle) : 0;
    const uint32_t handleHasVertexShaderAttached = (uint32_t)(stages & pipeline_stage::vertex_shader) ? g_vertexShaderManager.safeGetShaderHash(pipelineHandle.handle) : 0;

    if (!handleHasPixelShaderAttached && !handleHasVertexShaderAttached)
    {
        // draw call with unknown handle, don't collect it
        return;
    }
    CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = commandList->get_device()->get_private_data<DeviceDataContainer>();

    if (deviceData.current_runtime == nullptr || !deviceData.current_runtime->get_effects_state())
    {
        return;
    }
    
    if (commandList->get_device()->get_api() == device_api::vulkan || commandList->get_device()->get_api() == device_api::d3d12)
    {
        commandListData.stateTracker.OnBindPipeline(commandList, stages, pipelineHandle);
    }

    uint32_t pipelineChanged = 0;

    if ((uint32_t)(stages & pipeline_stage::pixel_shader) && handleHasPixelShaderAttached)
    {
        if (g_activeCollectorFrameCounter > 0)
        {
            // in collection mode
            g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
        }
        if (commandListData.ps.activeShaderHash != handleHasPixelShaderAttached)
        {
            pipelineChanged |= Rendering::MATCH_EFFECT_PS | Rendering::MATCH_BINDING_PS;
            commandListData.ps.constantBuffersToUpdate.clear();
        }

        commandListData.ps.blockedShaderGroups = g_addonUIData.GetToggleGroupsForPixelShaderHash(handleHasPixelShaderAttached);
        commandListData.ps.activeShaderHash = handleHasPixelShaderAttached;
    }

    if ((uint32_t)(stages & pipeline_stage::vertex_shader) && handleHasVertexShaderAttached)
    {
        if (g_activeCollectorFrameCounter > 0)
        {
            // in collection mode
            g_vertexShaderManager.addActivePipelineHandle(pipelineHandle.handle);
        }
        if (commandListData.vs.activeShaderHash != handleHasVertexShaderAttached)
        {
            pipelineChanged |= Rendering::MATCH_EFFECT_VS | Rendering::MATCH_BINDING_VS;
            commandListData.vs.constantBuffersToUpdate.clear();
        }

        commandListData.vs.blockedShaderGroups = g_addonUIData.GetToggleGroupsForVertexShaderHash(handleHasVertexShaderAttached);
        commandListData.vs.activeShaderHash = handleHasVertexShaderAttached;
    }

    if (pipelineChanged > 0)
    {
        // Perform updates scheduled for after a shader has been applied. Only do so once the draw flag has been cleared.
        if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_PIPELINE_BINDING && !(commandListData.commandQueue & pipelineChanged & Rendering::MATCH_BINDING))
        {
            renderingManager.UpdateTextureBindings(commandList, Rendering::CALL_BIND_PIPELINE, pipelineChanged & Rendering::MATCH_BINDING);
        }

        if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_PIPELINE_EFFECT && !(commandListData.commandQueue & pipelineChanged & Rendering::MATCH_EFFECT))
        {
            renderingManager.RenderEffects(commandList, Rendering::CALL_BIND_PIPELINE, pipelineChanged & Rendering::MATCH_EFFECT);
        }

        // Make sure we dequeue whatever is left over scheduled for CALL_DRAW/CALL_BIND_PIPELINE in case re-queueing was enabled for some group
        renderingManager.ClearQueue2(commandListData, Rendering::CALL_DRAW, Rendering::CALL_BIND_PIPELINE);

        renderingManager.CheckCallForCommandList(commandList);
    }
}


static void onBindRenderTargetsAndDepthStencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }
    
    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    commandListData.stateTracker.OnBindRenderTargetsAndDepthStencil(cmd_list, count, rtvs, dsv);

    if (count > 0)
    {
        if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_RENDERTARGET_BINDING && !(commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_BINDING))
        {
            renderingManager.UpdateTextureBindings(cmd_list, Rendering::CALL_BIND_RENDER_TARGET, Rendering::MATCH_BINDING);
        }

        if (commandListData.commandQueue & Rendering::CHECK_MATCH_BIND_RENDERTARGET_EFFECT && !(commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_EFFECT))
        {
            renderingManager.RenderEffects(cmd_list, Rendering::CALL_BIND_RENDER_TARGET, Rendering::MATCH_EFFECT);
        }
    }
}


static void onBeginRenderPass(command_list* cmd_list, uint32_t count, const render_pass_render_target_desc* rts, const render_pass_depth_stencil_desc* ds)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }
    
    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();
    
    commandListData.stateTracker.OnBeginRenderPass(cmd_list, count, rts, ds);
    
    if (!deviceData.current_runtime->get_effects_state())
    {
        return;
    }

    if (commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_BINDING)
    {
        renderingManager.UpdateTextureBindings(cmd_list, Rendering::CALL_DRAW, Rendering::MATCH_BINDING_PS | Rendering::MATCH_BINDING_VS);
    }

    if (commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_EFFECT)
    {
        renderingManager.RenderEffects(cmd_list, Rendering::CALL_DRAW, Rendering::MATCH_EFFECT_PS | Rendering::MATCH_EFFECT_VS);
    }
}


static void onPushDescriptors(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, const descriptor_set_update& update)
{
    auto& data = cmd_list->get_private_data<CommandListDataContainer>();
    data.stateTracker.OnPushDescriptors(cmd_list, stages, layout, layout_param, update);
}


static void onBindDescriptorSets(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_set* sets)
{
    auto& data = cmd_list->get_private_data<CommandListDataContainer>();
    data.stateTracker.OnBindDescriptorSets(cmd_list, stages, layout, first, count, sets);
}


static void onBindViewports(command_list* cmd_list, uint32_t first, uint32_t count, const viewport* viewports)
{
    auto& data = cmd_list->get_private_data<CommandListDataContainer>();
    data.stateTracker.OnBindViewports(cmd_list, first, count, viewports);
}


static void onBindScissorRects(command_list* cmd_list, uint32_t first, uint32_t count, const rect* rects)
{
    auto& data = cmd_list->get_private_data<CommandListDataContainer>();
    data.stateTracker.OnBindScissorRects(cmd_list, first, count, rects);
}


static void onBindPipelineStates(command_list* cmd_list, uint32_t count, const dynamic_state* states, const uint32_t* values)
{
    auto& data = cmd_list->get_private_data<CommandListDataContainer>();
    data.stateTracker.OnBindPipelineStates(cmd_list, count, states, values);
}


static void onInitPipelineLayout(device* device, uint32_t param_count, const pipeline_layout_param* params, pipeline_layout layout)
{
    unique_lock<shared_mutex> lock(pipeline_layout_mutex);
    
    auto& data = device->get_private_data<DeviceDataContainer>();
    data.transient_mask[layout.handle].resize(param_count);
    
    for (uint32_t i = 0; i < param_count; i++)
    {
        if (params[i].type == pipeline_layout_param_type::push_constants)
        {
            data.transient_mask[layout.handle][i] = true;
        }
    }
}


static void onDestroyPipelineLayout(device* device, pipeline_layout layout)
{
    unique_lock<shared_mutex> lock(pipeline_layout_mutex);
    
    auto& data = device->get_private_data<DeviceDataContainer>();
    data.transient_mask.erase(layout.handle);
}


static void onReshadeOverlay(effect_runtime* runtime)
{
    DisplayOverlay(g_addonUIData, runtime);
}

static void onPresent(command_queue* queue, swapchain* swapchain, const rect* source_rect, const rect* dest_rect, uint32_t dirty_rect_count, const rect* dirty_rects)
{
    device* dev = queue->get_device();
    DeviceDataContainer& deviceData = dev->get_private_data<DeviceDataContainer>();

    if (deviceData.current_runtime == nullptr)
    {
        return;
    }

    effect_runtime* runtime = deviceData.current_runtime;

    if (queue == runtime->get_command_queue())
    {
        if (runtime->get_effects_state())
        {
            renderingManager.RenderRemainingEffects(runtime);
            renderingManager.ClearUnmatchedTextureBindings(runtime->get_command_queue()->get_immediate_command_list());
        }
    }

    if (dev->get_api() != device_api::d3d12 && dev->get_api() != device_api::vulkan)
        onResetCommandList(runtime->get_command_queue()->get_immediate_command_list());
}

static void onReshadePresent(effect_runtime* runtime)
{
    device* dev = runtime->get_device();
    DeviceDataContainer& deviceData = dev->get_private_data<DeviceDataContainer>();
    command_queue* queue = runtime->get_command_queue();
    
    deviceData.rendered_effects = false;
    
    std::for_each(deviceData.allEnabledTechniques.begin(), deviceData.allEnabledTechniques.end(), [](auto& el) {
        el.second = false;
        });
    deviceData.bindingsUpdated.clear();
    deviceData.constantsUpdated.clear();

    CheckHotkeys(g_addonUIData, runtime);
}


static void onMapBufferRegion(device* device, resource resource, uint64_t offset, uint64_t size, map_access access, void** data)
{
    if (constantCopy != nullptr)
        constantCopy->OnMapBufferRegion(device, resource, offset, size, access, data);
}


static void onUnmapBufferRegion(device* device, resource resource)
{
    if (constantCopy != nullptr)
        constantCopy->OnUnmapBufferRegion(device, resource);
}


static bool onUpdateBufferRegion(device* device, const void* data, resource resource, uint64_t offset, uint64_t size)
{
    if (constantCopy != nullptr)
        constantCopy->OnUpdateBufferRegion(device, data, resource, offset, size);

    return false;
}


static void displaySettings(effect_runtime* runtime)
{
    DisplaySettings(g_addonUIData, runtime);
}


static bool Init()
{
    resourceManager.SetResourceShim(g_addonUIData.GetResourceShim());
    resourceManager.Init();

    return constantManager.Init(g_addonUIData, &constantCopy, &constantHandler);
}


static bool UnInit()
{
    return constantManager.UnInit();
}

static void CheckDrawCall(command_list* cmd_list)
{
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();

    if (commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW)
    {
        if (constantHandler != nullptr && (commandListData.commandQueue & Rendering::MATCH_CONST))
        {
            constantHandler->UpdateConstants(cmd_list);
            commandListData.commandQueue &= ~Rendering::MATCH_CONST;
        }

        if (commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_BINDING)
        {
            renderingManager.UpdateTextureBindings(cmd_list, Rendering::CALL_DRAW, Rendering::MATCH_BINDING);
        }

        if (commandListData.commandQueue & Rendering::CHECK_MATCH_DRAW_EFFECT)
        {
            renderingManager.RenderEffects(cmd_list, Rendering::CALL_DRAW, Rendering::MATCH_EFFECT);
        }
    }
}

static bool onDraw(command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    CheckDrawCall(cmd_list);

    return false;
}

static bool onDrawIndexed(command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    CheckDrawCall(cmd_list);

    return false;
}

static bool onDrawOrDispatchIndirect(command_list* cmd_list, indirect_command type, resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride)
{
    switch (type)
    {
    case indirect_command::draw:
    case indirect_command::draw_indexed:
        CheckDrawCall(cmd_list);
        break;
    default:
        break;
    }

    return false;
}

/// <summary>
/// copied from Reshade
/// Returns the path to the module file identified by the specified <paramref name="module"/> handle.
/// </summary>
filesystem::path getModulePath(HMODULE module)
{
    WCHAR buf[4096];
    return GetModuleFileNameW(module, buf, ARRAYSIZE(buf)) ? buf : std::filesystem::path();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hModule))
        {
            return FALSE;
        }

        g_dllPath = getModulePath(hModule);

        g_addonUIData.SetBasePath(g_dllPath.parent_path());
        g_addonUIData.LoadShaderTogglerIniFile();
        Init();
        reshade::register_event<reshade::addon_event::init_swapchain>(onInitSwapchain);
        reshade::register_event<reshade::addon_event::destroy_swapchain>(onDestroySwapchain);
        reshade::register_event<reshade::addon_event::init_resource>(onInitResource);
        reshade::register_event<reshade::addon_event::create_resource>(onCreateResource);
        reshade::register_event<reshade::addon_event::map_buffer_region>(onMapBufferRegion);
        reshade::register_event<reshade::addon_event::update_buffer_region>(onUpdateBufferRegion);
        reshade::register_event<reshade::addon_event::unmap_buffer_region>(onUnmapBufferRegion);
        reshade::register_event<reshade::addon_event::destroy_resource>(onDestroyResource);
        reshade::register_event<reshade::addon_event::create_resource_view>(onCreateResourceView);
        reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
        reshade::register_event<reshade::addon_event::bind_viewports>(onBindViewports);
        reshade::register_event<reshade::addon_event::bind_scissor_rects>(onBindScissorRects);
        reshade::register_event<reshade::addon_event::bind_descriptor_sets>(onBindDescriptorSets);
        reshade::register_event<reshade::addon_event::init_pipeline_layout>(onInitPipelineLayout);
        reshade::register_event<reshade::addon_event::destroy_pipeline_layout>(onDestroyPipelineLayout);
        reshade::register_event<reshade::addon_event::bind_pipeline_states>(onBindPipelineStates);
        reshade::register_event<reshade::addon_event::init_command_list>(onInitCommandList);
        reshade::register_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
        reshade::register_event<reshade::addon_event::reset_command_list>(onResetCommandList);
        reshade::register_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
        reshade::register_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
        reshade::register_event<reshade::addon_event::reshade_present>(onReshadePresent);
        reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(onReshadeReloadedEffects);
        reshade::register_event<reshade::addon_event::reshade_set_technique_state>(onReshadeSetTechniqueState);
        reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
        reshade::register_event<reshade::addon_event::init_device>(onInitDevice);
        reshade::register_event<reshade::addon_event::destroy_device>(onDestroyDevice);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(onBindRenderTargetsAndDepthStencil);
        reshade::register_event<reshade::addon_event::begin_render_pass>(onBeginRenderPass);
        reshade::register_event<reshade::addon_event::init_effect_runtime>(onInitEffectRuntime);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(onDestroyEffectRuntime);
        reshade::register_event<reshade::addon_event::push_descriptors>(onPushDescriptors);
        reshade::register_event<reshade::addon_event::present>(onPresent);

        reshade::register_event<reshade::addon_event::draw>(onDraw);
        reshade::register_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
        reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);

        reshade::register_overlay(nullptr, &displaySettings);
        break;
    case DLL_PROCESS_DETACH:
        UnInit();
        reshade::unregister_event<reshade::addon_event::init_swapchain>(onInitSwapchain);
        reshade::unregister_event<reshade::addon_event::destroy_swapchain>(onDestroySwapchain);
        reshade::unregister_event<reshade::addon_event::reshade_present>(onReshadePresent);
        reshade::unregister_event<reshade::addon_event::map_buffer_region>(onMapBufferRegion);
        reshade::unregister_event<reshade::addon_event::update_buffer_region>(onUpdateBufferRegion);
        reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(onUnmapBufferRegion);
        reshade::unregister_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
        reshade::unregister_event<reshade::addon_event::init_pipeline>(onInitPipeline);
        reshade::unregister_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
        reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(onReshadeReloadedEffects);
        reshade::unregister_event<reshade::addon_event::reshade_set_technique_state>(onReshadeSetTechniqueState);
        reshade::unregister_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
        reshade::unregister_event<reshade::addon_event::bind_viewports>(onBindViewports);
        reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(onBindScissorRects);
        reshade::unregister_event<reshade::addon_event::bind_descriptor_sets>(onBindDescriptorSets);
        reshade::unregister_event<reshade::addon_event::init_pipeline_layout>(onInitPipelineLayout);
        reshade::unregister_event<reshade::addon_event::destroy_pipeline_layout>(onDestroyPipelineLayout);
        reshade::unregister_event<reshade::addon_event::bind_pipeline_states>(onBindPipelineStates);
        reshade::unregister_event<reshade::addon_event::init_command_list>(onInitCommandList);
        reshade::unregister_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
        reshade::unregister_event<reshade::addon_event::reset_command_list>(onResetCommandList);
        reshade::unregister_event<reshade::addon_event::init_device>(onInitDevice);
        reshade::unregister_event<reshade::addon_event::destroy_device>(onDestroyDevice);
        reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(onBindRenderTargetsAndDepthStencil);
        reshade::unregister_event<reshade::addon_event::begin_render_pass>(onBeginRenderPass);
        reshade::unregister_event<reshade::addon_event::init_effect_runtime>(onInitEffectRuntime);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(onDestroyEffectRuntime);
        reshade::unregister_event<reshade::addon_event::push_descriptors>(onPushDescriptors);
        reshade::unregister_event<reshade::addon_event::create_resource>(onCreateResource);
        reshade::unregister_event<reshade::addon_event::init_resource>(onInitResource);
        reshade::unregister_event<reshade::addon_event::destroy_resource>(onDestroyResource);
        reshade::unregister_event<reshade::addon_event::create_resource_view>(onCreateResourceView);
        reshade::unregister_event<reshade::addon_event::present>(onPresent);

        reshade::unregister_event<reshade::addon_event::draw>(onDraw);
        reshade::unregister_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
        reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);

        reshade::unregister_overlay(nullptr, &displaySettings);
        reshade::unregister_addon(hModule);

        break;
    }

    return TRUE;
}
