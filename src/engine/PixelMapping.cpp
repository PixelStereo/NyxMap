// Pixel mapping: the fixtures of the project (Fixtures.h) sampled in the composition at every frame (PixelSampler.h),
// their universes built from the colours read back and handed to the DMX sender (Dmx.h).
#include "EngineInternal.h"

FixturePatch Engine::fixturePatch() const
{
    Lock lk(&m_mutex);
    return m_fixtures;
}

void Engine::setFixturePatch(FixturePatch p)
{
    p.normalize();
    {
        Lock lk(&m_mutex);
        m_fixtures = std::move(p);
        ++m_fixturesVersion;
    }
    emit fixturesChanged();
}

QByteArray Engine::dmxUniverse(quint64 output, int universe) const
{
    std::lock_guard<std::mutex> lk(m_dmxMutex);
    const auto it = m_dmxLast.find({output, universe});
    return it != m_dmxLast.end() ? it->second : QByteArray();
}

DmxOutputState Engine::dmxOutputState(quint64 output) const { return m_dmx.state(output); }

void Engine::stepPixelMapping()
{
    if (m_fixturesFollowed != m_fixturesVersion || m_fixturePointsComp != m_compSize) {
        m_fixturesFollowed = m_fixturesVersion;
        m_fixturePointsComp = m_compSize;
        m_fixturePoints = fixtureSamplePoints(m_fixtures, m_compSize);
        m_dmx.follow(m_fixtures.outputs);
    }
    // The colours of an earlier frame, once the GPU is done with them: they make this frame's universes. Moved or
    // retyped fixtures since: their pixels counted again, the colours of another patch are let go
    std::vector<std::array<float, 3>> colors;
    quint64 count = 0;
    if (m_sampler.collect(&colors, &count) && count == m_fixturePoints.size()) {
        DmxFrame frame = buildDmxFrame(m_fixtures, colors);
        m_dmx.submit(frame);
        std::lock_guard<std::mutex> lk(m_dmxMutex);
        m_dmxLast = std::move(frame);
    } else if (m_fixturePoints.empty()) {
        std::lock_guard<std::mutex> lk(m_dmxMutex);
        if (!m_dmxLast.empty()) {
            m_dmxLast.clear();
            m_dmx.submit({});
        }
    }
    const RenderTarget &out = m_output[m_back];
    if (!m_fixturePoints.empty() && out.tex) m_sampler.sample(out.tex, m_fixturePoints, m_fixturePoints.size());
}
