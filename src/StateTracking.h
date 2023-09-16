/*
 * Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#pragma once

#include <vector>
#include <unordered_map>
#include "DescriptorTracking.h"

 /// <summary>
 /// A state block capturing current state of a command list.
 /// </summary>
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
    std::unordered_map<reshade::api::pipeline_stage, reshade::api::pipeline> pipelines;
    reshade::api::pipeline_stage current_graphics_vertex_stage;
    reshade::api::pipeline_stage current_graphics_pixel_stage;
    reshade::api::pipeline_stage current_compute_stage;
    reshade::api::primitive_topology primitive_topology = reshade::api::primitive_topology::undefined;
    uint32_t blend_constant = 0;
    std::vector<reshade::api::viewport> viewports;
    std::vector<reshade::api::rect> scissor_rects;
    std::unordered_map<reshade::api::shader_stage, std::pair<reshade::api::pipeline_layout, std::vector<reshade::api::descriptor_table>>> descriptor_tables;
    std::unordered_map<reshade::api::shader_stage, std::pair<reshade::api::pipeline_layout, std::vector<std::vector<descriptor_tracking::descriptor_data>>>> descriptors;
    std::unordered_map<reshade::api::shader_stage, std::pair<reshade::api::pipeline_layout, std::vector<std::vector<uint32_t>>>> push_constants;
};

/// <summary>
/// An instance of this is automatically created for all command lists and can be queried with <c>cmd_list->get_private_data&lt;state_tracking&gt;()</c> (assuming state tracking was registered via <see cref="state_tracking::register_events"/>).
/// </summary>
class __declspec(uuid("c9abddf0-f9c2-4a7b-af49-89d8d470e207")) state_tracking : public state_block
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
