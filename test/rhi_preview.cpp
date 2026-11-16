// The interface's previews with the QRhi renderer: its slots are OpenGL textures still, filled from the QRhi pictures
// read back. The composition's and a layer's previews are compared with the pictures the engine gives out.
//
// nyxmap_rhi_preview_test   Exit 77 (skipped) without OpenGL or a QRhi backend.
#include "Engine.h"
#include "Layer.h"

#include <QGuiApplication>
#include <QImage>
#include <QSurfaceFormat>
#include <QThread>

#include <cmath>
#include <cstdio>

static int g_failures = 0;
static void check(bool ok, const char *what)
{
    std::printf("  %s%s\n", what, ok ? "" : "  FAIL");
    g_failures += !ok;
}

// A preview slot's texture as an image, top row first (a framebuffer over the texture, read)
static QImage readSlot(Engine &e, int slot)
{
    QImage img;
    e.runGl([&] {
        Engine::Lock lock(&e.mutex());
        QSize size;
        const GLuint tex = e.previewTexture(slot, &size);
        if (!tex || size.isEmpty()) return;
        QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
        GLuint fbo = 0;
        f->glGenFramebuffers(1, &fbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        img = QImage(size, QImage::Format_RGBA8888);
        f->glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        f->glDeleteFramebuffers(1, &fbo);
        img = img.mirrored(false, true);
    });
    return img;
}

static double meanDifference(const QImage &a, const QImage &b)
{
    if (a.isNull() || a.size() != b.size()) return 255;
    const QImage x = a.convertToFormat(QImage::Format_RGBA8888), y = b.convertToFormat(QImage::Format_RGBA8888);
    double sum = 0;
    for (int r = 0; r < x.height(); ++r) {
        const uchar *p = x.constScanLine(r), *q = y.constScanLine(r);
        for (int c = 0; c < x.width() * 4; ++c) sum += std::abs(int(p[c]) - int(q[c]));
    }
    return sum / (double(x.width()) * x.height() * 4);
}

int main(int argc, char **argv)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QGuiApplication app(argc, argv);
    Engine e;
    e.setRhiRendering(true);
    QString err;
    if (!e.initialize(&err)) {
        std::printf("SKIP: no OpenGL: %s\n", qPrintable(err));
        return 77;
    }
    e.newProject();
    e.setCompositionSize(QSize(320, 180));
    const int li = e.addLayer(QStringLiteral("Bars"));
    if (!e.setLayerImage(li, QStringLiteral(TEST_DIR) + "/media/bars.png", &err)) {
        std::printf("FAIL: bars.png: %s\n", qPrintable(err));
        return 1;
    }
    const quint64 id = e.layerId(li);
    e.setPreviewSource(0, 0);  // the composition
    e.setPreviewSource(1, id); // a layer
    for (int k = 0; k < 10; ++k) {
        e.renderFrame();
        QThread::msleep(10);
    }
    if (!e.rhiRendering()) {
        std::printf("SKIP: no QRhi backend\n");
        return 77;
    }
    const QImage composition = readSlot(e, 0), layer = readSlot(e, 1);
    check(!composition.isNull(), "the composition's preview is there");
    check(!layer.isNull(), "the layer's preview is there");
    const double a = meanDifference(composition, e.grabOutput());
    std::printf("  composition preview against the composition: mean %.3f\n", a);
    check(a < 1.0, "the composition's preview is its picture, the right way up");
    // A layer that is not there shows nothing, never a stale picture
    e.setPreviewSource(2, 987654321);
    for (int k = 0; k < 3; ++k) e.renderFrame();
    GLuint missing = 1;
    e.runGl([&] {
        Engine::Lock lock(&e.mutex());
        missing = e.previewTexture(2, nullptr);
    });
    check(!missing, "a layer that is not there shows nothing");
    e.shutdown();
    std::printf(g_failures ? "%d failure(s)\n" : "ok\n", g_failures);
    return g_failures ? 1 : 0;
}
