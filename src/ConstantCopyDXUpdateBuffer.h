#pragma once

#include <reshade_api.hpp>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include "ConstantCopyBase.h"

namespace Shim
{
    namespace Constants
    {
        class ConstantCopyDXUpdateBuffer final : public virtual ConstantCopyBase {
        public:
            bool Init() override final { return true; };
            bool UnInit() override final { return true; };

            void OnInitResource(reshade::api::device* device, const reshade::api::resource_desc& desc, const reshade::api::subresource_data* initData, reshade::api::resource_usage usage, reshade::api::resource handle) override final;
            void OnDestroyResource(reshade::api::device* device, reshade::api::resource res) override final;
            void OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size) override final;
            void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data) override final {};
            void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource) override final {};
        };
    }
}