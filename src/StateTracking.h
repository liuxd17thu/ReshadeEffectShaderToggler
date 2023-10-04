/*
 * Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#pragma once

#include <vector>
#include <array>
#include <unordered_map>
#include "DescriptorTracking.h"

 /// <summary>
 /// A state block capturing current state of a command list.
 /// </summary>

namespace StateTracking
{
    constexpr reshade::api::pipeline_stage ALL_PIPELINE_STAGES[] = {
        reshade::api::pipeline_stage::pixel_shader,
        reshade::api::pipeline_stage::vertex_shader,
        reshade::api::pipeline_stage::compute_shader,
        reshade::api::pipeline_stage::depth_stencil,
        reshade::api::pipeline_stage::domain_shader,
        reshade::api::pipeline_stage::geometry_shader,
        reshade::api::pipeline_stage::hull_shader,
        reshade::api::pipeline_stage::input_assembler,
        reshade::api::pipeline_stage::output_merger,
        reshade::api::pipeline_stage::rasterizer,
        reshade::api::pipeline_stage::stream_output
    };

    constexpr uint32_t ALL_PIPELINE_STAGES_SIZE = sizeof(ALL_PIPELINE_STAGES) / sizeof(reshade::api::pipeline_stage);

    constexpr reshade::api::shader_stage ALL_SHADER_STAGES[] = {
        reshade::api::shader_stage::pixel,
        reshade::api::shader_stage::vertex,
        reshade::api::shader_stage::compute,
        reshade::api::shader_stage::hull,
        reshade::api::shader_stage::geometry,
        reshade::api::shader_stage::domain
    };

    constexpr uint32_t ALL_SHADER_STAGES_SIZE = sizeof(ALL_SHADER_STAGES) / sizeof(reshade::api::shader_stage);

    struct state_block
    {
        /// <summary>
        /// Binds all state captured by this state block on the specified command list.
        /// </summary>
        /// <param name="cmd_list">Target command list to bind the state on.</param>
        void apply(reshade::api::command_list* cmd_list) const;

        /// <summary>
        /// Removes all state in this state block.
        /// </summary>
        void clear();

        std::vector<reshade::api::resource_view> render_targets;
        reshade::api::resource_view depth_stencil = { 0 };
        std::array<reshade::api::pipeline, ALL_PIPELINE_STAGES_SIZE> current_pipeline;
        std::array<reshade::api::pipeline_stage, ALL_PIPELINE_STAGES_SIZE> current_pipeline_stage;
        reshade::api::primitive_topology primitive_topology = reshade::api::primitive_topology::undefined;
        uint32_t blend_constant = 0;
        uint32_t front_stencil_reference_value = 0;
        uint32_t back_stencil_reference_value = 0;
        std::vector<reshade::api::viewport> viewports;
        std::vector<reshade::api::rect> scissor_rects;
        std::array<std::pair<reshade::api::pipeline_layout, std::vector<reshade::api::descriptor_table>>, ALL_SHADER_STAGES_SIZE> descriptor_tables;
        std::array<reshade::api::shader_stage, ALL_SHADER_STAGES_SIZE> descriptor_tables_stages;
        std::array<std::pair<reshade::api::pipeline_layout, std::vector<std::vector<descriptor_tracking::descriptor_data>>>, ALL_SHADER_STAGES_SIZE> descriptors;
        std::array<std::pair<reshade::api::pipeline_layout, std::vector<std::vector<uint32_t>>>, ALL_SHADER_STAGES_SIZE> push_constants;
    };
}

/// <summary>
/// An instance of this is automatically created for all command lists and can be queried with <c>cmd_list->get_private_data&lt;state_tracking&gt;()</c> (assuming state tracking was registered via <see cref="state_tracking::register_events"/>).
/// </summary>
class __declspec(uuid("c9abddf0-f9c2-4a7b-af49-89d8d470e207")) state_tracking : public StateTracking::state_block
{
public:
    /// <summary>
    /// Registers all the necessary add-on events for state tracking to work.
    /// </summary>
    static void register_events(bool track = true);
    /// <summary>
    /// Unregisters all the necessary add-on events for state tracking to work.
    /// </summary>
    static void unregister_events();
private:
    static bool track_descriptors;
};
