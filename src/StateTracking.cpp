/*
 * Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "reshade.hpp"
#include "StateTracking.h"

using namespace reshade::api;

bool state_tracking::track_descriptors = true;

void state_block::apply(command_list* cmd_list) const
{
    if (!render_targets.empty() || depth_stencil != 0)
        cmd_list->bind_render_targets_and_depth_stencil(static_cast<uint32_t>(render_targets.size()), render_targets.data(), depth_stencil);

    if (static_cast<uint32_t>(current_graphics_pixel_stage) != 0)
        cmd_list->bind_pipeline(current_graphics_pixel_stage, pipelines.at(current_graphics_pixel_stage));

    if (static_cast<uint32_t>(current_graphics_vertex_stage) != 0)
        cmd_list->bind_pipeline(current_graphics_vertex_stage, pipelines.at(current_graphics_vertex_stage));

    if (static_cast<uint32_t>(current_compute_stage) != 0)
        cmd_list->bind_pipeline(current_compute_stage, pipelines.at(current_compute_stage));

    if (primitive_topology != primitive_topology::undefined)
        cmd_list->bind_pipeline_state(dynamic_state::primitive_topology, static_cast<uint32_t>(primitive_topology));
    if (blend_constant != 0)
        cmd_list->bind_pipeline_state(dynamic_state::blend_constant, blend_constant);

    if (!viewports.empty())
        cmd_list->bind_viewports(0, static_cast<uint32_t>(viewports.size()), viewports.data());
    if (!scissor_rects.empty())
        cmd_list->bind_scissor_rects(0, static_cast<uint32_t>(scissor_rects.size()), scissor_rects.data());

    for (const auto& [stages, descriptor_state] : descriptor_tables)
    {
        auto& descriptor_data = cmd_list->get_device()->get_private_data<descriptor_tracking>();
        const auto& [pipelinelayout, descriptor_set] = descriptor_state;

        bool done = false;
        uint32_t start_index = 0;
        uint32_t end_index = 0;

        while (end_index < descriptor_set.size() && start_index < descriptor_set.size())
        {
            while (start_index < descriptor_set.size() && (descriptor_data.get_pipeline_layout_param(pipelinelayout, start_index).type == pipeline_layout_param_type::push_constants || descriptor_set[start_index].handle == 0))
            {
                start_index++;
            }

            end_index = start_index;

            while (end_index < descriptor_set.size() && descriptor_data.get_pipeline_layout_param(pipelinelayout, end_index).type != pipeline_layout_param_type::push_constants && descriptor_set[end_index].handle != 0)
            {
                end_index++;
            }

            cmd_list->bind_descriptor_tables(stages, pipelinelayout, start_index, end_index - start_index, &descriptor_set.data()[start_index]);

            start_index = end_index;
        }
    }
}

void state_block::clear()
{
    render_targets.clear();
    depth_stencil = { 0 };
    pipelines.clear();
    primitive_topology = primitive_topology::undefined;
    blend_constant = 0;
    viewports.clear();
    scissor_rects.clear();
    descriptor_tables.clear();
    push_constants.clear();
    descriptors.clear();

    current_compute_stage = static_cast<pipeline_stage>(0);
    current_graphics_pixel_stage = static_cast<pipeline_stage>(0);
    current_graphics_vertex_stage = static_cast<pipeline_stage>(0);
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
    state.pipelines[stages] = pipeline;

    if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(pipeline_stage::pixel_shader))
        state.current_graphics_pixel_stage = stages;

    if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(pipeline_stage::vertex_shader))
        state.current_graphics_vertex_stage = stages;

    if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(pipeline_stage::compute_shader))
        state.current_compute_stage = stages;
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

static void clear_descriptors(std::vector<std::vector<descriptor_tracking::descriptor_data>>& desc)
{
    for (auto& i : desc)
    {
        for (auto& j : i)
        {
            j.constant.buffer = { 0 };
            j.view = { 0 };
            j.sampler = { 0 };
            j.sampler_and_view.sampler = { 0 };
            j.sampler_and_view.view = { 0 };
        }
    }
}

static void on_bind_descriptor_tables(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table* tables)
{
    auto& state = cmd_list->get_private_data<state_tracking>().descriptor_tables[stages];
    auto& [desc_layout, descriptors] = cmd_list->get_private_data<state_tracking>().descriptors[stages];
    auto& [_, push_constants] = cmd_list->get_private_data<state_tracking>().push_constants[stages];
    const auto& descriptor_state = cmd_list->get_device()->get_private_data<descriptor_tracking>();

    if (layout != state.first)
    {
        state.second.clear(); // Layout changed, which resets all descriptor set bindings
        push_constants.clear();
        clear_descriptors(descriptors);
    }

    state.first = layout;
    desc_layout = layout;

    if (state.second.size() < (first + count))
        state.second.resize(first + count);

    if (descriptors.size() < first + count)
        descriptors.resize(first + count);

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

        if (descriptors[first + i].size() < max_descriptor_size)
            descriptors[first + i].resize(max_descriptor_size);

        for (uint32_t k = 0; k < param.descriptor_table.count; ++k)
        {
            const descriptor_range& range = param.descriptor_table.ranges[k];

            if (range.count == UINT32_MAX || range.type == descriptor_type::sampler)
                continue; // Skip unbounded ranges

            uint32_t base_offset = 0;
            descriptor_heap heap = { 0 };
            cmd_list->get_device()->get_descriptor_heap_offset(tables[i], range.binding, 0, &heap, &base_offset);

            descriptor_state.set_all_descriptors(heap, base_offset, range.count, descriptors[first + i], range.binding);
        }
    }
}

static void on_bind_descriptor_tables_no_track(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table* tables)
{
    auto& state = cmd_list->get_private_data<state_tracking>().descriptor_tables[stages];

    if (layout != state.first)
    {
        state.second.clear(); // Layout changed, which resets all descriptor set bindings
    }

    state.first = layout;

    if (state.second.size() < (first + count))
        state.second.resize(first + count);

    for (uint32_t i = 0; i < count; ++i)
    {
        state.second[i + first] = tables[i];
    }
}

static void on_push_descriptors(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, const descriptor_table_update& update)
{
    auto& [desc_layout, descriptors] = cmd_list->get_private_data<state_tracking>().descriptors[stages];

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
            descriptor.constant = static_cast<const buffer_range*>(update.descriptors)[i];
        }
    }
}

static void on_push_constants(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, uint32_t first, uint32_t count, const void* values)
{
    auto& [desc_layout, constants] = cmd_list->get_private_data<state_tracking>().push_constants[stages];

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

    if (track_descriptors)
    {
        reshade::register_event<reshade::addon_event::bind_descriptor_tables>(on_bind_descriptor_tables);
        reshade::register_event<reshade::addon_event::push_descriptors>(on_push_descriptors);
        reshade::register_event<reshade::addon_event::push_constants>(on_push_constants);
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

    if (track_descriptors)
    {
        reshade::unregister_event<reshade::addon_event::bind_descriptor_tables>(on_bind_descriptor_tables);
        reshade::unregister_event<reshade::addon_event::push_descriptors>(on_push_descriptors);
        reshade::unregister_event<reshade::addon_event::push_constants>(on_push_constants);
    }
    else
    {
        reshade::unregister_event<reshade::addon_event::bind_descriptor_tables>(on_bind_descriptor_tables_no_track);
    }

    reshade::unregister_event<reshade::addon_event::reset_command_list>(on_reset_command_list);
}
