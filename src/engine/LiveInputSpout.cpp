// Spout inputs (Windows): the senders other applications share, copied on the GPU, on QRhi's Direct3D 11 device, into a
// texture QRhi samples. Compiled only on Windows (NYXMAP_HAS_SPOUT), with the Spout2 SDK (BSD license).

#include "SpoutInput.h"

#include "SpoutDX.h"

#include <d3d11.h>
#include <rhi/qrhi.h>

#include <QMutex>
#include <set>
#include <string>

QStringList spoutSenders()
{
    // One list for the program's life (it keeps the shared memory of the names open)
    static spoutSenderNames names;
    static QMutex mutex;
    QMutexLocker lk(&mutex);
    QStringList out;
    if (names.GetSenderCount() <= 0) return out; // also drops the senders that closed without saying so
    std::set<std::string> set;
    if (!names.GetSenderNames(&set)) return out;
    for (const std::string &s : set)
        if (!s.empty()) out << QString::fromUtf8(s.c_str());
    return out;
}

// --- Through QRhi: SpoutDX on QRhi's own Direct3D 11 device, into a texture of ours that QRhi samples
class SpoutReceiveDx : public SpoutReceiveRhi
{
public:
    explicit SpoutReceiveDx(const QString &sender) : m_name(sender.toUtf8()) {}
    ~SpoutReceiveDx() override
    {
        m_rhiTexture.reset();
        if (m_texture) m_texture->Release();
        m_receiver.ReleaseReceiver();
        m_receiver.CloseDirectX11();
    }
    QRhiTexture *receive(QRhi *rhi, QSize *size) override
    {
        if (!rhi || rhi->backend() != QRhi::D3D11) return nullptr;
        if (!m_open) {
            const auto *handles = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
            if (!handles || !handles->dev || !m_receiver.OpenDirectX11(static_cast<ID3D11Device *>(handles->dev))) return nullptr;
            m_receiver.SetReceiverName(m_name.constData()); // that sender only (waited for when it is not there)
            m_open = true;
        }
        // Not there: looked for again twice a second, not at every frame (Spout waits a little each time it lets go)
        const auto now = std::chrono::steady_clock::now();
        if (m_gone && now < m_retry) return m_rhiTexture.get();
        if (!m_receiver.ReceiveTexture(&m_texture) || !m_receiver.IsConnected()) {
            m_gone = true;
            m_retry = now + std::chrono::milliseconds(500);
            return m_rhiTexture.get(); // its last picture is kept
        }
        m_gone = false;
        if (m_receiver.IsUpdated()) { // a new sender, or another size: a texture of ours at its size and format
            const DXGI_FORMAT format = m_receiver.GetSenderFormat();
            const int w = int(m_receiver.GetSenderWidth()), h = int(m_receiver.GetSenderHeight());
            if (w <= 0 || h <= 0 || (format != DXGI_FORMAT_B8G8R8A8_UNORM && format != DXGI_FORMAT_R8G8B8A8_UNORM)) return nullptr;
            m_rhiTexture.reset();
            if (m_texture) m_texture->Release(), m_texture = nullptr;
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = UINT(w);
            d.Height = UINT(h);
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = format;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const auto *handles = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
            if (FAILED(static_cast<ID3D11Device *>(handles->dev)->CreateTexture2D(&d, nullptr, &m_texture))) return nullptr;
            m_rhiTexture.reset(rhi->newTexture(format == DXGI_FORMAT_B8G8R8A8_UNORM ? QRhiTexture::BGRA8 : QRhiTexture::RGBA8, QSize(w, h)));
            if (!m_rhiTexture->createFrom({quint64(quintptr(m_texture)), 0})) m_rhiTexture.reset();
        }
        if (!m_rhiTexture) return nullptr;
        *size = m_rhiTexture->pixelSize();
        return m_rhiTexture.get();
    }

private:
    QByteArray m_name;
    spoutDX m_receiver;
    ID3D11Texture2D *m_texture = nullptr; // ours (SpoutDX copies the sender's frames into it)
    std::unique_ptr<QRhiTexture> m_rhiTexture;
    bool m_open = false, m_gone = false;
    std::chrono::steady_clock::time_point m_retry;
};

std::unique_ptr<SpoutReceiveRhi> createSpoutReceiveRhi(const QString &sender) { return std::make_unique<SpoutReceiveDx>(sender); }
