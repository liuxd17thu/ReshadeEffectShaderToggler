#pragma once

#include <reshade.hpp>
#include "PipelinePrivateData.h"

namespace Util
{
    namespace Rendering
    {
        static inline void render_technique(DeviceDataContainer& deviceData, reshade::api::effect_technique technique, reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb = { 0 })
        {
            deviceData.current_runtime->render_technique(technique, cmd_list, rtv, rtv_srgb);
            deviceData.rendered_effects = true;
        }
    }
}