#include "render/SurfaceFormat.hpp"

QSurfaceFormat fvDefaultSurfaceFormat() {
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);               // MSAA 4× (FR-RND-6)
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    return fmt;
}
