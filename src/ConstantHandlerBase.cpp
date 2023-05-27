#include <cstring>
#include "ConstantHandlerBase.h"
#include "PipelinePrivateData.h"

using namespace Shim::Constants;
using namespace std;
using namespace reshade::api;
using namespace ShaderToggler;

unordered_map<string, tuple<constant_type, vector<effect_uniform_variable>>> ConstantHandlerBase::restVariables;
char ConstantHandlerBase::charBuffer[CHAR_BUFFER_SIZE];
ConstantCopyBase* ConstantHandlerBase::_constCopy;

ConstantHandlerBase::ConstantHandlerBase()
{
}

ConstantHandlerBase::~ConstantHandlerBase()
{
}


void ConstantHandlerBase::SetConstantCopy(ConstantCopyBase* constantCopy)
{
    _constCopy = constantCopy;
}

size_t ConstantHandlerBase::GetConstantBufferSize(const ToggleGroup* group)
{
    if (groupBufferSize.contains(group))
    {
        return groupBufferSize.at(group);
    }

    return 0;
}

uint8_t* ConstantHandlerBase::GetConstantBuffer(const ToggleGroup* group)
{
    if (groupBufferContent.contains(group))
    {
        return groupBufferContent.at(group).data();
    }

    return nullptr;
}

unordered_map<string, tuple<constant_type, vector<effect_uniform_variable>>>* ConstantHandlerBase::GetRESTVariables()
{
    return &restVariables;
}

void ConstantHandlerBase::ReloadConstantVariables(effect_runtime* runtime)
{
    restVariables.clear();

    runtime->enumerate_uniform_variables(nullptr, [](effect_runtime* rt, effect_uniform_variable variable) {
        if (!rt->get_annotation_string_from_uniform_variable<CHAR_BUFFER_SIZE>(variable, "source", charBuffer))
        {
            return;
        }

        reshade::api::format format;
        uint32_t rows;
        uint32_t columns;
        uint32_t array_length;

        rt->get_uniform_variable_type(variable, &format, &rows, &columns, &array_length);
        constant_type type = constant_type::type_unknown;
        switch (format)
        {
        case reshade::api::format::r32_float:
            if (array_length > 0)
                type = constant_type::type_unknown;
            else
            {
                if (rows == 4 && columns == 4)
                    type = constant_type::type_float4x4;
                else if (rows == 3 && columns == 4)
                    type = constant_type::type_float4x3;
                else if (rows == 3 && columns == 3)
                    type = constant_type::type_float3x3;
                else if (rows == 3 && columns == 1)
                    type = constant_type::type_float3;
                else if (rows == 2 && columns == 1)
                    type = constant_type::type_float2;
                else if (rows == 1 && columns == 1)
                    type = constant_type::type_float;
                else
                    type = constant_type::type_unknown;
            }
            break;
        case reshade::api::format::r32_sint:
            if (array_length > 0 || rows > 1 || columns > 1)
                type = constant_type::type_unknown;
            else
                type = constant_type::type_int;
            break;
        case reshade::api::format::r32_uint:
            if (array_length > 0 || rows > 1 || columns > 1)
                type = constant_type::type_unknown;
            else
                type = constant_type::type_uint;
            break;
        }

        if (type == constant_type::type_unknown)
        {
            return;
        }

        string id(charBuffer);
        const auto& vars = restVariables.find(id);
        
        if (vars == restVariables.end())
        {
            restVariables.emplace(id, make_tuple(type, vector<effect_uniform_variable>{variable}));
        }
        else if(get<0>(vars->second) == type)
        {
            get<1>(vars->second).push_back(variable);
        }
        });
}

void ConstantHandlerBase::ClearConstantVariables()
{
    restVariables.clear();
}

void ConstantHandlerBase::OnReshadeSetTechniqueState(effect_runtime* runtime, int32_t enabledCount)
{
    previousEnableCount = enabledCount;
}

void ConstantHandlerBase::OnReshadeReloadedEffects(effect_runtime* runtime, int32_t enabledCount)
{
    std::unique_lock<shared_mutex> lock(varMutex);

    if (enabledCount == 0 || enabledCount - previousEnableCount < 0)
    {
        ClearConstantVariables();
    }
    else
    {
        ReloadConstantVariables(runtime);
    }

    previousEnableCount = enabledCount;
}

bool ConstantHandlerBase::UpdateConstantEntries(command_list* cmd_list, CommandListDataContainer& cmdData, DeviceDataContainer& devData, const ToggleGroup* group, uint32_t index)
{
    uint32_t slot_size = static_cast<uint32_t>(cmdData.stateTracker.GetPushDescriptorState()->current_descriptors[index].size());
    uint32_t slot = min(group->getSlotIndex(), slot_size - 1);

    if (slot_size == 0)
        return false;

    uint32_t desc_size = static_cast<uint32_t>(cmdData.stateTracker.GetPushDescriptorState()->current_descriptors[index][slot].size());
    uint32_t desc = min(group->getDescriptorIndex(), desc_size - 1);

    if (desc_size == 0)
        return false;

    buffer_range buf = cmdData.stateTracker.GetPushDescriptorState()->current_descriptors[index][slot][desc];

    if (buf.buffer != 0)
    {
        SetBufferRange(group, buf, cmd_list->get_device(), cmd_list);
        ApplyConstantValues(devData.current_runtime, group, restVariables);
        devData.constantsUpdated.insert(group);

        return true;
    }

    return false;
}

void ConstantHandlerBase::UpdateConstants(command_list* cmd_list)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    if (deviceData.current_runtime == nullptr ||
        (commandListData.ps.constantBuffersToUpdate.size() == 0 && commandListData.vs.constantBuffersToUpdate.size() == 0)) {
        return;
    }

    vector<const ToggleGroup*> psRemovalList;
    vector<const ToggleGroup*> vsRemovalList;

    for (const auto& cb : commandListData.ps.constantBuffersToUpdate)
    {
        if (!deviceData.constantsUpdated.contains(cb))
        {
            if (UpdateConstantEntries(cmd_list, commandListData, deviceData, cb, 0))
            {
                psRemovalList.push_back(cb);
            }
        }
    }

    for (const auto& cb : commandListData.vs.constantBuffersToUpdate)
    {
        if (!deviceData.constantsUpdated.contains(cb))
        {
            if (UpdateConstantEntries(cmd_list, commandListData, deviceData, cb, 1))
            {
                vsRemovalList.push_back(cb);
            }
        }
    }

    for (const auto& g : psRemovalList)
    {
        commandListData.ps.constantBuffersToUpdate.erase(g);
    }

    for (const auto& g : vsRemovalList)
    {
        commandListData.vs.constantBuffersToUpdate.erase(g);
    }
}

void ConstantHandlerBase::ApplyConstantValues(effect_runtime* runtime, const ToggleGroup* group,
    const unordered_map<string, tuple<constant_type, vector<effect_uniform_variable>>>& constants)
{
    std::unique_lock<shared_mutex> lock(varMutex);

    if (!groupBufferContent.contains(group) || runtime == nullptr)
    {
        return;
    }

    const uint8_t* buffer = groupBufferContent.at(group).data();
    const uint8_t* prevBuffer = groupPrevBufferContent.at(group).data();

    for (const auto& vars : group->GetVarOffsetMapping())
    {
        const string& var = get<0>(vars);
        uintptr_t offset = get<0>(get<1>(vars));
        bool prevValue = get<1>(get<1>(vars));

        const uint8_t* bufferInUse = prevValue ? prevBuffer : buffer;

        if (!constants.contains(var))
        {
            continue;
        }

        constant_type type = std::get<0>(constants.at(var));
        uint32_t typeIndex = static_cast<uint32_t>(type);
        size_t bufferSize = groupBufferSize.at(group);

        if (offset + type_size[typeIndex] * type_length[typeIndex] >= bufferSize)
        {
            continue;
        }

        const vector<effect_uniform_variable>& effect_variables = std::get<1>(constants.at(var));

        for (const auto& effect_var : effect_variables)
        {
            if (type <= constant_type::type_float4x4)
            {
                runtime->set_uniform_value_float(effect_var, reinterpret_cast<const float*>(bufferInUse + offset), type_length[typeIndex], 0);
            }
            else if (type == constant_type::type_int)
            {
                runtime->set_uniform_value_int(effect_var, reinterpret_cast<const int32_t*>(bufferInUse + offset), type_length[typeIndex], 0);
            }
            else
            {
                runtime->set_uniform_value_uint(effect_var, reinterpret_cast<const uint32_t*>(bufferInUse + offset), type_length[typeIndex], 0);
            }
        }
    }
}

void ConstantHandlerBase::SetBufferRange(const ToggleGroup* group, buffer_range range, device* dev, command_list* cmd_list)
{
    if (dev == nullptr || cmd_list == nullptr || range.buffer == 0)
    {
        return;
    }

    if (!groupBufferContent.contains(group))
    {
        groupBufferRanges.emplace(group, range);
        groupBufferSize.emplace(group, 0);
    }
    else
    {
        groupBufferRanges[group] = range;
    }

    CopyToScratchpad(group, dev, cmd_list);
}

void ConstantHandlerBase::CopyToScratchpad(const ToggleGroup* group, device* dev, command_list* cmd_list)
{
    buffer_range currentBufferRange = groupBufferRanges.at(group);
    resource_desc targetBufferDesc = dev->get_resource_desc(currentBufferRange.buffer);

    CreateScratchpad(group, dev, targetBufferDesc);

    vector<uint8_t>& bufferContent = groupBufferContent.at(group);
    vector<uint8_t>& prevBufferContent = groupPrevBufferContent.at(group);

    uint64_t size = targetBufferDesc.buffer.size;

    std::memcpy(prevBufferContent.data(), bufferContent.data(), size);
    _constCopy->GetHostConstantBuffer(bufferContent, size, currentBufferRange.buffer.handle);
}

bool ConstantHandlerBase::CreateScratchpad(const ToggleGroup* group, device* dev, resource_desc& targetBufferDesc)
{
    groupBufferSize.at(group) = targetBufferDesc.buffer.size;

    if (groupBufferContent.contains(group))
    {
        groupBufferContent.at(group).resize(targetBufferDesc.buffer.size, 0);
        groupPrevBufferContent.at(group).resize(targetBufferDesc.buffer.size, 0);
    }
    else
    {
        groupBufferContent.emplace(group, vector<uint8_t>(targetBufferDesc.buffer.size, 0));
        groupPrevBufferContent.emplace(group, vector<uint8_t>(targetBufferDesc.buffer.size, 0));
    }

    return true;
}

void ConstantHandlerBase::RemoveGroup(const ToggleGroup* group, device* dev)
{
    if (!groupBufferContent.contains(group))
    {
        return;
    }

    groupBufferRanges.erase(group);
    groupBufferContent.erase(group);
    groupPrevBufferContent.erase(group);
}