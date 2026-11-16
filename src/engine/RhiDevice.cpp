#include "RhiDevice.h"

#include <QtGlobal>
#include <QWindow>
#if !defined(Q_OS_MACOS) && !defined(Q_OS_IOS) && !defined(Q_OS_WIN) && QT_CONFIG(vulkan)
#define NYXMAP_RHI_VULKAN
#include <QVulkanInstance>
#endif

namespace rhidevice {

#ifdef NYXMAP_RHI_VULKAN
// One Vulkan instance for the process's life (every QRhi and every window of the process use it)
static QVulkanInstance *vulkanInstance()
{
    static QVulkanInstance *instance = [] {
        auto *inst = new QVulkanInstance;
        inst->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (!inst->create()) {
            delete inst;
            return static_cast<QVulkanInstance *>(nullptr);
        }
        return inst;
    }();
    return instance;
}
#endif

QSurface::SurfaceType surfaceType()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    return QSurface::MetalSurface;
#elif defined(Q_OS_WIN)
    return QSurface::Direct3DSurface;
#elif defined(NYXMAP_RHI_VULKAN)
    return vulkanInstance() ? QSurface::VulkanSurface : QSurface::OpenGLSurface;
#else
    return QSurface::OpenGLSurface;
#endif
}

void prepareWindow(QWindow *window)
{
#ifdef NYXMAP_RHI_VULKAN
    if (window && window->surfaceType() == QSurface::VulkanSurface) window->setVulkanInstance(vulkanInstance());
#else
    Q_UNUSED(window)
#endif
}

std::unique_ptr<QRhi> create(QRhi::Flags flags, QString *name)
{
    std::unique_ptr<QRhi> rhi;
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    QRhiMetalInitParams params;
    rhi.reset(QRhi::create(QRhi::Metal, &params, flags));
#elif defined(Q_OS_WIN)
    QRhiD3D11InitParams params;
    rhi.reset(QRhi::create(QRhi::D3D11, &params, flags));
#elif defined(NYXMAP_RHI_VULKAN)
    if (QVulkanInstance *instance = vulkanInstance()) {
        QRhiVulkanInitParams params;
        params.inst = instance;
        rhi.reset(QRhi::create(QRhi::Vulkan, &params, flags));
    }
#endif
    if (rhi && name) *name = QString::fromLatin1(rhi->backendName());
    return rhi;
}

float ndcFlip(const QRhi *rhi) { return rhi->isYUpInNDC() != rhi->isYUpInFramebuffer() ? -1.0f : 1.0f; }

bool isVirtualDevice(const QRhi *rhi)
{
    const QByteArray name = rhi->driverInfo().deviceName.toLower();
    for (const char *w : {"paravirtual", "warp", "basic render", "llvmpipe", "software", "swiftshader", "lavapipe"})
        if (name.contains(w)) return true;
    return false;
}
} // namespace rhidevice
