#include "Engine.h"
#include "ColorInfo.h"
#include "Commands.h"
#include <QUndoStack>
#include "color_reference.h"
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <vector>

// Exercise the source boundary itself, before ROI/FX/compositing can hide
// a transfer error or accidentally premultiply alpha.
static int runSourceColorTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL source_color: %s\n", why); }
    };
    engine.newProject();
    constexpr int width = 1024, height = 16;
    engine.setCompositionSize({width, height});
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {width, height});
    QTemporaryDir files;
    QImage source(width, height, QImage::Format_RGBA64);
    for (int y = 0; y < height; ++y) {
        auto *row = reinterpret_cast<QRgba64 *>(source.scanLine(y));
        for (int x = 0; x < width; ++x)
            row[x] = QRgba64::fromRgba64(quint16(4000+x*54), quint16(60000-x*47), quint16(12000+x*29), 32768);
    }
    const QString path = files.path()+"/untagged16.png";
    check(source.save(path), "write an untagged 16-bit source");
    int row = engine.addLayer("Declared source");
    quint64 id = engine.layerId(row);
    QString error;
    check(engine.setLayerImage(row, path, &error), "load untagged source");
    auto read = [&](quint64 layerId) {
        std::vector<quint16> pixels(size_t(width)*4);
        engine.runGl([&] {
            auto f = gl();
            GLuint fbo = 0;
            f->glGenFramebuffers(1, &fbo);
            f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            const auto *layer = engine.layer(engine.indexOfId(layerId));
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, layer->rawTex, 0);
            check(f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "normalized source is readable");
            f->glReadPixels(0, height/2, width, 1, GL_RGBA, GL_UNSIGNED_SHORT, pixels.data());
            f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            f->glDeleteFramebuffers(1, &fbo);
        });
        return pixels;
    };
    auto matches = [&](const std::vector<quint16> &pixels, const QString &input) {
        int worst = 0;
        const auto *samples = reinterpret_cast<const QRgba64 *>(source.constScanLine(height/2));
        for (int x = 0; x < width; ++x) {
            const auto sample = samples[x];
            const auto expected = colorReference::convert({sample.red()/65535.,sample.green()/65535.,sample.blue()/65535.}, input, "srgb");
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(int(pixels[size_t(x)*4+c])-int(std::lround(expected[c]*65535))));
            worst = std::max(worst, std::abs(int(pixels[size_t(x)*4+3])-int(sample.alpha())));
        }
        if (worst > 16) std::printf("Source %s -> sRGB: worst error %d/65535\n", qPrintable(input), worst);
        return worst <= 16;
    };
    for (const QString &key : {QString("srgb"), QString("linear709"), QString("displayp3"), QString("rec709-g24"), QString("rec709")}) {
        check(engine.setParameter(id, "source/color_space", key), "declare a native source space");
        engine.renderFrame();
        check(matches(read(id), key), "source conversion matches reference and preserves straight alpha at 16 bits");
    }
    check(engine.setParameter(id, "source/color_space", "auto"), "restore Auto");
    engine.renderFrame();
    check(matches(read(id), "srgb"), "unknown Auto preserves legacy samples");
    source.setColorSpace(QColorSpace::DisplayP3);
    check(source.save(path) && engine.setLayerImage(engine.indexOfId(id), path, &error), "replace with tagged P3 source");
    engine.renderFrame();
    const auto p3 = read(id);
    check(matches(p3, "displayp3"), "Auto converts embedded P3 before effects/compositing");
    QUndoStack undo;
    check(cmd::setValues(&undo, &engine, id, {{"source/color_space", "srgb"}}, "Declare source"), "source declaration supports Undo");
    engine.renderFrame();
    check(matches(read(id), "srgb"), "manual declaration overrides rather than double-converts the embedded profile");
    undo.undo(); engine.renderFrame();
    check(read(id) == p3, "Undo restores the same Auto-normalized pixels");
    // A layer reference is already in working RGB. It must not inherit another decode.
    int tap = engine.addLayer("Working-space tap");
    const quint64 tapId = engine.layerId(tap);
    check(engine.setLayerSourceLayer(tap, id, LayerTap::PostFx, &error), "tap the normalized layer");
    engine.renderFrame();
    check(read(tapId) == p3, "layer references are not normalized twice");
    engine.removeLayer(engine.indexOfId(tapId));

    QImage background(width, height, QImage::Format_RGBA64);
    background.setColorSpace(QColorSpace::SRgb);
    background.fill(QColor::fromRgba64(12000, 24000, 36000, 65535));
    const QString backgroundPath = files.path()+"/srgb16.png";
    check(background.save(backgroundPath), "write sRGB background");
    const int backgroundRow = engine.addLayer("sRGB background");
    const quint64 backgroundId = engine.layerId(backgroundRow);
    check(engine.setLayerImage(backgroundRow, backgroundPath, &error), "mix an sRGB source with P3");
    engine.renderFrame();
    const auto mixed = read(engine.mainViewportId());
    bool mixedCorrect = true;
    const int bg[] = {12000, 24000, 36000};
    for (int x = 0; x < width; ++x) for (int c = 0; c < 3; ++c) {
        const double a = p3[size_t(x)*4+3]/65535.;
        const int expectedMixed = int(std::lround(p3[size_t(x)*4+c]*a+bg[c]*(1-a)));
        mixedCorrect = mixedCorrect && std::abs(int(mixed[size_t(x)*4+c])-expectedMixed) <= 16;
    }
    check(mixedCorrect, "mixed P3/sRGB sources blend only after normalization with preserved alpha");
    engine.removeLayer(engine.indexOfId(backgroundId));

    // An uncommon ICC profile uses Qt's one-time CPU conversion; original samples
    // are retained so a later explicit declaration can still reinterpret them.
    source.setColorSpace(QColorSpace::AdobeRgb);
    check(source.save(path) && engine.setLayerImage(engine.indexOfId(id), path, &error), "load Adobe RGB ICC source");
    engine.renderFrame();
    const auto converted = source.convertedToFormat(QImage::Format_RGBA64).convertedToColorSpace(QColorSpace::SRgb);
    const auto pixels = read(id);
    const auto *expected = reinterpret_cast<const quint16 *>(converted.constScanLine(height/2));
    bool equal = true;
    for (int x = 0; x < width*4; ++x) equal = equal && std::abs(int(pixels[size_t(x)])-int(expected[x])) <= 1;
    check(equal, "ICC conversion preserves the 16-bit result and alpha");
    engine.setParameter(id, "source/color_space", "srgb"); engine.renderFrame();
    check(matches(read(id), "srgb"), "ICC manual override still reads the original source samples");
    engine.setParameter(id, "source/color_space", "linear709");
    const QString project = files.path()+"/source-color.pinol";
    check(engine.saveProject(project, {}, &error) && engine.loadProject(project, nullptr, &error), "reload source color project");
    check(engine.parameterValue(id, "source/color_space") == "linear709", "project load restores the declaration");
    engine.renderFrame();
    check(matches(read(id), "linear709"), "reloaded declaration changes actual rendered pixels");
    return failures;
}

int runOutputColorTests(Engine &engine)
{
    int failures = runSourceColorTests(engine);
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL output_color: %s\n", why); }
    };
    engine.newProject();
    engine.fadeCompositionOpacity(1, 0);
    engine.setBlackout(false, 0);
    constexpr int width = 1024, height = 16;
    engine.setCompositionSize({width,height});
    const quint64 first = engine.mainViewportId();
    engine.setViewportSize(engine.indexOfId(first), {width,height});
    QTemporaryDir files;
    QImage source(width, height, QImage::Format_RGBA64);
    source.setColorSpace(QColorSpace::SRgb);
    for (int y=0; y<height; ++y) {
        auto *row = reinterpret_cast<QRgba64 *>(source.scanLine(y));
        for (int x=0; x<width; ++x)
            row[x] = QRgba64::fromRgba64(quint16(x*65535/1023), quint16(65535-x*41), quint16(8000+x*37), 65535);
    }
    const QString path = files.path()+"/profiled16.png";
    check(source.save(path), "write tagged 16-bit reference image");
    const int layer = engine.addLayer("Reference colors");
    const quint64 sourceId = engine.layerId(layer);
    QString error;
    check(engine.setLayerImage(layer,path,&error), "load reference image");
    engine.renderFrame();
    auto read = [&](quint64 viewport, bool preOutput=false) {
        std::vector<quint16> pixels(size_t(width)*4);
        engine.runGl([&] {
            Engine::Lock lock(&engine.mutex());
            Layer *v = engine.layer(engine.indexOfId(viewport));
            auto f=gl();
            GLuint sourceFbo=0;
            if(preOutput) {
                f->glGenFramebuffers(1,&sourceFbo);
                f->glBindFramebuffer(GL_FRAMEBUFFER,sourceFbo);
                f->glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,v->finalTex,0);
            } else v->vpOut[v->vpPublished.load()].bind();
            f->glReadPixels(0, height/2, width, 1, GL_RGBA, GL_UNSIGNED_SHORT,pixels.data());
            f->glBindFramebuffer(GL_FRAMEBUFFER,0);
            if(sourceFbo) f->glDeleteFramebuffers(1,&sourceFbo);
        });
        return pixels;
    };
    const auto identity=read(first);
    // Compare transforms against their actual input texture, before the neutral
    // output mesh introduces any additional sampling/UNORM rounding.
    const auto sourceSamples=read(first,true);
    const QImage composition=engine.grabOutput();
    int levels=1;
    for(int x=1;x<width;++x) if(identity[size_t(x)*4]>identity[size_t(x-1)*4]) ++levels;
    check(levels>1000,"16-bit reference gradient retains more than 1000 levels through compositing");
    const int added = engine.addViewport("Independent output", {width,height});
    const quint64 second = engine.layerId(added);
    // Analytical D65/chromaticity reference: older Qt versions approximate
    // gamma conversions and are not a stable oracle at 16-bit precision.
    for(const QString &input:outputColorSpaceKeys()) {
        for(const QString &output:outputColorSpaceKeys()) {
            engine.setParameter(first,"output/input_space",input);
            engine.setParameter(first,"output/space",output);
            engine.renderFrame();
            const auto actual=read(first);
            int worst=0;
            for(int x=0;x<width;++x) {
                const size_t k=size_t(x)*4;
                const auto reference=colorReference::convert(
                    {sourceSamples[k]/65535.,sourceSamples[k+1]/65535.,sourceSamples[k+2]/65535.},input,output);
                const int expected[]={int(std::lround(reference[0]*65535)),int(std::lround(reference[1]*65535)),
                                      int(std::lround(reference[2]*65535)),65535};
                for(int c=0;c<4;++c) worst=std::max(worst,std::abs(int(actual[k+c])-expected[c]));
            }
            if(worst>16) std::printf("Color transform %s -> %s: worst error %d/65535\n",qPrintable(input),qPrintable(output),worst);
            check(worst<=16,"named output spaces match an independent color conversion across 1024 colors");
            check(read(second)==identity,"one viewport's transform does not change another viewport");
            check(engine.grabOutput()==composition,"output transform does not rewrite the composition preview");
        }
    }
    engine.setParameter(first,"output/input_space","linear709");
    engine.setParameter(first,"output/space","same");
    engine.setParameter(first,"output/exposure",1);
    engine.renderFrame();
    const auto exposed=read(first);
    bool doubled=true;
    for(int x=0;x<width;++x) for(int c=0;c<3;++c) {
        const size_t k=size_t(x)*4+c;
        doubled=doubled && std::abs(int(exposed[k])-std::min(65535,int(sourceSamples[k])*2))<=2;
    }
    check(doubled,"exposure doubles linear RGB before final encoding and clips SDR highlights");

    // Source alpha and viewport opacity are black-matted before the transform.
    source.fill(QColor::fromRgba64(QRgba64::fromRgba64(65535,0,0,32768)));
    check(source.save(path),"write alpha fixture");
    check(engine.setLayerImage(engine.indexOfId(sourceId),path,&error),"load alpha fixture");
    engine.setParameter(first,"output/exposure",0);
    engine.setParameter(first,"output/space","srgb");
    engine.setParameter(first,"opacity",.5);
    engine.renderFrame();
    const auto alpha=read(first);
    const double encodedQuarter=1.055*std::pow(.25,1/2.4)-.055;
    check(std::abs(alpha[0]/65535.0-encodedQuarter)<.0001 && alpha[3]==65535,
          "alpha and viewport opacity are applied once before the output transform");
    engine.setPaused(true);
    engine.fadeCompositionOpacity(.5,0);
    engine.renderFrame();
    check(std::abs(read(first)[0]/65535.0-encodedQuarter*.5)<.0001,"composition fader remains after encoding while held");
    engine.setBlackout(true,0);
    engine.renderFrame();
    const auto black=read(first);
    check(black[0]==0 && black[1]==0 && black[2]==0 && black[3]==65535,"blackout stays exact black in every output space");

    engine.setPaused(false);
    engine.setBlackout(false,0);
    engine.fadeCompositionOpacity(1,0);
    engine.setParameter(first,"output/input_space","displayp3");
    engine.setParameter(first,"output/space","rec709-g24");
    engine.setParameter(first,"output/exposure",.25);
    const QString project=files.path()+"/color.pinol";
    check(engine.saveProject(project,{},&error),"save color output project");
    QJsonObject ui;
    check(engine.loadProject(project,&ui,&error),"reload color output project");
    check(engine.parameterValue(first,"output/input_space")=="displayp3" &&
          engine.parameterValue(first,"output/space")=="rec709-g24" &&
          engine.parameterValue(first,"output/exposure")==.25,"real project reload retains per-output color calibration");
    engine.newProject();
    return failures;
}
