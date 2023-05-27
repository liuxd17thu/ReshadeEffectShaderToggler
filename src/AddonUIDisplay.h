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
#include "KeyData.h"
#include "ResourceManager.h"

#define MAX_DESCRIPTOR_INDEX 10

// From Reshade, see https://github.com/crosire/reshade/blob/main/source/imgui_widgets.cpp
static bool key_input_box(const char* name, uint32_t* keys, const reshade::api::effect_runtime* runtime)
{
    char buf[48]; buf[0] = '\0';
    if (*keys)
        buf[ShaderToggler::reshade_key_name(*keys).copy(buf, sizeof(buf) - 1)] = '\0';

    ImGui::InputTextWithHint(name, "点击以设置键盘快捷键", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_NoHorizontalScroll);

    if (ImGui::IsItemActive())
    {
        const uint32_t last_key_pressed = ShaderToggler::reshade_last_key_pressed(runtime);
        if (last_key_pressed != 0)
        {
            if (last_key_pressed == static_cast<uint32_t>(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
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
        ImGui::SetTooltip("在字段中单击，然后按任意键，以将其设置为新的快捷键。");
    }

    return false;
}


static constexpr const char* invocationDescription[] =
{
    "BEFORE DRAW",
    "AFTER DRAW",
    "ON RENDER TARGET CHANGE"
};


static void DisplayIsPartOfToggleGroup()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
    ImGui::SameLine();
    ImGui::Text(" 着色器属于当前切换组。");
    ImGui::PopStyleColor();
}


static void DisplayTechniqueSelection(AddonImGui::AddonUIData& instance, ShaderToggler::ToggleGroup* group)
{
    if (group == nullptr)
    {
        return;
    }

    const uint32_t columns = 2;
    const std::vector<std::string>* techniquesPtr = instance.GetAllTechniques();
    std::unordered_set<std::string> curTechniques = group->preferredTechniques();
    std::unordered_set<std::string> newTechniques;
    static char searchBuf[256] = "\0";

    ImGui::SetNextWindowSize({ 500, 300 }, ImGuiCond_Once);
    bool wndOpen = true;
    if (ImGui::Begin("效果器选择", &wndOpen))
    {
        if (ImGui::BeginChild("效果器选择##child", { 0, 0 }, true, ImGuiWindowFlags_AlwaysAutoResize))
        {
            bool allowAll = group->getAllowAllTechniques();
            ImGui::Checkbox("捕获所有效果器", &allowAll);
            group->setAllowAllTechniques(allowAll);

            bool exceptions = group->getHasTechniqueExceptions();
            if (allowAll)
            {
                ImGui::SameLine();
                ImGui::Checkbox("排除勾选的效果器", &exceptions);
                group->setHasTechniqueExceptions(exceptions);
            }

            if (ImGui::Button("全部不选"))
            {
                curTechniques.clear();
            }

            ImGui::SameLine();
            ImGui::Text("搜索：");
            ImGui::SameLine();
            ImGui::InputText("##techniqueSearch", searchBuf, 256, ImGuiInputTextFlags_None);

            ImGui::Separator();

            if (allowAll && !exceptions)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::BeginTable("效果器选择##table", columns, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders))
            {
                std::string searchString(searchBuf);

                for (techniquesPtr != nullptr; const auto& technique : *techniquesPtr)
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

            if (allowAll && !exceptions)
            {
                ImGui::EndDisabled();
            }
        }
        ImGui::EndChild();

        group->setPreferredTechniques(newTechniques);
    }
    ImGui::End();

    if (!wndOpen)
    {
        instance.EndEffectEditing();
    }
}

static void DisplayOverlay(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime)
{
    if (instance.GetToggleGroupIdConstantEditing() >= 0)
    {
        ShaderToggler::ToggleGroup* tGroup = nullptr;
        const int idx = instance.GetToggleGroupIdConstantEditing();
        if (instance.GetToggleGroups().find(idx) != instance.GetToggleGroups().end())
        {
            tGroup = &instance.GetToggleGroups()[idx];
        }

        DisplayConstantViewer(instance, tGroup, runtime->get_device());
    }

    if (instance.GetToggleGroupIdEffectEditing() >= 0)
    {
        ShaderToggler::ToggleGroup* tGroup = nullptr;
        const int idx = instance.GetToggleGroupIdEffectEditing();
        if (instance.GetToggleGroups().find(idx) != instance.GetToggleGroups().end())
        {
            tGroup = &instance.GetToggleGroups()[idx];
        }

        DisplayTechniqueSelection(instance, tGroup);
    }

    if (instance.GetToggleGroupIdShaderEditing() >= 0)
    {
        std::string editingGroupName = "";
        const int idx = instance.GetToggleGroupIdShaderEditing();
        ShaderToggler::ToggleGroup* tGroup = nullptr;
        if (instance.GetToggleGroups().find(idx) != instance.GetToggleGroups().end())
        {
            editingGroupName = instance.GetToggleGroups()[idx].getName();
            tGroup = &instance.GetToggleGroups()[idx];
        }

        bool wndOpen = true;
        ImGui::SetNextWindowBgAlpha(*instance.OverlayOpacity());
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        if (!ImGui::Begin(std::format("编辑组 {}", editingGroupName).c_str(), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("包含顶点着色器的管线数量：%d。# 聚合的不同顶点着色器的数量：%d。", instance.GetVertexShaderManager()->getPipelineCount(), instance.GetVertexShaderManager()->getShaderCount());
        ImGui::Text("包含像素着色器的管线数量：%d。# 聚合的不同像素着色器的数量：%d。", instance.GetPixelShaderManager()->getPipelineCount(), instance.GetPixelShaderManager()->getShaderCount());
        if (*instance.ActiveCollectorFrameCounter() > 0)
        {
            const uint32_t counterValue = *instance.ActiveCollectorFrameCounter();
            ImGui::Text("收集激活中的着色器… 剩余帧数：%d", counterValue);
        }
        else
        {
            if (instance.GetVertexShaderManager()->isInHuntingMode() || instance.GetPixelShaderManager()->isInHuntingMode())
            {
                ImGui::Text("正在为该组编辑着色器：%s", editingGroupName.c_str());
                if (tGroup != nullptr)
                {
                    ImGui::Text("调用位置：%s", invocationDescription[tGroup->getInvocationLocation()]);
                    ImGui::Text("渲染目标索引：%d", tGroup->getDescriptorIndex());
                    ImGui::Text("渲染目标格式：%d", (uint32_t)instance.cFormat);
                }
            }
            if (instance.GetVertexShaderManager()->isInHuntingMode())
            {
                ImGui::Text("激活的顶点着色器数量：%d。组中的顶点着色器数量：%d。", instance.GetVertexShaderManager()->getAmountShaderHashesCollected(), instance.GetVertexShaderManager()->getMarkedShaderCount());
                ImGui::Text("当前选择的顶点着色器：%d / %d。", instance.GetVertexShaderManager()->getActiveHuntedShaderIndex(), instance.GetVertexShaderManager()->getAmountShaderHashesCollected());
                if (instance.GetVertexShaderManager()->isHuntedShaderMarked())
                {
                    DisplayIsPartOfToggleGroup();
                }
            }
            if (instance.GetPixelShaderManager()->isInHuntingMode())
            {
                ImGui::Text("激活的像素着色器数量：%d。组中的像素着色器数量：%d。", instance.GetPixelShaderManager()->getAmountShaderHashesCollected(), instance.GetPixelShaderManager()->getMarkedShaderCount());
                ImGui::Text("当前选择的像素着色器：%d / %d。", instance.GetPixelShaderManager()->getActiveHuntedShaderIndex(), instance.GetPixelShaderManager()->getAmountShaderHashesCollected());
                if (instance.GetPixelShaderManager()->isHuntedShaderMarked())
                {
                    DisplayIsPartOfToggleGroup();
                }
            }
        }
        ImGui::End();
    }
}

static void CheckHotkeys(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime)
{
    if (*instance.ActiveCollectorFrameCounter() > 0)
    {
        --(*instance.ActiveCollectorFrameCounter());
    }

    if (instance.GetToggleGroupIdShaderEditing() == -1)
    {
        return;
    }

    ShaderToggler::ToggleGroup* editGroup = nullptr;

    for (auto& group : instance.GetToggleGroups())
    {
        if (group.second.getId() == instance.GetToggleGroupIdShaderEditing())
        {
            editGroup = &group.second;
            break;
        }

        if (group.second.getToggleKey() > 0 && ShaderToggler::areKeysPressed(group.second.getToggleKey(), runtime))
        {
            group.second.toggleActive();
            // if the group's shaders are being edited, it should toggle the ones currently marked.
            if (group.second.getId() == instance.GetToggleGroupIdShaderEditing())
            {
                instance.GetVertexShaderManager()->toggleHideMarkedShaders();
                instance.GetPixelShaderManager()->toggleHideMarkedShaders();
            }
        }
    }

    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_DOWN), runtime))
    {
        instance.GetPixelShaderManager()->huntPreviousShader(false);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_UP), runtime))
    {
        instance.GetPixelShaderManager()->huntNextShader(false);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_MARKED_DOWN), runtime))
    {
        instance.GetPixelShaderManager()->huntPreviousShader(true);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_MARKED_UP), runtime))
    {
        instance.GetPixelShaderManager()->huntNextShader(true);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::PIXEL_SHADER_MARK), runtime))
    {
        instance.GetPixelShaderManager()->toggleMarkOnHuntedShader();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_DOWN), runtime))
    {
        instance.GetVertexShaderManager()->huntPreviousShader(false);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_UP), runtime))
    {
        instance.GetVertexShaderManager()->huntNextShader(false);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_MARKED_DOWN), runtime))
    {
        instance.GetVertexShaderManager()->huntPreviousShader(true);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_MARKED_UP), runtime))
    {
        instance.GetVertexShaderManager()->huntNextShader(true);
        instance.UpdateToggleGroupsForShaderHashes();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::VERTEX_SHADER_MARK), runtime))
    {
        instance.GetVertexShaderManager()->toggleMarkOnHuntedShader();
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::INVOCATION_DOWN), runtime))
    {
        if (instance.GetInvocationLocation() > 0)
        {
            instance.GetInvocationLocation()--;
            if (editGroup != nullptr)
            {
                editGroup->setInvocationLocation(instance.GetInvocationLocation());
            }
        }
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::INVOCATION_UP), runtime))
    {
        if (instance.GetInvocationLocation() < 2)
        {
            instance.GetInvocationLocation()++;
            if (editGroup != nullptr)
            {
                editGroup->setInvocationLocation(instance.GetInvocationLocation());
            }
        }
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::DESCRIPTOR_DOWN), runtime))
    {
        if (instance.GetDescriptorIndex() > 0)
        {
            instance.GetDescriptorIndex()--;
            if (editGroup != nullptr)
            {
                editGroup->setDescriptorIndex(instance.GetDescriptorIndex());
            }
        }
    }
    if (ShaderToggler::areKeysPressed(instance.GetKeybinding(AddonImGui::Keybind::DESCRIPTOR_UP), runtime))
    {
        if (instance.GetDescriptorIndex() < MAX_DESCRIPTOR_INDEX)
        {
            instance.GetDescriptorIndex()++;
            if (editGroup != nullptr)
            {
                editGroup->setDescriptorIndex(instance.GetDescriptorIndex());
            }
        }
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
    if (ImGui::CollapsingHeader("一般信息与帮助"))
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
        ImGui::TextUnformatted("\n当遍历着色器时，当前所在着色器在3D场景中被禁用，因此你可以观察它是不是你寻找的那个。");
        ImGui::TextUnformatted("完成标记后，请确认你点击了“保存所有切换分组”以保存你定义的分组，由此下次启动游戏时它们将被自动加载，从而可以立刻使用。");
        ImGui::PopTextWrapPos();
    }

    ImGui::AlignTextToFramePadding();
    if (ImGui::CollapsingHeader("着色器选择参数", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
        ImGui::SliderFloat("覆盖面板不透明度", instance.OverlayOpacity(), 0.0f, 1.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::SliderInt("所需收集的帧数量", instance.StartValueFramecountCollectionPhase(), 10, 1000);
        ImGui::SameLine();
        ShowHelpMarker("这是插件用以收集激活的着色器所用的帧数量。如果你想标记的着色器只是偶尔被使用，调高该值。只有在这些帧当中用到的着色器会被收集。");
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
    }

    if (ImGui::CollapsingHeader("快捷键绑定", ImGuiTreeNodeFlags_None))
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

    if (ImGui::CollapsingHeader("分组列表", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button(" 新 "))
        {
            instance.AddDefaultGroup();
        }
        ImGui::Separator();

        std::vector<ShaderToggler::ToggleGroup> toRemove;
        for (auto& groupKV : instance.GetToggleGroups())
        {
            ShaderToggler::ToggleGroup& group = groupKV.second;

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
                if (ImGui::Button("更改着色器"))
                {
                    ImGui::SameLine();
                    instance.StartShaderEditing(group);
                }
            }

            ImGui::SameLine();
            if (instance.GetToggleGroupIdEffectEditing() >= 0)
            {
                if (instance.GetToggleGroupIdEffectEditing() == group.getId())
                {
                    if (ImGui::Button("  完成  "))
                    {
                        instance.EndEffectEditing();
                    }
                }
                else
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Button("        ");
                    ImGui::EndDisabled();
                }
            }
            else
            {
                if (ImGui::Button("更改效果器"))
                {
                    instance.StartEffectEditing(group);
                    instance.GetInvocationLocation() = group.getInvocationLocation();
                }
            }

            ImGui::SameLine();
            if (instance.GetToggleGroupIdConstantEditing() >= 0)
            {
                if (instance.GetToggleGroupIdConstantEditing() == group.getId())
                {
                    if (ImGui::Button("完成"))
                    {
                        instance.EndConstantEditing();
                    }
                }
                else
                {
                    ImGui::BeginDisabled(true);
                    ImGui::Button("    ");
                    ImGui::EndDisabled();
                }
            }
            else
            {
                if (ImGui::Button("常量"))
                {
                    instance.StartConstantEditing(group);
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
                ImGui::Text("编辑分组 %d", group.getId());

                // Name of group
                char tmpBuffer[150];
                const std::string& name = group.getName();
                strncpy_s(tmpBuffer, 150, name.c_str(), name.size());
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("名字");
                ImGui::SameLine(ImGui::GetWindowWidth() * 0.2f);
                ImGui::InputText("##Name", tmpBuffer, 149);
                group.setName(tmpBuffer);
                ImGui::PopItemWidth();

                // Name of Binding
                bool isBindingEnabled = group.isProvidingTextureBinding();
                const std::string& bindingName = group.getTextureBindingName();
                strncpy_s(tmpBuffer, 150, bindingName.c_str(), bindingName.size());
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("绑定名字");
                ImGui::SameLine(ImGui::GetWindowWidth() * 0.2f);
                if (isBindingEnabled)
                {
                    ImGui::InputText("##BindingName", tmpBuffer, 149);
                }
                else
                {
                    ImGui::BeginDisabled();
                    ImGui::InputText("##BindingName", tmpBuffer, 149);
                    ImGui::EndDisabled();
                }
                ImGui::PopItemWidth();
                
                group.setTextureBindingName(tmpBuffer);

                ImGui::SameLine(ImGui::GetWindowWidth() * 0.905f);
                ImGui::Checkbox("##isBindingEnabled", &isBindingEnabled);
                group.setProvidingTextureBinding(isBindingEnabled);
                
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


                // Misc. Options
                bool retry = group.getRequeueAfterRTMatchingFailure();
                bool matchRes = group.getMatchSwapchainResolution();

                ImGui::AlignTextToFramePadding();
                ImGui::Checkbox("Retry RT assignment", &retry);
                group.setRequeueAfterRTMatchingFailure(retry);
                ImGui::SameLine();
                ImGui::Checkbox("Match swapchain resolution", &matchRes);
                group.setMatchSwapchainResolution(matchRes);

                if (ImGui::Button("OK"))
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
