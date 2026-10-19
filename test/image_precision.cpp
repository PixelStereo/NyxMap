#include "Engine.h"
#include "Gl.h"
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <vector>

int runImagePrecisionTests(Engine &engine) {
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::printf("FAIL image_precision: %s\n", why);
        }
    };
    QTemporaryDir files;
    const int width = 1024, height = 2;
    QImage image(width, height, QImage::Format_RGBA64);
    for (int y = 0; y < height; ++y) {
        auto *row = reinterpret_cast<QRgba64 *>(image.scanLine(y));
        for (int x = 0; x < width; ++x)
            row[x] = QRgba64::fromRgba64(quint16(x * 63 + y), quint16(65535 - x * 41), quint16(300 + x),
                                         quint16(20000 + x * 31));
    }
    const QString path = files.path() + "/ramp16.png";
    check(image.save(path), "write 16-bit RGBA fixture");
    const QImage expected = prepareImageUpload(image);
    auto verify = [&](GLuint texture, const char *why) {
        GLuint fbo = 0;
        auto f = gl();
        f->glGenFramebuffers(1, &fbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        const bool complete = f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        check(complete, "16-bit texture is framebuffer-readable");
        if (complete) {
            GLint bits = 0;
            f->glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                     GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &bits);
            check(bits == 16, "GPU storage has 16 bits per channel");
            std::vector<quint16> pixels(size_t(width) * height * 4);
            f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            f->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT, pixels.data());
            bool equal = true;
            int unique = 0;
            for (int y = 0; y < height; ++y) {
                const auto *row = reinterpret_cast<const quint16 *>(expected.constScanLine(y));
                for (int x = 0; x < width * 4; ++x)
                    if (std::abs(int(pixels[size_t(y) * width * 4 + x]) - int(row[x])) > 1)
                        equal = false;
            }
            for (int x = 0; x < width; ++x)
                if (!x || pixels[size_t(x) * 4] > pixels[size_t(x - 1) * 4])
                    ++unique;
            check(equal, why);
            check(unique > 1000, "more than 1000 ramp levels survive upload/render");
        }
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        f->glDeleteFramebuffers(1, &fbo);
    };
    engine.newProject();
    engine.setCompositionSize(QSize(width, height));
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), QSize(width, height));
    const int layer = engine.addLayer("16-bit image");
    QString error;
    check(engine.setLayerImage(layer, path, &error), "load 16-bit layer");
    engine.renderFrame();
    engine.runGl([&] {
        verify(engine.layer(layer)->sourceTex.tex, "layer source preserves exact color, alpha and orientation");
    });
    engine.runGl([&] {
        Texture2D texture;
        QImage eight(width, height, QImage::Format_RGBA8888);
        eight.fill(Qt::white);
        texture.uploadImage(eight);
        texture.uploadImage(expected);
        verify(texture.tex, "8-to-16 format switch reallocates equal-sized storage");
        texture.uploadImage(eight);
        std::vector<float> hdr(size_t(width) * height * 4, 2.f);
        texture.uploadFloat(hdr.data(), width, height);
        GLuint fbo = 0;
        auto f = gl();
        f->glGenFramebuffers(1, &fbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.tex, 0);
        float sample[4] = {};
        f->glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, sample);
        check(sample[0] == 2.f, "8-to-float format switch does not clamp float samples");
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        f->glDeleteFramebuffers(1, &fbo);
        texture.uploadImage(expected);
        verify(texture.tex, "float-to-16 format switch restores normalized storage");
        texture.destroy();
    });
    const QString shader = files.path() + "/Image.fs";
    {
        QFile file(shader);
        check(file.open(QIODevice::WriteOnly), "create targeted ISF fixture");
        file.write("/*{\"ISFVSN\":\"2\",\"INPUTS\":[{\"NAME\":\"picture\",\"TYPE\":\"image\"}]}*/\nvoid "
                   "main(){gl_FragColor=IMG_NORM_PIXEL(picture,isf_FragNormCoord);}\n");
    }
    const int generator = engine.addLayer("Image input");
    check(engine.setLayerIsf(generator, shader, &error), "compile a single image-input regression shader");
    engine.runGl([&] {
        auto *instance = engine.layer(generator)->generator.get();
        check(instance && instance->setImageInput(0, path, &error), "load a 16-bit ISF image input");
        if (instance && instance->input("picture"))
            verify(instance->input("picture")->imageTex.tex, "ISF image input preserves all 16-bit samples");
    });
    const QString imported = files.path() + "/Imported.fs";
    {
        QFile file(imported);
        check(file.open(QIODevice::WriteOnly), "create imported-image fixture");
        file.write("/*{\"ISFVSN\":\"2\",\"IMPORTED\":{\"picture\":{\"PATH\":\"ramp16.png\"}}}*/\nvoid "
                   "main(){gl_FragColor=IMG_NORM_PIXEL(picture,isf_FragNormCoord);}\n");
    }
    const int importLayer = engine.addLayer("Imported image");
    check(engine.setLayerIsf(importLayer, imported, &error), "load a 16-bit ISF imported image");
    engine.setGeneratorSize(importLayer, width, height);
    engine.renderFrame();
    engine.runGl([&] {
        verify(engine.layer(importLayer)->generatorTarget.tex,
               "ISF import and shader pass preserve 16-bit color and alpha");
    });
    engine.newProject();
    return failures;
}
