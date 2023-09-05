#include <algorithm>
#include "PipelineStateTracker.h"

using namespace StateTracker;
using namespace std;

PipelineStateTracker::PipelineStateTracker()
{
    Reset();
}

PipelineStateTracker::~PipelineStateTracker()
{
}

void PipelineStateTracker::ApplyBoundDescriptorSets(command_list* cmd_list, shader_stage stage, pipeline_layout layout,
    const vector<descriptor_table>& descriptors, const vector<bool>& mask)
{
    size_t count = std::min(descriptors.size(), mask.size());
    for (uint32_t i = 0; i < count; i++)
    {
        if (descriptors[i] == 0 || (i < mask.size() && mask[i]))
            continue;

        for (uint32_t j = i + 1; j < count + 1; j++)
        {
            if (j == count || descriptors[j] == 0 || (j < mask.size() && mask[j]))
            {
                cmd_list->bind_descriptor_tables(stage, layout, i, j - i, &descriptors.data()[i]);
                i = j;

                break;
            }
        }
    }
}

void PipelineStateTracker::ReApplyState(command_list* cmd_list, const unordered_map<uint64_t, vector<bool>>& transient_mask)
{
    vector<PipelineBindingBase*> states = {
        &_descriptorSetsState,
        &_renderTargetState,
        &_scissorRectsState,
        &_viewportsState,
        &_pipelineStatesState.states[0],
        &_pipelineStatesState.states[1],
        &_pipelineState
    };

    if (states.size() > 1)
    {
        std::sort(states.begin(), states.end(), [](const auto& lhs, const auto& rhs)
            {
                return lhs->callIndex < rhs->callIndex;
            });
    }

    for (auto state : states)
    {
        switch (state->GetType())
        {
            case PipelineBindingTypes::bind_descriptor_sets:
            {
                if (_descriptorSetsState.cmd_list != nullptr)
                {
                    vector<bool> emptyMask;
                    const vector<bool>* mask_graphics = &emptyMask;
                    const vector<bool>* mask_compute = &emptyMask;

                    if (transient_mask.contains(_descriptorSetsState.current_layout[0].handle))
                    {
                        mask_graphics = &transient_mask.at(_descriptorSetsState.current_layout[0].handle);
                    }

                    if (transient_mask.contains(_descriptorSetsState.current_layout[1].handle))
                    {
                        mask_compute = &transient_mask.at(_descriptorSetsState.current_layout[1].handle);
                    }

                    ApplyBoundDescriptorSets(cmd_list, shader_stage::all_graphics, _descriptorSetsState.current_layout[0],
                        _descriptorSetsState.current_sets[0], *mask_graphics);
                    ApplyBoundDescriptorSets(cmd_list, shader_stage::all_compute, _descriptorSetsState.current_layout[1],
                        _descriptorSetsState.current_sets[1], *mask_compute);
                }
                break;
            }
            case PipelineBindingTypes::bind_render_target:
            {
                if (_renderTargetState.cmd_list != nullptr)
                    cmd_list->bind_render_targets_and_depth_stencil(_renderTargetState.count, _renderTargetState.rtvs.data(), _renderTargetState.dsv);
                break;
            }
            case PipelineBindingTypes::bind_scissor_rect:
            {
                if (_scissorRectsState.cmd_list != nullptr)
                    cmd_list->bind_scissor_rects(_scissorRectsState.first, _scissorRectsState.count, _scissorRectsState.rects.data());
                break;
            }
            case PipelineBindingTypes::bind_viewport:
            {
                if (_viewportsState.cmd_list != nullptr)
                    cmd_list->bind_viewports(_viewportsState.first, _viewportsState.count, _viewportsState.viewports.data());
                break;
            }
            case PipelineBindingTypes::bind_pipeline_states:
            {
                BindPipelineStatesState* ss = static_cast<BindPipelineStatesState*>(state);
                if (ss->cmd_list != nullptr)
                    cmd_list->bind_pipeline_states(1, &ss->state, &ss->value);
                break;
            }
            case PipelineBindingTypes::bind_pipeline:
            {
                if (_pipelineState.cmd_list != nullptr)
                    cmd_list->bind_pipeline(_pipelineState.stages, _pipelineState.pipeline);
                break;
            }
        }
    }
}

void PipelineStateTracker::OnBindRenderTargetsAndDepthStencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv)
{
    _renderPassState.cmd_list = nullptr;
    _renderTargetState.callIndex = _callIndex;
    _callIndex++;

    _renderTargetState.cmd_list = cmd_list;
    _renderTargetState.count = count;
    _renderTargetState.dsv = dsv;
    _renderTargetState.rtvs.assign(rtvs, rtvs + count);
}

void PipelineStateTracker::OnBeginRenderPass(command_list* cmd_list, uint32_t count, const render_pass_render_target_desc* rts, const render_pass_depth_stencil_desc* ds)
{
    _renderTargetState.cmd_list = nullptr;

    _renderPassState.callIndex = _callIndex;
    _callIndex++;

    _renderPassState.cmd_list = cmd_list;
    _renderPassState.count = count;

    _renderPassState.rtvs.clear();

    if (ds != nullptr)
        _renderPassState.dsv = *ds;
    else
        _renderPassState.dsv = { 0 };

    if (_renderPassState.rtvs.size() != count) {
        _renderPassState.rtvs.resize(count);
    }

    for (int i = 0; i < count; i++)
    {
        _renderPassState.rtvs[i] = rts[i];
    }
}

void PipelineStateTracker::OnBindDescriptorSets(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table* sets)
{
    if (cmd_list->get_device()->get_api() != device_api::d3d12)
        return;

    const int type_index = (stages == shader_stage::all_compute) ? 1 : 0;

    _descriptorSetsState.callIndex = _callIndex;
    _callIndex++;

    _descriptorSetsState.cmd_list = cmd_list;

    _descriptorSetsState.current_layout[type_index] = layout;

    if (_descriptorSetsState.current_sets[type_index].size() < (count + first))
    {
        _descriptorSetsState.current_sets[type_index].resize(count + first);
    }

    for (size_t i = 0; i < count; ++i)
    {
        _descriptorSetsState.current_sets[type_index][i + first] = sets[i];
    }
}

void PipelineStateTracker::OnPushConstants(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, uint32_t first, uint32_t count, const void* values)
{
    const int stage_index = (static_cast<uint32_t>(stages) & static_cast<uint32_t>(shader_stage::pixel)) ? 0 : 1;

    _pushConstantsState.callIndex = _callIndex;
    _callIndex++;

    _pushConstantsState.cmd_list = cmd_list;

    _pushConstantsState.current_layout[stage_index] = layout;

    if (_pushConstantsState.current_constants[stage_index].size() < layout_param + 1)
    {
        _pushConstantsState.current_constants[stage_index].resize(layout_param + 1);
    }

    if (_pushConstantsState.current_constants[stage_index][layout_param].size() < first + count)
    {
        _pushConstantsState.current_constants[stage_index][layout_param].resize(first + count);
    }

    for (uint32_t i = 0; i < count; i++)
    {
        _pushConstantsState.current_constants[stage_index][layout_param][first + i] = reinterpret_cast<const uint32_t*>(values)[i];
    }
}

void PipelineStateTracker::OnPushDescriptors(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, const descriptor_table_update& update)
{
    // only consider pixel and vertex shader CBs for now
    if ((update.type != descriptor_type::constant_buffer && update.type != descriptor_type::shader_resource_view) ||
        !(static_cast<uint32_t>(stages) & (static_cast<uint32_t>(shader_stage::pixel) | static_cast<uint32_t>(shader_stage::vertex))))
    {
        return;
    }

    const int stage_index = (static_cast<uint32_t>(stages) & static_cast<uint32_t>(shader_stage::pixel)) ? 0 : 1;

    _pushDescriptorsState.callIndex = _callIndex;
    _callIndex++;

    _pushDescriptorsState.cmd_list = cmd_list;

    _pushDescriptorsState.current_layout[stage_index] = layout;

    if (update.type == descriptor_type::constant_buffer)
    {
        if (_pushDescriptorsState.current_descriptors[stage_index].size() < layout_param + 1)
        {
            _pushDescriptorsState.current_descriptors[stage_index].resize(layout_param + 1);
        }

        if (_pushDescriptorsState.current_descriptors[stage_index][layout_param].size() < update.binding + update.count)
        {
            _pushDescriptorsState.current_descriptors[stage_index][layout_param].resize(update.binding + update.count);
        }

        const buffer_range* buffer = static_cast<const reshade::api::buffer_range*>(update.descriptors);

        for (uint32_t i = 0; i < update.count; i++)
        {
            _pushDescriptorsState.current_descriptors[stage_index][layout_param][update.binding + i] = buffer[i];
        }
    }
    else if (update.type == descriptor_type::shader_resource_view)
    {
        if (_pushDescriptorsState.current_srv[stage_index].size() < layout_param + 1)
        {
            _pushDescriptorsState.current_srv[stage_index].resize(layout_param + 1);
        }

        if (_pushDescriptorsState.current_srv[stage_index][layout_param].size() < update.binding + update.count)
        {
            _pushDescriptorsState.current_srv[stage_index][layout_param].resize(update.binding + update.count);
        }

        const resource_view* buffer = static_cast<const reshade::api::resource_view*>(update.descriptors);

        for (uint32_t i = 0; i < update.count; i++)
        {
            _pushDescriptorsState.current_srv[stage_index][layout_param][update.binding + i] = buffer[i];
        }
    }
}

void PipelineStateTracker::OnBindViewports(command_list* cmd_list, uint32_t first, uint32_t count, const viewport* viewports)
{
    if (cmd_list->get_device()->get_api() != device_api::d3d12 && cmd_list->get_device()->get_api() != device_api::vulkan)
        return;

    _viewportsState.callIndex = _callIndex;
    _callIndex++;

    _viewportsState.cmd_list = cmd_list;
    _viewportsState.first = first;
    _viewportsState.count = count;

    if (_viewportsState.viewports.size() != count)
        _viewportsState.viewports.resize(count);

    for (uint32_t i = first; i < count; i++)
    {
        _viewportsState.viewports[i] = viewports[i];
    }
}

void PipelineStateTracker::OnBindScissorRects(command_list* cmd_list, uint32_t first, uint32_t count, const rect* rects)
{
    if (cmd_list->get_device()->get_api() != device_api::d3d12 && cmd_list->get_device()->get_api() != device_api::vulkan)
        return;

    _scissorRectsState.callIndex = _callIndex;
    _callIndex++;

    _scissorRectsState.cmd_list = cmd_list;
    _scissorRectsState.first = first;
    _scissorRectsState.count = count;

    if (_scissorRectsState.rects.size() != count)
        _scissorRectsState.rects.resize(count);

    for (uint32_t i = first; i < count; i++)
    {
        _scissorRectsState.rects[i] = rects[i];
    }
}

void PipelineStateTracker::OnBindPipelineStates(command_list* cmd_list, uint32_t count, const dynamic_state* states, const uint32_t* values)
{
    if (cmd_list->get_device()->get_api() != device_api::d3d12)
        return;

    for (uint32_t i = 0; i < count; i++)
    {
        if (states[i] == dynamic_state::primitive_topology)
        {
            _pipelineStatesState.states[1].cmd_list = cmd_list;
            _pipelineStatesState.states[1].callIndex = _callIndex;
            _pipelineStatesState.states[1].value = values[i];
            _pipelineStatesState.states[1].valuesSet = true;
            _callIndex++;
        }
        else if (states[i] == dynamic_state::blend_constant)
        {
            _pipelineStatesState.states[0].cmd_list = cmd_list;
            _pipelineStatesState.states[0].callIndex = _callIndex;
            _pipelineStatesState.states[0].value = values[i];
            _pipelineStatesState.states[0].valuesSet = true;
            _callIndex++;
        }
    }
}

void PipelineStateTracker::OnBindPipeline(command_list* cmd_list, pipeline_stage stages, pipeline pipelineHandle)
{
    if (cmd_list->get_device()->get_api() != device_api::d3d12 && cmd_list->get_device()->get_api() != device_api::vulkan)
        return;

    _pipelineState.callIndex = _callIndex;
    _callIndex++;
    _pipelineState.pipeline = pipelineHandle;
    _pipelineState.stages = stages;
    _pipelineState.cmd_list = cmd_list;
}

bool PipelineStateTracker::IsInRenderPass() const
{
    return _renderPassState.cmd_list != nullptr;
}

void PipelineStateTracker::ClearPushDescriptorState(pipeline_stage stage = pipeline_stage::all)
{
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(shader_stage::pixel))
    {
        _pushDescriptorsState.current_descriptors[0].clear();
    }

    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(shader_stage::vertex))
    {
        _pushDescriptorsState.current_descriptors[1].clear();
    }
}

const vector<resource_view>& PipelineStateTracker::GetBoundRenderTargetViews() const
{
    return _renderTargetState.rtvs;
}

void PipelineStateTracker::Reset()
{
    _callIndex = 0;
    _renderTargetState.Reset();
    _descriptorSetsState.Reset();
    _pushDescriptorsState.Reset();
    _pushConstantsState.Reset();
    _viewportsState.Reset();
    _scissorRectsState.Reset();
    _pipelineStatesState.Reset();
    _renderPassState.Reset();
    _pipelineState.Reset();
}