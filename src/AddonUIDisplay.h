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

#pragma once

#include <format>
#include <ranges>
#include <cwctype>
#include "AddonUIConstants.h"
#include "AddonUIAbout.h"
#include "KeyData.h"
#include "ResourceManager.h"
#include "ConstantManager.h"

#define MAX_DESCRIPTOR_INDEX 10

// From Reshade, see https://github.com/crosire/reshade/blob/main/source/imgui_widgets.cpp
static bool key_input_box(const char* name, uint32_t* keys, const reshade::api::effect_runtime* runtime)
{
    char buf[48]; buf[0] = '\0';
    if (*keys)
        buf[ShaderToggler::reshade_key_name(*keys).copy(buf, sizeof(buf) - 1)] = '\0';

    ImGui::InputTextWithHint(name, "单击以设定键盘快捷键", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_NoHorizontalScroll);

    if (ImGui::IsItemActive())
    {
        const uint32_t last_key_pressed = ShaderToggler::reshade_last_key_pressed(runtime);
        if (last_key_pressed != 0)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
            {
                *keys = 0;

            }
            else if (last_key_pressed < 0x10 || last_key_pressed > 0x12) // Exclude modifier keys
            {
                *keys = last_key_pressed;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x11)) << 8;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x10)) << 16;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x12)) << 24;
            }

            return true;
        }
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("在字段中单击，并按任意键将其设为快捷键。");
    }

    return false;
}


static constexpr const char* invocationDescription[] =
{
    "绘制前",
    "绘制后",
    "渲染目标改变时"
};


static void DisplayIsPartOfToggleGroup()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
    ImGui::SameLine();
    ImGui::Text(" 着色器是该切换组的一部分。");
    ImGui::PopStyleColor();
}


static void DisplayTechniqueSelection(AddonImGui::AddonUIData& instance, ShaderToggler::ToggleGroup* group, float tblWidth = 0)
{
    if (group == nullptr)
    {
        return;
    }

    const std::vector<std::string>* techniquesPtr = instance.GetAllTechniques();
    std::unordered_set<std::string> curTechniques = group->preferredTechniques();
    std::unordered_set<std::string> newTechniques;
    static char searchBuf[256] = "\0";

    bool allowAll = group->getAllowAllTechniques();
    bool exceptions = group->getHasTechniqueExceptions();

    if (ImGui::BeginTable("Technique selection##options", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody))
    {
        ImGui::TableSetupColumn("##columnsetup", ImGuiTableColumnFlags_WidthFixed, tblWidth);

        ImGui::TableNextColumn();
        ImGui::Text("捕获所有效果器");
        ImGui::TableNextColumn();
        ImGui::Checkbox("##Catchalltechniques", &allowAll);

        ImGui::TableNextRow();

        if (allowAll)
        {
            ImGui::TableNextColumn();
            ImGui::Text("排除勾选的效果器");
            ImGui::TableNextColumn();
            ImGui::Checkbox("##Exceptfor", &exceptions);

            ImGui::TableNextRow();
        }

        ImGui::TableNextColumn();
        ImGui::Text("搜索");
        ImGui::TableNextColumn();
        ImGui::InputText("##techniqueSearch", searchBuf, 256, ImGuiInputTextFlags_None);


        ImGui::TableNextRow();


        ImGui::TableNextColumn();
        if (ImGui::Button("全部不选"))
        {
            curTechniques.clear();
        }
        ImGui::TableNextColumn();

        ImGui::EndTable();
    }

    ImGui::Separator();

    if (allowAll && !exceptions)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::BeginTable("Technique selection##table", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoBordersInBody))
    {
        ImGui::TableSetupColumn("##columnsetupSelection", ImGuiTableColumnFlags_WidthFixed, tblWidth);

        std::string searchString(searchBuf);

        if (techniquesPtr != nullptr)
        {
            for (const auto& technique : *techniquesPtr)
            {
                bool enabled = curTechniques.contains(technique);

                if (std::ranges::search(technique, searchString,
                    [](const wchar_t lhs, const wchar_t rhs) {return lhs == rhs; },
                    std::towupper, std::towupper).begin() != technique.end())
                {
                    ImGui::TableNextColumn();
                    ImGui::Checkbox(technique.c_str(), &enabled);
                }

                if (enabled)
                {
                    newTechniques.insert(technique);
                }
            }
        }

        ImGui::EndTable();
    }

    if (allowAll && !exceptions)
    {
        ImGui::EndDisabled();
    }

    group->setHasTechniqueExceptions(exceptions);
    group->setAllowAllTechniques(allowAll);

    if(techniquesPtr != nullptr && techniquesPtr->size() > 0)
        group->setPreferredTechniques(newTechniques);
}

static void DrawPreview(unsigned long long textureId, uint32_t srcWidth, uint32_t srcHeight)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    float height = ImGui::GetWindowHeight();
    float width = ImGui::GetWindowWidth();

    float new_width = static_cast<float>(srcWidth);
    float new_height = static_cast<float>(srcHeight);

    float ratio = std::min(width / new_width, height / new_height);
    new_width *= ratio;
    new_height *= ratio;

    auto initialCursorPos = ImGui::GetCursorPos();
    auto centralizedCursorpos = ImVec2((width - new_width) * 0.5f, (height - new_height) * 0.5f);
    ImGui::SetCursorPos(centralizedCursorpos);

    ImGui::Image(textureId, ImVec2(new_width, new_height));

    ImGui::PopStyleVar();
}

static void DisplayPreview(AddonImGui::AddonUIData& instance, Rendering::ResourceManager& resManager, reshade::api::effect_runtime* runtime, ShaderToggler::ToggleGroup* group, float width = 0)
{
    if (ImGui::BeginChild("RTPreview##child", { width, 0 }, true, ImGuiWindowFlags_None))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

        DeviceDataContainer& deviceData = runtime->get_device()->get_private_data<DeviceDataContainer>();
        reshade::api::resource_view srv = reshade::api::resource_view{ 0 };
        resManager.SetPongPreviewHandles(nullptr, nullptr, &srv);
        bool clearAlpha = group->getClearPreviewAlpha();

        ImGui::Text("清除Alpha通道");
        ImGui::SameLine();
        ImGui::Checkbox("##Clearalpha", &clearAlpha);

        if (srv != 0)
        {
            ImGui::SameLine();
            ImGui::Text(std::format(" 格式: {} ", static_cast<uint32_t>(deviceData.huntPreview.format)).c_str());
            ImGui::SameLine();
            ImGui::Text(std::format("宽度: {} ", deviceData.huntPreview.width).c_str());
            ImGui::SameLine();
            ImGui::Text(std::format("高度: {} ", deviceData.huntPreview.height).c_str());
            ImGui::Separator();

            if (ImGui::BeginChild("RTPreview##preview", { 0, 0 }, false, ImGuiWindowFlags_None))
            {
                DrawPreview(srv.handle, deviceData.huntPreview.width, deviceData.huntPreview.height);
            }
            ImGui::EndChild();
        }

        group->setClearPreviewAlpha(clearAlpha);

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

static void DisplayBindingPreview(AddonImGui::AddonUIData& instance, Rendering::ResourceManager& resManager, reshade::api::effect_runtime* runtime, const std::string& binding)
{
    if (ImGui::BeginChild("BindingPreview##child", { 0, 0 }, true, ImGuiWindowFlags_None))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

        DeviceDataContainer& deviceData = runtime->get_device()->get_private_data<DeviceDataContainer>();
        reshade::api::resource_view srv = reshade::api::resource_view{ 0 };
        resManager.SetPongPreviewHandles(nullptr, nullptr, &srv);
        const auto& it = deviceData.bindingMap.find(binding);

        if (it != deviceData.bindingMap.end())
        {
            auto& [_, texData] = *it;
            ImGui::Text(std::format("格式: {} ", static_cast<uint32_t>(texData.format)).c_str());
            ImGui::SameLine();
            ImGui::Text(std::format("宽度: {} ", texData.width).c_str());
            ImGui::SameLine();
            ImGui::Text(std::format("高度: {} ", texData.height).c_str());
            ImGui::Separator();

            if (ImGui::BeginChild("BindingPreview##preview", { 0, 0 }, false, ImGuiWindowFlags_None))
            {
                DrawPreview(texData.srv.handle, texData.width, texData.height);
            }
            ImGui::EndChild();
        }

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

static void DisplayRenderTargets(AddonImGui::AddonUIData& instance, Rendering::ResourceManager& resManager, reshade::api::effect_runtime* runtime, ShaderToggler::ToggleGroup* group)
{
    static float height = ImGui::GetWindowHeight();
    static float width = ImGui::GetWindowWidth();

    const char* typeSelectedItem = invocationDescription[group->getInvocationLocation()];
    uint32_t selectedIndex = group->getInvocationLocation();

    bool retry = group->getRequeueAfterRTMatchingFailure();
    bool tonemap = group->getToneMap();
    static const char* swapchainMatchOptions[] = { "分辨率", "宽高比", "扩展的宽高比", "无"};
    uint32_t selectedSwapchainMatchMode = group->getMatchSwapchainResolution();
    const char* typesSelectedSwapchainMatchMode = swapchainMatchOptions[selectedSwapchainMatchMode];

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (ImGui::BeginChild("RenderTargets", { 0, height / 1.5f }, true, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

        if (ImGui::BeginTable("RenderTargetsSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody))
        {
            ImGui::TableSetupColumn("##RTcolumnsetup", ImGuiTableColumnFlags_WidthFixed, ImGui::GetWindowWidth() / 3);

            ImGui::TableNextColumn();
            ImGui::Text("渲染目标索引");
            ImGui::TableNextColumn();
            ImGui::Text("%u", group->getRenderTargetIndex());
            ImGui::SameLine();

            if (ImGui::SmallButton("+"))
            {
                group->setRenderTargetIndex(group->getRenderTargetIndex() + 1);
            }

            if (group->getRenderTargetIndex() != 0)
            {
                ImGui::SameLine();

                if (ImGui::SmallButton("-"))
                {
                    group->setRenderTargetIndex(group->getRenderTargetIndex() - 1);
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::Text("调用位置");
            ImGui::TableNextColumn();
            if (ImGui::BeginCombo("##Invocationlocation", typeSelectedItem, ImGuiComboFlags_None))
            {
                for (int n = 0; n < IM_ARRAYSIZE(invocationDescription); n++)
                {
                    bool is_selected = (typeSelectedItem == invocationDescription[n]);
                    if (ImGui::Selectable(invocationDescription[n], is_selected))
                    {
                        typeSelectedItem = invocationDescription[n];
                        selectedIndex = n;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::Text("重试渲染目标[RT]分配");
            ImGui::TableNextColumn();
            ImGui::Checkbox("##RetryRTassignment", &retry);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::Text("应用色调映射钳位");
            ImGui::TableNextColumn();
            ImGui::Checkbox("##tonemap", &tonemap);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::Text("匹配交换链");
            ImGui::TableNextColumn();
            if (ImGui::BeginCombo("##effSwapChainMatchMode", typesSelectedSwapchainMatchMode, ImGuiComboFlags_None))
            {
                for (int n = 0; n < IM_ARRAYSIZE(swapchainMatchOptions); n++)
                {
                    bool is_selected = (typesSelectedSwapchainMatchMode == swapchainMatchOptions[n]);
                    if (ImGui::Selectable(swapchainMatchOptions[n], is_selected))
                    {
                        typesSelectedSwapchainMatchMode = swapchainMatchOptions[n];
                        selectedSwapchainMatchMode = n;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::EndTable();
        }

        group->setRequeueAfterRTMatchingFailure(retry);
        group->setMatchSwapchainResolution(selectedSwapchainMatchMode);
        group->setInvocationLocation(selectedIndex);
        group->setToneMap(tonemap);

        ImGui::Separator();

        DisplayTechniqueSelection(instance, group, ImGui::GetWindowWidth() / 3);

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::PushID(4);
    ImGui::Button("", ImVec2(-1, 8.0f));
    ImGui::PopID();
    if (ImGui::IsItemActive())
        height += ImGui::GetIO().MouseDelta.y;

    DisplayPreview(instance, resManager, runtime, group);

    ImGui::PopStyleVar();
}

static void DisplayGroupView(AddonImGui::AddonUIData& instance, Rendering::ResourceManager& resManager, reshade::api::effect_runtime* runtime, ShaderToggler::ToggleGroup* group, ShaderToggler::ShaderManager* shaderManager)
{
    float height = ImGui::GetWindowHeight();
    float width = ImGui::GetWindowWidth();

    if (*instance.ActiveCollectorFrameCounter() > 0)
    {
        return;
    }

    const std::unordered_set<uint32_t>& hashes = shaderManager->getCollectedShaderHashes();
    static int32_t selected = -1;
    uint32_t index = 0;
    ImGuiStyle style = ImGui::GetStyle();

    if (ImGui::BeginTable("ShaderHashView", 1, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoBordersInBody | ImGuiTableColumnFlags_NoHeaderLabel, ImVec2(0, height - 47)))
    {
        for (auto h : hashes)
        {
            ImGui::TableNextColumn();

            bool marked = false;
            if (shaderManager->isHuntedShaderMarked(shaderManager->getCollectedShaderHash(index)))
            {
                marked = true;
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
            }

            if (ImGui::Selectable(std::format("{:#08x}", h).c_str(), selected == index, ImGuiSelectableFlags_AllowDoubleClick) && (ImGui::IsMouseDoubleClicked(0) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
            {
                shaderManager->toggleMarkOnHuntedShader();
            }

            if (marked)
            {
                ImGui::PopStyleColor();
            }

            if (ImGui::IsItemFocused())
            {
                shaderManager->setActivedHuntedShaderIndex(index);
                instance.UpdateToggleGroupsForShaderHashes();
                selected = index;
            };

            index++;
        }

        ImGui::EndTable();
    }
}

static void DisplayTextureBindings(AddonImGui::AddonUIData& instance, ShaderToggler::ToggleGroup* group, reshade::api::effect_runtime* runtime, Rendering::ResourceManager& resManager)
{
    static float height = ImGui::GetWindowHeight();
    float width = ImGui::GetWindowWidth();

    const char* typeItems[] = { "渲染目标", "着色器资源视图" };
    uint32_t selectedIndex = group->getExtractResourceViews() ? 1 : 0;
    const char* typeSelectedItem = typeItems[selectedIndex];
    DeviceDataContainer& deviceData = runtime->get_device()->get_private_data<DeviceDataContainer>();

    static const char* swapchainMatchOptions[] = { "分辨率", "宽高比", "扩展的宽高比", "无" };
    uint32_t selectedSwapchainMatchMode = group->getBindingMatchSwapchainResolution();
    const char* typesSelectedSwapchainMatchMode = swapchainMatchOptions[selectedSwapchainMatchMode];

    static const char* stageItems[] = { "顶点", "像素", "计算" };
    uint32_t selectedStageIndex = group->getSRVShaderStage();
    const char* selectedStage = stageItems[selectedStageIndex];

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (ImGui::BeginChild("Texture bindings viewer", { 0, height / 2.0f }, true, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

        // Name of group
        char tmpBuffer[150];

        // Name of Binding
        bool isBindingEnabled = group->isProvidingTextureBinding();

        const std::string& bindingName = group->getTextureBindingName();
        strncpy_s(tmpBuffer, 150, bindingName.c_str(), bindingName.size());

        bool copyBinding = group->getCopyTextureBinding();
        bool clearBinding = group->getClearBindings();

        if (ImGui::BeginTable("Bindingsettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody))
        {
            ImGui::TableSetupColumn("##BindingColumnSetup", ImGuiTableColumnFlags_WidthFixed, ImGui::GetWindowWidth() / 3);

            ImGui::TableNextColumn();
            ImGui::Text("纹理绑定已启用");
            ImGui::TableNextColumn();
            ImGui::Checkbox("##Texturebindingenabled", &isBindingEnabled);

            if (!isBindingEnabled)
            {
                ImGui::BeginDisabled();
                group->setProvidingTextureBinding(false);
            }
            else
            {
                group->setProvidingTextureBinding(true);
            }

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("纹理语义");
            ImGui::TableNextColumn();
            ImGui::InputText("##BindingName", tmpBuffer, 149);

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("纹理源");
            ImGui::TableNextColumn();
            if (ImGui::BeginCombo("##Bindingsource", typeSelectedItem, ImGuiComboFlags_None))
            {
                for (int n = 0; n < IM_ARRAYSIZE(typeItems); n++)
                {
                    bool is_selected = (typeSelectedItem == typeItems[n]);
                    if (ImGui::Selectable(typeItems[n], is_selected))
                    {
                        typeSelectedItem = typeItems[n];
                        selectedIndex = n;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("为绑定创建纹理拷贝");
            ImGui::TableNextColumn();
            ImGui::Checkbox("##Copybinding", &copyBinding);

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("Hash缺失时清除绑定");
            ImGui::TableNextColumn();
            ImGui::Checkbox("##Clearbinding", &clearBinding);

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (ImGui::Button("应用"))
            {
                deviceData.reload_bindings = true;
            }
            ImGui::TableNextColumn();

            ImGui::EndTable();
        }

        group->setTextureBindingName(tmpBuffer);
        group->setCopyTextureBinding(copyBinding);
        group->setClearBindings(clearBinding);

        ImGui::Separator();

        if (ImGui::BeginTable("BindingSourcesettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody))
        {
            ImGui::TableSetupColumn("##BindingSourceColumnSetup", ImGuiTableColumnFlags_WidthFixed, ImGui::GetWindowWidth() / 3);

            if (selectedIndex == 1)
            {
                if (!instance.GetTrackDescriptors())
                {
                    ImGui::BeginDisabled();
                    group->setExtractResourceViews(false);
                }
                else
                {
                    group->setExtractResourceViews(true);
                }

                ImGui::TableNextColumn();
                ImGui::Text("着色器阶段");
                ImGui::TableNextColumn();
                if (ImGui::BeginCombo("##ShaderStage", selectedStage, ImGuiComboFlags_None))
                {
                    for (int n = 0; n < IM_ARRAYSIZE(stageItems); n++)
                    {
                        bool is_selected = (selectedStage == stageItems[n]);
                        if (ImGui::Selectable(stageItems[n], is_selected))
                        {
                            selectedStageIndex = n;
                            selectedStage = stageItems[n];
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                group->setSRVShaderStage(selectedStageIndex);

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("槽");
                ImGui::TableNextColumn();
                ImGui::Text("%u", group->getBindingSRVSlotIndex());
                ImGui::SameLine();
                ImGui::PushID(0);
                if (ImGui::SmallButton("+"))
                {
                    group->setBindingSRVSlotIndex(group->getBindingSRVSlotIndex() + 1);
                }
                ImGui::PopID();

                if (group->getBindingSRVSlotIndex() != 0)
                {
                    ImGui::SameLine();

                    if (ImGui::SmallButton("-"))
                    {
                        group->setBindingSRVSlotIndex(group->getBindingSRVSlotIndex() - 1);
                    }
                }

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("绑定");
                ImGui::TableNextColumn();
                ImGui::Text("%u", group->getBindingSRVDescriptorIndex());
                ImGui::SameLine();
                ImGui::PushID(2);
                if (ImGui::SmallButton("+"))
                {
                    group->dispatchSRVCycle(ShaderToggler::CYCLE_UP);
                }
                ImGui::PopID();

                if (group->getBindingSRVDescriptorIndex() != 0)
                {
                    ImGui::SameLine();

                    ImGui::PushID(1);
                    if (ImGui::SmallButton("-"))
                    {
                        group->dispatchSRVCycle(ShaderToggler::CYCLE_DOWN);
                    }
                    ImGui::PopID();
                }

                if (!instance.GetTrackDescriptors())
                {
                    ImGui::EndDisabled();
                }
            }
            else
            {
                group->setExtractResourceViews(false);

                const char* rtTypeSelectedItem = invocationDescription[group->getBindingInvocationLocation()];
                uint32_t rtSelectedIndex = group->getBindingInvocationLocation();

                ImGui::TableNextColumn();
                ImGui::Text("渲染目标索引");
                ImGui::TableNextColumn();
                ImGui::Text("%u", group->getBindingRenderTargetIndex());
                ImGui::SameLine();

                ImGui::PushID(0);
                if (ImGui::SmallButton("+"))
                {
                    group->setBindingRenderTargetIndex(group->getBindingRenderTargetIndex() + 1);
                }
                ImGui::PopID();

                if (group->getBindingRenderTargetIndex() != 0)
                {
                    ImGui::SameLine();

                    if (ImGui::SmallButton("-"))
                    {
                        group->setBindingRenderTargetIndex(group->getBindingRenderTargetIndex() - 1);
                    }
                }

                ImGui::TableNextRow();

                if (!copyBinding)
                {
                    ImGui::BeginDisabled();
                }

                ImGui::TableNextColumn();
                ImGui::Text("调用位置");
                ImGui::TableNextColumn();
                if (ImGui::BeginCombo("##Invocationlocation", rtTypeSelectedItem, ImGuiComboFlags_None))
                {
                    for (int n = 0; n < IM_ARRAYSIZE(invocationDescription); n++)
                    {
                        bool is_selected = (rtTypeSelectedItem == invocationDescription[n]);
                        if (ImGui::Selectable(invocationDescription[n], is_selected))
                        {
                            rtTypeSelectedItem = invocationDescription[n];
                            rtSelectedIndex = n;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (!copyBinding)
                {
                    ImGui::EndDisabled();
                }

                ImGui::TableNextColumn();
                ImGui::Text("匹配交换链");
                ImGui::TableNextColumn();
                if (ImGui::BeginCombo("##swapChainMatchMode", typesSelectedSwapchainMatchMode, ImGuiComboFlags_None))
                {
                    for (int n = 0; n < IM_ARRAYSIZE(swapchainMatchOptions); n++)
                    {
                        bool is_selected = (typesSelectedSwapchainMatchMode == swapchainMatchOptions[n]);
                        if (ImGui::Selectable(swapchainMatchOptions[n], is_selected))
                        {
                            typesSelectedSwapchainMatchMode = swapchainMatchOptions[n];
                            selectedSwapchainMatchMode = n;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                group->setBindingInvocationLocation(rtSelectedIndex);
                group->setBindingMatchSwapchainResolution(selectedSwapchainMatchMode);
            }

            ImGui::EndTable();
        }

        if (!isBindingEnabled)
        {
            ImGui::EndDisabled();
        }

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::PushID(3);
    ImGui::Button("", ImVec2(-1, 8.0f));
    ImGui::PopID();
    if (ImGui::IsItemActive())
        height += ImGui::GetIO().MouseDelta.y;

    DisplayBindingPreview(instance, resManager, runtime, group->getTextureBindingName());

    ImGui::PopStyleVar();
}

static void DisplayOverlay(AddonImGui::AddonUIData& instance, Rendering::ResourceManager& resManager, reshade::api::effect_runtime* runtime)
{
    if (instance.GetToggleGroupIdShaderEditing() >= 0)
    {
        std::string editingGroupName = "";
        const int idx = instance.GetToggleGroupIdShaderEditing();
        ShaderToggler::ToggleGroup* group = nullptr;
        if (instance.GetToggleGroups().find(idx) != instance.GetToggleGroups().end())
        {
            editingGroupName = instance.GetToggleGroups()[idx].getName();
            group = &instance.GetToggleGroups()[idx];
        }

        if (group == nullptr)
            return;

        ImGui::SetNextWindowBgAlpha(1.0);
        ImGui::SetNextWindowSize({ 1024, 768 }, ImGuiCond_Once);
        bool wndOpen = true;

        static float height = ImGui::GetWindowHeight();
        static float width = ImGui::GetWindowWidth();

        const char* typeItems[] = { "像素着色器", "顶点着色器", "计算着色器"};
        static const char* typeSelectedItem = typeItems[0];
        static uint32_t selectedIndex = 0;

        ShaderToggler::ShaderManager* selectedShaderManager = selectedIndex == 0 ? instance.GetPixelShaderManager() : (selectedIndex == 1 ? instance.GetVertexShaderManager() : instance.GetComputeShaderManager());

        if (ImGui::Begin(std::format("分组设置 ({})", editingGroupName).c_str(), &wndOpen))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            if (ImGui::BeginChild("GroupView", { width / 3.0f, 0 }, true, ImGuiWindowFlags_NoScrollbar))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

                DisplayGroupView(instance, resManager, runtime, group, selectedShaderManager);

                ImGui::PushItemWidth(ImGui::GetWindowWidth() - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x * 2);
                if (ImGui::BeginCombo("##shaderType", typeSelectedItem, ImGuiComboFlags_None))
                {
                    for (int n = 0; n < IM_ARRAYSIZE(typeItems); n++)
                    {
                        bool is_selected = (typeSelectedItem == typeItems[n]);
                        if (ImGui::Selectable(typeItems[n], is_selected))
                        {
                            if (n != selectedIndex)
                            {
                                // Reset hunting selections in other managers on switch
                                switch (n)
                                {
                                case 0:
                                {
                                    instance.GetVertexShaderManager()->resetActiveHuntedShader();
                                    instance.GetComputeShaderManager()->resetActiveHuntedShader();
                                }
                                    break;
                                case 1:
                                {
                                    instance.GetPixelShaderManager()->resetActiveHuntedShader();
                                    instance.GetComputeShaderManager()->resetActiveHuntedShader();
                                }
                                    break;
                                case 2:
                                {
                                    instance.GetPixelShaderManager()->resetActiveHuntedShader();
                                    instance.GetVertexShaderManager()->resetActiveHuntedShader();
                                }
                                    break;
                                default:
                                    break;
                                }

                                instance.UpdateToggleGroupsForShaderHashes();
                            }

                            typeSelectedItem = typeItems[n];
                            selectedIndex = n;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::PushID(0);
            ImGui::Button("", ImVec2(8.0f, -1));
            ImGui::PopID();
            if (ImGui::IsItemActive())
                width += ImGui::GetIO().MouseDelta.x;

            ImGui::SameLine();

            if (ImGui::BeginChild("GroupSettings", { 0, 0 }, true, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

                ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
                if (ImGui::BeginTabBar("MyTabBar", tab_bar_flags))
                {
                    if (ImGui::BeginTabItem("效果"))
                    {
                        instance.SetCurrentTabType(AddonImGui::TAB_RENDER_TARGET);
                        DisplayRenderTargets(instance, resManager, runtime, group);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("常量绑定"))
                    {
                        instance.SetCurrentTabType(AddonImGui::TAB_CONSTANT_BUFFER);
                        DisplayConstantTab(instance, group, runtime->get_device());
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("纹理绑定"))
                    {
                        instance.SetCurrentTabType(AddonImGui::TAB_TEXTURE_BINDING);
                        DisplayTextureBindings(instance, group, runtime, resManager);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();

            ImGui::PopStyleVar();
        }
        ImGui::End();

        if (!wndOpen)
        {
            instance.SetCurrentTabType(AddonImGui::TAB_NONE);
            instance.EndShaderEditing(true, *group);
        }
    }
    else
    {
        instance.SetCurrentTabType(AddonImGui::TAB_NONE);
    }
}


static void CheckHotkeys(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime)
{
    if (*instance.ActiveCollectorFrameCounter() > 0)
    {
        --(*instance.ActiveCollectorFrameCounter());
    }
}


static void ShowHelpMarker(const char* desc)
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


static void DisplaySettings(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime)
{
    DisplayAbout();

    if (ImGui::CollapsingHeader("一般信息与帮助"))
    {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted("Shader Toggler允许你创建若干个分组，每个分组包含需要开关切换的着色器。你可以为每个分组指定键盘快捷键（可以包含Shift、Alt、Ctrl），以及方便的命名。每个分组可以有与之对应的若干个顶点着色器或像素着色器。按下对应的快捷键时，任何使用了这些游戏着色器的绘制调用会被禁用，有效地隐藏3D场景中的元素。");
        ImGui::TextUnformatted("\n以下（硬编码的）键盘快捷键可以在单击分组的“更改着色器”按钮后使用。");
        ImGui::TextUnformatted("* [小键盘1] 和 [小键盘2]: 前一个/后一个像素着色器");
        ImGui::TextUnformatted("* [Ctrl + 小键盘1] 和 [Ctrl + 小键盘2]: 分组中前一个/后一个已标记的像素着色器");
        ImGui::TextUnformatted("* [小键盘3]: 标记/取消标记当前的像素着色器，控制是否置入该分组");
        ImGui::TextUnformatted("* [小键盘4] 和 [小键盘5]:  前一个/后一个顶点着色器");
        ImGui::TextUnformatted("* [Ctrl + 小键盘4] 和 [Ctrl + 小键盘5]: 分组中前一个/后一个已标记的顶点着色器");
        ImGui::TextUnformatted("* [小键盘6]: 标记/取消标记当前的像素着色器，控制是否置入该分组");
        ImGui::TextUnformatted("\n当遍历游戏着色器时，当前选中的着色器将在3D场景中被禁用，于是你可以观察并确认它是不是你在找的那个。");
        ImGui::TextUnformatted("完成后，确保你单击了“保存所有切换分组”，保存你定义的所有分组。下次启动游戏时它们会自动加载，以便你直接使用。");
        ImGui::PopTextWrapPos();
    }

    ImGui::AlignTextToFramePadding();
    if (ImGui::CollapsingHeader("着色器选择参数", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
        ImGui::SliderFloat("覆盖层不透明度", instance.OverlayOpacity(), 0.0f, 1.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::SliderInt("收集的帧数量", instance.StartValueFramecountCollectionPhase(), 10, 1000);
        ImGui::SameLine();
        ShowHelpMarker("本插件收集活跃着色器所使用的帧数量。如果你要找的游戏着色器仅仅偶尔生效，则设置一个较高的值。只有在这些帧中使用过的着色器才会被记录。");
        ImGui::PopItemWidth();
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("选项", ImGuiTreeNodeFlags_None))
    {
        ImGui::AlignTextToFramePadding();
        std::string varSelectedItem = instance.GetResourceShim();
        if (ImGui::BeginCombo("资源Shim", varSelectedItem.c_str(), ImGuiComboFlags_None))
        {
            for (auto& v : Rendering::ResourceShimNames)
            {
                bool is_selected = (varSelectedItem == v);
                if (ImGui::Selectable(v.c_str(), is_selected))
                {
                    varSelectedItem = v;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        instance.SetResourceShim(varSelectedItem);

        ImGui::AlignTextToFramePadding();
        std::string varSelectedCopyMethod = instance.GetConstHookCopyType();
        if (ImGui::BeginCombo("常量缓冲区拷贝方法", varSelectedCopyMethod.c_str(), ImGuiComboFlags_None))
        {
            for (auto& v : Shim::Constants::ConstantCopyTypeNames)
            {
                bool is_selected = (varSelectedCopyMethod == v);
                if (ImGui::Selectable(v.c_str(), is_selected))
                {
                    varSelectedCopyMethod = v;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        instance.SetConstHookCopyType(varSelectedCopyMethod);

        ImGui::AlignTextToFramePadding();
        bool trackDescriptors = instance.GetTrackDescriptors();
        ImGui::Checkbox("跟踪描述符", &trackDescriptors);
        instance.SetTrackDescriptors(trackDescriptors);
    }

    if (ImGui::CollapsingHeader("快捷键", ImGuiTreeNodeFlags_None))
    {
        for (uint32_t i = 0; i < IM_ARRAYSIZE(AddonImGui::KeybindNames); i++)
        {
            uint32_t keys = instance.GetKeybinding(static_cast<AddonImGui::Keybind>(i));
            ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.35f);
            if (key_input_box(AddonImGui::KeybindNames[i], &keys, runtime))
            {
                instance.SetKeybinding(static_cast<AddonImGui::Keybind>(i), keys);
            }
            ImGui::PopItemWidth();
        }
    }

    if (ImGui::CollapsingHeader("切换分组列表", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button(" 新 "))
        {
            instance.AddDefaultGroup();
        }
        ImGui::Separator();

        std::vector<ShaderToggler::ToggleGroup> toRemove;
        for (auto& [_,group] : instance.GetToggleGroups())
        {

            ImGui::PushID(group.getId());
            ImGui::AlignTextToFramePadding();
            if (ImGui::Button("X"))
            {
                toRemove.push_back(group);
            }
            ImGui::SameLine();
            ImGui::Text(" %d ", group.getId());

            ImGui::SameLine();
            bool groupActive = group.isActive();
            ImGui::Checkbox("激活", &groupActive);
            if (groupActive != group.isActive())
            {
                group.toggleActive();

                if (!groupActive && instance.GetConstantHandler() != nullptr)
                {
                    instance.GetConstantHandler()->RemoveGroup(&group, runtime->get_device());
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("编辑"))
            {
                group.setEditing(true);
            }

            ImGui::SameLine();
            if (instance.GetToggleGroupIdShaderEditing() >= 0)
            {
                if (instance.GetToggleGroupIdShaderEditing() == group.getId())
                {
                    if (ImGui::Button(" 完成 "))
                    {
                        instance.EndShaderEditing(true, group);
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
                if (ImGui::Button("设置"))
                {
                    ImGui::SameLine();
                    instance.StartShaderEditing(group);
                }
            }

            ImGui::SameLine();
            if (group.getToggleKey() > 0)
            {
                ImGui::Text(" %s (%s)", group.getName().c_str(), ShaderToggler::reshade_key_name(group.getToggleKey()).c_str());
            }
            else
            {
                ImGui::Text(" %s", group.getName().c_str());
            }

            if (group.isEditing())
            {
                ImGui::Separator();
                ImGui::Text("编辑 组%d", group.getId());

                // Name of group
                char tmpBuffer[150];
                const std::string& name = group.getName();
                strncpy_s(tmpBuffer, 150, name.c_str(), name.size());
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("命名");
                ImGui::SameLine(ImGui::GetWindowWidth() * 0.2f);
                ImGui::InputText("##Name", tmpBuffer, 149);
                group.setName(tmpBuffer);
                ImGui::PopItemWidth();
                
                // Key binding of group
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("快捷键");
                ImGui::SameLine(ImGui::GetWindowWidth() * 0.2f);

                uint32_t keys = group.getToggleKey();
                if (key_input_box(ShaderToggler::reshade_key_name(keys).c_str(), &keys, runtime))
                {
                    group.setToggleKey(keys);
                }
                ImGui::PopItemWidth();

                if (ImGui::Button("确认"))
                {
                    group.setEditing(false);
                }
                ImGui::Separator();
            }

            ImGui::PopID();
        }
        if (toRemove.size() > 0)
        {
            // switch off keybinding editing or shader editing, if in progress
            instance.GetToggleGroupIdEffectEditing() = -1;
            instance.GetToggleGroupIdShaderEditing() = -1;
            instance.GetToggleGroupIdConstantEditing() = -1;
            instance.StopHuntingMode();
        }
        for (const auto& group : toRemove)
        {
            std::erase_if(instance.GetToggleGroups(), [&group](const auto& item) {
                return item.first == group.getId();
                });
        }

        if (toRemove.size() > 0)
        {
            instance.UpdateToggleGroupsForShaderHashes();
        }

        ImGui::Separator();
        if (instance.GetToggleGroups().size() > 0)
        {
            if (ImGui::Button("保存所有切换分组"))
            {
                instance.SaveShaderTogglerIniFile();
            }
        }
    }
}
