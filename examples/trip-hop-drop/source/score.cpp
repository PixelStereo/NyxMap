// The soundtrack of Trip Hop Drop: the track, cut on its own beat and sent through effects, under kicks, 808 basses, snares
// and hats made with NyxMap's own Faust DSP (src/engine/generated). Rendered ahead, so every hit falls on its sample.
//   score <track.f32 stereo 44100> <out.f32 stereo> <cues.json>
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "generated/NyxFaustSynth.h"
#include "generated/NyxFxBandPass.h"
#include "generated/NyxFxCompressor.h"
#include "generated/NyxFxDelay.h"
#include "generated/NyxFxDistortion.h"
#include "generated/NyxFxHighPass.h"
#include "generated/NyxFxLowPass.h"
#include "generated/NyxFxReverb.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

static const int SR = 44100, BLOCK = 16;
static const double BPM = 78, BEAT = 60 / BPM, BAR = 4 * BEAT, S16 = BEAT / 4, PHASE = 0.06; // PHASE: beat 0 of the track
static const double SWING = 0.10;                                                              // even 16ths, late by this much of a 16th
static double beats(double b) { return b * BEAT; }
static double step16(int bar, int s) { return bar * BAR + (s + ((s & 1) ? SWING : 0)) * S16; }

struct Collect : UI {
    std::map<std::string, FAUSTFLOAT *> at;
    void openTabBox(const char *) override {}
    void openHorizontalBox(const char *) override {}
    void openVerticalBox(const char *) override {}
    void closeBox() override {}
    void addButton(const char *l, FAUSTFLOAT *z) override { at[l] = z; }
    void addCheckButton(const char *l, FAUSTFLOAT *z) override { at[l] = z; }
    void addVerticalSlider(const char *l, FAUSTFLOAT *z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { at[l] = z; }
    void addHorizontalSlider(const char *l, FAUSTFLOAT *z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { at[l] = z; }
    void addNumEntry(const char *l, FAUSTFLOAT *z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { at[l] = z; }
    void addHorizontalBargraph(const char *, FAUSTFLOAT *, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char *, FAUSTFLOAT *, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char *, const char *, Soundfile **) override {}
};

// A stereo effect of NyxMap, block by block
template <class T> struct Fx {
    T dsp;
    Collect ui;
    float l[BLOCK], r[BLOCK];
    Fx() {
        dsp.init(SR);
        dsp.buildUserInterface(&ui);
    }
    void set(const char *name, double v) { *ui.at.at(name) = FAUSTFLOAT(v); }
    void run(float *a, float *b) {
        float *in[2] = {a, b}, *out[2] = {l, r};
        dsp.compute(BLOCK, in, out);
        std::copy(l, l + BLOCK, a);
        std::copy(r, r + BLOCK, b);
    }
};

// One voice of NyxMap's instrument: an FM pair or a noise, under its ADSR
struct Voice {
    enum Kind { FM = 0, WHITE = 3, PINK = 4 };
    NyxFaustSynth dsp;
    Collect ui;
    float zero[BLOCK] = {}, out[BLOCK];
    long gateOff = -1, gateOn = -1;
    Voice(Kind kind, double attack, double decay, double sustain, double release, double gain, double ratio = 1, double index = 0) {
        dsp.init(SR);
        dsp.buildUserInterface(&ui);
        set("_kind", kind);
        set("_speed", 1);
        set("attack", attack);
        set("decay", decay);
        set("sustain", sustain);
        set("release", release);
        set("gain", gain);
        set("ratio", ratio);
        set("index", index);
    }
    void set(const char *name, double v) { *ui.at.at(name) = FAUSTFLOAT(v); }
    void run(float *mix, double level = 1) {
        float *in[4] = {zero, zero, zero, zero}, *o[1] = {out};
        dsp.compute(BLOCK, in, o);
        for (int k = 0; k < BLOCK; ++k) mix[k] += float(out[k] * level);
    }
};

// Hits of an instrument: when, how strong, how long, what pitch
struct Hit {
    double t, vel, len, freq;
};
struct Part {
    std::vector<Hit> hits;
    size_t next = 0;
    double now = -1, vel = 0, freq = 0, off = 1e9, age = 1e9;
    void add(double t, double vel = 1, double len = 0.05, double freq = 0) { hits.push_back({t, vel, len, freq}); }
    void sort() { std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.t < b.t; }); }
    // Gives true at the block a hit starts
    bool start(double t) {
        age = now >= 0 ? t - now : 1e9;
        if (next < hits.size() && hits[next].t <= t) {
            now = hits[next].t;
            vel = hits[next].vel;
            off = now + hits[next].len;
            freq = hits[next].freq;
            age = t - now;
            ++next;
            return true;
        }
        return false;
    }
};

// The track, cut: where each piece comes from, at what rate
struct Piece {
    double at, len, from, rate0, rate1, gain;
};

static const double kG1 = 48.999, kFs1 = 46.249, kF1 = 43.654, kBb1 = 58.270, kD2 = 73.416, kG2 = 97.999, kC2 = 65.406;
static double rootOf(int bar) { // what the track's bass plays, bar by bar (measured)
    static const double r[] = {kG1, kFs1, kG1, kG1, kG1, kF1, kG1, kG1, kG1, kG1};
    return r[std::clamp(bar, 0, 9)];
}

// The sections, in bars: Intro 0, Drop 4, Break 8, Drop 2 10, Outro 18, End 20
int main(int argc, char **argv) {
    if (argc < 4) return 1;
    std::vector<float> track;
    {
        FILE *f = fopen(argv[1], "rb");
        if (!f) return 2;
        fseek(f, 0, SEEK_END);
        const long n = ftell(f) / sizeof(float);
        fseek(f, 0, SEEK_SET);
        track.resize(size_t(n));
        if (fread(track.data(), sizeof(float), size_t(n), f) != size_t(n)) return 2;
        fclose(f);
    }
    const double trackSeconds = double(track.size()) / 2 / SR;

    // -- The cut
    std::vector<Piece> pieces;
    const auto srcBeat = [](double b) { return PHASE + b * BEAT; };
    const auto bars = [&](double at, double from, double n, double gain = 1) { pieces.push_back({at * BAR, n * BAR, srcBeat(from * 4), 1, 1, gain}); };
    bars(0, 0, 4);       // Intro: its first bars, filtered open
    bars(4, 4, 4);       // Drop: the next ones
    bars(8, 8, 1);       // Break
    pieces.push_back({9 * BAR, 2 * BEAT, srcBeat(36), 1, 1, 1});
    {
        // It stutters, faster and faster, on one beat of it
        double t = 9 * BAR + 2 * BEAT;
        for (int k = 0; k < 2; ++k, t += BEAT / 2) pieces.push_back({t, BEAT / 2, srcBeat(36), 1, 1, 1});
        for (int k = 0; k < 2; ++k, t += BEAT / 4) pieces.push_back({t, BEAT / 4, srcBeat(36), 1, 1, 1});
        // And stops as a tape does
        pieces.push_back({t, 10 * BAR - t, srcBeat(36), 1, 0.12, 1});
    }
    bars(10, 0, 4); // Drop 2: from the start again, under all of it
    bars(14, 4, 4);
    bars(18, 8, 1);
    pieces.push_back({19 * BAR, 2.6 * BEAT, srcBeat(36), 1, 1, 1});
    pieces.push_back({19 * BAR + 2.6 * BEAT, 1.4 * BEAT, srcBeat(36 + 2.6), 1, 0.2, 1}); // the last tape stop
    const double endAt = 20 * BAR, totalSeconds = endAt + 5.5;

    // -- The parts
    Part kick, sub808, bass, snare, clap, hat, openHat, stab, riser, impact;
    const auto boom = [&](int bar, int s, double vel, double freq, double len) {
        const double t = step16(bar, s);
        kick.add(t, vel);
        bass.add(t + 0.002, vel, len, freq);
    };
    // Bar A and B of a boom-bap, the bass on the kicks, on the root of the bar the track is playing
    const auto groove = [&](int bar, int source, bool big) {
        const double root = rootOf(source);
        const double r2 = root * 2, up = root * 2 * 1.189; // its octave, its minor third
        if ((bar & 1) == 0) {
            boom(bar, 0, 1, root, S16 * 5.5);
            boom(bar, 7, 0.85, root, S16 * 2.5);
            boom(bar, 10, 0.95, big ? r2 : root, S16 * 3.5);
        } else {
            boom(bar, 0, 1, root, S16 * 2.5);
            boom(bar, 3, 0.8, root, S16 * 2.5);
            boom(bar, 6, 0.8, root * 1.189, S16 * 2);
            boom(bar, 10, 0.95, root, S16 * 2.5);
            if (big) boom(bar, 14, 0.8, up, S16 * 1.6);
        }
        snare.add(step16(bar, 4), 1);
        snare.add(step16(bar, 12), 1);
        if (big) {
            for (int off : {-2, -1, 0}) clap.add(step16(bar, 4) + off * 0.011, 1);
            for (int off : {-2, -1, 0}) clap.add(step16(bar, 12) + off * 0.011, 1);
            snare.add(step16(bar, 15), 0.35);
        }
        for (int s = 0; s < 16; s += big ? 1 : 2) {
            const bool on = (s % 4) == 0;
            hat.add(step16(bar, s), on ? 1 : (s % 2 == 0 ? 0.65 : 0.38));
        }
        if (big && (bar & 1)) openHat.add(step16(bar, 14), 0.9);
    };
    for (int b = 4; b < 8; ++b) groove(b, b, false);
    for (int b = 10; b < 18; ++b) groove(b, b - 10, true);
    // A big one when a section lands
    for (double t : {4 * BAR, 10 * BAR, endAt}) impact.add(t, 1);
    // Stabs, on a minor chord of G, into a delay
    const double chord[3] = {kG2 * 2, kBb1 * 4, kD2 * 4};
    for (int b : {8, 9}) for (int s : {0, 3, 6, 10, 13}) stab.add(step16(b, s), s == 0 ? 1 : 0.7, S16 * 1.3, chord[size_t(s % 3)]);
    for (int b = 12; b < 18; b += 2) for (int s : {3, 11}) stab.add(step16(b, s), 0.6, S16, chord[size_t(s % 3)]);
    // Fills
    for (int s = 0; s < 16; ++s) snare.add(step16(17, s), s < 8 ? 0.3 : 0.3 + (s - 8) * 0.08);
    for (int s = 8; s < 16; ++s) hat.add(step16(9, s), 0.5 + (s - 8) * 0.06);
    // Its end: kicks further and further apart, and the last one rings
    for (double b : {18.0, 18.5, 19.0}) boom(int(b), int((b - int(b)) * 16), 1, kG1, S16 * 6);
    bass.add(endAt + 0.002, 1, 3.2, kG1);
    kick.add(endAt, 1);
    for (Part *p : {&kick, &bass, &snare, &clap, &hat, &openHat, &stab, &impact}) p->sort();

    // -- The instruments: several voices for each one, each with its envelope
    static Voice kickBody(Voice::FM, 0.001, 0.26, 0, 0.02, 0.95), kickKnock(Voice::FM, 0.001, 0.05, 0, 0.01, 0.35, 2.02, 1.5),
        kickClick(Voice::WHITE, 0.001, 0.012, 0, 0.005, 0.22);
    static Voice bass1(Voice::FM, 0.004, 0.25, 0.75, 0.06, 0.55, 1, 0.5), bass2(Voice::FM, 0.004, 0.12, 0.55, 0.05, 0.2, 2, 3.2),
        bass3(Voice::FM, 0.004, 0.09, 0.35, 0.05, 0.08, 3.01, 5);
    static Voice snareBody(Voice::FM, 0.001, 0.09, 0, 0.02, 0.5), snareNoise(Voice::PINK, 0.001, 0.17, 0, 0.03, 0.75);
    static Voice clapNoise(Voice::WHITE, 0.001, 0.05, 0, 0.02, 0.55);
    static Voice hatNoise(Voice::WHITE, 0.001, 0.028, 0, 0.01, 0.62), openNoise(Voice::WHITE, 0.002, 0.2, 0, 0.05, 0.45);
    static Voice stab1(Voice::FM, 0.002, 0.18, 0.2, 0.15, 0.2, 3, 2.5), stab2(Voice::FM, 0.002, 0.14, 0.1, 0.12, 0.12, 1.01, 1.2);
    static Voice riseNoise(Voice::WHITE, 0.01, 0.1, 1, 0.1, 0.8), riseTone(Voice::FM, 0.01, 0.1, 1, 0.1, 0.25, 2, 3);
    static Voice dropTone(Voice::FM, 0.001, 1.8, 0, 0.1, 0.8), dropNoise(Voice::PINK, 0.001, 1.4, 0, 0.1, 0.5);
    static Voice drone(Voice::FM, 1.5, 0.1, 1, 0.3, 0.2, 1, 0.3);
    drone.set("frequency", kG1);
    riseNoise.set("gate", 0);

    static Fx<NyxFxHighPass> trackHigh, hatHigh, riseHigh;
    static Fx<NyxFxLowPass> trackLow, bassLow, dropLow;
    static Fx<NyxFxDistortion> trackDrive, bassDrive, kickDrive;
    static Fx<NyxFxDelay> send;
    static Fx<NyxFxReverb> room;
    static Fx<NyxFxBandPass> snareBand, riseBand;
    static Fx<NyxFxCompressor> master;
    trackHigh.set("resonance", 0.707);
    trackLow.set("resonance", 1.0);
    hatHigh.set("cutoff", 7500);
    hatHigh.set("resonance", 0.9);
    bassLow.set("cutoff", 1100);
    bassLow.set("resonance", 0.8);
    bassDrive.set("drive", 0.45);
    bassDrive.set("tone", 2400);
    kickDrive.set("drive", 0.28);
    kickDrive.set("tone", 4500);
    trackDrive.set("tone", 7000);
    send.set("time_ms", BEAT * 750); // a dotted eighth
    send.set("feedback", 0.52);
    room.set("room", 0.82);
    room.set("damping", 0.45);
    snareBand.set("cutoff", 1900);
    snareBand.set("resonance", 0.7);
    master.set("threshold_dB", -14);
    master.set("ratio", 3.5);
    master.set("attack_ms", 12);
    master.set("release_ms", 140);
    master.set("makeup_dB", 7);

    const long total = long(totalSeconds * SR) / BLOCK * BLOCK;
    std::vector<float> out(size_t(total) * 2);
    float tl[BLOCK], tr[BLOCK], ml[BLOCK], mr[BLOCK], sl[BLOCK], sr_[BLOCK], rl[BLOCK], rr[BLOCK], a[BLOCK], b[BLOCK];
    size_t piece = 0;
    double duck = 0, duckBass = 0, glide = kG1, srcPos = 0, rate = 1;
    long lastPiece = -1;
    const auto between = [](double x, double lo, double hi) { return std::clamp((x - lo) / (hi - lo), 0.0, 1.0); };
    const auto expo = [](double lo, double hi, double u) { return lo * std::pow(hi / lo, u); };

    for (long at = 0; at < total; at += BLOCK) {
        const double t = double(at) / SR, bar = t / BAR;
        std::fill(ml, ml + BLOCK, 0.f);
        std::fill(mr, mr + BLOCK, 0.f);
        std::fill(sl, sl + BLOCK, 0.f);
        std::fill(sr_, sr_ + BLOCK, 0.f);
        std::fill(rl, rl + BLOCK, 0.f);
        std::fill(rr, rr + BLOCK, 0.f);

        // ---- The track, where the cut says, through its effects
        while (piece + 1 < pieces.size() && t >= pieces[piece + 1].at) ++piece;
        const Piece &p = pieces[piece];
        const bool inPiece = t >= p.at && t < p.at + p.len;
        if (long(piece) != lastPiece) {
            srcPos = p.from;
            lastPiece = long(piece);
        }
        for (int k = 0; k < BLOCK; ++k) {
            double u = inPiece ? std::clamp((t + double(k) / SR - p.at) / p.len, 0.0, 1.0) : 1;
            rate = p.rate0 + (p.rate1 - p.rate0) * u;
            const double fade = !inPiece ? 0 : std::min({1.0, (t + double(k) / SR - p.at) / 0.003, (p.at + p.len - t - double(k) / SR) / 0.004 + 0.0});
            double pos = srcPos * SR;
            const long i = long(pos);
            const float fr = float(pos - double(i));
            float x = 0, y = 0;
            if (inPiece && i >= 0 && i + 1 < long(track.size() / 2)) {
                x = track[size_t(i) * 2] * (1 - fr) + track[size_t(i + 1) * 2] * fr;
                y = track[size_t(i) * 2 + 1] * (1 - fr) + track[size_t(i + 1) * 2 + 1] * fr;
            }
            tl[k] = float(x * fade * p.gain);
            tr[k] = float(y * fade * p.gain);
            srcPos += rate / SR;
        }
        // Its filter: opening over the intro, closing over the end. In the drops its low end makes room for the 808
        double lowCut = 18000, highCut = 20, drive = 0, level = 0.62, toDelay = 0, toRoom = 0.05;
        if (bar < 4) {
            lowCut = expo(260, 16000, std::pow(between(bar, 0, 3.98), 1.6));
            level = 1.05;
            toRoom = 0.12;
        } else if (bar < 8) {
            highCut = 95;
            level = 1.0;
        } else if (bar < 10) {
            highCut = expo(95, 750, between(bar, 8, 10));
            lowCut = expo(9000, 3500, between(bar, 8, 9));
            drive = 0.12 + 0.3 * between(bar, 9, 10);
            toDelay = 0.42;
            toRoom = 0.32;
            level = 0.72;
        } else if (bar < 18) {
            highCut = 110;
            drive = 0.06;
            level = 0.95;
            // A beat of air at the end of every fourth bar
            const double in = std::fmod(bar - 10, 4);
            if (in > 3.75) {
                highCut = 1400;
                toDelay = 0.55;
            }
        } else {
            lowCut = expo(14000, 240, between(bar, 18, 19.9));
            toRoom = 0.1 + 0.4 * between(bar, 18, 20);
            toDelay = 0.25;
            level = 1.0;
        }
        trackLow.set("cutoff", lowCut);
        trackHigh.set("cutoff", highCut);
        trackDrive.set("drive", std::max(0.001, drive));
        trackHigh.run(tl, tr);
        trackLow.run(tl, tr);
        if (drive > 0) trackDrive.run(tl, tr);

        // ---- The kick: three voices, a pitch that falls fast
        if (kick.start(t)) {
            kickBody.set("gate", 0);
            kickKnock.set("gate", 0);
            kickClick.set("gate", 0);
        } else if (kick.age < 0.04) {
            kickBody.set("gate", 1);
            kickKnock.set("gate", 1);
            kickClick.set("gate", 1);
        } else {
            kickBody.set("gate", 0);
            kickKnock.set("gate", 0);
            kickClick.set("gate", 0);
        }
        const double kf = 47 + 140 * std::exp(-std::max(0.0, kick.age) / 0.028);
        kickBody.set("frequency", kf);
        kickKnock.set("frequency", 170 + 280 * std::exp(-std::max(0.0, kick.age) / 0.012));
        std::fill(a, a + BLOCK, 0.f);
        kickBody.run(a, kick.vel);
        kickKnock.run(a, kick.vel);
        std::fill(b, b + BLOCK, 0.f);
        kickClick.run(b, kick.vel);
        for (int k = 0; k < BLOCK; ++k) a[k] += b[k];
        std::copy(a, a + BLOCK, b);
        kickDrive.run(a, b);
        for (int k = 0; k < BLOCK; ++k) {
            ml[k] += a[k];
            mr[k] += b[k];
        }
        // What the kick pushes down: it pumps
        duck = kick.age < 0.002 ? kick.vel : duck * std::exp(-double(BLOCK) / SR / 0.13);
        duckBass = kick.age < 0.002 ? kick.vel : duckBass * std::exp(-double(BLOCK) / SR / 0.05);

        // ---- The 808: three voices, stacked, that slide, into a filter and a drive
        const bool bassStarts = bass.start(t);
        const bool bassOn = t < bass.off && bass.now >= 0;
        const double gateBass = bassStarts ? 0 : (bassOn && bass.age >= 0.0007 ? 1 : 0);
        if (bass.freq > 0) glide += (bass.freq - glide) * (1 - std::exp(-double(BLOCK) / SR / 0.012));
        // The last one falls in pitch as it rings
        const double fall = t >= endAt ? std::pow(0.5, between(t, endAt + 1.0, endAt + 3.2)) : 1;
        for (Voice *v : {&bass1, &bass2, &bass3}) {
            v->set("gate", gateBass);
            v->set("frequency", std::max(20.0, glide * fall));
        }
        std::fill(a, a + BLOCK, 0.f);
        bass1.run(a, bass.vel);
        bass2.run(a, bass.vel);
        bass3.run(a, bass.vel);
        std::copy(a, a + BLOCK, b);
        bassDrive.run(a, b);
        bassLow.run(a, b);
        const double bassDuck = 1 - 0.55 * duckBass;
        for (int k = 0; k < BLOCK; ++k) {
            ml[k] += float(a[k] * bassDuck);
            mr[k] += float(b[k] * bassDuck);
        }

        // ---- Snare and clap, into the room
        if (snare.start(t)) {
            snareBody.set("gate", 0);
            snareNoise.set("gate", 0);
        } else if (snare.age < 0.03) {
            snareBody.set("gate", 1);
            snareNoise.set("gate", 1);
        } else {
            snareBody.set("gate", 0);
            snareNoise.set("gate", 0);
        }
        snareBody.set("frequency", 178 + 90 * std::exp(-std::max(0.0, snare.age) / 0.02));
        std::fill(a, a + BLOCK, 0.f);
        snareNoise.run(a, snare.vel);
        std::copy(a, a + BLOCK, b);
        snareBand.run(a, b);
        std::fill(sl, sl + BLOCK, 0.f);
        snareBody.run(sl, snare.vel);
        if (clap.start(t)) clapNoise.set("gate", 0);
        else clapNoise.set("gate", clap.age < 0.006 ? 1 : 0);
        std::fill(sr_, sr_ + BLOCK, 0.f);
        clapNoise.run(sr_, clap.vel);
        for (int k = 0; k < BLOCK; ++k) {
            const float left = a[k] * 1.5f + sl[k] + sr_[k] * 0.8f, right = b[k] * 1.5f + sl[k] + sr_[k] * 1.1f;
            ml[k] += left;
            mr[k] += right;
            rl[k] += left * 0.22f;
            rr[k] += right * 0.22f;
        }
        std::fill(sl, sl + BLOCK, 0.f);
        std::fill(sr_, sr_ + BLOCK, 0.f);

        // ---- Hats, a little to each side
        if (hat.start(t)) hatNoise.set("gate", 0);
        else hatNoise.set("gate", hat.age < 0.012 ? 1 : 0);
        if (openHat.start(t)) openNoise.set("gate", 0);
        else openNoise.set("gate", openHat.age < 0.08 ? 1 : 0);
        std::fill(a, a + BLOCK, 0.f);
        hatNoise.run(a, hat.vel);
        openNoise.run(a, openHat.vel);
        std::copy(a, a + BLOCK, b);
        hatHigh.run(a, b);
        for (int k = 0; k < BLOCK; ++k) {
            ml[k] += a[k] * 0.85f;
            mr[k] += b[k] * 1.0f;
        }

        // ---- Stabs, wide, into the delay
        if (stab.start(t)) {
            stab1.set("gate", 0);
            stab2.set("gate", 0);
        } else {
            const double g = t < stab.off && stab.now >= 0 ? 1 : 0;
            stab1.set("gate", g);
            stab2.set("gate", g);
        }
        if (stab.freq > 0) {
            stab1.set("frequency", stab.freq);
            stab2.set("frequency", stab.freq * 1.5);
        }
        std::fill(a, a + BLOCK, 0.f);
        stab1.run(a, stab.vel);
        stab2.run(a, stab.vel);
        for (int k = 0; k < BLOCK; ++k) {
            ml[k] += a[k] * 0.8f;
            mr[k] += a[k] * 0.8f;
            sl[k] += a[k] * 0.55f;
            sr_[k] += a[k] * 0.55f;
            rl[k] += a[k] * 0.2f;
            rr[k] += a[k] * 0.2f;
        }

        // ---- The swell under the intro, and the risers before each drop
        const double droneLevel = between(bar, 1.5, 3.0) * (1 - between(bar, 3.9, 4.0));
        drone.set("gate", bar > 1.5 && bar < 4 ? 1 : 0);
        std::fill(a, a + BLOCK, 0.f);
        drone.run(a, droneLevel);
        for (int k = 0; k < BLOCK; ++k) {
            ml[k] += a[k];
            mr[k] += a[k];
        }
        double up = 0;
        for (const auto &[from, to] : {std::pair<double, double>{3, 4}, {8.5, 10}, {16.5, 18}})
            if (bar >= from && bar < to) up = between(bar, from, to);
        riseNoise.set("gate", up > 0 ? 1 : 0);
        riseTone.set("gate", up > 0 ? 1 : 0);
        riseTone.set("frequency", expo(180, 2200, up));
        riseBand.set("cutoff", expo(300, 9000, up));
        riseBand.set("resonance", 1.4);
        std::fill(a, a + BLOCK, 0.f);
        riseNoise.run(a, std::pow(up, 2.0) * 0.5);
        std::copy(a, a + BLOCK, b);
        riseBand.run(a, b);
        std::fill(sl + 0, sl + 0, 0.f);
        float tone[BLOCK] = {};
        riseTone.run(tone, std::pow(up, 2.5));
        for (int k = 0; k < BLOCK; ++k) {
            ml[k] += a[k] + tone[k];
            mr[k] += b[k] + tone[k];
            rl[k] += (a[k] + tone[k]) * 0.3f;
            rr[k] += (b[k] + tone[k]) * 0.3f;
        }

        // ---- The impact when a section lands: a pitch falling for long, a noise, into the room
        if (impact.start(t)) {
            dropTone.set("gate", 0);
            dropNoise.set("gate", 0);
        } else if (impact.age < 0.05) {
            dropTone.set("gate", 1);
            dropNoise.set("gate", 1);
        } else {
            dropTone.set("gate", 0);
            dropNoise.set("gate", 0);
        }
        dropTone.set("frequency", 30 + 150 * std::exp(-std::max(0.0, impact.age) / 0.18));
        std::fill(a, a + BLOCK, 0.f);
        dropTone.run(a, 0.9);
        std::fill(b, b + BLOCK, 0.f);
        dropNoise.run(b, 0.35);
        for (int k = 0; k < BLOCK; ++k) {
            ml[k] += a[k] + b[k];
            mr[k] += a[k] + b[k];
            rl[k] += b[k] * 0.9f + a[k] * 0.1f;
            rr[k] += b[k] * 0.9f + a[k] * 0.1f;
        }

        // ---- The track, pumped by the kick, into its sends
        const double pump = 1 - (bar >= 4 && bar < 8 ? 0.45 : bar >= 10 && bar < 18 ? 0.6 : 0.3) * duck;
        for (int k = 0; k < BLOCK; ++k) {
            const float x = float(tl[k] * level * pump), y = float(tr[k] * level * pump);
            ml[k] += x;
            mr[k] += y;
            sl[k] += float(x * toDelay);
            sr_[k] += float(y * toDelay);
            rl[k] += float(x * toRoom);
            rr[k] += float(y * toRoom);
        }
        send.run(sl, sr_);
        for (int k = 0; k < BLOCK; ++k) {
            rl[k] += sl[k] * 0.18f;
            rr[k] += sr_[k] * 0.18f;
        }
        room.run(rl, rr);
        for (int k = 0; k < BLOCK; ++k) {
            ml[k] += sl[k] * 0.55f + rl[k] * 0.55f;
            mr[k] += sr_[k] * 0.55f + rr[k] * 0.55f;
        }
        master.run(ml, mr);
        for (int k = 0; k < BLOCK; ++k) { // pushed, its peaks rounded: it stays loud without breaking
            ml[k] = std::tanh(ml[k] * 1.25f);
            mr[k] = std::tanh(mr[k] * 1.25f);
        }
        for (int k = 0; k < BLOCK; ++k) {
            out[size_t(at + k) * 2] = ml[k];
            out[size_t(at + k) * 2 + 1] = mr[k];
        }
    }

    // ---- A last safety: a limiter that looks ahead, then a fade at its very end
    const double ceiling = 0.89; // -1 dB
    const int ahead = int(0.003 * SR);
    std::vector<float> gain(size_t(total), 1.f);
    double g = 1;
    for (long i = total - 1; i >= 0; --i) { // backwards: the gain falls before a peak
        const double peak = std::max(std::abs(out[size_t(i) * 2]), std::abs(out[size_t(i) * 2 + 1]));
        const double want = peak > ceiling ? ceiling / peak : 1.0;
        g = want < g ? want : std::min(1.0, g + (1 - g) * (1.0 / ahead));
        gain[size_t(i)] = float(g);
    }
    double smooth = 1, overs = 0;
    for (long i = 0; i < total; ++i) {
        smooth += (gain[size_t(i)] - smooth) * (gain[size_t(i)] < smooth ? 1.0 : 0.0006);
        overs += gain[size_t(i)] < 0.999;
        const double fade = std::clamp((totalSeconds - double(i) / SR) / 1.2, 0.0, 1.0);
        for (int c = 0; c < 2; ++c) {
            double v = out[size_t(i) * 2 + size_t(c)] * smooth * fade;
            v = std::clamp(v, -ceiling, ceiling);
            out[size_t(i) * 2 + size_t(c)] = float(v);
        }
    }
    std::printf("limited %.1f%% of the time, track %.2f s, total %.2f s\n", 100 * overs / double(total), trackSeconds, totalSeconds);
    FILE *f = fopen(argv[2], "wb");
    fwrite(out.data(), sizeof(float), out.size(), f);
    fclose(f);

    // ---- When everything falls, for the pictures
    f = fopen(argv[3], "w");
    const auto list = [&](const char *name, const Part &part, bool comma = true) {
        std::fprintf(f, "\"%s\":[", name);
        for (size_t k = 0; k < part.hits.size(); ++k) std::fprintf(f, "%s[%.4f,%.3f]", k ? "," : "", part.hits[k].t, part.hits[k].vel);
        std::fprintf(f, "]%s\n", comma ? "," : "");
    };
    std::fprintf(f, "{\"bpm\":%g,\"bar\":%.5f,\"end\":%.4f,\"total\":%.4f,\n", BPM, BAR, endAt, totalSeconds);
    list("kick", kick);
    list("snare", snare);
    list("hat", hat);
    list("open_hat", openHat);
    list("stab", stab);
    list("impact", impact, false);
    std::fprintf(f, "}\n");
    fclose(f);
    return 0;
}
