#pragma once
// Spout inputs (Windows): the senders other applications share, received on the GPU through SpoutDX (LiveInputSpout.cpp,
// compiled only with NYXMAP_HAS_SPOUT).

#include <QSize>
#include <QString>
#include <QStringList>
#include <memory>

QStringList spoutSenders(); // the senders there now

// Through QRhi (Direct3D 11): the sender's frames copied on the GPU, on QRhi's own device, into a texture QRhi samples
class QRhi;
class QRhiTexture;
class SpoutReceiveRhi
{
public:
    virtual ~SpoutReceiveRhi() = default;
    // Render thread: the sender's newest picture (nullptr: none yet, or no such sender). Valid until the next call.
    // Direct3D's first row is the picture's top
    virtual QRhiTexture *receive(QRhi *rhi, QSize *size) = 0;
};
std::unique_ptr<SpoutReceiveRhi> createSpoutReceiveRhi(const QString &sender);
