// Every ISF shader drawn by the QRhi renderer (IsfInstance::renderRhi) and by the OpenGL one (render()), from the
// same picture and the same time, three frames in a row (passes, persistent buffers, points): the pictures compared.
// Shaders whose noise is computed from sin() or fract() of large values differ between GPUs and APIs (not a fault):
// they are told apart as "noise" when the pictures averaged over 8 × 8 blocks still agree.
//
// nyxmap_isf_render_test [--verbose] [--dump DIR] [folders or .fs files]  (default: the shipped shaders)
// Fails when a shipped shader draws differently. Exit 77 (skipped) without a QRhi backend or an OpenGL context.
#include "Gl.h"
#include "Isf.h"
#include "RhiDevice.h"
#include "RhiResources.h"
#include "RhiTarget.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

static const QSize kSize(256, 144);
static const int kFrames = 3;

// The input picture: smooth colours, an edge, asymmetric (a flipped picture would show)
static QImage inputPicture()
{
    QImage img(kSize, QImage::Format_RGBA8888);
    for (int y = 0; y < kSize.height(); ++y)
        for (int x = 0; x < kSize.width(); ++x) {
            uchar *p = img.scanLine(y) + x * 4;
            p[0] = uchar(255 * x / (kSize.width() - 1));
            p[1] = uchar(255 * y / (kSize.height() - 1));
            p[2] = x > kSize.width() * 2 / 3 && y < kSize.height() / 3 ? 230 : 40;
            p[3] = 255;
        }
    return img;
}

struct Diff {
    int max = 0;
    double mean = 0, over8 = 0; // share of channels more than 8 apart
    double blocks = 0;          // the largest difference of 8 × 8 block averages (noise averages out, a fault does not)
};
static Diff compare(const QImage &a, const QImage &b)
{
    Diff d;
    if (a.size() != b.size() || a.isNull()) {
        d.max = 255;
        d.mean = 255;
        d.over8 = 1;
        return d;
    }
    double sum = 0, over = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar *p = a.constScanLine(y), *q = b.constScanLine(y);
        for (int x = 0; x < a.width() * 4; ++x) {
            const int v = std::abs(int(p[x]) - int(q[x]));
            d.max = std::max(d.max, v);
            sum += v;
            over += v > 8;
        }
    }
    const double n = double(a.width()) * a.height() * 4;
    d.mean = sum / n;
    d.over8 = over / n;
    for (int by = 0; by + 8 <= a.height(); by += 8)
        for (int bx = 0; bx + 8 <= a.width(); bx += 8)
            for (int c = 0; c < 4; ++c) {
                double sa = 0, sb = 0;
                for (int y = by; y < by + 8; ++y)
                    for (int x = bx; x < bx + 8; ++x) {
                        sa += a.constScanLine(y)[x * 4 + c];
                        sb += b.constScanLine(y)[x * 4 + c];
                    }
                d.blocks = std::max(d.blocks, std::abs(sa - sb) / 64);
            }
    return d;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    const bool verbose = args.removeAll(QStringLiteral("--verbose")) > 0;
    QString dump;
    if (const qsizetype i = args.indexOf(QStringLiteral("--dump")); i >= 0 && i + 1 < args.size()) {
        dump = args.takeAt(i + 1);
        args.removeAt(i);
        QDir().mkpath(dump);
    }
    const bool shipped = args.isEmpty();
    if (shipped) args = {QStringLiteral(SOURCE_DIR "/isf"), QStringLiteral(SOURCE_DIR "/test/isf")};
    QStringList files;
    for (const QString &a : args) {
        if (QFileInfo(a).isFile()) files << a;
        else
            for (QDirIterator it(a, {QStringLiteral("*.fs")}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
                files << it.next();
    }
    files.sort();

    QString backend;
    std::unique_ptr<QRhi> rhi = rhidevice::create({}, &backend);
    if (!rhi) {
        std::printf("SKIP: no QRhi backend here\n");
        return 77;
    }
    if (rhidevice::isVirtualDevice(rhi.get())) {
        std::printf("SKIP: %s is a virtual GPU, its pictures are not OpenGL's to the pixel\n", rhi->driverInfo().deviceName.constData());
        return 77;
    }
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();
    if (!ctx.create() || !ctx.makeCurrent(&surface)) {
        std::printf("SKIP: no OpenGL context\n");
        return 77;
    }

    // OpenGL: what the engine hands render() (a quad, points from gl_VertexID, a copy, a black picture)
    auto f = gl();
    const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    GLuint vao = 0, vbo = 0, pointsVao = 0, blackTex = 0;
    f->glGenVertexArrays(1, &vao);
    f->glBindVertexArray(vao);
    f->glGenBuffers(1, &vbo);
    f->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    f->glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    f->glBindVertexArray(0);
    f->glGenVertexArrays(1, &pointsVao);
    const unsigned char black[4] = {0, 0, 0, 0};
    f->glGenTextures(1, &blackTex);
    f->glBindTexture(GL_TEXTURE_2D, blackTex);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    QString err;
    const GLuint blitProgram = compileProgram(
        QStringLiteral("#version 330 core\nlayout(location = 0) in vec2 p; out vec2 uv;\n"
                       "void main() { uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n"),
        QStringLiteral("#version 330 core\nin vec2 uv; out vec4 o; uniform sampler2D t;\nvoid main() { o = texture(t, uv); }\n"), &err);
    IsfRenderContext rc;
    rc.dt = 1.0 / 60;
    rc.blackTex = blackTex;
    rc.drawQuad = [&] {
        f->glBindVertexArray(vao);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        f->glBindVertexArray(0);
    };
    rc.drawPoints = [&](int count) {
        f->glBindVertexArray(pointsVao);
        f->glDrawArrays(GL_TRIANGLES, 0, count * 6);
        f->glBindVertexArray(0);
    };
    rc.blit = [&](GLuint tex, const RenderTarget &t) {
        t.bind();
        f->glDisable(GL_BLEND);
        f->glUseProgram(blitProgram);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, tex);
        f->glUniform1i(f->glGetUniformLocation(blitProgram, "t"), 0);
        rc.drawQuad();
    };
    const QImage picture = inputPicture();
    Texture2D glInput;
    glInput.uploadImage(prepareImageUpload(picture)); // first row: the bottom

    // QRhi: the same picture
    RhiResources &res = RhiResources::of(rhi.get());
    std::unique_ptr<QRhiTexture> rhiInput(rhi->newTexture(QRhiTexture::RGBA8, kSize));
    rhiInput->create();
    {
        QRhiCommandBuffer *cb = nullptr;
        rhi->beginOffscreenFrame(&cb);
        QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
        u->uploadTexture(rhiInput.get(), prepareImageUpload(picture).convertToFormat(QImage::Format_RGBA8888));
        cb->resourceUpdate(u);
        rhi->endOffscreenFrame();
    }
    std::printf("QRhi %s against OpenGL, %d×%d, %d frames each\n", qPrintable(backend), kSize.width(), kSize.height(), kFrames);

    int same = 0, noise = 0, read = 0;
    QStringList different;
    QElapsedTimer timer;
    timer.start();
    for (const QString &path : files) {
        const QString name = QFileInfo(path).fileName();
        // OpenGL
        IsfInstance viaGl;
        if (!viaGl.load(path)) continue; // not drawn by NyxMap at all
        ++read;
        RenderTarget glOut;
        for (int k = 0; k < kFrames; ++k)
            viaGl.render(rc, viaGl.isFilter() ? glInput.tex : 0, kSize.width(), kSize.height(), glOut, kSize.width(), kSize.height());
        QImage g(kSize, QImage::Format_RGBA8888);
        glOut.bind();
        f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
        f->glReadPixels(0, 0, kSize.width(), kSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, g.bits());
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glOut.destroy();
        viaGl.releaseGl();

        // QRhi
        IsfInstance viaRhi;
        QImage q;
        if (viaRhi.loadRhi(path, rhi.get())) {
            RhiTarget out;
            QRhiReadbackResult rb;
            for (int k = 0; k < kFrames; ++k) {
                QRhiCommandBuffer *cb = nullptr;
                rhi->beginOffscreenFrame(&cb);
                res.frameStarted();
                viaRhi.renderRhi(cb, rc, viaRhi.isFilter() ? rhiInput.get() : nullptr, out, kSize);
                if (k == kFrames - 1 && out.ready()) {
                    QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
                    u->readBackTexture({out.texture()}, &rb);
                    cb->resourceUpdate(u);
                }
                rhi->endOffscreenFrame();
            }
            q = QImage(reinterpret_cast<const uchar *>(rb.data.constData()), rb.pixelSize.width(), rb.pixelSize.height(),
                       QImage::Format_RGBA8888)
                    .copy();
            viaRhi.releaseRhi();
        } else if (verbose) {
            std::printf("  %s: %s\n", qPrintable(name), qPrintable(viaRhi.error().left(300)));
        }

        const Diff d = compare(q, g);
        const bool equal = d.max <= 3 || (d.mean <= 0.3 && d.over8 <= 0.001);
        // GPU noise (hashes of sin() or fract() of large values, thin lines a sub-pixel apart): pixels differ, the
        // picture does not — its 8 × 8 averages agree, or only a few pixels differ
        const bool spread = !equal && (d.blocks <= 8 || (d.over8 <= 0.002 && d.mean <= 1));
        if (equal) ++same;
        else if (spread) ++noise;
        else different << QStringLiteral("%1 (max %2, mean %3, %4% over 8)")
                              .arg(name)
                              .arg(d.max)
                              .arg(d.mean, 0, 'f', 2)
                              .arg(d.over8 * 100, 0, 'f', 1);
        if (verbose || (!equal && !spread))
            std::printf("  %-36s max %3d mean %6.2f over8 %5.1f%% blocks %5.1f%s\n", qPrintable(name), d.max, d.mean, d.over8 * 100, d.blocks,
                        equal ? "" : spread ? "  (noise)" : "  DIFFERENT");
        if (!dump.isEmpty() && !equal) {
            q.mirrored(false, true).save(dump + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QStringLiteral(".rhi.png"));
            g.mirrored(false, true).save(dump + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QStringLiteral(".gl.png"));
        }
    }
    std::printf("ISF drawn through QRhi: %d of %d as OpenGL draws them, %d with GPU noise, %d different, %.1f s\n", same, read,
                noise, int(different.size()), timer.elapsed() / 1000.0);
    for (const QString &s : different) std::printf("  different: %s\n", qPrintable(s));
    glInput.destroy();
    RhiResources::release(rhi.get());
    return shipped && !different.isEmpty() ? 1 : 0;
}
