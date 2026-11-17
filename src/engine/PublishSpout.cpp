// Spout publishing (Windows): the picture the QRhi renderer read back is sent through SpoutDX (a Direct3D 11 shared
// texture other applications open). Compiled only on Windows (NYXMAP_HAS_SPOUT), with the Spout2 SDK (BSD license).

#include "Publish.h"

#include "SpoutDX.h"

#include <vector>

class SpoutPublisher : public GpuPublisher
{
public:
    ~SpoutPublisher() override { m_sender.ReleaseSender(); }

    bool start(const QString &name, QString *err) override
    {
        if (!m_sender.OpenDirectX11()) {
            if (err) *err = QStringLiteral("Spout: no Direct3D 11 device.");
            return false;
        }
        m_name = name.toUtf8();
        m_sender.SetSenderName(m_name.constData());
        return true;
    }

    void publish(const unsigned char *rgba, int width, int height) override
    {
        if (!rgba || width <= 0 || height <= 0) return;
        // Direct3D's first row is the top, its pixels BGRA: the picture's bottom row comes first, RGBA
        m_pixels.resize(size_t(width) * size_t(height) * 4);
        for (int y = 0; y < height; ++y) {
            const unsigned char *src = rgba + size_t(height - 1 - y) * size_t(width) * 4;
            unsigned char *dst = m_pixels.data() + size_t(y) * size_t(width) * 4;
            for (int x = 0; x < width; ++x, src += 4, dst += 4) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
            }
        }
        m_sender.SendImage(m_pixels.data(), unsigned(width), unsigned(height));
    }

private:
    spoutDX m_sender;
    QByteArray m_name;
    std::vector<unsigned char> m_pixels;
};

std::unique_ptr<GpuPublisher> createSpoutPublisher() { return std::make_unique<SpoutPublisher>(); }
