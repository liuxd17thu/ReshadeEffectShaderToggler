#include <cstring>
#include "ConstantCopyDXUpdateBuffer.h"

using namespace Shim::Constants;
using namespace reshade::api;

void ConstantCopyDXUpdateBuffer::OnUpdateBufferRegion(device* device, const void* data, resource resource, uint64_t offset, uint64_t size)
{
    const auto& buf = deviceToHostConstantBuffer.find(resource.handle);

    if (buf != deviceToHostConstantBuffer.end())
    {
        if (size == 0)
        {
            resource_desc desc = device->get_resource_desc(resource);
            size = desc.buffer.size;
        }

        memmove(get<1>(*buf).data() + offset, data, size);
    }
}

void ConstantCopyDXUpdateBuffer::OnInitResource(device* device, const resource_desc& desc, const subresource_data* initData, resource_usage usage, reshade::api::resource handle)
{
    if (static_cast<uint32_t>(desc.usage & resource_usage::constant_buffer))
    {
        CreateHostConstantBuffer(device, handle, desc.buffer.size);
        if (initData != nullptr && initData->data != nullptr)
        {
            SetHostConstantBuffer(handle.handle, initData->data, desc.buffer.size, 0, desc.buffer.size);
        }
    }
}

void ConstantCopyDXUpdateBuffer::OnDestroyResource(device* device, resource res)
{
    resource_desc desc = device->get_resource_desc(res);
    if (static_cast<uint32_t>(desc.usage & resource_usage::constant_buffer))
    {
        DeleteHostConstantBuffer(res);
    }
}