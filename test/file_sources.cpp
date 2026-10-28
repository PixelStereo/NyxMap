// File and stream sources: image sequences (a folder of numbered pictures played as a clip), SVG drawings, live
// network streams (URL, project round trip, an unreachable stream that never blocks the render thread).
// Called from engine_test.cpp (manual rendering mode, engine initialized).
#include "Engine.h"
#include "ImageFile.h"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char *why)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL file_sources: %s\n", why);
    } else {
        std::printf("ok    file_sources: %s\n", why);
    }
}

bool near(const QColor &c, int r, int g, int b, int tolerance = 6)
{
    return std::abs(c.red() - r) <= tolerance && std::abs(c.green() - g) <= tolerance && std::abs(c.blue() - b) <= tolerance;
}

// Picture k of the sequence: a red level of its own, so that each frame is told apart
QColor frameColor(int k) { return QColor(40 * k + 20, 100, 200 - 30 * k); }

// The first frame a decoder delivers from clock c (top row first)
QImage frameAt(VideoDecoder &d, double c)
{
    d.seek(c);
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    QElapsedTimer t;
    t.start();
    while (!d.fetchRgba(c + 0.001, rgba, &w, &h) && t.elapsed() < 3000) QThread::msleep(1);
    if (!w || !h) return {};
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) std::memcpy(img.scanLine(y), rgba.data() + size_t(h - 1 - y) * w * 4, size_t(w) * 4);
    return img;
}

int layerNamed(Engine &e, const QString &name)
{
    for (int k = 0; k < e.layerCount(); ++k)
        if (e.layer(k)->name == name) return k;
    return -1;
}

void sequenceTests(Engine &e, const QString &tmp)
{
    const QString folder = tmp + "/shot_seq";
    QDir(folder).removeRecursively();
    QDir().mkpath(folder);
    for (int k = 0; k < 5; ++k) {
        QImage img(32, 16, QImage::Format_RGB32);
        img.fill(frameColor(k));
        img.save(folder + QStringLiteral("/shot%1.png").arg(k + 1, 4, 10, QChar('0')));
    }
    QFile notes(folder + "/notes.txt");
    notes.open(QIODevice::WriteOnly);
    notes.write("not a picture");
    notes.close();

    Engine::SequenceInfo seq;
    check(Engine::detectImageSequence(folder, &seq), "a folder of numbered pictures is a sequence");
    check(seq.pattern == "shot%04d.png" && seq.start == 1 && seq.frames == 5, "its pattern, first number and length");
    check(!Engine::detectImageSequence(tmp + "/shot_seq/notes.txt"), "a file is not a sequence");
    {
        const QString unpadded = tmp + "/unpadded_seq";
        QDir(unpadded).removeRecursively();
        QDir().mkpath(unpadded);
        QImage img(4, 4, QImage::Format_RGB32);
        img.fill(Qt::red);
        for (int k : {8, 9, 10, 11}) img.save(unpadded + QStringLiteral("/f%1.png").arg(k));
        Engine::SequenceInfo u;
        check(Engine::detectImageSequence(unpadded, &u) && u.pattern == "f%d.png" && u.start == 8 && u.frames == 4,
              "numbers without padding: %d, across widths");
    }

    VideoSource source;
    source.pattern = seq.pattern;
    source.start = seq.start;
    source.fps = 25;
    VideoDecoder::Info info;
    QString err;
    check(VideoDecoder::probe(folder, &info, &err, source) && info.frames == 5 && info.width == 32 && info.height == 16,
          "probe: 5 frames, 32×16 (image2 demuxer)");
    VideoDecoder d;
    check(d.open(folder, &err, source) && std::abs(d.duration() - 0.2) < 1e-6 && std::abs(d.fps() - 25) < 1e-6,
          "opened as a clip of 0.2 s at 25 fps");
    Timeline t;
    t.mode = Timeline::Once;
    d.setTimeline(t);
    for (int k = 0; k < 5; ++k) {
        const QImage img = frameAt(d, k / 25.0);
        const QColor c = frameColor(k);
        std::printf("      frame %d: %s\n", k, img.isNull() ? "none" : qPrintable(img.pixelColor(8, 8).name()));
        check(!img.isNull() && img.size() == QSize(32, 16) && near(img.pixelColor(8, 8), c.red(), c.green(), c.blue()),
              "frame k shows picture k");
    }
    d.close();

    // A gap ends the clip
    {
        QImage img(32, 16, QImage::Format_RGB32);
        img.fill(Qt::black);
        img.save(folder + "/shot0007.png");
        Engine::SequenceInfo g;
        check(Engine::detectImageSequence(folder, &g) && g.frames == 5, "a gap in the numbering ends the sequence");
    }

    // On a layer: the folder loaded, its pattern found, saved with the project
    e.newProject();
    const int row = e.addLayer("Seq");
    check(e.setLayerFile(row, folder, &err), "a folder loads as a sequence");
    {
        QElapsedTimer wait;
        wait.start();
        while (e.videoFramesShown(row) < 1 && wait.elapsed() < 3000) {
            e.renderFrame();
            QThread::msleep(10);
        }
    }
    {
        Engine::Lock lk(&e.mutex());
        const Layer *l = e.layer(row);
        check(l->type == SourceType::Video && l->videoSource.pattern == "shot%04d.png" && l->srcWidth == 32,
              "sequence layer: a video of the folder's size");
    }
    const QImage src = e.grabLayerSource(row);
    check(src.size() == QSize(32, 16), "sequence layer renders");
    QJsonObject source0 = e.layerJson(row).value("source").toObject();
    check(source0.value("type").toString() == "video" && source0.value("path").toString() == QDir::cleanPath(folder) &&
              source0.value("sequence").toObject().value("pattern").toString() == "shot%04d.png" &&
              source0.value("sequence").toObject().value("start").toInt() == 1 &&
              source0.value("sequence").toObject().value("fps").toDouble() == 25,
          "JSON: path is the folder, sequence pattern / start / fps");
    VideoSource faster = e.layer(row)->videoSource;
    faster.fps = 50;
    check(e.setLayerVideoSource(row, faster, &err), "frame rate changed");
    check(std::abs(e.layer(row)->duration() - 0.1) < 1e-6 &&
              e.layerJson(row).value("source").toObject().value("sequence").toObject().value("fps").toDouble() == 50,
          "at 50 fps the clip lasts 0.1 s");

    const QString project = tmp + "/sequence.nyxmap";
    check(e.saveProject(project, {}, &err), "save the project");
    e.newProject();
    check(e.loadProject(project, nullptr, &err), "open it again");
    const int back = layerNamed(e, "Seq");
    check(back >= 0 && e.layer(back)->videoSource.isSequence() && e.layer(back)->videoSource.fps == 50 && e.layer(back)->video,
          "sequence and its rate restored");

    // Collect Files: the whole folder copied, the layer pointing to the copy
    const Engine::CollectPlan plan = e.planCollect(tmp + "/collected", "SeqShow", {});
    int pictures = 0;
    for (const Engine::CollectFile &f : plan.files)
        if (f.to.contains("/MEDIA/SEQUENCES/shot_seq/")) ++pictures;
    check(pictures == 7, "Collect Files copies the sequence's folder (every file in it)");
    QString collectedPath;
    for (const QJsonValue &v : plan.project.value("layers").toArray())
        if (v.toObject().value("name").toString() == "Seq") collectedPath = v.toObject().value("source").toObject().value("path").toString();
    check(collectedPath.endsWith("/MEDIA/SEQUENCES/shot_seq") && plan.missing.isEmpty(), "the collected layer uses the copied folder");
    // A numbered frame used separately still has to exist inside the copied sequence.
    e.addBinItems({folder + "/shot0003.png"});
    QTemporaryDir collection(tmp + "/collect-XXXXXX");
    check(collection.isValid(), "an isolated collection destination is available");
    const auto shared = e.planCollect(collection.path(), "SharedFrameShow", {});
    int copies = 0;
    for (const auto &file : shared.files) if (file.from == folder + "/shot0003.png") ++copies;
    check(copies == 2, "a frame used as a still has both necessary collected copies");
    check(e.collect(shared, &err), "collect a sequence with a separately imported frame");
    QString copiedFolder;
    for (const auto &layer : shared.project.value("layers").toArray())
        if (layer.toObject().value("name").toString() == "Seq")
            copiedFolder = layer.toObject().value("source").toObject().value("path").toString();
    check(VideoDecoder::probe(copiedFolder, &info, &err, faster) && info.frames == 5,
          "the collected sequence remains contiguous and keeps all five frames");
    d.close(); e.newProject();
    auto moved = shared;
#ifdef Q_OS_UNIX
    // The later reference uses another spelling of the same file. Once the
    // first is removed its canonical path is unavailable, even on Linux.
    const QString alias = collection.path() + "/source_alias";
    check(QFile::link(folder, alias), "a symbolic source-folder alias is available");
    for (auto it = moved.files.rbegin(); it != moved.files.rend(); ++it)
        if (it->from == folder + "/shot0003.png") {
            it->from = alias + "/shot0003.png";
            break;
        }
#endif
    QStringList failed;
    check(Engine::removeCollectedOriginals(moved, &failed) == int(shared.files.size()) - 1 && failed.isEmpty(),
          "Move removes each original once even when it has two collected copies");
}

void svgTests(Engine &e, const QString &tmp)
{
    if (!Engine::imageExtensions().contains("svg")) {
        std::printf("      SVG: Qt SVG absent from this build, skipped\n");
        return;
    }
    const QString path = tmp + "/drawing.svg";
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    // 20 × 10: the left half red, the right half transparent
    f.write("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='10' viewBox='0 0 20 10'>"
            "<rect x='0' y='0' width='10' height='10' fill='#ff0000'/></svg>");
    f.close();
    QString err;
    const QImage img = readImageFile(path, -1, &err, 400);
    check(img.size() == QSize(400, 200), "SVG rendered at the asked longer side, its aspect kept");
    check(!img.isNull() && near(img.pixelColor(100, 100), 255, 0, 0) && img.pixelColor(300, 100).alpha() == 0,
          "SVG: red where drawn, transparent elsewhere");
    check(readImageFile(path, -1, &err, 100000).width() == kMaxSvgSide, "SVG size capped");
    check(readImageFile(path).size() == QSize(20, 10), "SVG at its own size when nothing larger is asked");

    e.newProject();
    e.setCompositionSize(QSize(1280, 720));
    const int row = e.addLayer("Drawing");
    check(e.setLayerFile(row, path, &err), "an SVG loads as an image");
    e.renderFrame();
    check(e.layer(row)->srcWidth == 1280 && e.layer(row)->srcHeight == 640, "drawn as large as the composition");
    const QImage src = e.grabLayerSource(row);
    check(!src.isNull() && near(src.pixelColor(100, 320), 255, 0, 0) && src.pixelColor(1000, 320).alpha() < 3,
          "SVG layer: red and transparent");
}

void streamTests(Engine &e, const QString &tmp)
{
    check(Engine::isStreamUrl("rtsp://cam.local/live") && Engine::isStreamUrl("SRT://10.0.0.2:9000") &&
              Engine::isStreamUrl("https://example.com/live.m3u8") && Engine::isStreamUrl("udp://@239.0.0.1:1234"),
          "stream URLs recognised");
    check(!Engine::isStreamUrl("/media/clip.mov") && !Engine::isStreamUrl("file:///media/clip.mov") &&
              !Engine::isStreamUrl("C:/media/clip.mov"),
          "files are not streams");
    const QString url = "rtsp://user:secret@cam.local:8554/live/main?x=1";
    check(Engine::streamUrlOfPath(Engine::streamPath(url)) == url && Engine::streamUrlOfPath(url) == url,
          "a stream's virtual path gives its URL back");
    check(Engine::streamName(url) == "cam.local:8554/live/main", "its name: host, port and path, no credentials");

    // Nothing listens there: the layer is set at once, the decoder reports why it connects again, the render thread
    // never waits for it
    e.newProject();
    // Keep software OpenGL allocation and the previous SVG test's large render
    // target out of the network latency measurement.
    e.setCompositionSize(QSize(128, 64));
    e.renderFrame();
    const int row = e.addLayer("Cam");
    const QString refused = "rtsp://127.0.0.1:9/none";
    QElapsedTimer timer;
    timer.start();
    QString err;
    check(e.setLayerStream(row, refused, false, &err), "a stream is set without connecting first");
    check(timer.elapsed() < 1000, "setting it does not wait for the connection");
    qint64 slowest = 0;
    bool reconnecting = false;
    QString reason;
    timer.start();
    while (timer.elapsed() < 10000 && !reconnecting) {
        QElapsedTimer frame;
        frame.start();
        e.renderFrame();
        slowest = std::max(slowest, frame.elapsed());
        Engine::Lock lk(&e.mutex());
        const Layer *l = e.layer(row);
        if (l->video && l->video->isLive()) {
            const VideoDecoder::LiveStatus st = l->video->liveStatus();
            reconnecting = st.state == VideoDecoder::LiveState::Reconnecting && !st.reason.isEmpty();
            reason = st.reason;
        }
        lk.unlock();
        QThread::msleep(10);
    }
    std::printf("      unreachable stream: %s (slowest frame %lld ms)\n", qPrintable(reason), static_cast<long long>(slowest));
    check(reconnecting, "an unreachable stream reports an error and connects again");
    check(slowest < 250, "the render thread never waits for the stream");

    const QJsonObject src = e.layerJson(row).value("source").toObject();
    check(src.value("type").toString() == "video" && src.value("path").toString() == refused &&
              src.value("live").toBool() && !src.value("rtsp_tcp").toBool(true) && !src.contains("relative_path"),
          "JSON: the URL as it is, live, RTSP over UDP");

    e.addBinStream("srt://127.0.0.1:9/feed", false);
    bool inBin = false;
    for (const Engine::MediaRef &r : e.mediaUsage())
        if (r.path == "srt://127.0.0.1:9/feed") inBin = r.video && !r.missing;
    check(inBin, "a stream in the Media Bin: a video, never missing");

    const QString project = tmp + "/stream.nyxmap";
    check(e.saveProject(project, {}, &err), "save the project");
    e.newProject();
    check(e.loadProject(project, nullptr, &err), "open it again");
    const int back = layerNamed(e, "Cam");
    check(back >= 0 && e.layer(back)->videoSource.live && !e.layer(back)->videoSource.rtspTcp &&
              e.layer(back)->sourcePath == refused,
          "stream restored with its transport");
    check(e.binItems().contains("srt://127.0.0.1:9/feed") && !e.binStreamTcp("srt://127.0.0.1:9/feed"),
          "Media Bin stream restored");

    // An address that does not answer: closing never waits for the connection's time limit
    const QString silent = "rtsp://10.255.255.1:554/none";
    check(e.setLayerStream(back, silent, true, &err), "a stream that does not answer");
    QThread::msleep(200);
    timer.start();
    e.clearLayerSource(back);
    check(timer.elapsed() < 2000, "closing a stream that is connecting is immediate");
}
// Collect Files ▸ Move: a shader of an ISF library is copied, its original never deleted (other projects, and the
// application's own bundled library, use it); a shader of the project's own is moved
void collectLibraryTests(Engine &e, const QString &tmp)
{
    const QString lib = tmp + "/isf_library", own = tmp + "/own_shaders";
    QDir(tmp + "/collect_library").removeRecursively();
    check(QDir().mkpath(lib + "/Generators") && QDir().mkpath(own), "library and project shader folders");
    auto put = [](const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write("/*{\"ISFVSN\":\"2\",\"INPUTS\":[]}*/\nvoid main(){ gl_FragColor = vec4(1.0); }\n");
        return true;
    };
    const QString shared = lib + "/Generators/Shared.fs", mine = own + "/Mine.fs";
    check(put(shared) && put(mine), "two generators written");
    const QStringList folders = e.library().userFolders();
    e.library().setUserFolders(folders + QStringList{lib});
    e.newProject();
    QString err;
    check(e.setLayerIsf(e.addLayer("Library"), shared, &err) && e.setLayerIsf(e.addLayer("Own"), mine, &err),
          "a library shader and a project shader");

    const Engine::CollectPlan plan = e.planCollect(tmp + "/collect_library", "LibShow", {});
    bool sharedKept = false, mineKept = true;
    for (const Engine::CollectFile &f : plan.files) {
        if (f.from == shared) sharedKept = f.keepOriginal;
        if (f.from == mine) mineKept = f.keepOriginal;
    }
    check(sharedKept && !mineKept && plan.kept == QStringList{shared}, "only the library's shader is kept by Move");
    check(e.collect(plan, &err) && e.loadProject(plan.projectFile, nullptr, &err), "collected and opened");
    QStringList failed;
    check(Engine::removeCollectedOriginals(plan, &failed) == 1 && failed.isEmpty(), "Move deletes one original");
    check(QFileInfo::exists(shared) && !QFileInfo::exists(mine), "the library's shader is still there, the project's moved");
    check(QFileInfo::exists(plan.folder + "/MEDIA/ISF/Shared.fs") && QFileInfo::exists(plan.folder + "/MEDIA/ISF/Mine.fs"),
          "both shaders collected");
    e.library().setUserFolders(folders);
    e.newProject();
}
} // namespace

int runFileSourceTests(Engine &e, const QString &tmp)
{
    failures = 0;
    collectLibraryTests(e, tmp);
    sequenceTests(e, tmp);
    svgTests(e, tmp);
    streamTests(e, tmp);
    return failures;
}
