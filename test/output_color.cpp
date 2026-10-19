#include "Engine.h"
#include "ColorInfo.h"
#include "color_reference.h"
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <vector>

int runOutputColorTests(Engine &engine)
{
    int failures = 0;
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
