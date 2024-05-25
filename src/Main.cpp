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

#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID unsigned long long // Change ImGui texture ID type to that of a 'reshade::api::resource_view' handle

#include <imgui.h>
#include <reshade.hpp>
#include "crc32_hash.hpp"
#include "ShaderManager.h"
#include "CDataFile.h"
#include "ToggleGroup.h"
#include <vector>
#include <filesystem>

using namespace reshade::api;
using namespace ShaderToggler;

extern "C" __declspec(dllexport) const char *NAME = "Shader Toggler";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "一个ReShade插件，允许你对游戏着色器进行分组并通过快捷键切换开关状态。";

struct __declspec(uuid("038B03AA-4C75-443B-A695-752D80797037")) CommandListDataContainer {
    uint64_t activePixelShaderPipeline;
    uint64_t activeVertexShaderPipeline;
	uint64_t activeComputeShaderPipeline;
};

#define FRAMECOUNT_COLLECTION_PHASE_DEFAULT 250;
#define HASH_FILE_NAME	"ShaderToggler.ini"

enum AddonKeybind: uint32_t
{
	PIXEL_SHADER_DOWN = 0,
	PIXEL_SHADER_UP,
	PIXEL_SHADER_MARK,
	PIXEL_SHADER_MARKED_DOWN,
	PIXEL_SHADER_MARKED_UP,
	VERTEX_SHADER_DOWN,
	VERTEX_SHADER_UP,
	VERTEX_SHADER_MARK,
	VERTEX_SHADER_MARKED_DOWN,
	VERTEX_SHADER_MARKED_UP,
	COMPUTE_SHADER_DOWN,
	COMPUTE_SHADER_UP,
	COMPUTE_SHADER_MARK,
	COMPUTE_SHADER_MARKED_DOWN,
	COMPUTE_SHADER_MARKED_UP
};

static const char* AddonKeybindNames[] = {
	"PIXEL_SHADER_DOWN",
	"PIXEL_SHADER_UP",
	"PIXEL_SHADER_MARK",
	"PIXEL_SHADER_MARKED_DOWN",
	"PIXEL_SHADER_MARKED_UP",
	"VERTEX_SHADER_DOWN",
	"VERTEX_SHADER_UP",
	"VERTEX_SHADER_MARK",
	"VERTEX_SHADER_MARKED_DOWN",
	"VERTEX_SHADER_MARKED_UP",
	"COMPUTE_SHADER_DOWN",
	"COMPUTE_SHADER_UP",
	"COMPUTE_SHADER_MARK",
	"COMPUTE_SHADER_MARKED_DOWN",
	"COMPUTE_SHADER_MARKED_UP"
};
// Format: {keyCode = 0, ctrl = false, shift = false, alt = false}, see the KeyData constructor.
static KeyData g_addonKeyBindings[ARRAYSIZE(AddonKeybindNames)] = {
	{VK_NUMPAD1},
	{VK_NUMPAD2},
	{VK_NUMPAD3},
	{VK_NUMPAD1, true},
	{VK_NUMPAD2, true},
	VK_NUMPAD4,
	VK_NUMPAD5,
	VK_NUMPAD6,
	{VK_NUMPAD4, true},
	{VK_NUMPAD5, true},
	VK_NUMPAD7,
	VK_NUMPAD8,
	VK_NUMPAD9,
	{VK_NUMPAD7, true},
	{VK_NUMPAD8, true},
};

static ShaderToggler::ShaderManager g_pixelShaderManager;
static ShaderToggler::ShaderManager g_vertexShaderManager;
static ShaderToggler::ShaderManager g_computeShaderManager;
static KeyData g_keyCollector;
static bool g_isSettingAddonKeybind = false;
static bool g_isSettingGroupKeybind = false;
static atomic_uint32_t g_activeCollectorFrameCounter = 0;
static std::vector<ToggleGroup> g_toggleGroups;
static atomic_int g_toggleGroupIdKeyBindingEditing = -1;
static atomic_int g_toggleGroupIdShaderEditing = -1;
static float g_overlayOpacity = 1.0f;
static int g_startValueFramecountCollectionPhase = FRAMECOUNT_COLLECTION_PHASE_DEFAULT;
static std::string g_iniFileName = "";

/// <summary>
/// Calculates a crc32 hash from the passed in shader bytecode. The hash is used to identity the shader in future runs.
/// </summary>
/// <param name="shaderData"></param>
/// <returns></returns>
static uint32_t calculateShaderHash(void* shaderData)
{
	if(nullptr==shaderData)
	{
		return 0;
	}

	const auto shaderDesc = *static_cast<shader_desc *>(shaderData);
	return compute_crc32(static_cast<const uint8_t *>(shaderDesc.code), shaderDesc.code_size);
}


/// <summary>
/// Adds a default group with VK_CAPITAL as toggle key. Only used if there aren't any groups defined in the ini file.
/// </summary>
void addDefaultGroup()
{
	ToggleGroup toAdd("Default", ToggleGroup::getNewGroupId());
	toAdd.setToggleKey(VK_CAPITAL, false, false, false);
	g_toggleGroups.push_back(toAdd);
}


/// <summary>
/// Loads the defined hashes and groups from the shaderToggler.ini file.
/// </summary>
void loadShaderTogglerIniFile()
{
	// Will assume it's started at the start of the application and therefore no groups are present.
	CDataFile iniFile;
	if(!iniFile.Load(g_iniFileName))
	{
		// not there
		return;
	}
	for(uint32_t i = 0; i < ARRAYSIZE(AddonKeybindNames); i++)
	{
		uint32_t keybinding = iniFile.GetUInt(AddonKeybindNames[i], "KeyBindings");
		if(keybinding != UINT_MAX)
		{
			g_addonKeyBindings[i].setKeyFromIniFile(keybinding);
		}
	}
	int groupCounter = 0;
	const int numberOfGroups = iniFile.GetInt("AmountGroups", "General");
	if(numberOfGroups==INT_MIN)
	{
		// old format file?
		addDefaultGroup();
		groupCounter=-1;	// enforce old format read for pre 1.0 ini file.
	}
	else
	{
		for(int i=0;i<numberOfGroups;i++)
		{
			g_toggleGroups.push_back(ToggleGroup("", ToggleGroup::getNewGroupId()));
		}
	}
	for(auto& group: g_toggleGroups)
	{
		group.loadState(iniFile, groupCounter);		// groupCounter is normally 0 or greater. For when the old format is detected, it's -1 (and there's 1 group).
		groupCounter++;
	}
}


/// <summary>
/// Saves the currently known toggle groups with their shader hashes to the shadertoggler.ini file
/// </summary>
void saveShaderTogglerIniFile()
{
	// format: first section with # of groups, then per group a section with pixel and vertex shaders, as well as their name and key value.
	// groups are stored with "Group" + group counter, starting with 0.
	CDataFile iniFile;
	for(uint32_t i = 0; i < ARRAYSIZE(AddonKeybindNames); i++)
	{
		uint32_t keybinding = iniFile.SetUInt(AddonKeybindNames[i], g_addonKeyBindings[i].getKeyForIniFile(), "", "KeyBindings");
	}
	iniFile.SetInt("AmountGroups", g_toggleGroups.size(), "",  "General");

	int groupCounter = 0;
	for(const auto& group: g_toggleGroups)
	{
		group.saveState(iniFile, groupCounter);
		groupCounter++;
	}
	iniFile.SetFileName(g_iniFileName);
	iniFile.Save();
}


static void onInitCommandList(command_list *commandList)
{
	commandList->create_private_data<CommandListDataContainer>();
}


static void onDestroyCommandList(command_list *commandList)
{
	commandList->destroy_private_data<CommandListDataContainer>();
}

static void onResetCommandList(command_list *commandList)
{
	CommandListDataContainer &commandListData = commandList->get_private_data<CommandListDataContainer>();
	commandListData.activePixelShaderPipeline = -1;
	commandListData.activeVertexShaderPipeline = -1;
	commandListData.activeComputeShaderPipeline = -1;
}


static void onInitPipeline(device *device, pipeline_layout, uint32_t subobjectCount, const pipeline_subobject *subobjects, pipeline pipelineHandle)
{
	// shader has been created, we will now create a hash and store it with the handle we got.
	for (uint32_t i = 0; i < subobjectCount; ++i)
	{
		switch (subobjects[i].type)
		{
			case pipeline_subobject_type::vertex_shader:
				g_vertexShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
				break;
			case pipeline_subobject_type::pixel_shader:
				g_pixelShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
				break;
			case pipeline_subobject_type::compute_shader:
				g_computeShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
				break;
		}
	}
}


static void onDestroyPipeline(device *device, pipeline pipelineHandle)
{
	g_pixelShaderManager.removeHandle(pipelineHandle.handle);
	g_vertexShaderManager.removeHandle(pipelineHandle.handle);
	g_computeShaderManager.removeHandle(pipelineHandle.handle);
}


static void displayIsPartOfToggleGroup()
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
	ImGui::SameLine();
	ImGui::Text(" Shader is part of this toggle group.");
	ImGui::PopStyleColor();
}


static void displayShaderManagerInfo(ShaderManager& toDisplay, const char* shaderType)
{
	auto make_label = [&shaderType](const std::string& base) -> const std::string {
		return (base + shaderType);
		};

	if(toDisplay.isInHuntingMode())
	{
		//ImGui::Text("当前%s着色器数量：%d，分组中已有%s着色器数量：%d。", shaderType, toDisplay.getAmountShaderHashesCollected(), shaderType, toDisplay.getMarkedShaderCount());
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s着色器：", shaderType);
		ImGui::SameLine();
		const auto font_size = ImGui::GetFontSize();
		if(ImGui::Button(make_label("<<##PrevMarkedShader").c_str(), ImVec2(1.5f * font_size, 0.0f)))
		{
			toDisplay.huntPreviousShader(true);
		}
		ImGui::SameLine(0, 0.5f * ImGui::GetStyle().ItemSpacing.x);
		if(ImGui::Button(make_label("<##PrevShader").c_str(), ImVec2(1.5f * font_size, 0.0f)))
		{
			toDisplay.huntPreviousShader(false);
		}
		ImGui::SameLine();
		bool shader_marked = toDisplay.isHuntedShaderMarked();
		char shader_label[40] = {};
		sprintf_s(shader_label, make_label("%d / %d [0x%08x]##").c_str(), toDisplay.getActiveHuntedShaderIndex(), toDisplay.getAmountShaderHashesCollected(), toDisplay.getActiveHuntedShaderHash());
		if(shader_marked)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1.0f, 0.5f));
		if(ImGui::Button(shader_label, ImVec2(12.0f * font_size, 0.0f)))
		{
			toDisplay.toggleMarkOnHuntedShader();
		}
		if(shader_marked)
			ImGui::PopStyleColor(1);
		ImGui::PopStyleVar(1);
		ImGui::SameLine();
		if(ImGui::Button(make_label(">##NextShader").c_str(), ImVec2(1.5f * font_size, 0.0f)))
		{
			toDisplay.huntNextShader(false);
		}
		ImGui::SameLine(0, 0.5f * ImGui::GetStyle().ItemSpacing.x);
		if(ImGui::Button(make_label(">>##NextMarkedShader").c_str(), ImVec2(1.5f * font_size, 0.0f)))
		{
			toDisplay.huntNextShader(true);
		}
		//if(shader_marked)
		//{
		//	displayIsPartOfToggleGroup();
		//}
	}
}

static void displayShaderManagerStats(ShaderManager& toDisplay, const char* shaderType)
{
	ImGui::Text("含有%s着色器的管线数量：%d. 收集的不同%s着色器数量：%d。", shaderType, toDisplay.getPipelineCount(), shaderType, toDisplay.getShaderCount());
}


static void onReshadeOverlay(reshade::api::effect_runtime *runtime)
{
	if(g_toggleGroupIdShaderEditing>=0)
	{
		ImGui::SetNextWindowBgAlpha(g_overlayOpacity);
		//ImGui::SetNextWindowPos(ImVec2(10, 10));
		if (!ImGui::Begin("ShaderTogglerInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | 
														ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::End();
			return;
		}
		string editingGroupName = "";
		for(auto& group:g_toggleGroups)
		{
			if(group.getId()==g_toggleGroupIdShaderEditing)
			{
				editingGroupName = group.getName();
				break;
			}
		}
		
		displayShaderManagerStats(g_vertexShaderManager, "顶点");
		displayShaderManagerStats(g_pixelShaderManager, "像素");
		displayShaderManagerStats(g_computeShaderManager, "计算");

		if(g_activeCollectorFrameCounter > 0)
		{
			const uint32_t counterValue = g_activeCollectorFrameCounter;
			ImGui::Text("收集活跃的着色器……还需 %d 帧。", counterValue);
		}
		else
		{
			if(g_vertexShaderManager.isInHuntingMode() || g_pixelShaderManager.isInHuntingMode() || g_computeShaderManager.isInHuntingMode())
			{
				ImGui::Text("编辑分组 %s 的着色器：", editingGroupName.c_str());
			}
			displayShaderManagerInfo(g_vertexShaderManager, "顶点");
			displayShaderManagerInfo(g_pixelShaderManager, "像素");
			displayShaderManagerInfo(g_computeShaderManager, "计算");
		}
		ImGui::End();
	}
}


static void onBindPipeline(command_list* commandList, pipeline_stage stages, pipeline pipelineHandle)
{
	if(nullptr != commandList && pipelineHandle.handle != 0)
	{
		const bool handleHasPixelShaderAttached = g_pixelShaderManager.isKnownHandle(pipelineHandle.handle);
		const bool handleHasVertexShaderAttached = g_vertexShaderManager.isKnownHandle(pipelineHandle.handle);
		const bool handleHasComputeShaderAttached = g_computeShaderManager.isKnownHandle(pipelineHandle.handle);
		if(!handleHasPixelShaderAttached && !handleHasVertexShaderAttached && !handleHasComputeShaderAttached)
		{
			// draw call with unknown handle, don't collect it
			return;
		}
		CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
		// always do the following code as that has to run for every bind on a pipeline:
		if(g_activeCollectorFrameCounter > 0)
		{
			// in collection mode
			if(handleHasPixelShaderAttached)
			{
				g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
			}
			if(handleHasVertexShaderAttached)
			{
				g_vertexShaderManager.addActivePipelineHandle(pipelineHandle.handle);
			}
			if(handleHasComputeShaderAttached)
			{
				g_computeShaderManager.addActivePipelineHandle(pipelineHandle.handle);
			}
		}
		else
		{
			commandListData.activePixelShaderPipeline = handleHasPixelShaderAttached ? pipelineHandle.handle : commandListData.activePixelShaderPipeline;
			commandListData.activeVertexShaderPipeline = handleHasVertexShaderAttached ? pipelineHandle.handle : commandListData.activeVertexShaderPipeline;
			commandListData.activeComputeShaderPipeline = handleHasComputeShaderAttached ? pipelineHandle.handle : commandListData.activeComputeShaderPipeline;
		}
		if((stages & pipeline_stage::pixel_shader) == pipeline_stage::pixel_shader)
		{
			if(handleHasPixelShaderAttached)
			{
				if(g_activeCollectorFrameCounter > 0)
				{
					// in collection mode
					g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
				}
				commandListData.activePixelShaderPipeline = pipelineHandle.handle;
			}
		}
		if((stages & pipeline_stage::vertex_shader) == pipeline_stage::vertex_shader)
		{
			if(handleHasVertexShaderAttached)
			{
				if(g_activeCollectorFrameCounter > 0)
				{
					// in collection mode
					g_vertexShaderManager.addActivePipelineHandle(pipelineHandle.handle);
				}
				commandListData.activeVertexShaderPipeline = pipelineHandle.handle;
			}
		}
		if((stages & pipeline_stage::compute_shader) == pipeline_stage::compute_shader)
		{
			if(handleHasComputeShaderAttached)
			{
				if(g_activeCollectorFrameCounter > 0)
				{
					// in collection mode
					g_computeShaderManager.addActivePipelineHandle(pipelineHandle.handle);
				}
				commandListData.activeComputeShaderPipeline = pipelineHandle.handle;
			}
		}
	}
}


/// <summary>
/// This function will return true if the command list specified has one or more shader hashes which are currently marked to be hidden. Otherwise false.
/// </summary>
/// <param name="commandList"></param>
/// <returns>true if the draw call has to be blocked</returns>
bool blockDrawCallForCommandList(command_list* commandList)
{
	if(nullptr==commandList)
	{
		return false;
	}

	const CommandListDataContainer &commandListData = commandList->get_private_data<CommandListDataContainer>();
	uint32_t shaderHash = g_pixelShaderManager.getShaderHash(commandListData.activePixelShaderPipeline);
	bool blockCall = g_pixelShaderManager.isBlockedShader(shaderHash);
	for(auto& group : g_toggleGroups)
	{
		blockCall |= group.isBlockedPixelShader(shaderHash);
	}
	shaderHash = g_vertexShaderManager.getShaderHash(commandListData.activeVertexShaderPipeline);
	blockCall |= g_vertexShaderManager.isBlockedShader(shaderHash);
	for(auto& group : g_toggleGroups)
	{
		blockCall |= group.isBlockedVertexShader(shaderHash);
	}
	shaderHash = g_computeShaderManager.getShaderHash(commandListData.activeComputeShaderPipeline);
	blockCall |= g_computeShaderManager.isBlockedShader(shaderHash);
	for(auto& group : g_toggleGroups)
	{
		blockCall |= group.isBlockedComputeShader(shaderHash);
	}
	return blockCall;
}


static bool onDraw(command_list* commandList, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	// check if for this command list the active shader handles are part of the blocked set. If so, return true
	return blockDrawCallForCommandList(commandList);
}


static bool onDrawIndexed(command_list* commandList, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
	// same as onDraw
	return blockDrawCallForCommandList(commandList);
}


static bool onDrawOrDispatchIndirect(command_list* commandList, indirect_command type, resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride)
{
	switch(type)
	{
		case indirect_command::unknown:
		case indirect_command::draw:
		case indirect_command::draw_indexed:
		case indirect_command::dispatch:
			// same as OnDraw
			return blockDrawCallForCommandList(commandList);
		// the rest aren't blocked
	}
	return false;
}


static void onReshadePresent(effect_runtime* runtime)
{
	if(g_activeCollectorFrameCounter>0)
	{
		--g_activeCollectorFrameCounter;
	}
	if(g_isSettingAddonKeybind || g_isSettingGroupKeybind)
		return;

	for(auto& group: g_toggleGroups)
	{
		if(group.isToggleKeyPressed(runtime))
		{
			group.toggleActive();
			// if the group's shaders are being edited, it should toggle the ones currently marked.
			if(group.getId() == g_toggleGroupIdShaderEditing)
			{
				g_vertexShaderManager.toggleHideMarkedShaders();
				g_pixelShaderManager.toggleHideMarkedShaders();
				g_computeShaderManager.toggleHideMarkedShaders();
			}
		}
	}

	// hardcoded hunting keys.
	// If Ctrl is pressed too, it'll step to the next marked shader (if any)
	// Numpad 1: previous pixel shader
	// Numpad 2: next pixel shader
	// Numpad 3: mark current pixel shader as part of the toggle group
	// Numpad 4: previous vertex shader
	// Numpad 5: next vertex shader
	// Numpad 6: mark current vertex shader as part of the toggle group
	// Numpad 7: previous compute shader
	// Numpad 8: next compute shader
	// Numpad 9: mark current compute shader as part of the toggle group
	if(g_addonKeyBindings[AddonKeybind::PIXEL_SHADER_DOWN].isKeyPressed(runtime))
	{
		g_pixelShaderManager.huntPreviousShader(false);
	}
	if(g_addonKeyBindings[AddonKeybind::PIXEL_SHADER_UP].isKeyPressed(runtime))
	{
		g_pixelShaderManager.huntNextShader(false);
	}
	if(g_addonKeyBindings[AddonKeybind::PIXEL_SHADER_MARKED_DOWN].isKeyPressed(runtime))
	{
		g_pixelShaderManager.huntPreviousShader(true);
	}
	if(g_addonKeyBindings[AddonKeybind::PIXEL_SHADER_MARKED_UP].isKeyPressed(runtime))
	{
		g_pixelShaderManager.huntNextShader(true);
	}
	if(g_addonKeyBindings[AddonKeybind::PIXEL_SHADER_MARK].isKeyPressed(runtime))
	{
		g_pixelShaderManager.toggleMarkOnHuntedShader();
	}

	if(g_addonKeyBindings[AddonKeybind::VERTEX_SHADER_DOWN].isKeyPressed(runtime))
	{
		g_vertexShaderManager.huntPreviousShader(false);
	}
	if(g_addonKeyBindings[AddonKeybind::VERTEX_SHADER_UP].isKeyPressed(runtime))
	{
		g_vertexShaderManager.huntNextShader(false);
	}
	if(g_addonKeyBindings[AddonKeybind::VERTEX_SHADER_MARKED_DOWN].isKeyPressed(runtime))
	{
		g_vertexShaderManager.huntPreviousShader(false);
	}
	if(g_addonKeyBindings[AddonKeybind::VERTEX_SHADER_MARKED_UP].isKeyPressed(runtime))
	{
		g_vertexShaderManager.huntNextShader(true);
	}
	if(g_addonKeyBindings[AddonKeybind::VERTEX_SHADER_MARK].isKeyPressed(runtime))
	{
		g_vertexShaderManager.toggleMarkOnHuntedShader();
	}

	if(g_addonKeyBindings[AddonKeybind::COMPUTE_SHADER_DOWN].isKeyPressed(runtime))
	{
		g_computeShaderManager.huntPreviousShader(false);
	}
	if(g_addonKeyBindings[AddonKeybind::COMPUTE_SHADER_UP].isKeyPressed(runtime))
	{
		g_computeShaderManager.huntNextShader(false);
	}
	if(g_addonKeyBindings[AddonKeybind::COMPUTE_SHADER_MARKED_DOWN].isKeyPressed(runtime))
	{
		g_computeShaderManager.huntPreviousShader(true);
	}
	if(g_addonKeyBindings[AddonKeybind::COMPUTE_SHADER_MARKED_UP].isKeyPressed(runtime))
	{
		g_computeShaderManager.huntNextShader(true);
	}
	if(g_addonKeyBindings[AddonKeybind::COMPUTE_SHADER_MARK].isKeyPressed(runtime))
	{
		g_computeShaderManager.toggleMarkOnHuntedShader();
	}
}


/// <summary>
/// Function which marks the end of a keybinding editing cycle
/// </summary>
/// <param name="acceptCollectedBinding"></param>
/// <param name="groupEditing"></param>
void endKeyBindingEditing(bool acceptCollectedBinding, ToggleGroup& groupEditing)
{
	if (acceptCollectedBinding && g_toggleGroupIdKeyBindingEditing == groupEditing.getId() && g_keyCollector.isValid())
	{
		if(g_keyCollector.getKeyForIniFile() != KeyData(VK_BACK).getKeyForIniFile())
			groupEditing.setToggleKey(g_keyCollector);
	}
	g_toggleGroupIdKeyBindingEditing = -1;
	g_keyCollector.clear();
}


/// <summary>
/// Function which marks the start of a keybinding editing cycle for the passed in toggle group
/// </summary>
/// <param name="groupEditing"></param>
void startKeyBindingEditing(ToggleGroup& groupEditing)
{
	if (g_toggleGroupIdKeyBindingEditing == groupEditing.getId())
	{
		return;
	}
	if (g_toggleGroupIdKeyBindingEditing >= 0)
	{
		endKeyBindingEditing(false, groupEditing);
	}
	g_toggleGroupIdKeyBindingEditing = groupEditing.getId();
}


/// <summary>
/// Function which marks the end of a shader editing cycle for a given toggle group
/// </summary>
/// <param name="acceptCollectedShaderHashes"></param>
/// <param name="groupEditing"></param>
void endShaderEditing(bool acceptCollectedShaderHashes, ToggleGroup& groupEditing)
{
	if(acceptCollectedShaderHashes && g_toggleGroupIdShaderEditing == groupEditing.getId())
	{
		groupEditing.storeCollectedHashes(g_pixelShaderManager.getMarkedShaderHashes(), g_vertexShaderManager.getMarkedShaderHashes(), g_computeShaderManager.getMarkedShaderHashes());
		g_pixelShaderManager.stopHuntingMode();
		g_vertexShaderManager.stopHuntingMode();
		g_computeShaderManager.stopHuntingMode();
	}
	g_toggleGroupIdShaderEditing = -1;
}


/// <summary>
/// Function which marks the start of a shader editing cycle for a given toggle group.
/// </summary>
/// <param name="groupEditing"></param>
void startShaderEditing(ToggleGroup& groupEditing)
{
	if(g_toggleGroupIdShaderEditing==groupEditing.getId())
	{
		return;
	}
	if(g_toggleGroupIdShaderEditing >= 0)
	{
		endShaderEditing(false, groupEditing);
	}
	g_toggleGroupIdShaderEditing = groupEditing.getId();
	g_activeCollectorFrameCounter = g_startValueFramecountCollectionPhase;
	g_pixelShaderManager.startHuntingMode(groupEditing.getPixelShaderHashes());
	g_vertexShaderManager.startHuntingMode(groupEditing.getVertexShaderHashes());
	g_computeShaderManager.startHuntingMode(groupEditing.getComputeShaderHashes());

	// after copying them to the managers, we can now clear the group's shader.
	groupEditing.clearHashes();
}


static void showHelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(450.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}


static void displaySettings(reshade::api::effect_runtime* runtime)
{
	if(g_toggleGroupIdKeyBindingEditing >= 0)
	{
		// a keybinding is being edited. Read current pressed keys into the collector, cumulatively;
		g_keyCollector.collectKeysPressed(runtime);
	}

	if(ImGui::CollapsingHeader("一般信息与帮助"))
    {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted("着色器切换器允许你创建若干分组，每个分组包含需要切换开启/关闭的着色器。你可以为每个分组绑定键盘快捷键（包括Shift、Alt、Ctrl），并为分组命名。每个分组可以拥有若干与之绑定的顶点着色器。当你按下相应键盘快捷键时，任何使用这些着色器的绘制调用将被禁用，从而有效隐藏3D场景中的元素。");
        ImGui::TextUnformatted("\n以下（硬编码的）键盘快捷键在单击分组的“更改着色器”按钮时可用：");
        ImGui::TextUnformatted("* [小键盘1] 和 [小键盘2]：前一个/后一个像素着色器");
        ImGui::TextUnformatted("* [Ctrl+小键盘1] 和 [Ctrl+小键盘2]：前一个/后一个分组内已标记的像素着色器");
        ImGui::TextUnformatted("* [小键盘3]：标记/取消标记当前像素着色器，即是否置入分组");
        ImGui::TextUnformatted("* [小键盘4] 和 [小键盘5]：前一个/后一个顶点着色器");
        ImGui::TextUnformatted("* [Ctrl+小键盘4] 和 [Ctrl+小键盘5]：前一个/后一个分组内已标记的顶点着色器");
        ImGui::TextUnformatted("* [小键盘6]：标记/取消标记当前顶点着色器，即是否置入分组");
        ImGui::TextUnformatted("* [小键盘7] 和 [小键盘8]：前一个/后一个顶点着色器");
		ImGui::TextUnformatted("* [Ctrl+小键盘7] 和 [Ctrl+小键盘8]：前一个/后一个分组内已标记的计算着色器");
        ImGui::TextUnformatted("* [小键盘9]：标记/取消标记当前顶点着色器，即是否置入分组");
        ImGui::TextUnformatted("\n当遍历着色器时，当前所在着色器在3D场景中被禁用，因此你可以观察它是不是你寻找的那个。");
        ImGui::TextUnformatted("完成标记后，请确认你点击了“保存所有切换分组”以保存你定义的分组，由此下次启动游戏时它们将被自动加载，从而可以立刻使用。");
        ImGui::PopTextWrapPos();
    }

	ImGui::AlignTextToFramePadding();
    if (ImGui::CollapsingHeader("着色器选择参数", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
        ImGui::SliderFloat("覆盖面板不透明度", &g_overlayOpacity, 0.0f, 1.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::SliderInt("所需收集的帧数量", &g_startValueFramecountCollectionPhase, 10, 1000);
        ImGui::SameLine();
        showHelpMarker("这是插件用以收集激活的着色器所用的帧数量。如果你想标记的着色器只是偶尔被使用，调高该值。只有在这些帧当中用到的着色器会被收集。");
        ImGui::PopItemWidth();
    }
	ImGui::Separator();

	g_isSettingAddonKeybind = false;
	if(ImGui::CollapsingHeader("键盘快捷键", ImGuiTreeNodeFlags_None))
	{
		for(uint32_t i = 0; i < IM_ARRAYSIZE(AddonKeybindNames); i++)
		{
			KeyData keys = g_addonKeyBindings[static_cast<AddonKeybind>(i)];
			
			char buf[48]{'\0'};
			if(keys.isValid())
				buf[keys.getKeyAsString().copy(buf, sizeof(buf) - 1)] = '\0';

			ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.35f);
			ImGui::InputTextWithHint(AddonKeybindNames[i], "点击设定键盘快捷键", buf, sizeof(buf), ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_ReadOnly);
			if(ImGui::IsItemActive())
			{
				g_isSettingAddonKeybind = true;
				g_keyCollector.collectKeysPressed(runtime);
				if(g_keyCollector.getKeyForIniFile() == KeyData(VK_BACK).getKeyForIniFile())
				{
					g_addonKeyBindings[i].clear();
				}
				else
				{
					if(g_keyCollector.isValid())// The case of only modifier keys pressed has been handled in collectKeysPressed
						g_addonKeyBindings[i] = std::move(g_keyCollector);
				}
				g_keyCollector.clear();
			}
			ImGui::PopItemWidth();
		}
	}

	g_isSettingGroupKeybind = false;

	if(ImGui::CollapsingHeader("切换分组列表", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if(ImGui::Button(" 新 "))
		{
			addDefaultGroup();
		}
		ImGui::Separator();

		std::vector<ToggleGroup> toRemove;
		for(auto& group : g_toggleGroups)
		{
			ImGui::PushID(group.getId());
			ImGui::AlignTextToFramePadding();
			if(ImGui::Button("X"))
			{
				toRemove.push_back(group);
			}
			ImGui::SameLine();
			bool active = group.isActive();
			if(ImGui::Checkbox("##Toggle", &active))
			{
				group.toggleActive();
			}
			ImGui::SameLine();
			ImGui::Text(" %d ", group.getId());
			ImGui::SameLine();
			if(ImGui::Button("编辑"))
			{
				group.setEditing(true);
			}

			ImGui::SameLine();
			if(g_toggleGroupIdShaderEditing >= 0)
			{
				if(g_toggleGroupIdShaderEditing == group.getId())
				{
					if(ImGui::Button(" 完成 "))
					{
						endShaderEditing(true, group);
					}
				}
				else
				{
					ImGui::BeginDisabled(true);
					ImGui::Button("      ");
					ImGui::EndDisabled();
				}
			}
			else
			{
				if(ImGui::Button("更改着色器"))
				{
					ImGui::SameLine();
					startShaderEditing(group);
				}
			}
			ImGui::SameLine();
			ImGui::Text(" %s (%s%s)", group.getName().c_str(), group.getToggleKeyAsString().c_str(), group.isActive() ? "，已激活" : "");
			if(group.isActiveAtStartup())
			{
				ImGui::SameLine();
				ImGui::Text(" (启动时激活)");
			}
			if(group.isEditing())
			{
				g_isSettingGroupKeybind = true;
				ImGui::Separator();
				ImGui::Text("编辑分组 %d", group.getId());

				// Name of group
				char tmpBuffer[150];
				const string& name = group.getName();
				strncpy_s(tmpBuffer, 150, name.c_str(), name.size());
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("名字");
				ImGui::SameLine(ImGui::GetWindowWidth() * 0.25f);
				ImGui::InputText("##Name", tmpBuffer, 149);
				group.setName(tmpBuffer);
				ImGui::PopItemWidth();

				// Key binding of group
				bool isKeyEditing = false;
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
				ImGui::AlignTextToFramePadding();

				KeyData keys = group.getToggleKeyData();
				char buf[48]{'\0'};
				if(keys.isValid())
					buf[keys.getKeyAsString().copy(buf, sizeof(buf) - 1)] = '\0';
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
				ImGui::Text("快捷键");
				ImGui::SameLine(ImGui::GetWindowWidth() * 0.25f);
				ImGui::InputTextWithHint("", "点击设定键盘快捷键", buf, sizeof(buf), ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_ReadOnly);
				if(ImGui::IsItemActive())
				{
					g_keyCollector.collectKeysPressed(runtime);
					if(g_keyCollector.isValid())
					{
						if(g_keyCollector.getKeyForIniFile() == KeyData(VK_BACK).getKeyForIniFile())
							keys.clear();
						else
							keys = std::move(g_keyCollector);

						group.setToggleKey(keys);
						g_keyCollector.clear();
					}
				}
				ImGui::PopItemWidth();

				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
				ImGui::Text(" ");
				ImGui::SameLine(ImGui::GetWindowWidth() * 0.25f);
				bool isDefaultActive = group.isActiveAtStartup();
				ImGui::Checkbox("启动时激活", &isDefaultActive);
				group.setIsActiveAtStartup(isDefaultActive);
				ImGui::PopItemWidth();

				if(!isKeyEditing)
				{
					if(ImGui::Button("OK"))
					{
						group.setEditing(false);
						g_toggleGroupIdKeyBindingEditing = -1;
						g_keyCollector.clear();
					}
				}
				ImGui::Separator();
			}

			ImGui::PopID();
		}
		if(toRemove.size() > 0)
		{
			// switch off keybinding editing or shader editing, if in progress
			g_toggleGroupIdKeyBindingEditing = -1;
			g_keyCollector.clear();
			g_toggleGroupIdShaderEditing = -1;
			g_pixelShaderManager.stopHuntingMode();
			g_vertexShaderManager.stopHuntingMode();
		}
		for(const auto& group : toRemove)
		{
			std::erase(g_toggleGroups, group);
		}

		ImGui::Separator();
		if(g_toggleGroups.size() > 0)
		{
			if(ImGui::Button("保存所有切换分组"))
			{
				saveShaderTogglerIniFile();
			}
		}
	}
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		{
			if(!reshade::register_addon(hModule))
			{
				return FALSE;
			}

			// We'll pass a nullptr for the module handle so we get the containing process' executable + path. We can't use the reshade's api as we don't have the runtime
			// and we can't use reshade's handle because under vulkan reshade is stored in a central space and therefore it won't get the folder of the exe (where the reshade dll is located as well).
			WCHAR buf[MAX_PATH];
			const std::filesystem::path dllPath = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf)) ? buf : std::filesystem::path();		// <installpath>/shadertoggler.addon64
			const std::filesystem::path basePath = dllPath.parent_path();																// <installpath>
			const std::string& hashFileName = HASH_FILE_NAME;
			g_iniFileName = (basePath / hashFileName).string();																			// <installpath>/shadertoggler.ini
			reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
			reshade::register_event<reshade::addon_event::init_command_list>(onInitCommandList);
			reshade::register_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
			reshade::register_event<reshade::addon_event::reset_command_list>(onResetCommandList);
			reshade::register_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
			reshade::register_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
			reshade::register_event<reshade::addon_event::reshade_present>(onReshadePresent);
			reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
			reshade::register_event<reshade::addon_event::draw>(onDraw);
			reshade::register_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
			reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);
			reshade::register_overlay(nullptr, &displaySettings);
			loadShaderTogglerIniFile();
		}
		break;
	case DLL_PROCESS_DETACH:
		reshade::unregister_event<reshade::addon_event::reshade_present>(onReshadePresent);
		reshade::unregister_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
		reshade::unregister_event<reshade::addon_event::init_pipeline>(onInitPipeline);
		reshade::unregister_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
		reshade::unregister_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
		reshade::unregister_event<reshade::addon_event::draw>(onDraw);
		reshade::unregister_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
		reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);
		reshade::unregister_event<reshade::addon_event::init_command_list>(onInitCommandList);
		reshade::unregister_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
		reshade::unregister_event<reshade::addon_event::reset_command_list>(onResetCommandList);
		reshade::unregister_overlay(nullptr, &displaySettings);
		reshade::unregister_addon(hModule);
		break;
	}

	return TRUE;
}
