// Where a media plays, read back: Source › Playhead (s) and Source › Progress (in point 0, out point 1) are values an
// Internal interaction follows — here a layer's Position X following a video — read-only and never saved.
// Standalone CTest area: nyxmap_tests --area media_playhead.
#include "EngineInternal.h"

#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstdio>

int runMediaPlayheadTests(Engine &e, const QString &root)
{
    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        if (ok) return;
        std::printf("FAIL media_playhead: %s\n", what);
        ++failures;
    };
    auto near = [](double a, double b) { return std::abs(a - b) < 0.05; };
    e.newProject();
    e.setFadesManual(true);
    const int vi = e.addLayer(QStringLiteral("Video"));
    QString err;
    if (!e.setLayerVideo(vi, root + "/media/h264.mp4", &err)) {
        check(false, "open test video (sh test/make_media.sh)");
        e.setFadesManual(false);
        return failures;
    }
    const quint64 video = e.layerId(vi);
    check(e.setParameter(video, QStringLiteral("play"), false), "pause the video");
    double d = 0;
    {
        Engine::Lock lk(&e.mutex());
        d = e.layer(vi)->duration();
    }
    check(d > 2, "the test video lasts more than 2 s");
    auto value = [&](const char *path) { // as an Internal interaction reads it (not a stored value)
        Engine::Lock lk(&e.mutex());
        const Parameter *p = e.layer(e.indexOfId(video))->parameter(QString::fromLatin1(path));
        return p ? p->value().toDouble() : -1.0;
    };

    // Listed for Internal interactions: readable numbers, not written, not animated
    bool listed = false;
    for (const ParamInfo &p : e.parameters(video))
        if (p.path == QLatin1String("playhead") || p.path == QLatin1String("progress")) {
            check((p.access & ParamInfo::Read) && !(p.access & ParamInfo::Write) && !p.animatable && p.isNumber(),
                  "playhead and progress are read-only numbers");
            listed = true;
        }
    check(listed, "a video lists its playhead and its progress");
    ParamInfo info;
    check(e.parameterInfo(video, QStringLiteral("playhead"), &info) && near(info.max, d), "the playhead ranges over the media");

    e.seekLayer(vi, 1.0);
    check(near(value("playhead"), 1.0), "the playhead reads where the video is");
    check(near(value("progress"), 1.0 / d), "the progress is the share of the media played");
    check(!e.setParameter(video, QStringLiteral("playhead"), 0.0) && near(value("playhead"), 1.0), "the playhead is not written");

    // Within the in / out points
    check(e.setParameter(video, QStringLiteral("in"), 0.5) && e.setParameter(video, QStringLiteral("out"), 1.5), "set in and out");
    e.seekLayer(vi, 1.0);
    check(near(value("progress"), 0.5), "the progress runs from the in point to the out point");
    check(e.parameterInfo(video, QStringLiteral("playhead"), &info) && near(info.min, 0.5) && near(info.max, 1.5),
          "the playhead's range is the played one (an Internal interaction's source range starts from it)");

    // A layer's Position X follows the video's progress
    const int ti = e.addLayer(QStringLiteral("Follower"));
    e.layer(ti)->type = SourceType::Text; // no GPU source needed
    const quint64 follower = e.layerId(ti);
    Interaction link = Interaction::made(InteractionSource::Internal);
    link.sourceLayer = video;
    link.sourceParam = QStringLiteral("progress");
    link.filters = {InteractionStage::remap(0, 1, 100, 300)};
    link.layer = follower;
    link.param = QStringLiteral("position/x");
    link.takeover = Takeover::Jump;
    e.addInteraction(link);
    e.renderFrame();
    auto x = [&] { return e.parameterValue(follower, QStringLiteral("position/x")).toDouble(); };
    check(std::abs(x() - 200) < 1, "Position X follows the progress (half way)");
    e.seekLayer(e.indexOfId(video), 1.25); // the follower added above it moved its index
    e.renderFrame();
    check(std::abs(x() - 250) < 1, "Position X moves with the playhead");

    // Never saved with the layer
    const QJsonObject values = e.layerJson(vi).value(QStringLiteral("params")).toObject();
    check(!values.contains(QStringLiteral("playhead")) && !values.contains(QStringLiteral("progress")), "not saved with the layer");

    e.setFadesManual(false);
    if (!failures) std::printf("media_playhead: OK\n");
    return failures;
}
