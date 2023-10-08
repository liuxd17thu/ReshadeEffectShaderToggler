/*
 * Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "reshade.hpp"
#include "StateTracking.h"

using namespace reshade::api;
using namespace StateTracking;

bool state_tracking::track_descriptors = true;

void state_block::apply_descriptors_dx12_vulkan(command_list* cmd_list) const
{
    uint32_t shader_stages_set = 0;
    for (uint32_t stageIdx = 0; stageIdx < ALL_SHADER_STAGES_SIZE; stageIdx++)
    {
        auto& descriptor_data = cmd_list->get_device()->get_private_data<descriptor_tracking>();
        const auto& [pipelinelayout, descriptor_set] = descriptor_tables[stageIdx];
        shader_stage stages = descriptor_tables_stages[stageIdx];

        if ((static_cast<uint32_t>(stages) | shader_stages_set) <= shader_stages_set)
        {
            continue;
        }

        shader_stages_set |= static_cast<uint32_t>(stages);
        uint32_t start_index = 0;
        uint32_t end_index = 0;

        // Restore descriptor tables
        while (end_index < descriptor_set.size() && start_index < descriptor_set.size())
        {
            while (start_index < descriptor_set.size() && (descriptor_data.get_pipeline_layout_param(pipelinelayout, start_index).type != pipeline_layout_param_type::descriptor_table || descriptor_set[start_index].handle == 0))
            {
                start_index++;
            }

            end_index = start_index;

            while (end_index < descriptor_set.size() && descriptor_data.get_pipeline_layout_param(pipelinelayout, end_index).type == pipeline_layout_param_type::descriptor_table && descriptor_set[end_index].handle != 0)
            {
                end_index++;
            }

            cmd_list->bind_descriptor_tables(stages, pipelinelayout, start_index, end_index - start_index, &descriptor_set.data()[start_index]);

            start_index = end_index;
        }

        // Restore root signature
        if (descriptor_set.size() == 0 && pipelinelayout != 0)
        {
            cmd_list->bind_descriptor_tables(stages, pipelinelayout, 0, 0, nullptr);
        }

        // Restore push descriptors
        const auto& pushes = push_descriptors[stageIdx].second;

        for (uint32_t i = 0; i < pushes.size(); i++)
        {
            if (descriptor_data.get_pipeline_layout_param(pipelinelayout, i).type != pipeline_layout_param_type::push_descriptors || pushes[i].size() == 0)
            {
                continue;
            }
            switch (pushes[i][0].type)
            {
            case descriptor_type::sampler:
                cmd_list->push_descriptors(stages, pipelinelayout, i, descriptor_table_update{ {}, 0, 0, 1, pushes[i][0].type, &pushes[i][0].sampler });
                break;
            case descriptor_type::sampler_with_resource_view:
                cmd_list->push_descriptors(stages, pipelinelayout, i, descriptor_table_update{ {}, 0, 0, 1, pushes[i][0].type, &pushes[i][0].sampler_and_view });
                break;
            case descriptor_type::shader_resource_view:
            case descriptor_type::unordered_access_view:
                cmd_list->push_descriptors(stages, pipelinelayout, i, descriptor_table_update{ {}, 0, 0, 1, pushes[i][0].type, &pushes[i][0].view });
                break;
            case descriptor_type::constant_buffer:
            case descriptor_type::shader_storage_buffer:
                cmd_list->push_descriptors(stages, pipelinelayout, i, descriptor_table_update{ {}, 0, 0, 1, pushes[i][0].type, &pushes[i][0].constant });
            }
        }

        // Restore push constants
        const auto& constants = push_constants[stageIdx].second;

        for (uint32_t i = 0; i < constants.size(); i++)
        {
            if (descriptor_data.get_pipeline_layout_param(pipelinelayout, i).type != pipeline_layout_param_type::push_descriptors || constants[i].size() == 0)
            {
                continue;
            }

            cmd_list->push_constants(stages, pipelinelayout, i, 0, static_cast<uint32_t>(constants[i].size()), constants[i].data());
        }
    }
}

void state_block::apply_descriptors(command_list* cmd_list) const
{
    // Restores descriptors potentially overwritten by our preview copy pipeline
    auto& [desc_layout, descriptors] = cmd_list->get_private_data<state_tracking>().push_descriptors[0];

    std::array<descriptor_tracking::descriptor_data*, 2> copyDescriptors;

    if (descriptors.size() > 0 && descriptors[0].size() > 0)
        copyDescriptors[0] = &descriptors[0][0];
    else
        copyDescriptors[0] = nullptr;

    if (descriptors.size() > 1 && descriptors[1].size() > 0)
        copyDescriptors[1] = &descriptors[1][0];
    else
        copyDescriptors[1] = nullptr;

    for (uint32_t i = 0; i < copyDescriptors.size(); ++i)
    {
        if (copyDescriptors[i] == nullptr)
            continue;

        switch (copyDescriptors[i]->type)
        {
        case descriptor_type::sampler:
            cmd_list->push_descriptors(shader_stage::pixel, desc_layout, i, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::sampler, &copyDescriptors[i]->sampler });
            break;
        case descriptor_type::constant_buffer:
            cmd_list->push_descriptors(shader_stage::pixel, desc_layout, i, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::constant_buffer, &copyDescriptors[i]->constant });
            break;
        case descriptor_type::sampler_with_resource_view:
            cmd_list->push_descriptors(shader_stage::pixel, desc_layout, i, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::sampler_with_resource_view, &copyDescriptors[i]->sampler_and_view });
            break;
        case descriptor_type::shader_resource_view:
            cmd_list->push_descriptors(shader_stage::pixel, desc_layout, i, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::shader_resource_view, &copyDescriptors[i]->view });
            break;
        case descriptor_type::unordered_access_view:
            cmd_list->push_descriptors(shader_stage::pixel, desc_layout, i, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::unordered_access_view, &copyDescriptors[i]->view });
            break;
        }
    }
}

void state_block::capture(command_list* cmd_list)
{
    if (cmd_list->get_device()->get_api() == device_api::d3d9 && dx_state == nullptr)
    {
        IDirect3DDevice9* device = reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native());

        if (SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &dx_state)))
        {
            dx_state->Capture();
        }
    }
}

void state_block::apply(command_list* cmd_list)
{
    switch (cmd_list->get_device()->get_api())
    {
    case device_api::d3d9:
        apply_dx9(cmd_list);
        break;
    default:
        apply_default(cmd_list);
    }
}

void state_block::apply_dx9(reshade::api::command_list* cmd_list)
{
    // ???
    if (!render_targets.empty() || depth_stencil != 0)
        cmd_list->bind_render_targets_and_depth_stencil(static_cast<uint32_t>(render_targets.size()), render_targets.data(), depth_stencil);

    if (dx_state != nullptr)
    {
        dx_state->Apply();
        dx_state->Release();
        dx_state = nullptr;
    }
}
void state_block::apply_dx12_vulkan(reshade::api::command_list* cmd_list) const
{
}

void state_block::apply_default(reshade::api::command_list* cmd_list) const
{
    if (!render_targets.empty() || depth_stencil != 0)
        cmd_list->bind_render_targets_and_depth_stencil(static_cast<uint32_t>(render_targets.size()), render_targets.data(), depth_stencil);

    uint32_t pipeline_stages_set = 0;
    for (uint32_t s = 0; s < ALL_PIPELINE_STAGES_SIZE; s++)
    {
        if ((static_cast<uint32_t>(current_pipeline_stage[s]) | pipeline_stages_set) > pipeline_stages_set)
        {
            pipeline_stages_set |= static_cast<uint32_t>(current_pipeline_stage[s]);
            cmd_list->bind_pipeline(current_pipeline_stage[s], current_pipeline[s]);
        }
    }

    if (primitive_topology != primitive_topology::undefined)
        cmd_list->bind_pipeline_state(dynamic_state::primitive_topology, static_cast<uint32_t>(primitive_topology));
    if (blend_constant != 0)
        cmd_list->bind_pipeline_state(dynamic_state::blend_constant, blend_constant);
    if (sample_mask != 0xFFFFFFFF)
        cmd_list->bind_pipeline_state(dynamic_state::sample_mask, sample_mask);
    if (srgb_write_enable != 0)
        cmd_list->bind_pipeline_state(dynamic_state::srgb_write_enable, srgb_write_enable);
    if (front_stencil_reference_value != 0)
        cmd_list->bind_pipeline_state(dynamic_state::front_stencil_reference_value, front_stencil_reference_value);
    if (back_stencil_reference_value != 0)
        cmd_list->bind_pipeline_state(dynamic_state::back_stencil_reference_value, back_stencil_reference_value);

    if (!viewports.empty())
        cmd_list->bind_viewports(0, static_cast<uint32_t>(viewports.size()), viewports.data());
    if (!scissor_rects.empty())
        cmd_list->bind_scissor_rects(0, static_cast<uint32_t>(scissor_rects.size()), scissor_rects.data());

    if (cmd_list->get_device()->get_api() == device_api::d3d12 || cmd_list->get_device()->get_api() == device_api::vulkan)
    {
        apply_descriptors_dx12_vulkan(cmd_list);
    }
    else
    {
        apply_descriptors(cmd_list);
    }
}

void state_block::clear()
{
    render_targets.clear();
    depth_stencil = { 0 };
    primitive_topology = primitive_topology::undefined;
    blend_constant = 0;
    front_stencil_reference_value = 0;
    back_stencil_reference_value = 0;
    srgb_write_enable = 0;
    sample_mask = 0xFFFFFFFF;
    viewports.clear();
    scissor_rects.clear();
    descriptor_tables.fill(make_pair(pipeline_layout{ 0 }, std::vector<descriptor_table>()));
    descriptor_tables_stages.fill(static_cast<shader_stage>(0));
    push_constants.fill(make_pair(pipeline_layout{ 0 }, std::vector<std::vector<uint32_t>>()));
    push_descriptors.fill(make_pair(pipeline_layout{ 0 }, std::vector<std::vector<descriptor_tracking::descriptor_data>>()));
    current_pipeline.fill(pipeline{ 0 });
    current_pipeline_stage.fill(static_cast<pipeline_stage>(0));
    resource_barrier_track.clear();
}

static inline int32_t get_shader_stage_index(shader_stage stages)
{
    const uint32_t stage_value = static_cast<uint32_t>(stages);

    for (int32_t index = 0; index < ALL_SHADER_STAGES_SIZE; ++index)
    {
        if (stage_value & static_cast<uint32_t>(ALL_SHADER_STAGES[index]))
        {
            return index;
        }
    }

    return -1;
}

static inline int32_t get_pipeline_stage_index(pipeline_stage stages)
{
    const uint32_t stage_value = static_cast<uint32_t>(stages);

    for (int32_t index = 0; index < ALL_PIPELINE_STAGES_SIZE; ++index)
    {
        if (stage_value & static_cast<uint32_t>(ALL_PIPELINE_STAGES[index]))
        {
            return index;
        }
    }

    return -1;
}

static void on_init_command_list(command_list* cmd_list)
{
    cmd_list->create_private_data<state_tracking>();
}
static void on_destroy_command_list(command_list* cmd_list)
{
    cmd_list->destroy_private_data<state_tracking>();
}

static void on_bind_render_targets_and_depth_stencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv)
{
    auto& state = cmd_list->get_private_data<state_tracking>();
    state.render_targets.assign(rtvs, rtvs + count);
    state.depth_stencil = dsv;
}

static void on_bind_pipeline(command_list* cmd_list, pipeline_stage stages, pipeline pipeline)
{
    auto& state = cmd_list->get_private_data<state_tracking>();

    uint32_t idx = get_pipeline_stage_index(stages);

    state.current_pipeline[idx] = pipeline;
    state.current_pipeline_stage[idx] = stages;
}

static void on_bind_pipeline_states(command_list* cmd_list, uint32_t count, const dynamic_state* states, const uint32_t* values)
{
    auto& state = cmd_list->get_private_data<state_tracking>();

    for (uint32_t i = 0; i < count; ++i)
    {
        switch (states[i])
        {
        case dynamic_state::primitive_topology:
            state.primitive_topology = static_cast<primitive_topology>(values[i]);
            break;
        case dynamic_state::blend_constant:
            state.blend_constant = values[i];
            break;
        case dynamic_state::front_stencil_reference_value:
            state.front_stencil_reference_value = values[i];
            break;
        case dynamic_state::back_stencil_reference_value:
            state.back_stencil_reference_value = values[i];
            break;
        case dynamic_state::sample_mask:
            state.sample_mask = values[i];
            break;
        case dynamic_state::srgb_write_enable:
            state.srgb_write_enable = values[i];
            break;
        }
    }
}

static void on_bind_viewports(command_list* cmd_list, uint32_t first, uint32_t count, const viewport* viewports)
{
    auto& state = cmd_list->get_private_data<state_tracking>();

    if (state.viewports.size() < (first + count))
        state.viewports.resize(first + count);

    for (uint32_t i = 0; i < count; ++i)
        state.viewports[i + first] = viewports[i];
}

static void on_bind_scissor_rects(command_list* cmd_list, uint32_t first, uint32_t count, const rect* rects)
{
    auto& state = cmd_list->get_private_data<state_tracking>();

    if (state.scissor_rects.size() < (first + count))
        state.scissor_rects.resize(first + count);

    for (uint32_t i = 0; i < count; ++i)
        state.scissor_rects[i + first] = rects[i];
}

static void on_bind_descriptor_tables(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table* tables)
{
    int32_t idx = get_shader_stage_index(stages);

    if (idx < 0)
        return;

    auto& state_tracker = cmd_list->get_private_data<state_tracking>();
    auto& state = state_tracker.descriptor_tables[idx];
    auto& state_stages = state_tracker.descriptor_tables_stages[idx];
    auto& [desc_layout, push_descriptors] = state_tracker.push_descriptors[idx];
    auto& [_, push_constants] = state_tracker.push_constants[idx];
    const auto& descriptor_state = cmd_list->get_device()->get_private_data<descriptor_tracking>();

    if (layout != state.first)
    {
        state.second.clear(); // Layout changed, which resets all descriptor set bindings
        push_constants.clear();
        //clear_descriptors(descriptors);
        push_descriptors.clear();
    }

    state.first = layout;
    desc_layout = layout;
    state_stages = stages;

    if (state.second.size() < (first + count))
        state.second.resize(first + count);

    if (push_descriptors.size() < first + count)
        push_descriptors.resize(first + count);

    for (uint32_t i = 0; i < count; ++i)
    {
        state.second[i + first] = tables[i];

        const pipeline_layout_param param = descriptor_state.get_pipeline_layout_param(layout, first + i);
        if (param.type != pipeline_layout_param_type::descriptor_table)
            continue;

        uint32_t max_descriptor_size = 0;
        for (uint32_t k = 0; k < param.descriptor_table.count; ++k)
        {
            const descriptor_range& range = param.descriptor_table.ranges[k];
            if (range.count != UINT32_MAX && range.type != descriptor_type::sampler)
                max_descriptor_size = std::max(max_descriptor_size, range.binding + range.count);
        }

        if (push_descriptors[first + i].size() < max_descriptor_size)
            push_descriptors[first + i].resize(max_descriptor_size);

        for (uint32_t k = 0; k < param.descriptor_table.count; ++k)
        {
            const descriptor_range& range = param.descriptor_table.ranges[k];

            if (range.count == UINT32_MAX || range.type == descriptor_type::sampler)
                continue; // Skip unbounded ranges

            uint32_t base_offset = 0;
            descriptor_heap heap = { 0 };
            cmd_list->get_device()->get_descriptor_heap_offset(tables[i], range.binding, 0, &heap, &base_offset);

            descriptor_state.set_all_descriptors(heap, base_offset, range.count, push_descriptors[first + i], range.binding);
        }
    }
}

static void on_bind_descriptor_tables_no_track(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table* tables)
{
    int32_t idx = get_shader_stage_index(stages);

    if (idx < 0)
        return;

    auto& state_tracker = cmd_list->get_private_data<state_tracking>();
    auto& [tables_layout, descriptor_tables] = state_tracker.descriptor_tables[idx];
    auto& state_stages = state_tracker.descriptor_tables_stages[idx];
    auto& [push_layout, push_descriptors] = state_tracker.push_descriptors[idx];
    auto& [const_layout, push_constants] = state_tracker.push_constants[idx];

    if (layout != tables_layout)
    {
        descriptor_tables.clear(); // Layout changed, which resets all descriptor set bindings
        push_constants.clear();
        push_descriptors.clear();
    }

    tables_layout = layout;
    push_layout = layout;
    const_layout = layout;
    state_stages = stages;

    if (descriptor_tables.size() < (first + count))
        descriptor_tables.resize(first + count);

    for (uint32_t i = 0; i < count; ++i)
    {
        descriptor_tables[i + first] = tables[i];
    }
}

static void on_push_descriptors(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, const descriptor_table_update& update)
{
    int32_t idx = get_shader_stage_index(stages);

    if (idx < 0)
        return;

    auto& [desc_layout, descriptors] = cmd_list->get_private_data<state_tracking>().push_descriptors[idx];

    desc_layout = layout;

    if (descriptors.size() < layout_param + 1)
    {
        descriptors.resize(layout_param + 1);
    }

    if (descriptors[layout_param].size() < update.binding + update.count)
    {
        descriptors[layout_param].resize(update.binding + update.count);
    }

    for (uint32_t i = 0; i < update.count; i++)
    {
        descriptor_tracking::descriptor_data& descriptor = descriptors[layout_param][update.binding + i];

        descriptor.type = update.type;

        switch (update.type)
        {
        case descriptor_type::sampler:
            descriptor.sampler = static_cast<const sampler*>(update.descriptors)[i];
            break;
        case descriptor_type::sampler_with_resource_view:
            descriptor.sampler_and_view = static_cast<const sampler_with_resource_view*>(update.descriptors)[i];
            descriptor.view = descriptor.sampler_and_view.view;
            descriptor.sampler = descriptor.sampler_and_view.sampler;
            break;
        case descriptor_type::shader_resource_view:
        case descriptor_type::unordered_access_view:
            descriptor.view = static_cast<const resource_view*>(update.descriptors)[i];
            break;
        case descriptor_type::constant_buffer:
        case descriptor_type::shader_storage_buffer:
            descriptor.constant = static_cast<const buffer_range*>(update.descriptors)[i];
        }
    }
}

static void on_push_constants(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, uint32_t first, uint32_t count, const void* values)
{
    int32_t idx = get_shader_stage_index(stages);

    if (idx < 0)
        return;

    auto& [desc_layout, constants] = cmd_list->get_private_data<state_tracking>().push_constants[idx];

    desc_layout = layout;

    if (constants.size() < layout_param + 1)
    {
        constants.resize(layout_param + 1);
    }

    if (constants[layout_param].size() < first + count)
    {
        constants[layout_param].resize(first + count);
    }

    for (uint32_t i = 0; i < count; i++)
    {
        constants[layout_param][first + i] = reinterpret_cast<const uint32_t*>(values)[i];
    }
}

static void on_reset_command_list(command_list* cmd_list)
{
    auto& state = cmd_list->get_private_data<state_tracking>();
    state.clear();
}

static void on_reshade_present(effect_runtime* runtime)
{
    if (runtime->get_device()->get_api() != device_api::d3d12 && runtime->get_device()->get_api() != device_api::vulkan)
        on_reset_command_list(runtime->get_command_queue()->get_immediate_command_list());
}

void state_block::start_resource_barrier_tracking(reshade::api::resource res, reshade::api::resource_usage current_usage)
{
    const auto& restrack = resource_barrier_track.find(res.handle);

    if (restrack != resource_barrier_track.end())
    {
        restrack->second.ref_count++;
    }
    else
    {
        resource_barrier_track.emplace(res.handle, barrier_track{ current_usage, 1 });
    }
}

reshade::api::resource_usage state_block::stop_resource_barrier_tracking(reshade::api::resource res)
{
    const auto& restrack = resource_barrier_track.find(res.handle);

    if (restrack != resource_barrier_track.end())
    {
        resource_usage usage = restrack->second.usage;

        restrack->second.ref_count--;
        if (restrack->second.ref_count <= 0)
        {
            resource_barrier_track.erase(res.handle);
        }

        return usage;
    }

    return resource_usage::undefined;
}

static void on_barrier(command_list* cmd_list, uint32_t count, const resource* resources, const resource_usage* old_states, const resource_usage* new_states)
{
    auto& barrier_track = cmd_list->get_private_data<state_tracking>().resource_barrier_track;
    if (barrier_track.size() > 0)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            const auto& restrack = barrier_track.find(resources[i].handle);

            if (restrack != barrier_track.end())
            {
                restrack->second.usage = new_states[i];
            }
        }
    }
}

void state_tracking::register_events(bool track)
{
    track_descriptors = track;
    descriptor_tracking::register_events(track);

    reshade::register_event<reshade::addon_event::init_command_list>(on_init_command_list);
    reshade::register_event<reshade::addon_event::destroy_command_list>(on_destroy_command_list);

    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets_and_depth_stencil);
    reshade::register_event<reshade::addon_event::bind_pipeline>(on_bind_pipeline);
    reshade::register_event<reshade::addon_event::bind_pipeline_states>(on_bind_pipeline_states);
    reshade::register_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
    reshade::register_event<reshade::addon_event::bind_scissor_rects>(on_bind_scissor_rects);
    reshade::register_event<reshade::addon_event::reshade_present>(on_reshade_present);
    reshade::register_event<reshade::addon_event::barrier>(on_barrier);

    reshade::register_event<reshade::addon_event::push_descriptors>(on_push_descriptors);
    reshade::register_event<reshade::addon_event::push_constants>(on_push_constants);

    if (track_descriptors)
    {
        reshade::register_event<reshade::addon_event::bind_descriptor_tables>(on_bind_descriptor_tables);
    }
    else
    {
        reshade::register_event<reshade::addon_event::bind_descriptor_tables>(on_bind_descriptor_tables_no_track);
    }

    reshade::register_event<reshade::addon_event::reset_command_list>(on_reset_command_list);
}
void state_tracking::unregister_events()
{
    descriptor_tracking::unregister_events(track_descriptors);

    reshade::unregister_event<reshade::addon_event::init_command_list>(on_init_command_list);
    reshade::unregister_event<reshade::addon_event::destroy_command_list>(on_destroy_command_list);

    reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets_and_depth_stencil);
    reshade::unregister_event<reshade::addon_event::bind_pipeline>(on_bind_pipeline);
    reshade::unregister_event<reshade::addon_event::bind_pipeline_states>(on_bind_pipeline_states);
    reshade::unregister_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
    reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(on_bind_scissor_rects);
    reshade::unregister_event<reshade::addon_event::reshade_present>(on_reshade_present);
    reshade::unregister_event<reshade::addon_event::barrier>(on_barrier);

    reshade::unregister_event<reshade::addon_event::push_descriptors>(on_push_descriptors);
    reshade::unregister_event<reshade::addon_event::push_constants>(on_push_constants);

    if (track_descriptors)
    {
        reshade::unregister_event<reshade::addon_event::bind_descriptor_tables>(on_bind_descriptor_tables);
    }
    else
    {
        reshade::unregister_event<reshade::addon_event::bind_descriptor_tables>(on_bind_descriptor_tables_no_track);
    }

    reshade::unregister_event<reshade::addon_event::reset_command_list>(on_reset_command_list);
}
