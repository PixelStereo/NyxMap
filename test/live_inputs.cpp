#include "DeckLink.h"
#include "Engine.h"
#include "LiveInput.h"

#include <QtGlobal>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Live inputs without their hardware: the 4:2:2 conversions of the DeckLink input (known values, a round trip through
// the output's), and the listings and the inputs made when nothing is there (no card, no driver, no sender)
namespace {

// Height selects the conversion matrix and also the number of rows processed.
// Supply every row, even when the assertions inspect just one of them.
std::vector<uint8_t> repeatRow(const uint8_t *row, size_t stride, int height)
{
    std::vector<uint8_t> frame(stride * size_t(height));
    for (int y = 0; y < height; ++y) std::copy(row, row + stride, frame.data() + size_t(y) * stride);
    return frame;
}

// Six pixels in 16 bytes: Cb0 Y0 Cr0 | Y1 Cb2 Y2 | Cr2 Y3 Cb4 | Y4 Cr4 Y5, 10 bits each, little-endian words
std::vector<uint8_t> packV210(const std::vector<int> &ys, const std::vector<int> &cbs, const std::vector<int> &crs, int width)
{
    std::vector<uint8_t> row(size_t(decklink::v210RowBytes(width)), 0);
    for (int x = 0; x < width; x += 6) {
        int s[12] = {};
        for (int k = 0; k < 6; ++k) {
            const size_t i = size_t(std::min(x + k, width - 1)), c = size_t(std::min(x + k, width - 1) / 2);
            const int y = ys[i], cb = cbs[c], cr = crs[c];
            static const int yAt[6] = {1, 3, 5, 7, 9, 11};
            s[yAt[k]] = y;
            if (k % 2 == 0) {
                static const int cbAt[3] = {0, 4, 8}, crAt[3] = {2, 6, 10};
                s[cbAt[k / 2]] = cb;
                s[crAt[k / 2]] = cr;
            }
        }
        uint8_t *d = row.data() + size_t(x / 6) * 16;
        for (int w = 0; w < 4; ++w) {
            const uint32_t v = uint32_t(s[3 * w]) | uint32_t(s[3 * w + 1]) << 10 | uint32_t(s[3 * w + 2]) << 20;
            for (int b = 0; b < 4; ++b) d[4 * w + b] = uint8_t(v >> (8 * b));
        }
    }
    return row;
}

bool near(const uint8_t *px, int r, int g, int b, int tolerance)
{
    return std::abs(px[0] - r) <= tolerance && std::abs(px[1] - g) <= tolerance && std::abs(px[2] - b) <= tolerance && px[3] == 255;
}

} // namespace

int runLiveInputTests(Engine &e)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::printf("FAIL live_inputs: %s\n", why);
        } else {
            std::printf("ok    live_inputs: %s\n", why);
        }
    };

    // 8-bit 4:2:2, video range: white, black, and BT.709's red (HD) and BT.601's (SD)
    {
        const uint8_t row[8] = {128, 235, 128, 16, 128, 16, 128, 235}; // Cb Y0 Cr Y1: white, black; black, white
        std::vector<uint8_t> output(size_t(1080) * 16);
        uint8_t *out = output.data();
        const auto picture = repeatRow(row, 8, 1080);
        decklink::uyvyToRgba(picture.data(), 8, 4, 1080, out, 16);
        check(near(out, 255, 255, 255, 0) && near(out + 4, 0, 0, 0, 0) && near(out + 8, 0, 0, 0, 0) && near(out + 12, 255, 255, 255, 0),
              "UYVY white and black");
        const uint8_t red709[4] = {102, 63, 240, 63}, red601[4] = {90, 81, 240, 81};
        const auto hdRed = repeatRow(red709, 4, 1080), sdRed = repeatRow(red601, 4, 576);
        decklink::uyvyToRgba(hdRed.data(), 4, 2, 1080, out, 8);
        check(near(out, 255, 0, 0, 2) && near(out + 4, 255, 0, 0, 2), "UYVY BT.709 red (HD)");
        decklink::uyvyToRgba(sdRed.data(), 4, 2, 576, out, 8);
        check(near(out, 255, 0, 0, 2), "UYVY BT.601 red (SD)");
        const uint8_t odd[8] = {128, 126, 128, 126, 128, 235, 128, 0}; // three pixels: the last one of its pair
        const auto oddPicture = repeatRow(odd, 8, 1080);
        decklink::uyvyToRgba(oddPicture.data(), 8, 3, 1080, out, 12);
        check(near(out, 128, 128, 128, 1) && near(out + 8, 255, 255, 255, 0), "UYVY odd width");
    }

    // A round trip: the output's conversion (RGB to 4:2:2) and the input's, within a few levels (pairs of one color)
    {
        const int colors[][3] = {{255, 255, 255}, {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {200, 120, 40}, {30, 90, 160}};
        for (int height : {1080, 576}) {
            bool all = true;
            for (const auto &c : colors) {
                const uint8_t bgra[8] = {uint8_t(c[2]), uint8_t(c[1]), uint8_t(c[0]), 255, uint8_t(c[2]), uint8_t(c[1]), uint8_t(c[0]), 255};
                // A picture of `height` rows (each conversion chooses its coefficients by it), its first row compared
                std::vector<uint8_t> picture(size_t(8) * size_t(height)), uyvy(size_t(4) * size_t(height));
                for (int y = 0; y < height; ++y) std::copy(bgra, bgra + 8, picture.begin() + y * 8);
                decklink::bgraToUyvy(picture.data(), 8, 2, height, uyvy.data(), 4);
                std::vector<uint8_t> back(size_t(8) * size_t(height));
                decklink::uyvyToRgba(uyvy.data(), 4, 2, height, back.data(), 8);
                const uint8_t *rgba = back.data();
                all = all && near(rgba, c[0], c[1], c[2], 3) && near(rgba + 4, c[0], c[1], c[2], 3);
            }
            check(all, height >= 720 ? "RGB to UYVY to RGB, BT.709" : "RGB to UYVY to RGB, BT.601");
        }
    }

    // 10-bit ('v210'): the same pictures as in 8 bits (samples four times larger), over a partial block (width 8)
    {
        const int width = 8;
        const std::vector<int> ys = {16, 235, 63, 63, 126, 126, 235, 16}, cbs = {128, 102, 128, 128}, crs = {128, 240, 128, 128};
        std::vector<int> ys10, cbs10, crs10;
        for (int v : ys) ys10.push_back(v * 4);
        for (int v : cbs) cbs10.push_back(v * 4);
        for (int v : crs) crs10.push_back(v * 4);
        const std::vector<uint8_t> v210 = packV210(ys10, cbs10, crs10, width);
        check(v210.size() == 128, "v210 row: whole blocks of 48 pixels");
        std::vector<uint8_t> uyvy(size_t(width) * 2);
        for (int x = 0; x < width; x += 2) {
            uyvy[size_t(x) * 2] = uint8_t(cbs[size_t(x / 2)]);
            uyvy[size_t(x) * 2 + 1] = uint8_t(ys[size_t(x)]);
            uyvy[size_t(x) * 2 + 2] = uint8_t(crs[size_t(x / 2)]);
            uyvy[size_t(x) * 2 + 3] = uint8_t(ys[size_t(x) + 1]);
        }
        const auto v210Picture = repeatRow(v210.data(), v210.size(), 1080);
        const auto uyvyPicture = repeatRow(uyvy.data(), uyvy.size(), 1080);
        std::vector<uint8_t> a(size_t(width) * 4 * 1080), b(a.size());
        decklink::v210ToRgba(v210Picture.data(), int(v210.size()), width, 1080, a.data(), width * 4);
        decklink::uyvyToRgba(uyvyPicture.data(), width * 2, width, 1080, b.data(), width * 4);
        bool same = true;
        for (size_t i = 0; i < a.size(); ++i) same = same && std::abs(a[i] - b[i]) <= 1;
        check(same, "v210 as UYVY");
        check(near(a.data(), 0, 0, 0, 0) && near(a.data() + 4, 255, 255, 255, 0) && near(a.data() + 8, 255, 0, 0, 2), "v210 black, white, red");
    }

    // The listings: whatever is there (often nothing on a test machine), each one well formed
    {
        const std::vector<LiveInputInfo> found = listLiveInputs();
        bool formed = true;
        for (const LiveInputInfo &i : found)
            formed = formed && isLiveInputUri(i.uri) && !i.kind.isEmpty() && liveInputKind(i.uri) == i.kind && !i.name.isEmpty();
        check(formed, "inputs listed (none is fine): URIs, kinds, names");
        bool screens = true;
        for (const LiveInputInfo &i : found)
            if (i.kind == QLatin1String("Screen") || i.kind == QLatin1String("Window"))
                screens = screens && (i.uri.startsWith(QLatin1String("input:screen:")) || i.uri.startsWith(QLatin1String("input:window:"))) &&
                          !i.name.contains(QLatin1String("Pinol"), Qt::CaseInsensitive); // never Pinol's own windows
        check(screens, "screens and windows listed (Qt 6.5 / 6.6), not Pinol's");
        check(liveInputKind("input:spout:A") == "Spout" && liveInputKind("input:decklink:p1") == "DeckLink" &&
                  liveInputKind("input:screen:1") == "Screen" && liveInputKind("input:window:Notes") == "Window",
              "the new kinds");
        check(liveInputName("input:window:Notes: a list") == "Notes: a list", "a window's title, colons included");
    }

    // Nothing there: a DeckLink port that is not (no driver, no card), Spout elsewhere than on Windows
    {
        QString err;
        std::unique_ptr<LiveInput> in = createLiveInput("input:decklink:no such port", QString(), &err);
        check(!in && !err.isEmpty(), "DeckLink input without its port: an error");
        decklink::VideoInput video;
        err.clear();
        check(!video.open("no such port", [](const decklink::InputFrame &) {}, &err) && !err.isEmpty() && !video.hasSignal(),
              "DeckLink video input without its port");
        decklink::TimecodeInput tc;
        check(!tc.open("no such port", [](const Timecode &) {}, &err), "DeckLink timecode input without its port");
#ifndef Q_OS_WIN
        err.clear();
        check(!createLiveInput("input:spout:Sender", QString(), &err) && !err.isEmpty(), "Spout: Windows only");
#endif
        const int layer = e.addLayer("live");
        err.clear();
        check(!e.setLayerInput(layer, "input:decklink:no such port", &err) && !err.isEmpty(), "a layer keeps its source");
        for (int k = 0; k < 2; ++k) e.renderFrame();
    }
    return failures;
}
