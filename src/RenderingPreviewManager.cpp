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

const std::tuple<resource_view, bool> RenderingPreviewManager::GetCurrentPreviewResourceView(command_list* cmd_list, DeviceDataContainer& deviceData, const ToggleGroup* group, CommandListDataContainer& commandListData, uint32_t descIndex, uint64_t action)
{
    resource_view active_view = { 0 };

    if (deviceData.current_runtime == nullptr)
    {
        return make_tuple(active_view, false);
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
            return make_tuple(active_view, false);
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
                return make_tuple(active_view, false);
            }
        }

        if (group->getClearPreviewAlpha() && (device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12))
        {
            resourceManager.SetShaderResourceViewHandles(rs.handle, &active_view, nullptr);

            if (active_view != 0)
            {
                return make_tuple(active_view, true);
            }
        }

        if (!group->getClearPreviewAlpha() || !(device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12) || active_view == 0)
        {
            active_view = rtvs[index];
        }
    }

    return make_tuple(active_view, false);
}

void RenderingPreviewManager::OnInitDevice(reshade::api::device* device)
{
    // Create copy pipeline in order to omit alpha channel in preview. Only SM4.0 for now
    if (device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12)
    {
        sampler_desc sampler_desc = {};
        sampler_desc.filter = filter_mode::min_mag_mip_point;
        sampler_desc.address_u = texture_address_mode::clamp;
        sampler_desc.address_v = texture_address_mode::clamp;
        sampler_desc.address_w = texture_address_mode::clamp;

        pipeline_layout_param layout_params[2];
        layout_params[0] = descriptor_range{ 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::sampler };
        layout_params[1] = descriptor_range{ 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::shader_resource_view };

        const EmbeddedResourceData vs = resourceManager.GetResourceData(SHADER_FULLSCREEN_VS_4_0);
        const EmbeddedResourceData ps = resourceManager.GetResourceData(SHADER_PREVIEW_COPY_PS_4_0);

        shader_desc vs_desc = { vs.data, vs.size };
        shader_desc ps_desc = { ps.data, ps.size };

        std::vector<pipeline_subobject> subobjects;
        subobjects.push_back({ pipeline_subobject_type::vertex_shader, 1, &vs_desc });
        subobjects.push_back({ pipeline_subobject_type::pixel_shader, 1, &ps_desc });

        if (!device->create_pipeline_layout(2, layout_params, &copyPipelineLayout) ||
            !device->create_pipeline(copyPipelineLayout, static_cast<uint32_t>(subobjects.size()), subobjects.data(), &copyPipeline) ||
            !device->create_sampler(sampler_desc, &copyPipelineSampler))
        {
            reshade::log_message(reshade::log_level::warning, "Unable to create preview copy pipeline");
        }
    }
}

void RenderingPreviewManager::OnDestroyDevice(reshade::api::device* device)
{
    device->destroy_pipeline(copyPipeline);
    device->destroy_pipeline_layout(copyPipelineLayout);
    device->destroy_sampler(copyPipelineSampler);
    copyPipeline = {};
    copyPipelineLayout = {};
    copyPipelineSampler = {};
}

void RenderingPreviewManager::CopyResource(command_list* cmd_list, resource_view srv_src, resource_view rtv_dst, uint32_t width, uint32_t height)
{
    device* device = cmd_list->get_device();
    if (copyPipeline == 0 || !(device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12))
    {
        return;
    }

    cmd_list->bind_render_targets_and_depth_stencil(1, &rtv_dst);

    cmd_list->bind_pipeline(pipeline_stage::all_graphics, copyPipeline);

    cmd_list->push_descriptors(shader_stage::pixel, copyPipelineLayout, 0, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::sampler, &copyPipelineSampler });
    cmd_list->push_descriptors(shader_stage::pixel, copyPipelineLayout, 1, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::shader_resource_view, &srv_src });

    const viewport viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    cmd_list->bind_viewports(0, 1, &viewport);
    const rect scissor_rect = { 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) };
    cmd_list->bind_scissor_rects(0, 1, &scissor_rect);

    cmd_list->draw(3, 1, 0, 0);

    cmd_list->get_private_data<state_tracking>().apply(cmd_list);
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
        std::tuple<resource_view, bool> active_data = make_tuple(resource_view{0}, false);

        if (invocation & MATCH_PREVIEW_PS)
        {
            active_data = GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 0, invocation & MATCH_PREVIEW_PS);
        }
        else if (invocation & MATCH_PREVIEW_VS)
        {
            active_data = GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 1, invocation & MATCH_PREVIEW_VS);
        }
        else if (invocation & MATCH_PREVIEW_CS)
        {
            active_data = GetCurrentPreviewResourceView(cmd_list, deviceData, &group, commandListData, 2, invocation & MATCH_PREVIEW_CS);
        }

        const auto& [active_view, is_srv] = active_data;

        if (active_view != 0)
        {
            resource res = device->get_resource_from_view(active_view);
            resource_desc desc = device->get_resource_desc(res);

            deviceData.huntPreview.target_view = active_view;
            deviceData.huntPreview.is_srv = is_srv;
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

        if (!resourceManager.IsCompatibleWithPreviewFormat(deviceData.current_runtime, rs))
        {
            deviceData.huntPreview.target_desc = cmd_list->get_device()->get_resource_desc(rs);
            deviceData.huntPreview.recreate_preview = true;
        }
        else
        {
            resource previewRes = resource{ 0 };
            resource_view preview_rtv = resource_view{ 0 };
            resourceManager.SetPreviewViewHandles(&previewRes, &preview_rtv, nullptr);

            if (previewRes != 0 && (!group.getClearPreviewAlpha() || !deviceData.huntPreview.is_srv))
            {
                cmd_list->copy_resource(rs, previewRes);
            }
            else if (previewRes != 0 && preview_rtv != 0 && group.getClearPreviewAlpha() && (device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12) && deviceData.huntPreview.is_srv)
            {
                CopyResource(cmd_list, deviceData.huntPreview.target_view, preview_rtv, deviceData.huntPreview.width, deviceData.huntPreview.height);
            }
        }

        deviceData.huntPreview.matched = true;
    }
}