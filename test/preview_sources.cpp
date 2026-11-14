// GPU preview regression: independent layer/viewport sources, bounded copies, deletion and source switching.
#include "Engine.h"
#include "Gl.h"
#include <QFile>
#include <QImage>
#include <cstdio>
int runPreviewSourceTests(Engine &e, const QString &tmp)
{
    int failures = 0;
    auto check = [&](bool ok, const char *message) { if (!ok) { ++failures; std::printf("FAIL preview_sources: %s\n", message); } };
    e.newProject();
    e.setCompositionSize(QSize(64, 32));
    const quint64 viewport = e.mainViewportId();
    e.setViewportSize(e.indexOfId(viewport), QSize(64, 32));
    QImage image(2048, 1024, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    const QString path = tmp + "/preview-red.png";
    check(image.save(path), "save image");
    const int index = e.addLayer("Red");
    QString error;
    check(e.setLayerImage(index, path, &error), "load image");
    const quint64 id = e.layerId(index);
    e.setPreviewSource(0, id);
    e.setPreviewSource(1, viewport);
    e.setPreviewSource(2, 0);
    e.setPreviewSource(3, id);
    e.renderFrame();
    e.runGl([&] {
        Engine::Lock lock(&e.mutex());
        auto *f = gl();
        for (int slot = 0; slot < 4; ++slot) {
            QSize size;
            const GLuint texture = e.previewTexture(slot, &size);
            check(texture && !size.isEmpty(), "all four sources published");
            check(size.width() <= 1024 && size.height() <= 1024, "bounded preview dimensions");
            GLuint framebuffer = 0;
            f->glGenFramebuffers(1, &framebuffer);
            f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            unsigned char pixel[4] = {};
            f->glReadPixels(size.width()/2, size.height()/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            check(pixel[0] > 240 && pixel[1] < 10 && pixel[2] < 10, "selected source has red pixels");
            f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            f->glDeleteFramebuffers(1, &framebuffer);
        }
    });
    e.setPreviewSource(0, 999999);
    e.runGl([&] { Engine::Lock lock(&e.mutex()); check(!e.previewTexture(0, nullptr), "source switch never shows previous source"); });
    e.renderFrame();
    e.runGl([&] { Engine::Lock lock(&e.mutex()); check(!e.previewTexture(0, nullptr), "missing source stays empty"); });
    e.removeLayer(e.indexOfId(id));
    e.renderFrame();
    e.runGl([&] { Engine::Lock lock(&e.mutex()); check(!e.previewTexture(3, nullptr), "deleted layer never leaves a stale image"); });
    for (int slot = 0; slot < 4; ++slot) e.setPreviewSource(slot, 0);

    const int sourceRow = e.addLayer("Hidden source");
    const quint64 source = e.layerId(sourceRow);
    check(e.setLayerImage(sourceRow, path, &error), "load the linked source");
    e.setParameter(source, "opacity", 0.0);
    const int intermediateRow = e.addLayer("Transparent link");
    const quint64 intermediate = e.layerId(intermediateRow);
    check(e.setLayerSourceLayer(intermediateRow, source, LayerTap::PostFx, &error), "link the source");
    e.setParameter(intermediate, "opacity", 0.0);
    e.renderFrame();
    check(!e.layer(e.indexOfId(source))->needed && !e.layer(e.indexOfId(intermediate))->needed,
          "a transparent, unused link does not render its source");
    e.setPreviewSource(0, intermediate);
    e.renderFrame();
    check(e.layer(e.indexOfId(source))->finalTex && e.layer(e.indexOfId(intermediate))->finalTex,
          "an explicit preview still renders the transparent link and its source");
    e.setPreviewSource(0, 0);
    const int visible = e.addLayer("Visible link");
    check(e.setLayerSourceLayer(visible, intermediate, LayerTap::PostFx, &error), "link through the transparent layer");
    e.renderFrame();
    check(e.layer(e.indexOfId(source))->finalTex && e.layer(visible)->finalTex,
          "a visible link still follows a chain through transparent layers");
    const quint64 child = e.layerId(visible);
    const quint64 group = e.layerId(e.addGroup("Transparent group"));
    e.setStructure(tree::intoGroup(e.structure(), {child}, group));
    e.setParameter(group, "opacity", 0.0);
    e.renderFrame();
    check(!e.layer(e.indexOfId(group))->finalTex && !e.layer(e.indexOfId(child))->needed &&
              !e.layer(e.indexOfId(intermediate))->needed && !e.layer(e.indexOfId(source))->needed,
          "a transparent group and its otherwise unused source chain go idle");
    e.setPreviewSource(0, group);
    e.renderFrame();
    check(e.layer(e.indexOfId(group))->finalTex && e.layer(e.indexOfId(source))->finalTex,
          "a preview still composites a transparent group and wakes its source chain");
    e.setPreviewSource(0, 0);
    e.newProject();

    const QSize size(64, 32);
    e.setCompositionSize(size);
    const quint64 shifted = e.mainViewportId();
    e.setViewportSize(e.indexOfId(shifted), size);
    auto setViewCenter = [&](double x) {
        e.layer(e.indexOfId(shifted))->mapping.setRect({QPointF(x, 16), 64, 32, 0}, size);
    };
    setViewCenter(32);
    const int outside = e.addLayer("Outside the composition");
    check(e.setLayerImage(outside, path, &error), "load offscreen image");
    e.layer(outside)->mapping.position = QPointF(2.5, 0.5);
    e.renderFrame();
    check(!e.layer(outside)->needed && !e.layer(outside)->finalTex, "offscreen image is skipped");
    setViewCenter(160);
    e.renderFrame();
    check(e.layer(outside)->needed && e.grabViewport(shifted).pixelColor(32, 16).red() > 240,
          "a viewport outside the composition still sees the image");
    setViewCenter(32);
    e.renderFrame();
    e.setPaused(true);
    e.layer(outside)->mapping.position = QPointF(0.5, 0.5);
    e.renderFrame();
    check(e.grabOutput().pixelColor(32, 16).red() > 240, "an image moved into view while paused can be drawn");
    e.setPaused(false);
    e.newProject();
    e.setCompositionSize(size);

    auto writeShader = [&](const QString &file, const QByteArray &sourceCode) {
        QFile shader(tmp + '/' + file);
        check(shader.open(QIODevice::WriteOnly) && shader.write(sourceCode) == sourceCode.size(), "write ISF fixture");
        return shader.fileName();
    };
    const QString simple = writeShader(QStringLiteral("visibility-clock.fs"),
        "/*{\"ISFVSN\":\"2.0\"}*/\nvoid main() { gl_FragColor = vec4(float(FRAMEINDEX) / 10.0, 0.0, 0.0, 1.0); }\n");
    const int generator = e.addLayer("Hidden ISF");
    check(e.setLayerIsf(generator, simple, &error), "load single-pass generator");
    const quint64 generatorId = e.layerId(generator);
    e.setParameter(generatorId, "opacity", 0.0);
    for (int k = 0; k < 3; ++k) e.renderFrame();
    check(!e.layer(generator)->needed && !e.layer(generator)->generatorTarget.tex,
          "an unused single-pass ISF performs no GL draw or allocation");
    e.setParameter(generatorId, "opacity", 1.0);
    e.renderFrame();
    check(std::abs(e.grabOutput().pixelColor(32, 16).red() - 77) <= 2,
          "the generator's FRAMEINDEX advances while its drawing is skipped");
    e.setParameter(generatorId, "opacity", 0.0);
    e.renderFrame();
    e.renderFrame();
    e.setPaused(true);
    e.setParameter(generatorId, "opacity", 1.0);
    e.renderFrame();
    check(std::abs(e.grabOutput().pixelColor(32, 16).red() - 128) <= 2,
          "a generator brought back while paused draws the held frame");
    e.setPaused(false);
    e.renderFrame();
    check(std::abs(e.grabOutput().pixelColor(32, 16).red() - 153) <= 2,
          "drawing the held frame does not consume a frame of the generator's clock");

    const QString feedback = writeShader(QStringLiteral("visibility-feedback.fs"),
        "/*{\"ISFVSN\":\"2.0\",\"PASSES\":[{\"TARGET\":\"history\",\"PERSISTENT\":true},{}]}*/\n"
        "void main() { gl_FragColor = IMG_THIS_PIXEL(history) + vec4(0.1, 0.0, 0.0, 1.0); }\n");
    const int persistent = e.addLayer("Feedback ISF");
    check(e.setLayerIsf(persistent, feedback, &error), "load feedback generator");
    e.setParameter(e.layerId(persistent), "opacity", 0.0);
    e.renderFrame();
    check(e.layer(persistent)->needed && e.layer(persistent)->generatorTarget.tex,
          "a persistent shader keeps rendering while transparent");

    const quint64 scene = e.layerId(e.addScene("Invisible 3D scene"));
    check(e.setParameter(scene, "scene/reflections", 0.0), "turn off temporal reflections");
    e.setParameter(scene, "opacity", 0.0);
    const auto renderer = e.layer(e.indexOfId(scene))->sceneRenderer;
    const int drawnBefore = renderer->framesDrawn();
    e.renderFrame();
    check(renderer->framesDrawn() == drawnBefore && !e.layer(e.indexOfId(scene))->finalTex,
          "invisible 3D scene without temporal effects skips rendering");
    e.setPreviewSource(0, scene);
    e.renderFrame();
    check(renderer->framesDrawn() > drawnBefore && e.layer(e.indexOfId(scene))->finalTex,
          "preview wakes an invisible 3D scene");
    e.setPreviewSource(0, 0);
    e.newProject();
    return failures;
}
