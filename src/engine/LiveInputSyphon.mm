// Syphon inputs (macOS): the servers other applications publish, received as Metal textures of QRhi's device (no CPU copy).
#include "LiveInput.h"

#import <Foundation/Foundation.h>
#import <Syphon/SyphonMetalClient.h>
#import <Syphon/SyphonServerDirectory.h>
#import <Metal/Metal.h>
#include <rhi/qrhi.h>

static QString serverName(NSDictionary *d)
{
    NSString *app = d[SyphonServerDescriptionAppNameKey], *name = d[SyphonServerDescriptionNameKey];
    const QString a = app ? QString::fromNSString(app) : QString(), n = name ? QString::fromNSString(name) : QString();
    if (a.isEmpty()) return n;
    return n.isEmpty() ? a : a + QStringLiteral(" - ") + n;
}

std::vector<LiveInputInfo> listSyphonInputs()
{
    std::vector<LiveInputInfo> out;
    @autoreleasepool {
        for (NSDictionary *d in [[SyphonServerDirectory sharedDirectory] servers]) {
            const QString name = serverName(d);
            out.push_back({QStringLiteral("input:syphon:") + name, name, QStringLiteral("Syphon")});
        }
    }
    return out;
}

class SyphonInput : public LiveInput
{
public:
    explicit SyphonInput(const QString &name) : m_name(name) {}
    ~SyphonInput() override
    {
        @autoreleasepool {
            [m_metal stop];
            m_metal = nil;
            m_image = nil;
        }
    }
    bool update(Texture2D &) override { return false; } // the picture comes with takeTexture()
    // QRhi (Metal): the server's IOSurface as a texture of QRhi's device, no copy
    QRhiTexture *takeTexture(QRhi *rhi, QSize *size, bool *flipped) override
    {
        @autoreleasepool {
            if (!rhi || rhi->backend() != QRhi::Metal) return nullptr;
            if (!m_metal) {
                auto *handles = static_cast<const QRhiMetalNativeHandles *>(rhi->nativeHandles());
                id<MTLDevice> device = (id<MTLDevice>)handles->dev;
                for (NSDictionary *d in [[SyphonServerDirectory sharedDirectory] servers]) {
                    if (serverName(d) != m_name) continue;
                    m_metal = [[SyphonMetalClient alloc] initWithServerDescription:d device:device options:nil newFrameHandler:nil];
                    break;
                }
            }
            if (m_metal && [m_metal hasNewFrame]) {
                id<MTLTexture> image = [m_metal newFrameImage];
                if (image && image != m_image) {
                    m_image = image;
                    const QSize s(int(image.width), int(image.height));
                    m_rhiTexture.reset(rhi->newTexture(QRhiTexture::BGRA8, s));
                    if (!m_rhiTexture->createFrom({quint64(quintptr((__bridge void *)image)), 0})) m_rhiTexture.reset();
                }
            }
            if (!m_rhiTexture) return nullptr;
            m_seen = true;
            *size = m_rhiTexture->pixelSize();
            *flipped = false; // an IOSurface from a GL server: its first row is the picture's bottom, as ours
            return m_rhiTexture.get();
        }
    }
    QString status() const override { return m_seen ? QString() : QStringLiteral("Waiting for the Syphon server \"%1\"").arg(m_name); }

private:
    QString m_name;
    SyphonMetalClient *m_metal = nil;
    id<MTLTexture> m_image = nil;
    std::unique_ptr<QRhiTexture> m_rhiTexture;
    bool m_seen = false;
};

std::unique_ptr<LiveInput> createSyphonInput(const QString &name, QString *)
{
    return std::make_unique<SyphonInput>(name);
}
