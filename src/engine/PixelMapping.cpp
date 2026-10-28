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

std::map<quint64, std::vector<std::array<float, 3>>> Engine::fixtureLooks() const
{
    std::lock_guard<std::mutex> lk(m_dmxMutex);
    return m_fixtureLooks;
}

void Engine::followLineFixtures()
{
    bool moved = false;
    for (Fixture &f : m_fixtures.fixtures) {
        if (!f.lineLayer) continue;
        const int i = indexOfId(f.lineLayer);
        if (i < 0) continue; // the layer gone: the strip stays where it was
        const Layer &l = *m_layers[size_t(i)];
        if (l.type != SourceType::Isf || !isLineGeneratorPath(l.sourcePath) || f.lineIndex >= int(l.lines.size())) continue;
        const LinePath &line = l.lines[size_t(f.lineIndex)];
        std::vector<QPointF> path;
        path.reserve(line.points.size());
        for (const QPointF &q : line.points) path.push_back(l.mapping.map(q.x(), q.y()));
        if (path != f.path || line.closed != f.pathClosed) {
            f.path = std::move(path);
            f.pathClosed = line.closed;
            moved = true;
        }
    }
    if (moved) ++m_fixturesVersion; // its pixels sampled elsewhere
}

void Engine::stepPixelMapping()
{
    followLineFixtures();
    if (m_fixturesFollowed != m_fixturesVersion || m_fixturePointsComp != m_compSize) {
        m_fixturesFollowed = m_fixturesVersion;
        m_fixturePointsComp = m_compSize;
        m_fixturePoints = fixtureSamplePoints(m_fixtures, m_compSize);
        m_dmx.follow(m_fixtures.outputs);
    }
    // The colours of an earlier frame, once the GPU is done with them: they make this frame's universes. Moved or
    // retyped fixtures since: their pixels counted again, the colours of another patch are let go
    std::vector<std::array<float, 3>> colors, newer;
    quint64 count = 0, newerCount = 0;
    bool got = false;
    while (m_sampler.collect(&newer, &newerCount)) { // the newest of those finished
        colors.swap(newer);
        count = newerCount;
        got = true;
    }
    if (got && count == m_fixturePoints.size()) {
        DmxFrame frame = buildDmxFrame(m_fixtures, colors);
        auto looks = fixtureOutput(m_fixtures, colors);
        m_dmx.submit(frame);
        std::lock_guard<std::mutex> lk(m_dmxMutex);
        m_dmxLast = std::move(frame);
        m_fixtureLooks = std::move(looks);
    } else if (m_fixturePoints.empty()) {
        std::lock_guard<std::mutex> lk(m_dmxMutex);
        m_fixtureLooks.clear();
        if (!m_dmxLast.empty()) {
            m_dmxLast.clear();
            m_dmx.submit({});
        }
    }
    const RenderTarget &out = m_output[m_back];
    if (!m_fixturePoints.empty() && out.tex) m_sampler.sample(out.tex, m_fixturePoints, m_fixturePoints.size());
}
