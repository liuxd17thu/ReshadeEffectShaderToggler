#include <cstring>
#include "ConstantCopyGPUReadback.h"
#include "PipelinePrivateData.h"

using namespace Shim::Constants;
using namespace reshade::api;
using namespace ShaderToggler;
using namespace std;


void ConstantCopyGPUReadback::GetHostConstantBuffer(reshade::api::command_list* cmd_list, vector<uint8_t>& dest, size_t size, uint64_t resourceHandle)
{
    shared_lock<shared_mutex> lock(deviceHostMutex);

    const auto& it = resToCopyBuffer.find(resourceHandle);
    if (it != resToCopyBuffer.end())
    {
        const auto& [_, cpuRead] = *it;
        void* data = nullptr;
        resource src = resource{ resourceHandle };

        cmd_list->copy_resource(src, cpuRead);
        if (cmd_list->get_device()->map_buffer_region(cpuRead, 0, size, map_access::read_only, &data))
        {
            memcpy(dest.data(), data, size);
            cmd_list->get_device()->unmap_buffer_region(cpuRead);
        }
        
    }
}

void ConstantCopyGPUReadback::OnInitResource(device* device, const resource_desc& desc, const subresource_data* initData, resource_usage usage, reshade::api::resource handle)
{
    if (static_cast<uint32_t>(desc.usage & resource_usage::constant_buffer) && desc.type == resource_type::buffer)
    {
        unique_lock<shared_mutex> lock(deviceHostMutex);
        if (resToCopyBuffer.find(handle.handle) == resToCopyBuffer.end())
        {
            resource backrs = resource{ 0 };
            if (device->create_resource(resource_desc(desc.buffer.size, memory_heap::gpu_to_cpu, resource_usage::copy_dest | resource_usage::copy_source), nullptr, resource_usage::copy_dest, &backrs))
            {
                resToCopyBuffer.emplace(handle.handle, backrs);
            }
        }
    }
}

void ConstantCopyGPUReadback::OnDestroyResource(device* device, resource res)
{
    resource_desc desc = device->get_resource_desc(res);
    if (static_cast<uint32_t>(desc.usage & resource_usage::constant_buffer) && desc.type == resource_type::buffer)
    {
        unique_lock<shared_mutex> lock(deviceHostMutex);
        const auto& el = resToCopyBuffer.find(res.handle);

        if (el != resToCopyBuffer.end())
        {
            device->destroy_resource(el->second);
            resToCopyBuffer.erase(res.handle);
        }
    }
}