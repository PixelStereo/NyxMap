#pragma once
// TouchEngine through Direct3D 11 (Windows): the textures shared with QRhi's device, no OpenGL. The inputs are copied on
// the GPU from the source layers' QRhi textures into pooled textures TouchEngine reads (its D3D11 context does the
// synchronisation); an output is a shared texture opened on QRhi's device, held through its DXGI keyed mutex while the
// frame reads it, and wrapped as a QRhi texture. Same interface as ToxMetal. Used by ToxTouchEngine.cpp.

#include <TouchEngine/TEInstance.h>

#include <QSize>
#include <QString>
#include <memory>

#include <rhi/qrhi.h>

class ToxD3D11
{
public:
    static constexpr QRhi::Implementation kBackend = QRhi::D3D11;
    // The Direct3D 11 device of `rhi` (nullptr when it is not a Direct3D 11 QRhi)
    static std::unique_ptr<ToxD3D11> create(QRhi *rhi, QString *error);
    ~ToxD3D11();
    TEGraphicsContext *context() const; // to associate with the instance

    // An input TOP gets `source`'s picture (copied on the GPU; nullptr: no picture). False: `error` says why
    bool setInput(TEInstance *instance, const char *identifier, QRhiTexture *source, QString *error);
    // An output TOP's current picture as a QRhi texture, kept until releaseOutput(); nullptr: none (error: why).
    // flipped: its first row is the picture's top
    QRhiTexture *receive(TEInstance *instance, const char *identifier, QSize *size, bool *flipped, QString *error);
    bool outputHeld() const;
    // The output is no longer used: its keyed mutex released, the texture given back to the instance
    void releaseOutput(TEInstance *instance);
    // The instance is gone (released): the pools and caches go
    void close();

private:
    ToxD3D11();
    struct Impl;
    std::unique_ptr<Impl> d;
};
