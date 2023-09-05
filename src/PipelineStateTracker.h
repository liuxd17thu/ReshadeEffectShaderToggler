#pragma once

#include <reshade.hpp>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

using namespace reshade::api;

namespace StateTracker
{
    enum PipelineBindingTypes : uint32_t
    {
        unknown = 0,
        bind_pipeline,
        bind_render_target,
        bind_viewport,
        bind_scissor_rect,
        bind_descriptor_sets,
        bind_pipeline_states,
        push_descriptors,
        push_constants,
        render_pass
    };

    struct PipelineBindingBase
    {
    public:
        command_list* cmd_list = nullptr;
        uint32_t callIndex = 0;
        virtual PipelineBindingTypes GetType() { return PipelineBindingTypes::unknown; }
    };

    template<PipelineBindingTypes T>
    struct PipelineBinding : PipelineBindingBase
    {
    public:
        PipelineBindingTypes GetType() override { return T; }
    };

    struct __declspec(novtable) BindRenderTargetsState final : PipelineBinding<PipelineBindingTypes::bind_render_target> {
        uint32_t count;
        std::vector<resource_view> rtvs;
        resource_view dsv;

        void Reset()
        {
            callIndex = 0;
            cmd_list = nullptr;
            rtvs.clear();
            dsv = { 0 };
            count = 0;
        }
    };

    struct __declspec(novtable) RenderPassState final : PipelineBinding<PipelineBindingTypes::render_pass> {
        uint32_t count;
        std::vector<render_pass_render_target_desc> rtvs;
        render_pass_depth_stencil_desc dsv;

        void Reset()
        {
            callIndex = 0;
            cmd_list = nullptr;
            rtvs.clear();
            dsv = { 0 };
            count = 0;
        }
    };

    struct __declspec(novtable) BindViewportsState final : PipelineBinding<PipelineBindingTypes::bind_viewport> {
        uint32_t first;
        uint32_t count;
        std::vector<viewport> viewports;

        void Reset()
        {
            callIndex = 0;
            cmd_list = nullptr;
            first = 0;
            count = 0;
            viewports.clear();
        }
    };

    struct __declspec(novtable) BindScissorRectsState final : PipelineBinding<PipelineBindingTypes::bind_scissor_rect> {
        uint32_t first;
        uint32_t count;
        std::vector<rect> rects;

        void Reset()
        {
            callIndex = 0;
            cmd_list = nullptr;
            first = 0;
            count = 0;
            rects.clear();
        }
    };

    struct __declspec(novtable) PushConstantsState final : PipelineBinding<PipelineBindingTypes::push_constants> {
        pipeline_layout current_layout[3];
        uint32_t first;
        uint32_t count;
        std::vector<std::vector<uint32_t>> current_constants[3]; // consider only CBs for now

        void Reset()
        {
            callIndex = 0;
            cmd_list = nullptr;
            current_layout[0] = { 0 };
            current_layout[1] = { 0 };
            current_layout[2] = { 0 };
            first = 0;
            count = 0;
            current_constants[0].clear();
            current_constants[1].clear();
            current_constants[2].clear();
        }
    };

    struct __declspec(novtable) PushDescriptorsState final : PipelineBinding<PipelineBindingTypes::push_descriptors> {
        pipeline_layout current_layout[3];
        std::vector<std::vector<buffer_range>> current_descriptors[3]; // consider only CBs for now
        std::vector<std::vector<resource_view>> current_srv[3];

        void Reset()
        {
            callIndex = 0;
            cmd_list = nullptr;
            current_layout[0] = { 0 };
            current_layout[1] = { 0 };
            current_layout[2] = { 0 };
            current_descriptors[0].clear();
            current_descriptors[1].clear();
            current_descriptors[2].clear();
            current_srv[0].clear();
            current_srv[1].clear();
            current_srv[2].clear();
        }
    };

    struct __declspec(novtable) BindDescriptorSetsState final : PipelineBinding<PipelineBindingTypes::bind_descriptor_sets> {
        pipeline_layout current_layout[2];
        std::vector<descriptor_table> current_sets[2];
        std::unordered_map<uint64_t, std::vector<bool>> transient_mask;

        void Reset()
        {
            callIndex = 0;
            cmd_list = nullptr;
            current_layout[0] = { 0 };
            current_layout[1] = { 0 };
            current_sets[0].clear();
            current_sets[1].clear();
            transient_mask.clear();
        }
    };

    struct __declspec(novtable) BindPipelineStatesState final : PipelineBinding<PipelineBindingTypes::bind_pipeline_states> {
        uint32_t value;
        bool valuesSet;
        dynamic_state state;

        BindPipelineStatesState(dynamic_state s)
        {
            state = s;
            Reset();
        }

        void Reset()
        {
            callIndex = 0;
            cmd_list = nullptr;
            value = 0;
            valuesSet = false;
        }
    };

    struct __declspec(novtable) BindPipelineStatesStates final {
        BindPipelineStatesState states[2] = { BindPipelineStatesState(dynamic_state::blend_constant), BindPipelineStatesState(dynamic_state::primitive_topology) };

        void Reset()
        {
            states[0].Reset();
            states[1].Reset();
        }
    };

    struct __declspec(novtable) BindPipelineState final : PipelineBinding<PipelineBindingTypes::bind_pipeline> {
        pipeline_stage stages;
        pipeline pipeline;

        void Reset()
        {
            callIndex = 0;
            cmd_list = nullptr;
            stages = pipeline_stage::all;
            pipeline = { 0 };
        }
    };

    class __declspec(novtable) PipelineStateTracker final
    {
    public:
        PipelineStateTracker();
        ~PipelineStateTracker();

        void Reset();
        void ReApplyState(command_list* cmd_list, const std::unordered_map<uint64_t, std::vector<bool>>& transient_mask);

        void OnBeginRenderPass(command_list* cmd_list, uint32_t count, const render_pass_render_target_desc* rts, const render_pass_depth_stencil_desc* ds);
        void OnBindRenderTargetsAndDepthStencil(command_list* cmd_list, uint32_t count, const resource_view* rtvs, resource_view dsv);
        void OnBindPipelineStates(command_list* cmd_list, uint32_t count, const dynamic_state* states, const uint32_t* values);
        void OnBindScissorRects(command_list* cmd_list, uint32_t first, uint32_t count, const rect* rects);
        void OnBindViewports(command_list* cmd_list, uint32_t first, uint32_t count, const viewport* viewports);
        void OnBindDescriptorSets(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table* sets);
        void OnPushDescriptors(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, const descriptor_table_update& update);
        void OnBindPipeline(command_list* commandList, pipeline_stage stages, pipeline pipelineHandle);
        void OnPushConstants(command_list* cmd_list, shader_stage stages, pipeline_layout layout, uint32_t layout_param, uint32_t first, uint32_t count, const void* values);

        const PushDescriptorsState* GetPushDescriptorState() { return &_pushDescriptorsState; }
        const PushConstantsState* GetPushConstantsState() { return &_pushConstantsState; }
        const std::vector<resource_view>& GetBoundRenderTargetViews() const;

        void ClearPushDescriptorState(pipeline_stage);

        bool IsInRenderPass() const;

    private:
        void ApplyBoundDescriptorSets(command_list* cmd_list, shader_stage stage, pipeline_layout layout,
            const std::vector<descriptor_table>& descriptors, const std::vector<bool>& mask);

        uint32_t _callIndex = 0;
        BindRenderTargetsState _renderTargetState;
        BindDescriptorSetsState _descriptorSetsState;
        PushConstantsState _pushConstantsState;
        PushDescriptorsState _pushDescriptorsState;
        BindViewportsState _viewportsState;
        BindScissorRectsState _scissorRectsState;
        BindPipelineStatesStates _pipelineStatesState;
        RenderPassState _renderPassState;
        BindPipelineState _pipelineState;
    };
}