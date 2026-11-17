#pragma once
// TouchEngine through Metal (macOS): the textures shared with QRhi's device. The inputs are copied on the
// GPU from the source layers' QRhi textures into pooled Metal textures TouchEngine reads; an output is wrapped as a
// QRhi texture, after the GPU wait TouchEngine asks for. Used by ToxTouchEngine.cpp (compiled with the SDK, on macOS).

#include <TouchEngine/TEInstance.h>

#include <QSize>
#include <QString>
#include <memory>

#include <rhi/qrhi.h>

class QRhiTexture;

class ToxMetal
{
public:
    static constexpr QRhi::Implementation kBackend = QRhi::Metal;
    // The Metal device of `rhi` (nullptr when it is not a Metal QRhi)
    static std::unique_ptr<ToxMetal> create(QRhi *rhi, QString *error);
    ~ToxMetal();
    TEGraphicsContext *context() const; // to associate with the instance

    // An input TOP gets `source`'s picture (copied on the GPU; nullptr: no picture). False: `error` says why
    bool setInput(TEInstance *instance, const char *identifier, QRhiTexture *source, QString *error);
    // An output TOP's current picture as a QRhi texture, kept until releaseOutput(); nullptr: none (error: why).
    // flipped: its first row is the picture's top
    QRhiTexture *receive(TEInstance *instance, const char *identifier, QSize *size, bool *flipped, QString *error);
    bool outputHeld() const;
    // The output is no longer used: given back to the instance (when it asks for the transfer)
    void releaseOutput(TEInstance *instance);
    // The instance is gone (released): the pools and caches go
    void close();

private:
    ToxMetal();
    struct Impl;
    std::unique_ptr<Impl> d;
};
