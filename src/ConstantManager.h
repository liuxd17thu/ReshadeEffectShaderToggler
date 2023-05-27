#pragma once

#include "AddonUIData.h"
#include "ConstantHandlerBase.h"
#include "ConstantCopyBase.h"

namespace Shim
{
    namespace Constants
    {
        enum ConstantCopyType
        {
            Copy_None,
            Copy_MemcpySingular,
            Copy_MemcpyNested,
            Copy_FFXIV, //actually only works with ConstantHandlerType::FFXIV, but shhhhh
            Copy_NierReplicant,
            Copy_DXUpdateBuffer
        };

        enum ConstantHandlerType
        {
            Handler_Default,
        };

        class ConstantManager
        {
        public:
            static bool Init(AddonImGui::AddonUIData& data, ConstantCopyBase**, ConstantHandlerBase**);
            static bool UnInit();

        private:
            static ConstantCopyType ResolveConstantCopyType(const std::string&);
            static ConstantHandlerType ResolveConstantHandlerType(const std::string&);
        };
    }
}
