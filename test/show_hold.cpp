#include "Engine.h"
#include "LiveInput.h"
#include <QColor>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

namespace {
class CountingInput final : public LiveInput {
public:
    int frames = 0;
    bool update(Texture2D &texture) override {
        ++frames;
        QImage image(32, 32, QImage::Format_RGBA8888);
        image.fill(frames % 2 ? Qt::green : Qt::blue);
        texture.uploadImage(image);
        return true;
    }
};
}

int runShowHoldTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL show_hold: %s\n", why); }
    };
    engine.newProject();
    engine.setCompositionSize({32, 32});
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {32, 32});
    QTemporaryDir files;
    const QString feedback = files.path() + "/Feedback.fs";
    {
        QFile file(feedback);
        check(file.open(QIODevice::WriteOnly), "create feedback shader");
        file.write(R"(/*{"ISFVSN":"2","PASSES":[{"TARGET":"history","PERSISTENT":true},{}]}*/
void main() {
    if (PASSINDEX == 0) gl_FragColor = vec4(IMG_NORM_PIXEL(history,isf_FragNormCoord).r + 0.05,0.0,0.0,1.0);
    else gl_FragColor = IMG_NORM_PIXEL(history,isf_FragNormCoord);
}
)");
    }
    QString error;
    const int generator = engine.addLayer("Feedback");
    check(engine.setLayerIsf(generator, feedback, &error), "load persistent shader");
    engine.setGeneratorSize(generator, 32, 32);
    engine.renderFrame();
    const QImage first = engine.grabOutput();
    check(!first.isNull() && first.pixelColor(16, 16).red() > 5, "feedback produces a visible first frame");
    engine.setPaused(true);
    engine.applyComposition({{"included", true}, {"opacity", 0.0}}, .001);
    for (int i = 0; i < 4; ++i) engine.renderFrame();
    check(engine.compositionOpacityTarget() == 1, "snapshot composition fades pause with layer fades");
    check(engine.grabOutput() == first, "Pause freezes persistent feedback and FRAMEINDEX");
    engine.setBlackout(true, 0);
    engine.renderFrame();
    check(engine.grabOutput().pixelColor(16, 16).red() == 0, "blackout remains available while paused");
    engine.setBlackout(false, 0);
    engine.renderFrame();
    check(engine.grabOutput() == first, "removing blackout reveals the held frame");
    engine.applyComposition({{"included", true}, {"opacity", 1.0}}, 0);
    engine.setPaused(false);
    engine.renderFrame();
    const QImage resumed = engine.grabOutput();
    check(resumed.pixelColor(16, 16).red() > first.pixelColor(16, 16).red(), "feedback resumes from retained buffer");
    engine.stopShowPlayback();
    check(engine.paused(), "Escape latches a visible Pause");
    for (int i = 0; i < 4; ++i) engine.renderFrame();
    check(engine.grabOutput() == resumed, "Escape holds shader frames on later renders");
    engine.setPaused(false);
    engine.renderFrame();
    check(engine.grabOutput().pixelColor(16, 16).red() > resumed.pixelColor(16, 16).red(), "release Pause resumes after Escape");

    const int live = engine.addLayer("Live frame counter");
    auto input = std::make_unique<CountingInput>();
    auto *counter = input.get();
    engine.layer(live)->type = SourceType::Input;
    engine.layer(live)->input = std::move(input);
    engine.renderFrame();
    const int received = counter->frames;
    const QImage liveFrame = engine.grabOutput();
    engine.setCompositionSpeed(0);
    for (int i = 0; i < 4; ++i) engine.renderFrame();
    check(counter->frames == received && engine.grabOutput() == liveFrame, "speed zero holds external input consumption");
    engine.setCompositionSpeed(1);
    engine.renderFrame();
    check(counter->frames == received + 1, "resume consumes the newest external frame once");

    // A picture loaded while paused shows at once, the other layers keep their held frame
    const QString yellow = files.path() + "/yellow.png";
    {
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::yellow);
        check(image.save(yellow), "write a picture");
    }
    engine.setPaused(true);
    engine.renderFrame();
    const int receivedPaused = counter->frames;
    const int picture = engine.addLayer("Picture", 0); // at the top of the list: drawn over the others
    check(engine.setLayerImage(picture, yellow, &error), "load a picture while paused");
    engine.renderFrame();
    const QColor shown = engine.grabOutput().pixelColor(16, 16);
    check(shown.red() > 250 && shown.green() > 250 && shown.blue() < 5, "a picture loaded while paused shows at once");
    check(counter->frames == receivedPaused, "the other layers stay held");
    engine.removeLayer(engine.indexOfId(engine.layerId(picture)));
    engine.setPaused(false);

    // Exercise the shader uniforms themselves, without wall-clock sleeps or a
    // rendering thread: long elapsed time reaches TIME but not TIMEDELTA.
    const QString clock = files.path() + "/Clock.fs";
    {
        QFile file(clock);
        check(file.open(QIODevice::WriteOnly), "create clock shader");
        file.write("/*{\"ISFVSN\":\"2\"}*/\nvoid main(){gl_FragColor=vec4(TIME/100.0,TIMEDELTA,float(FRAMEINDEX)/100.0,1.0);}\n");
    }
    engine.runGl([&] {
        IsfInstance shader;
        check(shader.load(clock), "load clock shader");
        GLuint vao = 0, vbo = 0;
        auto f = gl();
        const float quad[] = {-1,-1, 1,-1, -1,1, 1,1};
        f->glGenVertexArrays(1, &vao);
        f->glGenBuffers(1, &vbo);
        f->glBindVertexArray(vao);
        f->glBindBuffer(GL_ARRAY_BUFFER, vbo);
        f->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        IsfRenderContext rc;
        rc.drawQuad = [&] { f->glBindVertexArray(vao); f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); };
        RenderTarget output;
        double total = 0;
        for (const double elapsed : {1.0/60, .65, 3.1, 1.0/120}) {
            const auto step = FrameStep::fromElapsed(elapsed, true);
            rc.dt = step.shaderDelta;
            rc.elapsed = step.elapsed;
            total += elapsed;
            shader.render(rc, 0, 0, 0, output, 32, 32);
            float pixel[4]{};
            output.bind();
            f->glReadPixels(16, 16, 1, 1, GL_RGBA, GL_FLOAT, pixel);
            check(std::abs(pixel[0] * 100 - total) < .002, "TIME catches up across 650 ms and 3.1 s stalls");
            check(std::abs(pixel[1] - step.shaderDelta) < .0001, "TIMEDELTA retains a bounded integration step");
        }
        output.destroy();
        shader.releaseGl();
        f->glBindVertexArray(0);
        f->glDeleteBuffers(1, &vbo);
        f->glDeleteVertexArrays(1, &vao);
    });
    engine.newProject();
    return failures;
}
