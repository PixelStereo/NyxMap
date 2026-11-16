#include "ToxD3D11.h"

#include <TouchEngine/TED3D11.h>
#include <TouchEngine/TEObject.h>
#include <TouchEngine/TETexture.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

// A texture of ours TouchEngine reads: it may be reused only when it no longer reads it (EndUse)
struct Slot {
    ID3D11Texture2D *texture = nullptr;
    TED3D11Texture *handle = nullptr;
    int w = 0, h = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::atomic<int> users{0};
    std::atomic<bool> released{false}; // TouchEngine let go of it for good
    static void event(ID3D11Texture2D *, TEObjectEvent e, void *info)
    {
        auto &s = *static_cast<Slot *>(info);
        if (e == TEObjectEventBeginUse) ++s.users;
        if (e == TEObjectEventEndUse) --s.users;
        if (e == TEObjectEventRelease) s.released = true;
    }
    ~Slot()
    {
        if (handle) TERelease(&handle);
        if (texture) texture->Release();
    }
};

bool qrhiFormat(DXGI_FORMAT f, QRhiTexture::Format *out, QRhiTexture::Flags *flags)
{
    *flags = {};
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: *out = QRhiTexture::RGBA8; return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: *out = QRhiTexture::RGBA8; *flags = QRhiTexture::sRGB; return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM: *out = QRhiTexture::BGRA8; return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: *out = QRhiTexture::BGRA8; *flags = QRhiTexture::sRGB; return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: *out = QRhiTexture::RGBA16F; return true;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: *out = QRhiTexture::RGBA32F; return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM: *out = QRhiTexture::RGB10A2; return true;
    case DXGI_FORMAT_R8_UNORM: *out = QRhiTexture::R8; return true;
    case DXGI_FORMAT_R8G8_UNORM: *out = QRhiTexture::RG8; return true;
    case DXGI_FORMAT_R16_FLOAT: *out = QRhiTexture::R16F; return true;
    case DXGI_FORMAT_R32_FLOAT: *out = QRhiTexture::R32F; return true;
    default: return false;
    }
}

bool isIdentity(TETextureComponentMap m)
{
    return m.r == TETextureComponentSourceRed && m.g == TETextureComponentSourceGreen && m.b == TETextureComponentSourceBlue &&
           m.a == TETextureComponentSourceAlpha;
}

} // namespace

struct ToxD3D11::Impl {
    QRhi *rhi = nullptr;
    ID3D11Device *device = nullptr;         // QRhi's: borrowed
    ID3D11DeviceContext *immediate = nullptr;
    TED3D11Context *context = nullptr;
    std::map<std::string, std::array<std::shared_ptr<Slot>, 3>> pool;
    std::vector<std::shared_ptr<Slot>> retired; // let go of, kept until TouchEngine's final release
    // The output held: TouchEngine's texture value, our opened texture, its keyed mutex
    TETexture *output = nullptr;
    TED3D11Texture *opened = nullptr;
    IDXGIKeyedMutex *mutex = nullptr;
    bool acquired = false;
    uint64_t lastAcquire = UINT64_MAX;
    // QRhi textures over the opened textures, by texture; each keeps its TED3D11Texture so the texture stays valid
    struct Wrapped {
        std::unique_ptr<QRhiTexture> texture;
        TED3D11Texture *held = nullptr;
        unsigned lastUsed = 0;
    };
    std::map<ID3D11Texture2D *, Wrapped> wrapped;
    unsigned frame = 0;
    bool releaseToZero = false;
};

ToxD3D11::ToxD3D11() : d(std::make_unique<Impl>()) {}

std::unique_ptr<ToxD3D11> ToxD3D11::create(QRhi *rhi, QString *error)
{
    if (!rhi || rhi->backend() != QRhi::D3D11 || !rhi->nativeHandles()) return nullptr;
    const auto *handles = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
    if (!handles->dev || !handles->context) return nullptr;
    std::unique_ptr<ToxD3D11> t(new ToxD3D11);
    t->d->rhi = rhi;
    t->d->device = static_cast<ID3D11Device *>(handles->dev);
    t->d->immediate = static_cast<ID3D11DeviceContext *>(handles->context);
    const TEResult r = TED3D11ContextCreate(t->d->device, &t->d->context);
    if (r != TEResultSuccess || !t->d->context) {
        if (error) *error = QString::fromUtf8(TEResultGetDescription(r));
        return nullptr;
    }
    return t;
}

ToxD3D11::~ToxD3D11() { close(); }

TEGraphicsContext *ToxD3D11::context() const { return reinterpret_cast<TEGraphicsContext *>(d->context); }

bool ToxD3D11::setInput(TEInstance *instance, const char *identifier, QRhiTexture *source, QString *error)
{
    auto fail = [&](const QString &why) {
        if (error) *error = why;
        return false;
    };
    if (!source) { // no picture: the link is emptied
        const TEResult r = TEInstanceLinkSetTextureValue(instance, identifier, nullptr, context());
        return r == TEResultSuccess || TEResultGetSeverity(r) != TESeverityError;
    }
    auto *from = reinterpret_cast<ID3D11Texture2D *>(quintptr(source->nativeTexture().object));
    if (!from) return fail(QStringLiteral("The picture sent to TouchEngine has no Direct3D texture"));
    D3D11_TEXTURE2D_DESC sd = {};
    from->GetDesc(&sd);
    if (sd.SampleDesc.Count != 1) return fail(QStringLiteral("The picture sent to TouchEngine is multisampled"));
    auto &pool = d->pool[identifier];
    for (std::shared_ptr<Slot> &entry : pool) {
        if (!entry) entry = std::make_shared<Slot>();
        if (entry->users.load() != 0) continue; // TouchEngine is still reading it
        if (entry->texture && (entry->w != int(sd.Width) || entry->h != int(sd.Height) || entry->format != sd.Format)) {
            // Another size: it must outlive TouchEngine's last use, even after EndUse
            d->retired.push_back(entry);
            entry = std::make_shared<Slot>();
        }
        if (d->retired.size() >= 64) return fail(QStringLiteral("TouchEngine is retaining resized input textures; reload the component"));
        Slot &slot = *entry;
        if (!slot.texture) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = sd.Width;
            desc.Height = sd.Height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = sd.Format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(d->device->CreateTexture2D(&desc, nullptr, &slot.texture)))
                return fail(QStringLiteral("Cannot allocate a TouchEngine input texture"));
            slot.w = int(sd.Width);
            slot.h = int(sd.Height);
            slot.format = sd.Format;
            // Our pictures hold their bottom row first
            slot.handle = TED3D11TextureCreate(slot.texture, TETextureOriginBottomLeft, kTETextureComponentMapIdentity, Slot::event, &slot);
            if (!slot.handle) return fail(QStringLiteral("Cannot share a texture with TouchEngine"));
        }
        // The copy, on QRhi's own context (the picture was finished by the last frame); TouchEngine's D3D11 context takes
        // care of the synchronisation when it reads the texture
        d->immediate->CopyResource(slot.texture, from);
        d->immediate->Flush();
        const TEResult set = TEInstanceLinkSetTextureValue(instance, identifier, reinterpret_cast<TETexture *>(slot.handle), context());
        if (TEResultGetSeverity(set) == TESeverityError) return fail(QString::fromUtf8(TEResultGetDescription(set)));
        return true;
    }
    return true; // all three still in use: this frame's picture is not sent
}

QRhiTexture *ToxD3D11::receive(TEInstance *instance, const char *identifier, QSize *size, bool *flipped, QString *error)
{
    auto fail = [&](const QString &why) -> QRhiTexture * {
        if (error) *error = why;
        return nullptr;
    };
    ++d->frame;
    releaseOutput(instance);
    d->releaseToZero = TEInstanceRequiresKeyedMutexReleaseToZero(instance);
    TETexture *value = nullptr;
    const TEResult got = TEInstanceLinkGetTextureValue(instance, identifier, TELinkValueCurrent, &value);
    if (TEResultGetSeverity(got) == TESeverityError) return fail(QString::fromUtf8(TEResultGetDescription(got)));
    if (!value) return nullptr;
    d->output = value; // released by releaseOutput(), never retained again
    if (TETextureGetType(value) != TETextureTypeD3DShared) return fail(QStringLiteral("TouchEngine gave a texture type NyxMap cannot use"));
    TED3D11Texture *opened = nullptr;
    const TEResult open = TED3D11ContextGetTexture(d->context, reinterpret_cast<TED3DSharedTexture *>(value), &opened);
    if (TEResultGetSeverity(open) == TESeverityError || !opened) return fail(QStringLiteral("Cannot import the TouchEngine output texture"));
    d->opened = opened;
    ID3D11Texture2D *texture = TED3D11TextureGetTexture(opened);
    if (!texture) return fail(QStringLiteral("Cannot import the TouchEngine output texture"));
    texture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&d->mutex));
    // Its keyed mutex is the synchronisation: acquired at the value the instance gives, until this frame is done
    if (TEInstanceHasTextureTransfer(instance, value)) {
        TESemaphore *semaphore = nullptr;
        uint64_t wait = 0;
        if (TEInstanceGetTextureTransfer(instance, value, &semaphore, &wait) == TEResultSuccess) {
            if (semaphore) TERelease(&semaphore); // keyed mutexes come without one
            if (d->mutex) {
                if (FAILED(d->mutex->AcquireSync(wait, 1000))) return fail(QStringLiteral("TouchEngine's output is not ready"));
                d->acquired = true;
                d->lastAcquire = wait;
            }
        }
    }
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    Impl::Wrapped &wrap = d->wrapped[texture];
    wrap.lastUsed = d->frame;
    if (!wrap.texture) {
        QRhiTexture::Format format;
        QRhiTexture::Flags flags;
        if (!qrhiFormat(desc.Format, &format, &flags)) {
            d->wrapped.erase(texture);
            return fail(QStringLiteral("TouchEngine gave a texture format NyxMap cannot show"));
        }
        if (!isIdentity(TETextureGetComponentMap(value))) {
            d->wrapped.erase(texture);
            return fail(QStringLiteral("TouchEngine gave a swizzled texture NyxMap cannot show yet"));
        }
        wrap.held = static_cast<TED3D11Texture *>(TERetain(opened)); // keeps the texture alive as long as QRhi samples it
        wrap.texture.reset(d->rhi->newTexture(format, QSize(int(desc.Width), int(desc.Height)), 1, flags));
        if (!wrap.texture->createFrom({quint64(quintptr(texture)), 0})) {
            TERelease(&wrap.held);
            d->wrapped.erase(texture);
            return fail(QStringLiteral("Cannot use the TouchEngine output texture"));
        }
    }
    // The ones not used for a long time (TouchEngine recycles its own): dropped
    for (auto it = d->wrapped.begin(); it != d->wrapped.end();) {
        if (d->frame - it->second.lastUsed > 240) {
            it->second.texture.reset();
            if (it->second.held) TERelease(&it->second.held);
            it = d->wrapped.erase(it);
        } else {
            ++it;
        }
    }
    *size = wrap.texture->pixelSize();
    *flipped = TETextureGetOrigin(value) == TETextureOriginTopLeft;
    return wrap.texture.get();
}

bool ToxD3D11::outputHeld() const { return d->output != nullptr; }

void ToxD3D11::releaseOutput(TEInstance *instance)
{
    if (!d->output) return;
    if (d->acquired && d->mutex) {
        // The keyed mutex goes back to the instance: released to 0 when it asks for it, else to the value after ours
        uint64_t release = 0;
        if (!d->releaseToZero) release = d->lastAcquire == UINT64_MAX ? 0 : d->lastAcquire + 1;
        d->mutex->ReleaseSync(release);
        if (instance) TEInstanceAddTextureTransfer(instance, d->output, nullptr, release);
    }
    d->acquired = false;
    d->lastAcquire = UINT64_MAX;
    if (d->mutex) {
        d->mutex->Release();
        d->mutex = nullptr;
    }
    if (d->opened) TERelease(&d->opened);
    TERelease(&d->output);
}

void ToxD3D11::close()
{
    if (d->mutex) {
        d->mutex->Release();
        d->mutex = nullptr;
    }
    if (d->opened) TERelease(&d->opened);
    if (d->output) TERelease(&d->output);
    d->acquired = false;
    for (auto &entry : d->wrapped) {
        entry.second.texture.reset();
        if (entry.second.held) TERelease(&entry.second.held);
    }
    d->wrapped.clear();
    d->pool.clear(); // the slots release their handles and textures
    d->retired.clear();
    if (d->context) TERelease(&d->context);
}
