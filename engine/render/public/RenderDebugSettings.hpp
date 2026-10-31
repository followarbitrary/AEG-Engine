#pragma once

namespace aeg::render {
// Optional pass switches used by the editor diagnostics panel. Scene and
// presentation passes are intentionally not represented because they are
// required to produce a valid frame.
struct RenderDebugSettings {
    bool shadows{true};
    bool taa{true};
    bool toneMapping{true};
    bool fxaa{true};
    bool operator==(const RenderDebugSettings&) const = default;
};
}
