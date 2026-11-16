// Syphon inputs (macOS): the servers other applications publish, received on the GPU (no CPU copy).
#include "LiveInput.h"

#import <Foundation/Foundation.h>
#import <Syphon/SyphonMetalClient.h>
#import <Syphon/SyphonOpenGLClient.h>
#import <Syphon/SyphonOpenGLImage.h>
#import <Syphon/SyphonServerDirectory.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
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
            [m_client stop];
            m_client = nil;
            [m_metal stop];
            m_metal = nil;
            m_image = nil;
        }
        if (m_fbo[0] && QOpenGLContext::currentContext()) gl()->glDeleteFramebuffers(2, m_fbo);
    }
    bool update(Texture2D &tex) override
    {
        @autoreleasepool {
            if (!m_client) connect(); // with the render thread's context: the one the frames are shared with
            if (!m_client || ![m_client hasNewFrame]) return false;
            SyphonOpenGLImage *img = [m_client newFrameImage];
            if (!img) return false;
            const int w = int(img.textureSize.width), h = int(img.textureSize.height);
            if (w <= 0 || h <= 0) return false;
            if (tex.w != w || tex.h != h || !tex.tex) tex.upload(nullptr, w, h);
            auto f = gl();
            if (!m_fbo[0]) f->glGenFramebuffers(2, m_fbo);
            f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo[0]);
            f->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, img.textureName, 0);
            f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo[1]);
            f->glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.tex, 0);
            // Syphon's rows go up, the layers' textures down (their first row is the top)
            f->glBlitFramebuffer(0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            m_seen = true;
            return true;
        }
    }
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
    void connect()
    {
        CGLContextObj ctx = CGLGetCurrentContext();
        if (!ctx) return;
        for (NSDictionary *d in [[SyphonServerDirectory sharedDirectory] servers]) {
            if (serverName(d) != m_name) continue;
            m_client = [[SyphonOpenGLClient alloc] initWithServerDescription:d context:ctx options:nil newFrameHandler:nil];
            return;
        }
    }
    QString m_name;
    SyphonOpenGLClient *m_client = nil;
    SyphonMetalClient *m_metal = nil;
    id<MTLTexture> m_image = nil;
    std::unique_ptr<QRhiTexture> m_rhiTexture;
    GLuint m_fbo[2] = {0, 0};
    bool m_seen = false;
};

std::unique_ptr<LiveInput> createSyphonInput(const QString &name, QString *)
{
    return std::make_unique<SyphonInput>(name);
}
