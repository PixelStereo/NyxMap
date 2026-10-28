// Blackmagic DeckLink outputs (DeckLink.h): the ports, their modes, and the publisher that plays a viewport out.
#include "DeckLink.h"
#include <map>
#include "Publish.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

// ---------------------------------------------------------------------------
// BGRA to 4:2:2 Y'CbCr (also used by the tests, with or without the SDK)
// ---------------------------------------------------------------------------
void decklink::bgraToUyvy(const uint8_t *bgra, int stride, int width, int height, uint8_t *out, int outStride)
{
    // Video range: Y' 16 to 235, Cb Cr 16 to 240 around 128 (coefficients in 1/256ths, as integers)
    const bool hd = height >= 720;
    const int yr = hd ? 47 : 66, yg = hd ? 157 : 129, yb = hd ? 16 : 25;     // Y'  = (yr R + yg G + yb B) / 256 + 16
    const int ur = hd ? -26 : -38, ug = hd ? -87 : -74, ub = 112;            // Cb
    const int vr = 112, vg = hd ? -102 : -94, vb = hd ? -10 : -18;           // Cr
    auto clamp8 = [](int v) { return uint8_t(std::clamp(v, 0, 255)); };
    for (int y = 0; y < height; ++y) {
        const uint8_t *s = bgra + size_t(y) * size_t(stride);
        uint8_t *d = out + size_t(y) * size_t(outStride);
        for (int x = 0; x + 1 < width; x += 2, s += 8, d += 4) {
            const int b0 = s[0], g0 = s[1], r0 = s[2], b1 = s[4], g1 = s[5], r1 = s[6];
            const int r = r0 + r1, g = g0 + g1, b = b0 + b1; // the pair's chroma: their mean
            d[0] = clamp8(((ur * r + ug * g + ub * b + 256) >> 9) + 128);
            d[1] = clamp8(((yr * r0 + yg * g0 + yb * b0 + 128) >> 8) + 16);
            d[2] = clamp8(((vr * r + vg * g + vb * b + 256) >> 9) + 128);
            d[3] = clamp8(((yr * r1 + yg * g1 + yb * b1 + 128) >> 8) + 16);
        }
        if (width & 1) { // an odd width: the last pixel with itself
            const int b0 = s[0], g0 = s[1], r0 = s[2];
            d[0] = clamp8(((ur * r0 + ug * g0 + ub * b0 + 128) >> 8) + 128);
            d[1] = d[3] = clamp8(((yr * r0 + yg * g0 + yb * b0 + 128) >> 8) + 16);
            d[2] = clamp8(((vr * r0 + vg * g0 + vb * b0 + 128) >> 8) + 128);
        }
    }
}

// ---------------------------------------------------------------------------
// 4:2:2 Y'CbCr to RGBA (the inputs; also used by the tests, with or without the SDK)
// ---------------------------------------------------------------------------
namespace {
// Video range to full: R' = 1.164 Y' + 1.793 Cr ... (BT.709; BT.601 below 720 lines), in 1/1024ths. `bits`: 8 or 10
// (the 10-bit samples: their offsets four times larger, the result back to 8 bits)
struct YcbcrToRgb {
    int ky = 1192, kr, kgb, kgr, kb, yOff, cOff, shift, round;
    YcbcrToRgb(int height, int bits)
    {
        const bool hd = height >= 720;
        kr = hd ? 1836 : 1634;
        kgb = hd ? 218 : 401;
        kgr = hd ? 546 : 833;
        kb = hd ? 2163 : 2066;
        yOff = bits == 10 ? 64 : 16;
        cOff = bits == 10 ? 512 : 128;
        shift = bits == 10 ? 12 : 10;
        round = 1 << (shift - 1);
    }
    void operator()(int y, int cb, int cr, uint8_t *d) const
    {
        const int l = ky * (y - yOff) + round;
        cb -= cOff;
        cr -= cOff;
        d[0] = uint8_t(std::clamp((l + kr * cr) >> shift, 0, 255));
        d[1] = uint8_t(std::clamp((l - kgb * cb - kgr * cr) >> shift, 0, 255));
        d[2] = uint8_t(std::clamp((l + kb * cb) >> shift, 0, 255));
        d[3] = 255;
    }
};
} // namespace

void decklink::uyvyToRgba(const uint8_t *uyvy, int stride, int width, int height, uint8_t *out, int outStride)
{
    const YcbcrToRgb rgb(height, 8);
    for (int y = 0; y < height; ++y) {
        const uint8_t *s = uyvy + size_t(y) * size_t(stride);
        uint8_t *d = out + size_t(y) * size_t(outStride);
        int x = 0;
        for (; x + 1 < width; x += 2, s += 4, d += 8) {
            rgb(s[1], s[0], s[2], d);
            rgb(s[3], s[0], s[2], d + 4);
        }
        if (x < width) rgb(s[1], s[0], s[2], d); // an odd width: the last pixel of its pair
    }
}

void decklink::v210ToRgba(const uint8_t *v210, int stride, int width, int height, uint8_t *out, int outStride)
{
    const YcbcrToRgb rgb(height, 10);
    auto word = [](const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
    for (int y = 0; y < height; ++y) {
        const uint8_t *s = v210 + size_t(y) * size_t(stride);
        uint8_t *d = out + size_t(y) * size_t(outStride);
        // Each block of 16 bytes: six pixels. Its samples in order: Cb0 Y0 Cr0 | Y1 Cb2 Y2 | Cr2 Y3 Cb4 | Y4 Cr4 Y5
        for (int x = 0; x < width; x += 6, s += 16) {
            int v[12];
            for (int k = 0; k < 4; ++k) {
                const uint32_t w = word(s + 4 * k);
                v[3 * k] = int(w & 0x3FF);
                v[3 * k + 1] = int((w >> 10) & 0x3FF);
                v[3 * k + 2] = int((w >> 20) & 0x3FF);
            }
            const int ys[6] = {v[1], v[3], v[5], v[7], v[9], v[11]};
            const int cbs[3] = {v[0], v[4], v[8]}, crs[3] = {v[2], v[6], v[10]};
            for (int k = 0; k < 6 && x + k < width; ++k) rgb(ys[k], cbs[k / 2], crs[k / 2], d + size_t(x + k) * 4);
        }
    }
}

const decklink::Mode *decklink::bestMode(const std::vector<Mode> &modes, QSize size, double fps)
{
    auto pick = [&](int w, int h) -> const Mode * {
        const Mode *best = nullptr;
        for (const Mode &m : modes) {
            if (m.width != w || m.height != h) continue;
            // Progressive first, then the closest rate (a whole rate before its 1000/1001 neighbour)
            auto cost = [&](const Mode &x) { return (x.interlaced ? 1000.0 : 0.0) + std::abs(x.fps - fps) + (std::abs(x.fps - std::round(x.fps)) > 0.01 ? 0.05 : 0.0); };
            if (!best || cost(m) < cost(*best)) best = &m;
        }
        return best;
    };
    if (const Mode *m = pick(size.width(), size.height())) return m;
    if (const Mode *m = pick(1920, 1080)) return m;
    return modes.empty() ? nullptr : &modes.front();
}

#ifndef NYXMAP_HAS_DECKLINK

bool decklink::compiledIn() { return false; }
QString decklink::driverVersion() { return {}; }
std::vector<decklink::Port> decklink::outputPorts() { return {}; }
std::vector<decklink::Mode> decklink::outputModes(const QString &) { return {}; }
std::unique_ptr<CpuPublisher> createDeckLinkPublisher() { return nullptr; }
std::vector<decklink::Port> decklink::inputPorts() { return {}; }
void decklink::rescan() {}
int decklink::lookups() { return 0; }
struct decklink::VideoInput::Impl {};
decklink::VideoInput::VideoInput() : d(std::make_unique<Impl>()) {}
decklink::VideoInput::~VideoInput() = default;
bool decklink::VideoInput::open(const QString &, Callback, QString *err)
{
    if (err) *err = QStringLiteral("built without DeckLink");
    return false;
}
QString decklink::VideoInput::status() const { return {}; }
bool decklink::VideoInput::hasSignal() const { return false; }
struct decklink::TimecodeInput::Impl {};
decklink::TimecodeInput::TimecodeInput() : d(std::make_unique<Impl>()) {}
decklink::TimecodeInput::~TimecodeInput() = default;
bool decklink::TimecodeInput::open(const QString &, Callback, QString *err)
{
    if (err) *err = QStringLiteral("built without DeckLink");
    return false;
}
QString decklink::TimecodeInput::status() const { return {}; }

#else

// ---------------------------------------------------------------------------
// The SDK, per platform: strings, booleans, how its objects are made
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#include <windows.h>
#include <oleauto.h>
#include "DeckLinkAPI_h.h"
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "DeckLinkAPI_i.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
using DlBool = BOOL;
using DlInt = LONGLONG;
using DlString = BSTR;
static QString takeString(DlString s)
{
    const QString q = s ? QString::fromWCharArray(s) : QString();
    if (s) SysFreeString(s);
    return q;
}
static void comInit() // each thread that calls the SDK
{
    thread_local bool done = false;
    if (!done) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED); // already in another mode (the interface's thread): fine
        done = true;
    }
}
static IDeckLinkIterator *newIterator()
{
    comInit();
    IDeckLinkIterator *it = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator, reinterpret_cast<void **>(&it))))
        return nullptr;
    return it;
}
static IDeckLinkAPIInformation *newApiInformation()
{
    comInit();
    IDeckLinkAPIInformation *info = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CDeckLinkAPIInformation, nullptr, CLSCTX_ALL, IID_IDeckLinkAPIInformation,
                                reinterpret_cast<void **>(&info))))
        return nullptr;
    return info;
}
#else
#include "DeckLinkAPI.h"
using DlBool = bool;
using DlInt = int64_t;
#if defined(__APPLE__)
using DlString = CFStringRef;
static QString takeString(DlString s)
{
    if (!s) return {};
    const QString q = QString::fromCFString(s);
    CFRelease(s);
    return q;
}
#else
using DlString = const char *;
static QString takeString(DlString s)
{
    if (!s) return {};
    const QString q = QString::fromUtf8(s);
    free(const_cast<char *>(s));
    return q;
}
#endif
static void comInit() {}
static IDeckLinkIterator *newIterator() { return CreateDeckLinkIteratorInstance(); }
static IDeckLinkAPIInformation *newApiInformation() { return CreateDeckLinkAPIInformationInstance(); }
#endif

#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif

// A reference the SDK gave: released when it goes
template <class T> struct Ref {
    T *p = nullptr;
    Ref() = default;
    explicit Ref(T *x) : p(x) {}
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
    Ref(Ref &&o) noexcept : p(std::exchange(o.p, nullptr)) {}
    Ref &operator=(Ref &&o) noexcept
    {
        reset();
        p = std::exchange(o.p, nullptr);
        return *this;
    }
    ~Ref() { reset(); }
    void reset()
    {
        if (p) p->Release();
        p = nullptr;
    }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

template <class T> static Ref<T> query(IUnknown *from, REFIID iid)
{
    void *out = nullptr;
    if (!from || from->QueryInterface(iid, &out) != S_OK) return Ref<T>();
    return Ref<T>(static_cast<T *>(out));
}

static QString modeKey(BMDDisplayMode m)
{
    const char c[5] = {char((m >> 24) & 0xFF), char((m >> 16) & 0xFF), char((m >> 8) & 0xFF), char(m & 0xFF), 0};
    return QString::fromLatin1(c);
}

static BMDDisplayMode modeCode(const QString &key)
{
    const QByteArray b = key.toLatin1();
    if (b.size() != 4) return bmdModeUnknown;
    return BMDDisplayMode((uint32_t(uint8_t(b[0])) << 24) | (uint32_t(uint8_t(b[1])) << 16) | (uint32_t(uint8_t(b[2])) << 8) |
                          uint32_t(uint8_t(b[3])));
}

// A port's lasting identity: its persistent ID (the same after a restart), else its place, else its name
static QString portId(IDeckLink *d, const QString &name)
{
    if (auto attrs = query<IDeckLinkProfileAttributes>(d, IID_IDeckLinkProfileAttributes)) {
        DlInt v = 0;
        if (attrs->GetInt(BMDDeckLinkPersistentID, &v) == S_OK) return QStringLiteral("p%1").arg(qulonglong(v), 0, 16);
        if (attrs->GetInt(BMDDeckLinkTopologicalID, &v) == S_OK) return QStringLiteral("t%1").arg(qulonglong(v), 0, 16);
    }
    return name;
}

// It plays out (input: it takes a signal in)
static bool hasDirection(IDeckLink *d, bool input)
{
    auto attrs = query<IDeckLinkProfileAttributes>(d, IID_IDeckLinkProfileAttributes);
    DlInt io = 0;
    if (attrs && attrs->GetInt(BMDDeckLinkVideoIOSupport, &io) == S_OK)
        return (io & (input ? bmdDeviceSupportsCapture : bmdDeviceSupportsPlayback)) != 0;
    return input ? bool(query<IDeckLinkInput>(d, IID_IDeckLinkInput)) : bool(query<IDeckLinkOutput>(d, IID_IDeckLinkOutput));
}

// Every port that plays out (input: that takes in), with its name and id (fn: true to stop there, keeping the device)
static Ref<IDeckLink> eachPort(const std::function<bool(IDeckLink *, const decklink::Port &)> &fn, bool input = false)
{
    Ref<IDeckLinkIterator> it(newIterator());
    if (!it) return {};
    IDeckLink *raw = nullptr;
    while (it->Next(&raw) == S_OK) {
        Ref<IDeckLink> d(raw);
        if (!hasDirection(d.p, input)) continue;
        DlString s = nullptr;
        QString name = d->GetDisplayName(&s) == S_OK ? takeString(s) : QString();
        if (name.isEmpty()) name = QStringLiteral("DeckLink");
        if (fn(d.p, decklink::Port{portId(d.p, name), name})) return d;
    }
    return {};
}

bool decklink::compiledIn() { return true; }

QString decklink::driverVersion()
{
    Ref<IDeckLinkAPIInformation> info(newApiInformation());
    DlInt v = 0;
    if (!info || info->GetInt(BMDDeckLinkAPIVersion, &v) != S_OK) return {};
    const QString s = QStringLiteral("%1.%2").arg((v >> 24) & 0xFF).arg((v >> 16) & 0xFF);
    return (v >> 8) & 0xFF ? s + QStringLiteral(".%1").arg((v >> 8) & 0xFF) : s;
}

// The ports and modes, looked up once (decklink::rescan looks again): the interface asks for them often
namespace {
std::mutex g_cacheMutex;
bool g_cached = false;
std::vector<decklink::Port> g_outputs, g_inputs;
std::map<QString, std::vector<decklink::Mode>> g_modes;
std::atomic<int> g_lookups{0};

void fillCache() // g_cacheMutex held
{
    if (g_cached) return;
    ++g_lookups;
    g_outputs.clear();
    g_inputs.clear();
    eachPort([&](IDeckLink *, const decklink::Port &p) {
        g_outputs.push_back(p);
        return false;
    });
    eachPort(
        [&](IDeckLink *, const decklink::Port &p) {
            g_inputs.push_back(p);
            return false;
        },
        true);
    g_modes.clear();
    g_cached = true;
}
} // namespace

std::vector<decklink::Port> decklink::outputPorts()
{
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    fillCache();
    return g_outputs;
}

std::vector<decklink::Port> decklink::inputPorts()
{
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    fillCache();
    return g_inputs;
}

void decklink::rescan()
{
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    g_cached = false;
}

int decklink::lookups() { return g_lookups.load(); }

static Ref<IDeckLink> findPort(const QString &id, decklink::Port *found, bool input = false)
{
    return eachPort(
        [&](IDeckLink *, const decklink::Port &p) {
            if (!id.isEmpty() && p.id != id && p.name != id) return false; // empty: the first one
            *found = p;
            return true;
        },
        input);
}

static bool supports(IDeckLinkOutput *o, BMDDisplayMode m, BMDPixelFormat f)
{
    DlBool ok = false;
    BMDDisplayMode actual = bmdModeUnknown;
    return o->DoesSupportVideoMode(bmdVideoConnectionUnspecified, m, f, bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault,
                                   &actual, &ok) == S_OK &&
           ok;
}

static std::vector<decklink::Mode> modesOf(IDeckLinkOutput *o)
{
    std::vector<decklink::Mode> out;
    IDeckLinkDisplayModeIterator *rawIt = nullptr;
    if (o->GetDisplayModeIterator(&rawIt) != S_OK) return out;
    Ref<IDeckLinkDisplayModeIterator> it(rawIt);
    IDeckLinkDisplayMode *raw = nullptr;
    while (it->Next(&raw) == S_OK) {
        Ref<IDeckLinkDisplayMode> m(raw);
        const BMDDisplayMode code = m->GetDisplayMode();
        if (!supports(o, code, bmdFormat8BitBGRA) && !supports(o, code, bmdFormat8BitYUV)) continue;
        decklink::Mode x;
        x.key = modeKey(code);
        DlString s = nullptr;
        x.name = m->GetName(&s) == S_OK ? takeString(s) : x.key;
        x.width = int(m->GetWidth());
        x.height = int(m->GetHeight());
        BMDTimeValue dur = 0;
        BMDTimeScale scale = 0;
        if (m->GetFrameRate(&dur, &scale) == S_OK && dur > 0) x.fps = double(scale) / double(dur);
        const BMDFieldDominance f = m->GetFieldDominance();
        x.interlaced = f == bmdLowerFieldFirst || f == bmdUpperFieldFirst;
        out.push_back(x);
    }
    return out;
}

std::vector<decklink::Mode> decklink::outputModes(const QString &port)
{
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    fillCache();
    if (const auto it = g_modes.find(port); it != g_modes.end()) return it->second;
    bool known = port.isEmpty();
    for (const Port &p : g_outputs) known = known || p.id == port || p.name == port;
    if (!known) return {}; // no such port: the driver is not asked
    Port p;
    Ref<IDeckLink> d = findPort(port, &p);
    auto o = query<IDeckLinkOutput>(d.p, IID_IDeckLinkOutput);
    std::vector<Mode> modes = o ? modesOf(o.p) : std::vector<Mode>{};
    g_modes[port] = modes;
    return modes;
}

// ---------------------------------------------------------------------------
// Publisher: the card's clock asks for each frame (ScheduledFrameCompleted); it gets the latest picture received,
// again if none came since. So the render's rate and the output's never have to match.
// ---------------------------------------------------------------------------
namespace {

class DeckLinkPublisher;

class Callback : public IDeckLinkVideoOutputCallback
{
public:
    explicit Callback(DeckLinkPublisher *owner) : m_owner(owner) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) override
    {
        if (std::memcmp(&iid, &IID_IDeckLinkVideoOutputCallback, 16) == 0) {
            *ppv = static_cast<IDeckLinkVideoOutputCallback *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG n = --m_refs;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE ScheduledFrameCompleted(IDeckLinkVideoFrame *frame, BMDOutputFrameCompletionResult result) override;
    HRESULT STDMETHODCALLTYPE ScheduledPlaybackHasStopped() override { return S_OK; }
    void detach();

private:
    std::mutex m_mutex; // the owner goes: no call after detach()
    DeckLinkPublisher *m_owner;
    std::atomic<ULONG> m_refs{1};
};

class DeckLinkPublisher : public CpuPublisher
{
public:
    ~DeckLinkPublisher() override { stop(); }

    bool start(const QString &name, const PublishSettings &s, QString *err) override
    {
        comInit();
        m_wantMode = s.decklinkMode;
        m_device = findPort(name, &m_port);
        m_output = query<IDeckLinkOutput>(m_device.p, IID_IDeckLinkOutput);
        if (!m_output) {
            if (err) *err = name.isEmpty() ? QStringLiteral("No DeckLink output (is Desktop Video installed?)")
                                           : QStringLiteral("DeckLink output not found (unplugged?)");
            return false;
        }
        m_modes = modesOf(m_output.p);
        if (!m_wantMode.isEmpty()) { // a mode chosen: played out now (black until the first picture)
            const auto it = std::find_if(m_modes.begin(), m_modes.end(), [&](const decklink::Mode &m) { return m.key == m_wantMode; });
            if (it == m_modes.end()) {
                if (err) *err = QStringLiteral("%1 does not play this mode").arg(m_port.name);
                return false;
            }
            bool inUse = false;
            if (play(*it, err, &inUse)) return true;
            if (!inUse) return false;
            // The port may still be held by the publisher this one replaces (a mode change): that one goes in the
            // send thread, before this one's first picture. Played then, as the automatic mode does (an error
            // then, if the port is still in use, shown by liveState)
            m_deferred = *it;
            if (err) err->clear();
            return true;
        }
        return true; // automatic: the mode of the first picture's size and rate
    }

    void send(const CpuFrame &f) override
    {
        if (!m_playing && !m_failed) {
            comInit();
            const decklink::Mode *m = m_deferred ? &*m_deferred
                                                 : decklink::bestMode(m_modes, QSize(f.width, f.height), double(f.fpsN) / std::max(1, f.fpsD));
            QString err;
            if (!m || !play(*m, &err)) {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_failed = true;
                m_error = m ? err : QStringLiteral("%1 has no mode to play").arg(m_port.name);
                return;
            }
        }
        std::lock_guard<std::mutex> lk(m_mutex);
        m_picture.assign(f.bgra.begin(), f.bgra.end());
        m_picW = f.width;
        m_picH = f.height;
        m_picStride = f.stride;
        m_picTc = f.timecode;
        ++m_serial;
    }

    bool liveState(PublishState *st) const override
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_failed) {
            *st = {PublishState::Error, m_error, -1};
            return true;
        }
        if (!m_playing) {
            *st = {PublishState::Ok, QStringLiteral("%1: waiting for the picture").arg(m_port.name), -1};
            return true;
        }
        QString t = QStringLiteral("%1: %2 (%3)").arg(m_port.name, m_mode.name,
                                                     m_format == bmdFormat8BitBGRA ? QStringLiteral("RGB") : QStringLiteral("YUV 4:2:2"));
        if (m_picW && (m_picW != m_mode.width || m_picH != m_mode.height))
            t += QStringLiteral(" — scaled from %1 × %2").arg(m_picW).arg(m_picH);
        if (m_late) t += QStringLiteral(" — %1 late").arg(m_late);
        *st = {PublishState::Ok, t, -1};
        return true;
    }

    // The card is done with `frame`: it gets the latest picture and goes back in the queue
    void completed(IDeckLinkVideoFrame *frame, BMDOutputFrameCompletionResult result)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_playing) return;
        if (result == bmdOutputFrameDisplayedLate || result == bmdOutputFrameDropped) {
            ++m_late;
            BMDTimeValue now = 0;
            double speed = 0;
            if (m_output->GetScheduledStreamTime(m_scale, &now, &speed) == S_OK && m_next < now + m_duration)
                m_next = (now / m_duration + kQueued) * m_duration; // behind: back ahead of the card
        }
        fill(frame);
        m_output->ScheduleVideoFrame(frame, m_next, m_duration, m_scale);
        m_next += m_duration;
    }

private:
    static constexpr int kQueued = 3; // frames ahead of what is shown (the latency, in frames)

    // inUse: false returned because the port would not enable its output (another publisher or application has it)
    bool play(const decklink::Mode &mode, QString *err, bool *inUse = nullptr)
    {
        const BMDDisplayMode code = modeCode(mode.key);
        const BMDPixelFormat format = supports(m_output.p, code, bmdFormat8BitBGRA) ? bmdFormat8BitBGRA : bmdFormat8BitYUV;
        Ref<IDeckLinkDisplayMode> dm;
        {
            IDeckLinkDisplayMode *raw = nullptr;
            if (m_output->GetDisplayMode(code, &raw) != S_OK) {
                if (err) *err = QStringLiteral("%1 does not play %2").arg(m_port.name, mode.name);
                return false;
            }
            dm = Ref<IDeckLinkDisplayMode>(raw);
        }
        BMDTimeValue dur = 0;
        BMDTimeScale scale = 0;
        dm->GetFrameRate(&dur, &scale);
        // With its timecode (ATC): RP188 in HD, VITC in SD; without when the port takes neither
        m_tcFlags = mode.height >= 720 ? bmdVideoOutputRP188 : bmdVideoOutputVITC;
        if (m_output->EnableVideoOutput(code, BMDVideoOutputFlags(m_tcFlags)) != S_OK) m_tcFlags = 0;
        if (!m_tcFlags && m_output->EnableVideoOutput(code, bmdVideoOutputFlagDefault) != S_OK) {
            if (err) *err = QStringLiteral("%1 is in use (another viewport or application?)").arg(m_port.name);
            if (inUse) *inUse = true;
            return false;
        }
        m_enabled = true;
        const int rowBytes = format == bmdFormat8BitBGRA ? mode.width * 4 : ((mode.width + 1) / 2) * 4;
        for (int i = 0; i < kQueued; ++i) {
            IDeckLinkMutableVideoFrame *raw = nullptr;
            if (m_output->CreateVideoFrame(mode.width, mode.height, rowBytes, format, bmdFrameFlagDefault, &raw) != S_OK) {
                if (err) *err = QStringLiteral("%1: no memory for its frames").arg(m_port.name);
                stop();
                return false;
            }
            m_frames.emplace_back(raw);
        }
        std::lock_guard<std::mutex> lk(m_mutex);
        m_mode = mode;
        m_format = format;
        m_rowBytes = rowBytes;
        m_duration = dur;
        m_scale = scale;
        m_callback = new Callback(this);
        m_output->SetScheduledFrameCompletionCallback(m_callback);
        m_next = 0;
        for (auto &f : m_frames) { // black, then the pictures as they come
            fill(f.p);
            m_output->ScheduleVideoFrame(f.p, m_next, m_duration, m_scale);
            m_next += m_duration;
        }
        if (m_output->StartScheduledPlayback(0, m_scale, 1.0) != S_OK) {
            m_failed = true;
            m_error = QStringLiteral("%1 did not start playing").arg(m_port.name);
            return true; // shown as an error, not retried every frame
        }
        m_playing = true;
        return true;
    }

    void stop()
    {
        if (m_callback) m_callback->detach(); // a call on its way returns without touching this
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_playing = false;
        }
        if (m_output) {
            if (m_enabled) {
                BMDTimeValue at = 0;
                m_output->StopScheduledPlayback(0, &at, 0);
                m_output->SetScheduledFrameCompletionCallback(nullptr);
                m_output->DisableVideoOutput();
            }
            m_enabled = false;
        }
        if (m_callback) m_callback->Release();
        m_callback = nullptr;
        m_frames.clear();
        m_output.reset();
        m_device.reset();
    }

    // The latest picture into a frame of the card's (lock held): at its size, else scaled (nearest); black before any
    void fill(IDeckLinkVideoFrame *frame)
    {
        stamp(frame);
        void *bytes = nullptr;
        if (frame->GetBytes(&bytes) != S_OK || !bytes) return;
        uint8_t *dst = static_cast<uint8_t *>(bytes);
        const int w = m_mode.width, h = m_mode.height;
        if (m_picture.empty() || m_picW <= 0 || m_picH <= 0) {
            for (int y = 0; y < h; ++y) {
                uint8_t *row = dst + size_t(y) * size_t(m_rowBytes);
                if (m_format == bmdFormat8BitBGRA) {
                    for (int x = 0; x < w; ++x) std::memcpy(row + x * 4, "\0\0\0\xff", 4);
                } else {
                    for (int x = 0; x < m_rowBytes; x += 4) std::memcpy(row + x, "\x80\x10\x80\x10", 4);
                }
            }
            return;
        }
        const uint8_t *src = m_picture.data();
        int stride = m_picStride;
        if (m_picW != w || m_picH != h) { // another size: nearest pixel (the viewport's size should be the mode's)
            m_scaled.resize(size_t(w) * size_t(h) * 4);
            std::vector<int> xs(static_cast<size_t>(w));
            for (int x = 0; x < w; ++x) xs[size_t(x)] = std::min(m_picW - 1, int((x + 0.5) * m_picW / w));
            for (int y = 0; y < h; ++y) {
                const uint32_t *s = reinterpret_cast<const uint32_t *>(src + size_t(std::min(m_picH - 1, int((y + 0.5) * m_picH / h))) * size_t(stride));
                uint32_t *d = reinterpret_cast<uint32_t *>(m_scaled.data() + size_t(y) * size_t(w) * 4);
                for (int x = 0; x < w; ++x) d[x] = s[xs[size_t(x)]];
            }
            src = m_scaled.data();
            stride = w * 4;
        }
        if (m_format == bmdFormat8BitBGRA) {
            for (int y = 0; y < h; ++y) std::memcpy(dst + size_t(y) * size_t(m_rowBytes), src + size_t(y) * size_t(stride), size_t(w) * 4);
        } else {
            decklink::bgraToUyvy(src, stride, w, h, dst, m_rowBytes);
        }
    }

    // Its timecode (lock held): the picture's when it is new, else one frame on; above 30 frames a second, each
    // timecode twice, the second with the field mark (SMPTE 12-1)
    void stamp(IDeckLinkVideoFrame *frame)
    {
        if (!m_tcFlags || !m_picTc) return;
        IDeckLinkMutableVideoFrame *mut = nullptr;
        for (auto &f : m_frames)
            if (static_cast<IDeckLinkVideoFrame *>(f.p) == frame) mut = f.p;
        if (!mut) return;
        const bool high = m_mode.fps > 30.5;
        bool field = false;
        Timecode tc;
        if (m_serial != m_outSerial) {
            tc = *m_picTc;
            field = high && m_outTc && *m_outTc == tc && !m_outField;
        } else if (m_outTc) {
            if (high && !m_outField) {
                tc = *m_outTc;
                field = true;
            } else {
                tc = Timecode::fromFrames(m_outTc->toFrames() + 1, m_outTc->rate);
            }
        } else {
            return;
        }
        m_outSerial = m_serial;
        m_outTc = tc;
        m_outField = field;
        const BMDTimecodeFlags flags = BMDTimecodeFlags((Timecode::dropFrame(tc.rate) ? bmdTimecodeIsDropFrame : 0) |
                                                        (field ? bmdTimecodeFieldMark : 0));
        auto set = [&](BMDTimecodeFormat f) {
            mut->SetTimecodeFromComponents(f, uint8_t(tc.h), uint8_t(tc.m), uint8_t(tc.s), uint8_t(tc.f), flags);
        };
        if (m_tcFlags & bmdVideoOutputRP188) {
            set(bmdTimecodeRP188LTC);
            set(bmdTimecodeRP188VITC1);
        } else {
            set(bmdTimecodeVITC);
        }
    }

    bool m_outField = false;
    QString m_wantMode;
    std::optional<decklink::Mode> m_deferred; // the mode chosen, played at the first picture (its port was busy)
    decklink::Port m_port;
    std::vector<decklink::Mode> m_modes;
    Ref<IDeckLink> m_device;
    Ref<IDeckLinkOutput> m_output;
    std::vector<Ref<IDeckLinkMutableVideoFrame>> m_frames;
    Callback *m_callback = nullptr;
    bool m_enabled = false;

    mutable std::mutex m_mutex; // the send thread, the card's thread, the states
    bool m_playing = false, m_failed = false;
    QString m_error;
    decklink::Mode m_mode;
    BMDPixelFormat m_format = bmdFormat8BitBGRA;
    int m_rowBytes = 0;
    BMDTimeValue m_duration = 1, m_next = 0;
    BMDTimeScale m_scale = 25;
    std::vector<uint8_t> m_picture, m_scaled;
    std::optional<Timecode> m_picTc, m_outTc; // the picture's timecode, the last one played out
    uint64_t m_serial = 0, m_outSerial = 0;
    int m_tcFlags = 0; // the output carries a timecode (RP188 or VITC)
    int m_picW = 0, m_picH = 0, m_picStride = 0;
    int m_late = 0;
};

HRESULT Callback::ScheduledFrameCompleted(IDeckLinkVideoFrame *frame, BMDOutputFrameCompletionResult result)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_owner) m_owner->completed(frame, result);
    return S_OK;
}

void Callback::detach()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_owner = nullptr;
}

} // namespace

std::unique_ptr<CpuPublisher> createDeckLinkPublisher() { return std::make_unique<DeckLinkPublisher>(); }

// ---------------------------------------------------------------------------
// Inputs: a port opened once (PortCapture) for all that listen to it, its pictures (VideoInput) and its timecode
// (TimecodeInput, ATC: RP188, else VITC), in whatever mode comes in (format detection)
// ---------------------------------------------------------------------------
namespace {
TcRate rateOf(IDeckLinkDisplayMode *m, bool drop)
{
    BMDTimeValue dur = 0;
    BMDTimeScale scale = 0;
    double fps = 25;
    if (m && m->GetFrameRate(&dur, &scale) == S_OK && dur > 0) fps = double(scale) / double(dur);
    while (fps > 31) fps /= 2; // 50p, 59.94p, 60p: the timecode counts pairs of frames
    if (fps < 24.5) return TcRate::Fps24;
    if (fps < 27.5) return TcRate::Fps25;
    return drop || std::abs(fps - 29.97) < 0.01 ? (drop ? TcRate::Fps2997Df : TcRate::Fps30) : TcRate::Fps30;
}

class PortCapture : public IDeckLinkInputCallback
{
public:
    using Listener = std::function<void(const decklink::InputFrame &)>;

    // The port's capture: the one already open, else opened now; null with `err` when it cannot be
    static std::shared_ptr<PortCapture> acquire(const QString &port, QString *err);
    virtual ~PortCapture()
    {
        if (m_started) m_input->StopStreams();
        m_input->SetCallback(nullptr);
        m_input->DisableVideoInput();
    }

    int listen(Listener fn)
    {
        std::lock_guard<std::mutex> lk(m_listenMutex);
        m_listeners.emplace_back(++m_lastId, std::move(fn));
        return m_lastId;
    }
    void unlisten(int id) // no call to it once this returns (a call on its way is waited for)
    {
        std::lock_guard<std::mutex> lk(m_listenMutex);
        m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(), [id](const auto &l) { return l.first == id; }),
                          m_listeners.end());
    }
    QString status() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_framesSeen && !m_signal) return QStringLiteral("%1: no signal").arg(m_port.name);
        return m_status;
    }
    bool hasSignal() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_signal;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) override
    {
        if (std::memcmp(&iid, &IID_IDeckLinkInputCallback, 16) == 0) {
            *ppv = static_cast<IDeckLinkInputCallback *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --m_refs; } // owned by its listeners, not by the SDK
    HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode *mode,
                                                      BMDDetectedVideoInputFormatFlags flags) override
    {
        if (!mode) return S_OK;
        // RGB as 8-bit BGRA; 4:2:2 in 10 bits when it comes so (else, or when the card will not, in 8)
        BMDPixelFormat f = bmdFormat8BitYUV;
        if (flags & bmdDetectedVideoInputRGB444) f = bmdFormat8BitBGRA;
        else if (flags & (bmdDetectedVideoInput10BitDepth | bmdDetectedVideoInput12BitDepth)) f = bmdFormat10BitYUV;
        m_input->PauseStreams();
        if (m_input->EnableVideoInput(mode->GetDisplayMode(), f, bmdVideoInputEnableFormatDetection) != S_OK && f == bmdFormat10BitYUV) {
            f = bmdFormat8BitYUV;
            m_input->EnableVideoInput(mode->GetDisplayMode(), f, bmdVideoInputEnableFormatDetection);
        }
        m_input->FlushStreams();
        m_input->StartStreams();
        DlString s = nullptr;
        const QString name = mode->GetName(&s) == S_OK ? takeString(s) : QString();
        std::lock_guard<std::mutex> lk(m_mutex);
        m_rate = rateOf(mode, false);
        m_status = QStringLiteral("%1: %2").arg(m_port.name, name);
        if (f == bmdFormat8BitBGRA) m_status += QStringLiteral(" (RGB)");
        else if (f == bmdFormat10BitYUV) m_status += QStringLiteral(" (10-bit)");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame *frame, IDeckLinkAudioInputPacket *) override
    {
        if (!frame) return S_OK;
        decklink::InputFrame in;
        in.timecode = timecodeOf(frame);
        const bool signal = !(frame->GetFlags() & bmdFrameHasNoInputSource);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_signal = signal;
            m_framesSeen = true;
        }
        void *bytes = nullptr;
        if (signal && frame->GetBytes(&bytes) == S_OK && bytes) {
            const BMDPixelFormat pf = frame->GetPixelFormat();
            if (pf == bmdFormat8BitYUV || pf == bmdFormat10BitYUV || pf == bmdFormat8BitBGRA) {
                in.format = pf == bmdFormat8BitYUV    ? decklink::InputFrame::Uyvy
                            : pf == bmdFormat10BitYUV ? decklink::InputFrame::V210
                                                      : decklink::InputFrame::Bgra;
                in.data = static_cast<const uint8_t *>(bytes);
                in.width = int(frame->GetWidth());
                in.height = int(frame->GetHeight());
                in.rowBytes = int(frame->GetRowBytes());
            }
        }
        std::lock_guard<std::mutex> lk(m_listenMutex);
        for (const auto &l : m_listeners) l.second(in);
        return S_OK;
    }

private:
    PortCapture(Ref<IDeckLink> device, Ref<IDeckLinkInput> input, const decklink::Port &port)
        : m_device(std::move(device)), m_input(std::move(input)), m_port(port), m_status(port.name)
    {
    }

    bool start(QString *err)
    {
        m_input->SetCallback(this);
        // Any mode to begin with: the format detection switches to the one that comes in
        if (m_input->EnableVideoInput(bmdModeHD1080i50, bmdFormat8BitYUV, bmdVideoInputEnableFormatDetection) != S_OK &&
            m_input->EnableVideoInput(bmdModeHD1080i50, bmdFormat8BitYUV, bmdVideoInputFlagDefault) != S_OK) {
            if (err) *err = QStringLiteral("%1 is in use").arg(m_port.name);
            m_input->SetCallback(nullptr);
            return false;
        }
        if (m_input->StartStreams() != S_OK) {
            if (err) *err = QStringLiteral("%1 did not start").arg(m_port.name);
            m_input->SetCallback(nullptr);
            m_input->DisableVideoInput();
            return false;
        }
        m_started = true;
        return true;
    }

    // Its ATC: RP188, else VITC (above 30 frames a second, the frame count halved: the timecode counts pairs)
    std::optional<Timecode> timecodeOf(IDeckLinkVideoInputFrame *frame) const
    {
        IDeckLinkTimecode *t = nullptr;
        if (frame->GetTimecode(bmdTimecodeRP188Any, &t) != S_OK || !t)
            if (frame->GetTimecode(bmdTimecodeVITC, &t) != S_OK || !t) return std::nullopt;
        uint8_t h = 0, m = 0, sec = 0, f = 0;
        const bool ok = t->GetComponents(&h, &m, &sec, &f) == S_OK;
        const bool drop = (t->GetFlags() & bmdTimecodeIsDropFrame) != 0;
        t->Release();
        if (!ok) return std::nullopt;
        Timecode tc;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            tc = Timecode{h, m, sec, f, drop ? TcRate::Fps2997Df : m_rate};
        }
        if (tc.f >= Timecode::nominalFps(tc.rate)) tc.f /= 2;
        if (!tc.valid()) return std::nullopt;
        return tc;
    }

    Ref<IDeckLink> m_device;
    Ref<IDeckLinkInput> m_input;
    decklink::Port m_port;
    bool m_started = false;
    std::atomic<ULONG> m_refs{1};
    mutable std::mutex m_mutex; // the mode coming in (the card's thread), the states
    QString m_status;
    TcRate m_rate = TcRate::Fps25;
    bool m_signal = false, m_framesSeen = false;
    std::mutex m_listenMutex; // held while they are called: one that goes waits for its call to end
    std::vector<std::pair<int, Listener>> m_listeners;
    int m_lastId = 0;
};

// The ports open, by id. One closes with this held: the same port opened again just then waits for it to be free
std::mutex g_capturesMutex;
std::map<QString, std::weak_ptr<PortCapture>> g_captures;

std::shared_ptr<PortCapture> PortCapture::acquire(const QString &port, QString *err)
{
    comInit();
    decklink::Port p;
    Ref<IDeckLink> device = findPort(port, &p, true);
    Ref<IDeckLinkInput> input = query<IDeckLinkInput>(device.p, IID_IDeckLinkInput);
    if (!input) {
        if (err) *err = port.isEmpty() ? QStringLiteral("no DeckLink input") : QStringLiteral("DeckLink input not found");
        return nullptr;
    }
    std::lock_guard<std::mutex> lk(g_capturesMutex);
    if (std::shared_ptr<PortCapture> open = g_captures[p.id].lock()) return open;
    auto *c = new PortCapture(std::move(device), std::move(input), p);
    if (!c->start(err)) {
        delete c;
        return nullptr;
    }
    const QString id = p.id;
    std::shared_ptr<PortCapture> shared(c, [id](PortCapture *x) {
        std::lock_guard<std::mutex> lk(g_capturesMutex);
        delete x;
        const auto it = g_captures.find(id);
        if (it != g_captures.end() && it->second.expired()) g_captures.erase(it);
    });
    g_captures[id] = shared;
    return shared;
}
} // namespace

struct decklink::VideoInput::Impl {
    std::shared_ptr<PortCapture> capture;
    int listener = 0;
};

decklink::VideoInput::VideoInput() : d(std::make_unique<Impl>()) {}

decklink::VideoInput::~VideoInput()
{
    if (d->capture) d->capture->unlisten(d->listener);
}

bool decklink::VideoInput::open(const QString &port, Callback cb, QString *err)
{
    comInit();
    d->capture = PortCapture::acquire(port, err);
    if (!d->capture) return false;
    d->listener = d->capture->listen(std::move(cb));
    return true;
}

QString decklink::VideoInput::status() const { return d->capture ? d->capture->status() : QString(); }

bool decklink::VideoInput::hasSignal() const { return d->capture && d->capture->hasSignal(); }

struct decklink::TimecodeInput::Impl {
    std::shared_ptr<PortCapture> capture;
    int listener = 0;
};

decklink::TimecodeInput::TimecodeInput() : d(std::make_unique<Impl>()) {}

decklink::TimecodeInput::~TimecodeInput()
{
    if (d->capture) d->capture->unlisten(d->listener);
}

bool decklink::TimecodeInput::open(const QString &port, Callback cb, QString *err)
{
    comInit();
    d->capture = PortCapture::acquire(port, err);
    if (!d->capture) return false;
    d->listener = d->capture->listen([cb = std::move(cb)](const InputFrame &f) {
        if (cb && f.timecode) cb(*f.timecode);
    });
    return true;
}

QString decklink::TimecodeInput::status() const { return d->capture ? d->capture->status() : QString(); }

#endif // NYXMAP_HAS_DECKLINK
