// The 3D views through QRhi against OpenGL: a few primitives drawn flat with their edges (Simple) and with their colours as
// they are (Unlit), by Scene3D (OpenGL) and RhiScene (QRhi), picture by picture.
//
// nyxmap_rhi_scene_test   [--dump folder]   Exit 77 (skipped) without OpenGL or a QRhi backend.
#include "Gl.h"
#include "Model3D.h"
#include "RhiDevice.h"
#include "RhiResources.h"
#include "RhiScene.h"

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <rhi/qrhi.h>

#include <cmath>
#include <cstdio>

static int g_failures = 0;
static void check(bool ok, const char *what)
{
    std::printf("  %s%s\n", what, ok ? "" : "  FAIL");
    g_failures += !ok;
}

static double meanDifference(const QImage &a, const QImage &b, int *max = nullptr, double *over8 = nullptr)
{
    if (a.isNull() || a.size() != b.size()) return 255;
    double sum = 0, over = 0;
    int worst = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar *p = a.constScanLine(y), *q = b.constScanLine(y);
        for (int x = 0; x < a.width() * 4; ++x) {
            const int v = std::abs(int(p[x]) - int(q[x]));
            sum += v;
            over += v > 8;
            worst = std::max(worst, v);
        }
    }
    if (max) *max = worst;
    if (over8) *over8 = over / (double(a.width()) * a.height() * 4);
    return sum / (double(a.width()) * a.height() * 4);
}

int main(int argc, char **argv)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QGuiApplication app(argc, argv);
    QString dump;
    if (const int i = app.arguments().indexOf(QStringLiteral("--dump")); i >= 0 && i + 1 < app.arguments().size()) dump = app.arguments().at(i + 1);
    QString backend;
    std::unique_ptr<QRhi> rhi = rhidevice::create({}, &backend);
    if (!rhi) {
        std::printf("SKIP: no QRhi backend here\n");
        return 77;
    }
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();
    if (!ctx.create() || !ctx.makeCurrent(&surface)) {
        std::printf("SKIP: no OpenGL context\n");
        return 77;
    }
    std::printf("QRhi %s\n", qPrintable(backend));
    const QSize size(256, 192);
    SceneSettings view;
    view.distance = 3;
    view.yaw = 35;
    view.pitch = 20;
    view.background = {0.1f, 0.1f, 0.12f, 1};

    for (const char *name : {"Sphere", "Cube", "Cylinder", "Cone"}) {
        auto model = std::make_shared<Model3D>();
        QString err;
        if (!model->load(Model3D::primitivePath(QString::fromLatin1(name)), &err)) {
            check(false, "a primitive loads");
            continue;
        }
        Scene3D::Item item;
        item.model = model;
        item.placement.color = {0.9f, 0.4f, 0.2f};
        item.placement.rotation = {10, 25, 0};
        item.placement.position = {0.2f, 0, 0};
        for (Scene3D::ViewLook look : {Scene3D::ViewLook::Simple, Scene3D::ViewLook::Unlit}) {
            // OpenGL
            Scene3D viaGl;
            const GLuint tex = viaGl.drawView(view, {item}, size.width(), size.height(), look);
            auto f = gl();
            GLuint fbo = 0;
            f->glGenFramebuffers(1, &fbo);
            f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            QImage g(size, QImage::Format_RGBA8888);
            f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            f->glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, g.bits());
            f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            f->glDeleteFramebuffers(1, &fbo);
            viaGl.releaseGl();
            // QRhi
            RhiScene scene(rhi.get());
            QRhiCommandBuffer *cb = nullptr;
            QImage q;
            if (rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess) {
                RhiResources::of(rhi.get()).frameStarted();
                QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
                QRhiTexture *out = scene.drawView(cb, u, view, {item}, size.width(), size.height(), look);
                QRhiReadbackResult rb;
                if (out) {
                    QRhiResourceUpdateBatch *r = rhi->nextResourceUpdateBatch();
                    r->readBackTexture({out}, &rb);
                    cb->resourceUpdate(r);
                }
                rhi->endOffscreenFrame();
                if (out) q = QImage(reinterpret_cast<const uchar *>(rb.data.constData()), rb.pixelSize.width(), rb.pixelSize.height(), QImage::Format_RGBA8888).copy();
            }
            int worst = 0;
            const double mean = meanDifference(q, g, &worst);
            const QString what = QStringLiteral("%1, %2: QRhi against OpenGL, mean %3, worst %4")
                                     .arg(QLatin1String(name), look == Scene3D::ViewLook::Simple ? "simple" : "unlit")
                                     .arg(mean, 0, 'f', 3).arg(worst);
            check(!q.isNull() && mean < 1.5, qPrintable(what));
            if (!dump.isEmpty()) {
                const QString base = dump + "/" + QLatin1String(name) + (look == Scene3D::ViewLook::Simple ? "_simple" : "_unlit");
                g.save(base + "_gl.png");
                q.save(base + "_rhi.png");
            }
        }
    }

    // Lit scenes: lights of every kind, the studio, materials. Effects, shadows and antialiasing off on both sides
    {
        SceneSettings lit = view;
        lit.ao = 0;
        lit.reflections = 0;
        lit.glow = 0;
        lit.fogDensity = 0;
        lit.ambientIntensity = 0.1f;
        Scene3DQuality off;
        off.antialiasing = off.shadows = off.contact = off.reflections = off.glow = off.atmosphere = 0;
        struct Case {
            const char *name;
            const char *primitive;
            float metal, rough;
            int light; // -1: only the studio
        };
        for (const Case &c : {Case{"studio only", "Sphere", 0.0f, 0.5f, -1}, Case{"metal under the studio", "Sphere", 1.0f, 0.25f, -1},
                              Case{"point", "Sphere", 0.0f, 0.5f, 1}, Case{"spot", "Cube", 0.3f, 0.4f, 2},
                              Case{"directional", "Cylinder", 0.0f, 0.7f, 0}, Case{"area", "Sphere", 0.2f, 0.3f, 3}}) {
            auto model = std::make_shared<Model3D>();
            QString err;
            model->load(Model3D::primitivePath(QString::fromLatin1(c.primitive)), &err);
            Scene3D::Item item;
            item.model = model;
            item.placement.color = {0.8f, 0.5f, 0.3f};
            item.placement.metal = c.metal;
            item.placement.rough = c.rough;
            item.placement.rotation = {10, 25, 0};
            std::vector<Scene3D::Lamp> lamps;
            if (c.light >= 0) {
                Scene3D::Lamp lamp;
                lamp.light.type = c.light;
                lamp.light.shadow = false;
                lamp.light.intensity = c.light == 0 ? 3.0f : 25.0f;
                lamp.light.position = {1.5f, 2.5f, 2.5f};
                lamp.light.yaw = 200;
                lamp.light.pitch = -40;
                lamp.light.angle = 35;
                lamps.push_back(lamp);
            }
            Scene3D viaGl;
            const GLuint tex = viaGl.draw(lit, {item}, lamps, size.width(), size.height(), 0, false, off);
            auto f = gl();
            GLuint fbo = 0;
            f->glGenFramebuffers(1, &fbo);
            f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            QImage g(size, QImage::Format_RGBA8888);
            f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            f->glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, g.bits());
            f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            f->glDeleteFramebuffers(1, &fbo);
            viaGl.releaseGl();
            RhiScene scene(rhi.get());
            QRhiCommandBuffer *cb = nullptr;
            QImage q;
            if (rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess) {
                RhiResources::of(rhi.get()).frameStarted();
                QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
                QRhiTexture *out = scene.draw(cb, u, lit, {item}, lamps, size.width(), size.height(), off);
                QRhiReadbackResult rb;
                if (out) {
                    QRhiResourceUpdateBatch *r = rhi->nextResourceUpdateBatch();
                    r->readBackTexture({out}, &rb);
                    cb->resourceUpdate(r);
                }
                rhi->endOffscreenFrame();
                if (out) q = QImage(reinterpret_cast<const uchar *>(rb.data.constData()), rb.pixelSize.width(), rb.pixelSize.height(), QImage::Format_RGBA8888).copy();
            }
            int worst = 0;
            double over = 0;
            const double mean = meanDifference(q, g, &worst, &over);
            const QString what = QStringLiteral("lit, %1: QRhi against OpenGL, mean %2, %3% over 8")
                                     .arg(QLatin1String(c.name)).arg(mean, 0, 'f', 3).arg(over * 100, 0, 'f', 2);
            check(!q.isNull() && mean < 0.5 && over < 0.005, qPrintable(what)); // the silhouette's pixels differ: no antialiasing
            if (!dump.isEmpty()) {
                QString base = dump + "/lit_" + QLatin1String(c.name);
                base.replace(' ', '_');
                g.save(base + "_gl.png");
                q.save(base + "_rhi.png");
            }
        }
    }

    // No limit to the lights (OpenGL took eight): twelve give more light than eight
    {
        auto model = std::make_shared<Model3D>();
        QString err;
        model->load(Model3D::primitivePath(QStringLiteral("Sphere")), &err);
        Scene3D::Item item;
        item.model = model;
        item.placement.color = {0.8f, 0.8f, 0.8f};
        SceneSettings lit = view;
        lit.ambientIntensity = 0;
        lit.studio = 0;
        Scene3DQuality off;
        off.antialiasing = off.shadows = off.contact = off.reflections = off.glow = off.atmosphere = 0;
        const auto brightness = [&](int count) {
            std::vector<Scene3D::Lamp> lamps;
            for (int k = 0; k < count; ++k) {
                Scene3D::Lamp lamp;
                lamp.light.type = 1;
                lamp.light.shadow = false;
                lamp.light.intensity = 4;
                lamp.light.position = {1.5f * std::cos(k * 0.5f), 2.0f, 1.5f * std::sin(k * 0.5f) + 2};
                lamps.push_back(lamp);
            }
            RhiScene scene(rhi.get());
            QRhiCommandBuffer *cb = nullptr;
            double sum = -1;
            if (rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess) {
                RhiResources::of(rhi.get()).frameStarted();
                QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
                QRhiTexture *out = scene.draw(cb, u, lit, {item}, lamps, size.width(), size.height(), off);
                QRhiReadbackResult rb;
                if (out) {
                    QRhiResourceUpdateBatch *r = rhi->nextResourceUpdateBatch();
                    r->readBackTexture({out}, &rb);
                    cb->resourceUpdate(r);
                }
                rhi->endOffscreenFrame();
                if (out) {
                    const QImage img(reinterpret_cast<const uchar *>(rb.data.constData()), rb.pixelSize.width(), rb.pixelSize.height(), QImage::Format_RGBA8888);
                    sum = 0;
                    for (int y = 0; y < img.height(); ++y)
                        for (int x = 0; x < img.width(); ++x) sum += QColor(img.pixel(x, y)).lightnessF();
                }
            }
            return sum;
        };
        const double eight = brightness(8), twelve = brightness(12), thirty = brightness(30);
        std::printf("  lights: 8 -> %.1f, 12 -> %.1f, 30 -> %.1f\n", eight, twelve, thirty);
        check(eight > 0 && twelve > eight * 1.01 && thirty > twelve * 1.01, "lights: no limit (twelve and thirty light more than eight)");
    }
    std::printf(g_failures ? "%d failure(s)\n" : "ok\n", g_failures);
    return g_failures ? 1 : 0;
}
