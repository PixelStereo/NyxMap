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
        m_fixtureParamsDirty = true;
    }
    emit fixturesChanged();
}

// Each fixture's values a show changes: on / off, solo, its intensity. Read and written in the fixture of that id
// (the lock held by the caller); on / off and solo change the pixels sampled, a write tells the interface
const std::vector<std::unique_ptr<Parameter>> &Engine::fixtureParams()
{
    if (!m_fixtureParamsDirty) return m_fixtureParams;
    m_fixtureParamsDirty = false;
    m_fixtureParams.clear();
    using T = Parameter::Type;
    for (const Fixture &f : m_fixtures.fixtures) {
        const quint64 id = f.id;
        const QString prefix = QStringLiteral("fixture/%1/").arg(id);
        const QString cat = QStringLiteral("Fixtures › %1 › ").arg(f.name.isEmpty() ? QStringLiteral("Fixture %1").arg(id) : f.name);
        auto find = [this, id] { return m_fixtures.findFixture(id); };
        auto flag = [&](const QString &key, const QString &name, bool def, bool Fixture::*field) {
            m_fixtureParams.push_back(std::make_unique<Parameter>(prefix + key, cat + name, T::Bool));
            m_fixtureParams.back()->byDefault(def).ramp(Parameter::Ramp::Cut).bind(
                [find, field] {
                    const Fixture *x = find();
                    return x ? QVariant(x->*field) : QVariant();
                },
                [this, find, field](const QVariant &v) {
                    Fixture *x = find();
                    if (!x || x->*field == v.toBool()) return;
                    x->*field = v.toBool();
                    ++m_fixturesVersion; // the pixels sampled
                    fixtureValuesTouched();
                });
        };
        flag(QStringLiteral("enable"), QStringLiteral("On"), true, &Fixture::enabled);
        flag(QStringLiteral("solo"), QStringLiteral("Solo"), false, &Fixture::solo);
        m_fixtureParams.push_back(std::make_unique<Parameter>(prefix + QStringLiteral("intensity"), cat + QStringLiteral("Intensity"), T::Float));
        m_fixtureParams.back()->range(0, 1).limits(0, 1).step(0.01).byDefault(1.0).animatable().bind(
            [find] {
                const Fixture *x = find();
                return x ? QVariant(x->intensity) : QVariant();
            },
            [this, find](const QVariant &v) {
                Fixture *x = find();
                if (!x) return;
                x->intensity = std::clamp(v.toDouble(), 0.0, 1.0);
                fixtureValuesTouched();
            });
    }
    return m_fixtureParams;
}

void Engine::fixtureValuesTouched()
{
    if (m_fixtureValuesPending.exchange(true)) return; // one is on its way
    QMetaObject::invokeMethod(this, "notifyFixtureValuesChanged", Qt::QueuedConnection);
}

void Engine::notifyFixtureValuesChanged()
{
    m_fixtureValuesPending = false;
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
