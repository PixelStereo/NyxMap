// Spout inputs (Windows): the senders other applications share, copied on the GPU (GL/DX interop) into the layer's
// texture. Compiled only on Windows (PINOL_HAS_SPOUT), with the Spout2 SDK (BSD license).
// This file deliberately includes no Qt OpenGL header (Spout provides its own).

#include "SpoutInput.h"

#include "SpoutReceiver.h"

#include <QMutex>
#include <chrono>
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

class SpoutReceiveGl : public SpoutReceive
{
public:
    explicit SpoutReceiveGl(const QString &sender) : m_name(sender.toUtf8())
    {
        m_receiver.SetReceiverName(m_name.constData()); // that sender only (waited for when it is not there)
    }
    ~SpoutReceiveGl() override { m_receiver.ReleaseReceiver(); }

    Result receive(unsigned int texture, int w, int h) override
    {
        // Not there: looked for again twice a second, not at every frame (Spout waits a little each time it lets go)
        const auto now = std::chrono::steady_clock::now();
        if (m_gone && now < m_retry) return Gone;
        // Spout's texture is DirectX's (first row at the top), the layers' are OpenGL's (first row at the bottom)
        const bool ok = m_receiver.ReceiveTexture(texture, GL_TEXTURE_2D, true, 0);
        if (!ok || !m_receiver.IsConnected()) {
            m_gone = true;
            m_retry = now + std::chrono::milliseconds(500);
            return Gone;
        }
        m_gone = false;
        m_w = int(m_receiver.GetSenderWidth());
        m_h = int(m_receiver.GetSenderHeight());
        if (m_receiver.IsUpdated() || !texture || w != m_w || h != m_h) return m_w > 0 && m_h > 0 ? Resize : Nothing;
        return m_receiver.IsFrameNew() ? Received : Nothing;
    }
    int width() const override { return m_w; }
    int height() const override { return m_h; }

private:
    QByteArray m_name;
    SpoutReceiver m_receiver;
    int m_w = 0, m_h = 0;
    bool m_gone = false;
    std::chrono::steady_clock::time_point m_retry;
};

std::unique_ptr<SpoutReceive> createSpoutReceive(const QString &sender) { return std::make_unique<SpoutReceiveGl>(sender); }
