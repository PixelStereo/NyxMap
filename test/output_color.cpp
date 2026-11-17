#include "Engine.h"
#include "ColorInfo.h"
#include "Commands.h"
#include <QUndoStack>
#include "color_reference.h"
#include <QTemporaryDir>
#include <algorithm>
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
    // The picture of a layer (its source, after the normalizing) or of a viewport, 8 bits per channel as the QRhi renderer draws
    // them, as 16-bit samples
    auto read = [&](quint64 layerId) {
        std::vector<quint16> pixels(size_t(width)*4);
        engine.renderFrame();
        const int index = engine.indexOfId(layerId);
        const bool viewport = layerId == engine.mainViewportId();
        const QImage image = viewport ? engine.grabViewport(layerId, true) : engine.grabLayerSource(index, true);
        check(!image.isNull() && image.width() == width, "normalized source is readable");
        if (image.isNull() || image.width() != width) return pixels;
        const QImage rgba = image.convertToFormat(QImage::Format_RGBA64);
        const auto *line = reinterpret_cast<const quint16 *>(rgba.constScanLine(rgba.height() / 2));
        for (int x = 0; x < width * 4; ++x) pixels[size_t(x)] = line[x];
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
        if (worst > 160) std::printf("Source %s -> sRGB: worst error %d/65535\n", qPrintable(input), worst);
        return worst <= 160; // half floats: 10 to 11 bits
    };
    for (const QString &key : {QString("srgb"), QString("linear709"), QString("displayp3"), QString("rec709-g24"), QString("rec709")}) {
        check(engine.setParameter(id, "source/color_space", key), "declare a native source space");
        engine.renderFrame();
        check(matches(read(id), key), "source conversion matches reference and preserves straight alpha at 16 bits");
    }
    check(engine.setParameter(id, "source/color_space", "auto"), "restore Auto");
    engine.renderFrame();
    check(matches(read(id), "srgb"), "untagged Auto reads the samples as sRGB");
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
    const int backgroundRow = engine.addLayer("sRGB background", engine.layerCount());
    const quint64 backgroundId = engine.layerId(backgroundRow);
    check(engine.setLayerImage(backgroundRow, backgroundPath, &error), "mix an sRGB source with P3");
    engine.renderFrame();
    const auto mixed = read(engine.mainViewportId());
    bool mixedCorrect = true;
    const int bg[] = {12000, 24000, 36000};
    for (int x = 0; x < width; ++x) for (int c = 0; c < 3; ++c) {
        const double a = p3[size_t(x)*4+3]/65535.;
        const int expectedMixed = int(std::lround(p3[size_t(x)*4+c]*a+bg[c]*(1-a)));
        mixedCorrect = mixedCorrect && std::abs(int(mixed[size_t(x)*4+c])-expectedMixed) <= 160;
    }
    check(mixedCorrect, "mixed P3/sRGB sources blend only after normalization with preserved alpha");
    engine.removeLayer(engine.indexOfId(backgroundId));

    // An uncommon ICC profile uses Qt's one-time CPU conversion; original samples
    // are retained so a later explicit declaration can still reinterpret them.
    source.setColorSpace(QColorSpace::AdobeRgb);
    check(source.save(path) && engine.setLayerImage(engine.indexOfId(id), path, &error), "load Adobe RGB ICC source");
    engine.renderFrame();
    const auto converted = QImage(path).convertToFormat(QImage::Format_RGBA64).convertedToColorSpace(QColorSpace::SRgb);
    const auto pixels = read(id);
    const auto *expected = reinterpret_cast<const quint16 *>(converted.constScanLine(height/2));
    bool equal = true;
    for (int x = 0; x < width*4; ++x) equal = equal && std::abs(int(pixels[size_t(x)])-int(expected[x])) <= 700;
    check(equal, "ICC conversion preserves the 16-bit result and alpha");
    engine.setParameter(id, "source/color_space", "srgb"); engine.renderFrame();
    check(matches(read(id), "srgb"), "ICC manual override still reads the original source samples");
    engine.setParameter(id, "source/color_space", "linear709");
    const QString project = files.path()+"/source-color.nyxmap";
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
        (void)preOutput; // the QRhi renderer has no picture between the viewport's layers and its output color to read
        std::vector<quint16> pixels(size_t(width)*4);
        const QImage image = engine.grabViewport(viewport, true).convertToFormat(QImage::Format_RGBA64);
        if (image.isNull() || image.width() != width) return pixels;
        const auto *line = reinterpret_cast<const quint16 *>(image.constScanLine(image.height() / 2));
        for (int x = 0; x < width * 4; ++x) pixels[size_t(x)] = line[x];
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
            if(worst>160) std::printf("Color transform %s -> %s: worst error %d/65535\n",qPrintable(input),qPrintable(output),worst);
            check(worst<=160,"named output spaces match an independent color conversion across 1024 colors");
            check(read(second)==identity,"one viewport's transform does not change another viewport");
            check(engine.grabOutput()==composition,"output transform does not rewrite the composition preview");
        }
    }
    // Soft edge (Bourke's blend in linear light, then the projector's gamma) and flip, on the viewport's output
    {
        engine.setParameter(first,"output/input_space","srgb");
        engine.setParameter(first,"output/space","same");
        engine.setParameter(first,"output/exposure",0);
        engine.renderFrame();
        const auto base=read(first);
        engine.setParameter(first,"soft_edge/left/width",256);
        engine.setParameter(first,"soft_edge/left/curve",2);
        engine.setParameter(first,"soft_edge/gamma",2.2);
        engine.renderFrame();
        const auto soft=read(first);
        auto ramp=[](double x,double p){ x=std::clamp(x,0.0,1.0); return x<0.5 ? 0.5*std::pow(2*x,p) : 1-0.5*std::pow(2*(1-x),p); };
        bool ramped=soft[0]<=base[0]/50; // black at the edge
        for(int x : {40,128,200}) {
            const double expected=base[size_t(x)*4+1]*std::pow(ramp((x+.5)/256,2),1/2.2);
            ramped=ramped && std::abs(double(soft[size_t(x)*4+1])-expected)<=std::max(400.0,expected*.02);
        }
        for(int x=256;x<width;x+=97) ramped=ramped && soft[size_t(x)*4+1]==base[size_t(x)*4+1];
        check(ramped,"a soft edge fades the output over its width in pixels, by Bourke's curve at the projector's gamma, and leaves the rest");
        // two overlapping ramps add up to one in linear light
        double worst=0;
        for(int i=0;i<=64;++i) worst=std::max(worst,std::abs(std::pow(ramp(i/64.0,2),1/2.2*2.2)+std::pow(ramp(1-i/64.0,2),1/2.2*2.2)-1));
        check(worst<1e-9,"the blend of two overlapping edges adds up to one in linear light");
        engine.setParameter(first,"soft_edge/left/width",0);
        engine.setParameter(first,"flip/x",true);
        engine.renderFrame();
        const auto flipped=read(first);
        bool mirrored=true;
        for(int x=0;x<width;x+=51) for(int c=0;c<3;++c)
            mirrored=mirrored && std::abs(int(flipped[size_t(x)*4+c])-int(base[size_t(width-1-x)*4+c]))<=2;
        check(mirrored,"flip mirrors the output left to right");
        engine.setParameter(first,"flip/x",false);
        // the blend guide: the overlap in the viewport's color, the rest black when only the overlap is asked for
        engine.setParameter(first,"soft_edge/left/width",256);
        engine.setParameter(first,"soft_edge/guide/r",1);
        engine.setParameter(first,"soft_edge/guide/g",0.2);
        engine.setParameter(first,"soft_edge/guide/b",0.2);
        engine.setBlendGuide(first,1,true,false);
        engine.renderFrame();
        {
            const auto g=read(first);
            const bool tinted=std::abs(int(g[100*4])-32768)<400 && std::abs(int(g[100*4+1])-6554)<400 && g[600*4]==0 && g[600*4+1]==0 && g[600*4+2]==0;
            check(tinted,"the blend guide shows the overlap in the viewport's color and nothing else when only the overlap is asked for");
        }
        engine.setBlendGuide(first,2,false,true);
        engine.renderFrame();
        {
            const auto g=read(first);
            check(g[600*4]>=65530 && g[600*4+1]>=65530 && g[2*4]<g[600*4]/4,"the white field is white outside the ramp and fades inside it");
        }
        engine.setParameter(first,"soft_edge/black",10);
        engine.setBlendGuide(first,4,false,true);
        engine.renderFrame();
        {
            const auto g=read(first);
            const double lifted=std::pow(0.1,1/2.2)*65535;
            check(std::abs(double(g[600*4])-lifted)<500 && g[100*4]<600,"the black level is lifted outside the overlap and not inside it");
        }
        engine.setBlendGuide(0,0,false,true);
        engine.setParameter(first,"soft_edge/black",0);
        engine.setParameter(first,"soft_edge/left/width",0);
        engine.renderFrame();
        check(read(first)==base,"without the guide, the black level and the soft edge the output is as it was");
        // the cross where the pointer is, on the output only while it is set
        engine.setViewportProbe(first,QPointF(0.25,0.5));
        engine.renderFrame();
        {
            const QImage crossed=engine.grabViewport(first,true).convertToFormat(QImage::Format_RGBA64);
            const int cx=int(width*0.25), cy=crossed.height()/2;
            auto white=[&](int x,int y){ const QRgba64 c=crossed.pixelColor(x,y).rgba64(); return c.red()==65535 && c.green()==65535 && c.blue()==65535; };
            check(white(cx,3) && white(5,cy) && !white(cx+40,3) && !white(5,cy+6),"the cross draws a line across the output through the pointer");
        }
        engine.setViewportProbe(first,QPointF(-1,-1));
        engine.renderFrame();
        check(read(first)==base,"without the pointer the output has no cross");
        // the warp: pulling the left corners in leaves the left of the output black and the right edge where it was
        engine.setParameter(first,"warp/top_left/x",40);
        engine.setParameter(first,"warp/bottom_left/x",40);
        engine.renderFrame();
        const auto warped=read(first);
        check(warped[10*4+1]==0 && warped[10*4]==0 && warped[10*4+3]==65535 &&
              std::abs(int(warped[size_t(width-3)*4+1])-int(base[size_t(width-3)*4+1]))<=800,
              "a warp's corner pin moves the picture over the output and leaves black where it is not");
        engine.setParameter(first,"warp/top_left/x",0);
        engine.setParameter(first,"warp/bottom_left/x",0);
        engine.renderFrame();
        check(read(first)==base,"a flat warp is the output as it was");
    }
    engine.setParameter(first,"output/input_space","linear709");
    engine.setParameter(first,"output/space","same");
    engine.setParameter(first,"output/exposure",1);
    engine.renderFrame();
    const auto exposed=read(first);
    bool doubled=true;
    for(int x=0;x<width;++x) for(int c=0;c<3;++c) {
        const size_t k=size_t(x)*4+c;
        doubled=doubled && std::abs(int(exposed[k])-std::min(65535,int(sourceSamples[k])*2))<=64;
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
    check(std::abs(alpha[0]/65535.0-encodedQuarter)<.002 && alpha[3]==65535,
          "alpha and viewport opacity are applied once before the output transform");
    engine.setPaused(true);
    engine.fadeCompositionOpacity(.5,0);
    engine.renderFrame();
    check(std::abs(read(first)[0]/65535.0-encodedQuarter*.5)<.002,"composition fader remains after encoding while held");
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
    const QString project=files.path()+"/color.nyxmap";
    check(engine.saveProject(project,{},&error),"save color output project");
    QJsonObject ui;
    check(engine.loadProject(project,&ui,&error),"reload color output project");
    check(engine.parameterValue(first,"output/input_space")=="displayp3" &&
          engine.parameterValue(first,"output/space")=="rec709-g24" &&
          engine.parameterValue(first,"output/exposure")==.25,"real project reload retains per-output color calibration");
    engine.newProject();
    return failures;
}
