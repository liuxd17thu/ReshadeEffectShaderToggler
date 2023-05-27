#include "ConstantManager.h"
#include "ConstantHandlerBase.h"
#include "ConstantCopyMemcpySingular.h"
#include "ConstantCopyMemcpyNested.h"
#include "ConstantCopyFFXIV.h"
#include "ConstantCopyNierReplicant.h"
#include "ConstantCopyDXUpdateBuffer.h"

using namespace Shim::Constants;
using namespace std;

ConstantCopyType ConstantManager::ResolveConstantCopyType(const string& ctype)
{
    if (ctype == "none")
        return ConstantCopyType::Copy_None;
    else if (ctype == "memcpy_singular")
        return ConstantCopyType::Copy_MemcpySingular;
    else if (ctype == "memcpy_nested")
        return ConstantCopyType::Copy_MemcpyNested;
    else if (ctype == "ffxiv")
        return ConstantCopyType::Copy_FFXIV;
    else if (ctype == "nier_replicant")
        return ConstantCopyType::Copy_NierReplicant;
    else if (ctype == "dx_update_buffer")
        return ConstantCopyType::Copy_DXUpdateBuffer;
    
    return ConstantCopyType::Copy_None;
}

ConstantHandlerType ConstantManager::ResolveConstantHandlerType(const string& htype)
{
    if (htype == "default")
        return ConstantHandlerType::Handler_Default;

    return ConstantHandlerType::Handler_Default;
}

bool ConstantManager::Init(AddonImGui::AddonUIData& data, ConstantCopyBase** constantCopy, ConstantHandlerBase** constantHandler)
{
    const string& hookType = data.GetConstHookType();
    const string& hookCopyType = data.GetConstHookCopyType();

    switch (ResolveConstantCopyType(hookType))
    {
    case ConstantCopyType::Copy_None:
    {
        *constantCopy = nullptr;
    }
        break;
    case ConstantCopyType::Copy_MemcpySingular:
    {
        static ConstantCopyMemcpySingular constantTypeMemcpySingular;
        *constantCopy = &constantTypeMemcpySingular;
    }
        break;
    case ConstantCopyType::Copy_MemcpyNested:
    {
        static ConstantCopyMemcpyNested constantTypeMemcpyNested;
        *constantCopy = &constantTypeMemcpyNested;
    }
        break;
    case ConstantCopyType::Copy_FFXIV:
    {
        static ConstantCopyFFXIV constantTypeFFXIV;
        *constantCopy = &constantTypeFFXIV;
    }
        break;
    case ConstantCopyType::Copy_NierReplicant:
    {
        static ConstantCopyNierReplicant constantTypeNierReplicant;
        *constantCopy = &constantTypeNierReplicant;
    }
        break;
    case ConstantCopyType::Copy_DXUpdateBuffer:
    {
        static ConstantCopyDXUpdateBuffer constantTypeDXUpdateBuffer;
        *constantCopy = &constantTypeDXUpdateBuffer;
    }
        break;
    default:
        *constantCopy = nullptr;
    }

    if (*constantCopy != nullptr && (*constantCopy)->Init())
    {
        // No need for separate ones for now
        static ConstantHandlerBase constantBase;
        *constantHandler = &constantBase;

        ConstantHandlerBase::SetConstantCopy(*constantCopy);
        data.SetConstantHandler(*constantHandler);

        return true;
    }

    return false;
}

bool ConstantManager::UnInit()
{
    if (MH_Uninitialize() != MH_OK)
    {
        return false;
    }

    return true;
}