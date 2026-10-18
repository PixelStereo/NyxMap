#include "AudioInput.h"
#include "TimecodeIo.h"

#include <algorithm>
#include <cmath>

AudioInput::AudioInput() = default;

AudioInput::~AudioInput() { stop(); }

bool AudioInput::start(const QString &device, QString *err)
{
    stop();
    auto next = std::make_unique<tcaudio::Device>();
    if (!next->openCapture(device, [this](const float *mono, int frames) { feed(mono, frames); }, err)) return false;
    std::lock_guard<std::mutex> lk(m_mutex);
    m_name = next->name();
    m_device = std::move(next);
    m_running = true;
    return true;
}

void AudioInput::stop()
{
    std::unique_ptr<tcaudio::Device> closing;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        closing = std::move(m_device);
        m_name.clear();
        m_running = false;
    }
    closing.reset(); // its callback has returned
    m_analysis.reset();
    m_peak = 0.0f;
}

QString AudioInput::deviceName() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_name;
}

void AudioInput::feed(const float *mono, int frames)
{
    if (!mono || frames <= 0) return;
    float peak = m_peak.load() * std::exp(-float(frames) / tcaudio::kRate / 0.3f);
    for (int done = 0; done < frames;) {
        const int n = std::min(frames - done, 512);
        for (int i = 0; i < n; ++i) {
            const float x = std::isfinite(mono[done + i]) ? mono[done + i] : 0.0f;
            m_stereo[2 * i] = m_stereo[2 * i + 1] = x;
            peak = std::max(peak, std::abs(x));
        }
        m_analysis.process(m_stereo, n, tcaudio::kRate);
        done += n;
    }
    m_peak = peak;
}
