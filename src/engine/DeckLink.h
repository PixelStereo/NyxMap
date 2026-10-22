#pragma once
// Blackmagic DeckLink (SDI / HDMI cards, UltraStudio): the output ports this machine has, the video modes each one
// plays, and a viewport's picture sent to one of them (createDeckLinkPublisher, Publish.h). And the inputs: the
// pictures and the timecode coming in (a live input, LiveInput.h; ATC, TimecodeIo.h), both from the same port at once.
//
// The SDK's headers are in the source tree (third_party/decklink, Boost license); the driver (Desktop Video) is loaded
// at run time: without it there is simply no port. The ports and their modes are looked up once and kept: asking the
// driver over and over (the interface did every two seconds) made and dropped its IOKit connections each time, and a
// crash in macOS's libdispatch came with it. rescan() looks again (a card plugged in, an UltraStudio connected).

#include "Timecode.h"

#include <QSize>
#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace decklink {

struct Port {
    QString id;   // persistent: its persistent ID (else its topological ID, else its name)
    QString name; // "DeckLink Duo (1)"
};

struct Mode {
    QString key;  // its display mode code, four characters ("Hp25", "4k60"…)
    QString name; // "1080p25", "2160p59.94"…
    int width = 0, height = 0;
    double fps = 0; // frames (an interlaced mode: its frames, two fields each)
    bool interlaced = false;
};

bool compiledIn();                                 // built with the DeckLink SDK
QString driverVersion();                           // "12.4" (empty: no driver)
std::vector<Port> outputPorts();                   // the ports that play out (none without a driver or a card)
std::vector<Mode> outputModes(const QString &port); // the modes it plays (empty: no such port)
std::vector<Port> inputPorts();                    // the ports that take a signal in
void rescan();                                     // the ports and modes looked up again at the next call
int lookups();                                     // how many times the driver was asked for its ports (tests)
// The mode for a picture of `size` at `fps` among `modes`: the same size, progressive, the closest rate (else 1080p)
const Mode *bestMode(const std::vector<Mode> &modes, QSize size, double fps);

// BGRA rows (top to bottom) to 8-bit 4:2:2 Y'CbCr ('2vuy': Cb Y0 Cr Y1), BT.709 (BT.601 below 720 lines), video range
void bgraToUyvy(const uint8_t *bgra, int stride, int width, int height, uint8_t *out, int outStride);

// 8-bit 4:2:2 Y'CbCr ('2vuy': Cb Y0 Cr Y1) or 10-bit ('v210': six pixels in four little-endian words) rows to RGBA
// (top to bottom), BT.709 (BT.601 below 720 lines), video range; each pair of pixels with its own chroma
void uyvyToRgba(const uint8_t *uyvy, int stride, int width, int height, uint8_t *out, int outStride);
void v210ToRgba(const uint8_t *v210, int stride, int width, int height, uint8_t *out, int outStride);
inline int v210RowBytes(int width) { return ((width + 47) / 48) * 128; } // the SDK's: whole blocks of 48 pixels

// A picture that came in (valid during the call only): its rows top to bottom, as the card gives them
struct InputFrame {
    enum Format { Uyvy, V210, Bgra }; // 8-bit 4:2:2, 10-bit 4:2:2, 8-bit RGB (an RGB 4:4:4 signal)
    Format format = Uyvy;
    const uint8_t *data = nullptr; // null: no signal (the timecode may still be there)
    int width = 0, height = 0, rowBytes = 0;
    std::optional<Timecode> timecode; // its ATC (RP188, else VITC)
};

// The pictures an input takes, in whatever mode comes in (format detection): 4:2:2 as 8 or 10 bits as detected, RGB
// when the signal is. The port is opened once for its pictures and its timecode (a TimecodeInput on the same port)
class VideoInput
{
public:
    using Callback = std::function<void(const InputFrame &)>; // each frame (the card's thread: return quickly)
    VideoInput();
    ~VideoInput(); // no call after it
    bool open(const QString &port, Callback cb, QString *err); // empty: the first input
    QString status() const;                                    // the port and the mode coming in
    bool hasSignal() const;                                    // a picture is coming in

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// The timecode an input carries (ATC: RP188, else VITC), in whatever mode comes in
class TimecodeInput
{
public:
    using Callback = std::function<void(const Timecode &)>; // each frame's (the card's thread)
    TimecodeInput();
    ~TimecodeInput();
    bool open(const QString &port, Callback cb, QString *err); // empty: the first input
    QString status() const;                                    // the port and the mode coming in

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace decklink
