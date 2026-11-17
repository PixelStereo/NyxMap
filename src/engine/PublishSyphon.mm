// Syphon publishing (macOS): the picture the QRhi renderer read back is written into the server's IOSurface (BGRA, its first
// row the picture's bottom, as Syphon's servers leave it) that other applications map. Compiled only on macOS
// (NYXMAP_HAS_SYPHON), with the Syphon framework (BSD license); the base server only: no GL context and no Metal shaders.

#include "Publish.h"

#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <Syphon/SyphonServerBase.h>
#import <Syphon/SyphonSubclassing.h>

class SyphonPublisher : public GpuPublisher
{
public:
    ~SyphonPublisher() override
    {
        @autoreleasepool {
            [m_server stop];
            m_server = nil;
        }
    }

    bool start(const QString &name, QString *err) override
    {
        @autoreleasepool {
            NSString *n = [NSString stringWithUTF8String:name.toUtf8().constData()];
            m_server = [[SyphonServerBase alloc] initWithName:n options:nil];
            if (!m_server) {
                if (err) *err = QStringLiteral("The Syphon server failed to start.");
                return false;
            }
        }
        return true;
    }

    void publish(const unsigned char *rgba, int width, int height) override
    {
        if (!m_server || !rgba || width <= 0 || height <= 0) return;
        @autoreleasepool {
            IOSurfaceRef surface = [m_server newSurfaceForWidth:size_t(width) height:size_t(height) options:nil];
            if (!surface) return;
            if (IOSurfaceLock(surface, 0, nullptr) == kIOReturnSuccess) {
                auto *base = static_cast<unsigned char *>(IOSurfaceGetBaseAddress(surface));
                const size_t pitch = IOSurfaceGetBytesPerRow(surface);
                for (int y = 0; y < height; ++y) { // as Syphon's servers leave it: the picture's bottom row first
                    const unsigned char *src = rgba + size_t(y) * size_t(width) * 4;
                    unsigned char *dst = base + size_t(y) * pitch;
                    for (int x = 0; x < width; ++x, src += 4, dst += 4) {
                        dst[0] = src[2];
                        dst[1] = src[1];
                        dst[2] = src[0];
                        dst[3] = src[3];
                    }
                }
                IOSurfaceUnlock(surface, 0, nullptr);
                [m_server publish];
            }
            CFRelease(surface);
            m_clients = m_server.hasClients ? 1 : 0;
        }
    }

    int receivers() const override { return m_clients; }

private:
    SyphonServerBase *m_server = nil;
    int m_clients = 0;
};

std::unique_ptr<GpuPublisher> createSyphonPublisher() { return std::make_unique<SyphonPublisher>(); }
