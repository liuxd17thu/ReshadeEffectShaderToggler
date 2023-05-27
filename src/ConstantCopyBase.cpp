#include "ConstantCopyBase.h"

using namespace Shim::Constants;
using namespace reshade::api;
using namespace std;

std::unordered_map<uint64_t, vector<uint8_t>> ConstantCopyBase::deviceToHostConstantBuffer;
shared_mutex ConstantCopyBase::deviceHostMutex;

ConstantCopyBase::ConstantCopyBase()
{

}

ConstantCopyBase::~ConstantCopyBase()
{

}

void ConstantCopyBase::GetHostConstantBuffer(vector<uint8_t>& dest, size_t size, uint64_t resourceHandle)
{
    shared_lock<shared_mutex> lock(deviceHostMutex);
    const auto& ret = deviceToHostConstantBuffer.find(resourceHandle);
    if (ret != deviceToHostConstantBuffer.end())
    {
        std::memcpy(dest.data(), get<1>(*ret).data(), size);
    }
}

void ConstantCopyBase::CreateHostConstantBuffer(device* dev, resource resource, size_t size)
{
    unique_lock<shared_mutex> lock(deviceHostMutex);
    deviceToHostConstantBuffer.emplace(resource.handle, vector<uint8_t>(size, 0));
}

void ConstantCopyBase::DeleteHostConstantBuffer(resource resource)
{
    unique_lock<shared_mutex> lock(deviceHostMutex);
    deviceToHostConstantBuffer.erase(resource.handle);
}

inline void ConstantCopyBase::SetHostConstantBuffer(const uint64_t handle, const void* buffer, size_t size, uintptr_t offset, uint64_t bufferSize)
{
    unique_lock<shared_mutex> lock(deviceHostMutex);
    const auto& blah = deviceToHostConstantBuffer.find(handle);
    if (blah != deviceToHostConstantBuffer.end())
        memcpy(get<1>(*blah).data() + offset, buffer, size);
}

void ConstantCopyBase::OnInitResource(device* device, const resource_desc& desc, const subresource_data* initData, resource_usage usage, reshade::api::resource handle)
{
    if (desc.heap == memory_heap::cpu_to_gpu && static_cast<uint32_t>(desc.usage & resource_usage::constant_buffer))
    {
        CreateHostConstantBuffer(device, handle, desc.buffer.size);
        if (initData != nullptr && initData->data != nullptr)
        {
            SetHostConstantBuffer(handle.handle, initData->data, desc.buffer.size, 0, desc.buffer.size);
        }
    }
}

void ConstantCopyBase::OnDestroyResource(device* device, resource res)
{
    resource_desc desc = device->get_resource_desc(res);
    if (desc.heap == memory_heap::cpu_to_gpu && static_cast<uint32_t>(desc.usage & resource_usage::constant_buffer))
    {
        DeleteHostConstantBuffer(res);
    }
}