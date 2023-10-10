#include <d3d12.h>
#include "RenderingPreviewManager.h"
#include "StateTracking.h"
#include "resource.h"

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;
using namespace std;

RenderingPreviewManager::RenderingPreviewManager(AddonImGui::AddonUIData& data, ResourceManager& rManager) : uiData(data), resourceManager(rManager)
{
}

RenderingPreviewManager::~RenderingPreviewManager()
{

}

const resource_view RenderingPreviewManager::GetCurrentPreviewResourceView(command_list* cmd_list, DeviceDataContainer& deviceData, const ToggleGroup* group, CommandListDataContainer& commandListData, uint32_t descIndex, uint64_t action)
{
    resource_view active_view = { 0 };

    if (deviceData.current_runtime == nullptr)
    {
        return active_view;
    }

    device* device = deviceData.current_runtime->get_device();

    state_tracking& state = cmd_list->get_private_data<state_tracking>();
    const vector<resource_view>& rtvs = state.render_targets;

    size_t index = group->getRenderTargetIndex();
    index = std::min(index, rtvs.size() - 1);

    if (rtvs.size() > 0 && rtvs[index] != 0)
    {
        resource rs = device->get_resource_from_view(rtvs[index]);

        if (rs == 0)
        {
            // Render targets may not have a resource bound in D3D12, in which case writes to them are discarded
            return active_view;
        }

        // Don't apply effects to non-RGB buffers
        resource_desc desc = device->get_resource_desc(rs);

        // Make sure our target matches swap buffer dimensions when applying effects or it's explicitly requested
        if (group->getMatchSwapchainResolution() < ShaderToggler::SWAPCHAIN_MATCH_MODE_NONE)
        {
            uint32_t width, height;
            deviceData.current_runtime->get_screenshot_width_and_height(&width, &height);

            if ((group->getMatchSwapchainResolution() >= ShaderToggler::SWAPCHAIN_MATCH_MODE_ASPECT_RATIO &&
                !RenderingManager::check_aspect_ratio(static_cast<float>(desc.texture.width), static_cast<float>(desc.texture.height), width, height, group->getMatchSwapchainResolution())) ||
                (group->getMatchSwapchainResolution() == ShaderToggler::SWAPCHAIN_MATCH_MODE_RESOLUTION && (width != desc.texture.width || height != desc.texture.height)))
            {
                return active_view;
            }
        }

        active_view = rtvs[index];
    }

    return active_view;
}

struct vert_uv
{
    float x, y;
};

struct vert_input
{
    vert_uv uv;
};

void RenderingPreviewManager::InitShaders(reshade::api::device* device)
{
    if (copyPipeline == 0 && (device->get_api() == device_api::d3d9 || device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12))
    {
        sampler_desc sampler_desc = {};
        sampler_desc.filter = filter_mode::min_mag_mip_point;
        sampler_desc.address_u = texture_address_mode::clamp;
        sampler_desc.address_v = texture_address_mode::clamp;
        sampler_desc.address_w = texture_address_mode::clamp;

        pipeline_layout_param layout_params[2];
        layout_params[0] = descriptor_range{ 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::sampler };
        layout_params[1] = descriptor_range{ 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::shader_resource_view };

        uint16_t embedPS = 0;
        uint16_t embedVS = 0;
        if (device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12)
        {
            embedPS = SHADER_PREVIEW_COPY_PS_4_0;
            embedVS = SHADER_FULLSCREEN_VS_4_0;
        }
        else if (device->get_api() == device_api::d3d9)
        {
            embedPS = SHADER_PREVIEW_COPY_PS_3_0;
            embedVS = SHADER_FULLSCREEN_VS_3_0;
        }

        const EmbeddedResourceData vs = resourceManager.GetResourceData(embedVS);
        const EmbeddedResourceData ps = resourceManager.GetResourceData(embedPS);

        shader_desc vs_desc = { vs.data, vs.size };
        shader_desc ps_desc = { ps.data, ps.size };

        std::vector<pipeline_subobject> subobjects;
        subobjects.push_back({ pipeline_subobject_type::vertex_shader, 1, &vs_desc });
        subobjects.push_back({ pipeline_subobject_type::pixel_shader, 1, &ps_desc });

        if (device->get_api() == device_api::d3d9)
        {
            static input_element input_layout[1] = {
                { 0, "TEXCOORD", 0, format::r32g32_float, 0, offsetof(vert_input, uv), sizeof(vert_input), 0},
            };

            subobjects.push_back({ pipeline_subobject_type::input_layout, 1, reinterpret_cast<void*>(input_layout) });
        }

        blend_desc blend_state;
        blend_state.blend_enable[0] = true;
        blend_state.source_color_blend_factor[0] = blend_factor::source_alpha;
        blend_state.dest_color_blend_factor[0] = blend_factor::one_minus_source_alpha;
        blend_state.color_blend_op[0] = blend_op::add;
        blend_state.source_alpha_blend_factor[0] = blend_factor::one;
        blend_state.dest_alpha_blend_factor[0] = blend_factor::one_minus_source_alpha;
        blend_state.alpha_blend_op[0] = blend_op::add;
        blend_state.render_target_write_mask[0] = 0xF;

        subobjects.push_back({ pipeline_subobject_type::blend_state, 1, &blend_state });

        rasterizer_desc rasterizer_state;
        rasterizer_state.cull_mode = cull_mode::none;
        rasterizer_state.scissor_enable = true;

        subobjects.push_back({ pipeline_subobject_type::rasterizer_state, 1, &rasterizer_state });

        if (!device->create_pipeline_layout(2, layout_params, &copyPipelineLayout) ||
            !device->create_pipeline(copyPipelineLayout, static_cast<uint32_t>(subobjects.size()), subobjects.data(), &copyPipeline) ||
            !device->create_sampler(sampler_desc, &copyPipelineSampler))
        {
            copyPipeline = {};
            copyPipelineLayout = {};
            copyPipelineSampler = {};
            reshade::log_message(reshade::log_level::warning, "Unable to create preview copy pipeline");
        }

        if (vertexBuffer == 0 && device->get_api() == device_api::d3d9)
        {
            const uint32_t num_vertices = 4;

            if (!device->create_resource(resource_desc(num_vertices * sizeof(vert_input), memory_heap::cpu_to_gpu, resource_usage::vertex_buffer), nullptr, resource_usage::cpu_access, &vertexBuffer))
            {
                reshade::log_message(reshade::log_level::warning, "Unable to create preview copy pipeline vertex buffer");
            }
            else
            {
                const vert_input vertices[num_vertices] = {
                    vert_input { vert_uv { 0.0f, 0.0f } },
                    vert_input { vert_uv { 0.0f, 1.0f } },
                    vert_input { vert_uv { 1.0f, 0.0f } },
                    vert_input { vert_uv { 1.0f, 1.0f } }
                };

                void* host_memory;

                if (device->map_buffer_region(vertexBuffer, 0, UINT64_MAX, map_access::write_only, &host_memory))
                {
                    memcpy(host_memory, vertices, num_vertices * sizeof(vert_input));
                    device->unmap_buffer_region(vertexBuffer);
                }
            }
        }
    }
}

void RenderingPreviewManager::DestroyShaders(reshade::api::device* device)
{
    if (copyPipeline != 0)
    {
        device->destroy_pipeline(copyPipeline);
        device->destroy_pipeline_layout(copyPipelineLayout);
        device->destroy_sampler(copyPipelineSampler);

        copyPipeline = {};
        copyPipelineLayout = {};
        copyPipelineSampler = {};
    }

    if (vertexBuffer != 0)
    {
        device->destroy_resource(vertexBuffer);
    }
}

void RenderingPreviewManager::CopyResource(command_list* cmd_list, resource_view srv_src, resource_view rtv_dst, uint32_t width, uint32_t height)
{
    device* device = cmd_list->get_device();
    if (copyPipeline == 0 || !(device->get_api() == device_api::d3d9 || device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12))
    {
        return;
    }

    cmd_list->get_private_data<state_tracking>().capture(cmd_list, true);

    cmd_list->bind_render_targets_and_depth_stencil(1, &rtv_dst);

    cmd_list->bind_pipeline(pipeline_stage::all_graphics, copyPipeline);
    
    cmd_list->push_descriptors(shader_stage::pixel, copyPipelineLayout, 0, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::sampler, &copyPipelineSampler });
    cmd_list->push_descriptors(shader_stage::pixel, copyPipelineLayout, 1, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::shader_resource_view, &srv_src });
    
    const viewport viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    cmd_list->bind_viewports(0, 1, &viewport);

    const rect scissor_rect = { 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) };
    cmd_list->bind_scissor_rects(0, 1, &scissor_rect);

    if (cmd_list->get_device()->get_api() == device_api::d3d9)
    {
        cmd_list->bind_pipeline_state(dynamic_state::primitive_topology, static_cast<uint32_t>(primitive_topology::triangle_strip));
        cmd_list->bind_vertex_buffer(0, vertexBuffer, 0, sizeof(vert_input));
        cmd_list->draw(4, 1, 0, 0);
    }
    else
    {
        cmd_list->draw(3, 1, 0, 0);
    }

    cmd_list->get_private_data<state_tracking>().apply(cmd_list, true);
}

void RenderingPreviewManager::UpdatePreview(command_list* cmd_list, uint64_t callLocation, uint64_t invocation)
{
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr)
    {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    // Remove call location from queue
    commandListData.commandQueue &= ~(invocation << (callLocation * MATCH_DELIMITER));

    if (deviceData.current_runtime == nullptr || uiData.GetToggleGroupIdShaderEditing() < 0) {
        return;
    }

    const ToggleGroup& group = uiData.GetToggleGroups().at(uiData.GetToggleGroupIdShaderEditing());

    // Set views during draw call since we can be sure the correct ones are bound at that point
    if (!callLocation && deviceData.huntPreview.target_view == 0)
    {
        resource_view active_view = resource_view{ 0 };

        if (invocation & MATCH_PREVIEW_PS)
        {
            active_view = GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 0, invocation & MATCH_PREVIEW_PS);
        }
        else if (invocation & MATCH_PREVIEW_VS)
        {
            active_view = GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 1, invocation & MATCH_PREVIEW_VS);
        }
        else if (invocation & MATCH_PREVIEW_CS)
        {
            active_view = GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 2, invocation & MATCH_PREVIEW_CS);
        }

        if (active_view != 0)
        {
            resource res = device->get_resource_from_view(active_view);
            resource_desc desc = device->get_resource_desc(res);
            //cmd_list->get_private_data<state_tracking>().start_resource_barrier_tracking(res, resource_usage::render_target);

            deviceData.huntPreview.target_view = active_view;
            deviceData.huntPreview.is_srv = false;
            deviceData.huntPreview.format = desc.texture.format;
            deviceData.huntPreview.width = desc.texture.width;
            deviceData.huntPreview.height = desc.texture.height;
        }
        else
        {
            return;
        }
    }

    if (deviceData.huntPreview.target_view == 0 || !(!callLocation && !deviceData.huntPreview.target_invocation_location || callLocation & deviceData.huntPreview.target_invocation_location))
    {
        return;
    }

    if (group.getId() == uiData.GetToggleGroupIdShaderEditing() && !deviceData.huntPreview.matched)
    {
        resource rs = device->get_resource_from_view(deviceData.huntPreview.target_view);

        if (rs == 0)
        {
            return;
        }

        resource_usage rs_usage = {}; //cmd_list->get_private_data<state_tracking>().stop_resource_barrier_tracking(rs);

        if (!resourceManager.IsCompatibleWithPreviewFormat(deviceData.current_runtime, rs))
        {
            deviceData.huntPreview.target_desc = cmd_list->get_device()->get_resource_desc(rs);
            deviceData.huntPreview.recreate_preview = true;
        }
        else
        {
            bool supportsAlphaClear = device->get_api() == device_api::d3d9 || device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12;

            resource previewResPing = resource{ 0 };
            resource previewResPong = resource{ 0 };
            resource_view preview_pong_rtv = resource_view{ 0 };
            resource_view preview_ping_srv = resource_view{ 0 };

            resourceManager.SetPingPreviewHandles(&previewResPing, nullptr, &preview_ping_srv);
            resourceManager.SetPongPreviewHandles(&previewResPong, &preview_pong_rtv, nullptr);

            if (previewResPong != 0 && (!group.getClearPreviewAlpha() || !supportsAlphaClear))
            {
                //resource resources[2] = { rs, previewResPong };
                //resource_usage from[2] = { rs_usage, resource_usage::shader_resource };
                //resource_usage to[2] = { resource_usage::copy_source, resource_usage::copy_dest };
                //
                //cmd_list->barrier(2, resources, from, to);
                cmd_list->copy_resource(rs, previewResPong);
                //cmd_list->barrier(2, resources, to, from);
            }
            else if (previewResPing != 0 && previewResPong != 0 && preview_ping_srv != 0 && preview_pong_rtv != 0 && supportsAlphaClear)
            {
                //resource resources[2] = { rs, previewResPing };
                //resource_usage from[2] = { rs_usage, resource_usage::shader_resource };
                //resource_usage to[2] = { resource_usage::copy_source, resource_usage::copy_dest };
                //
                //cmd_list->barrier(2, resources, from, to);
                cmd_list->copy_resource(rs, previewResPing);
                //cmd_list->barrier(2, resources, to, from);

                //cmd_list->barrier(previewResPong, resource_usage::shader_resource, resource_usage::render_target);
                CopyResource(cmd_list, preview_ping_srv, preview_pong_rtv, deviceData.huntPreview.width, deviceData.huntPreview.height);
                //cmd_list->barrier(previewResPong, resource_usage::render_target, resource_usage::shader_resource);
            }
        }

        deviceData.huntPreview.matched = true;
    }
}