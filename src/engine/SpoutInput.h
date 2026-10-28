#pragma once
// Spout inputs (Windows): the senders other applications share, received on the GPU (LiveInputSpout.cpp, compiled
// only with NYXMAP_HAS_SPOUT). This file depends on no OpenGL header (Spout brings its own): the texture is passed by
// its name, and made by the caller at the size asked for (LiveInput.cpp).

#include <QString>
#include <QStringList>
#include <memory>

QStringList spoutSenders(); // the senders there now

class SpoutReceive
{
public:
    enum Result {
        Nothing,  // no new frame
        Received, // the newest frame is in the texture
        Resize,   // the sender is new or has another size: the texture made at width() × height(), then call again
        Gone,     // no such sender (closed, not started yet): the texture is left as it was
    };
    virtual ~SpoutReceive() = default;
    // Render thread, context current: the sender's newest frame into `texture` (2D, RGBA8, w × h; 0: none yet)
    virtual Result receive(unsigned int texture, int w, int h) = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
};

std::unique_ptr<SpoutReceive> createSpoutReceive(const QString &sender);
