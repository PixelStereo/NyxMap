#include "RhiDevice.h"

#include <QtGlobal>
#if !defined(Q_OS_MACOS) && !defined(Q_OS_IOS) && !defined(Q_OS_WIN) && QT_CONFIG(vulkan)
#define NYXMAP_RHI_VULKAN
#include <QVulkanInstance>
#endif

namespace rhidevice {

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
    // One Vulkan instance for the process's life (every QRhi and every window of the process use it)
    static QVulkanInstance *instance = [] {
        auto *inst = new QVulkanInstance;
        inst->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (!inst->create()) {
            delete inst;
            return static_cast<QVulkanInstance *>(nullptr);
        }
        return inst;
    }();
    if (instance) {
        QRhiVulkanInitParams params;
        params.inst = instance;
        rhi.reset(QRhi::create(QRhi::Vulkan, &params, flags));
    }
#endif
    if (rhi && name) *name = QString::fromLatin1(rhi->backendName());
    return rhi;
}

float ndcFlip(const QRhi *rhi) { return rhi->isYUpInNDC() != rhi->isYUpInFramebuffer() ? -1.0f : 1.0f; }

} // namespace rhidevice
