#pragma once
// The QRhi of the platform's native graphics API: Metal (macOS), Direct3D 11 (Windows), Vulkan (Linux).
// A QGuiApplication must exist. Created and used on one thread (the render thread).

#include <QString>
#include <QSurface>
#include <memory>
#include <rhi/qrhi.h>

namespace rhidevice {
// nullptr when the API is not there (no GPU, no driver). name: the backend's name ("Metal"…)
std::unique_ptr<QRhi> create(QRhi::Flags flags = {}, QString *name = nullptr);

// A GPU that is not a real one (a virtual machine's paravirtual device, WARP, a software rasterizer): its arithmetic differs
// from the real thing's, so tests that compare pictures to the pixel are not run on it
bool isVirtualDevice(const QRhi *rhi);

// NyxMap's pictures are stored with the picture's bottom in their first row, so that every shader
// (ISF, conversions, compositing) samples them unchanged on every backend: drawn with y × ndcFlip (-1 where the
// framebuffer's first row is the top of NDC: Metal, Direct3D; 1 on Vulkan, whose NDC is y-down)
float ndcFlip(const QRhi *rhi);

// What a QWindow shown through the platform's QRhi is: Metal, Direct3D or Vulkan surface.
// Set before the window is created; prepareWindow() after (Vulkan: the window's instance)
QSurface::SurfaceType surfaceType();
void prepareWindow(QWindow *window);
}
