#include "ToxMetal.h"

#include <TouchEngine/TEMetal.h>
#include <TouchEngine/TEObject.h>
#include <TouchEngine/TETexture.h>

#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#include <rhi/qrhi.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

// A texture of ours TouchEngine reads: it may be given back (EndUse) only when it no longer reads it
struct Slot {
    id<MTLTexture> texture = nil;
    TEMetalTexture *handle = nullptr;
    int w = 0, h = 0;
    MTLPixelFormat format = MTLPixelFormatInvalid;
    std::atomic<int> users{0};
    std::atomic<bool> released{false}; // TouchEngine let go of it for good
    static void event(id<MTLTexture>, TEObjectEvent e, void *info)
    {
        auto &s = *static_cast<Slot *>(info);
        if (e == TEObjectEventBeginUse) ++s.users;
        if (e == TEObjectEventEndUse) --s.users;
        if (e == TEObjectEventRelease) s.released = true;
    }
};

bool qrhiFormat(MTLPixelFormat f, QRhiTexture::Format *out, QRhiTexture::Flags *flags)
{
    *flags = {};
    switch (f) {
    case MTLPixelFormatRGBA8Unorm: *out = QRhiTexture::RGBA8; return true;
    case MTLPixelFormatRGBA8Unorm_sRGB: *out = QRhiTexture::RGBA8; *flags = QRhiTexture::sRGB; return true;
    case MTLPixelFormatBGRA8Unorm: *out = QRhiTexture::BGRA8; return true;
    case MTLPixelFormatBGRA8Unorm_sRGB: *out = QRhiTexture::BGRA8; *flags = QRhiTexture::sRGB; return true;
    case MTLPixelFormatRGBA16Float: *out = QRhiTexture::RGBA16F; return true;
    case MTLPixelFormatRGBA32Float: *out = QRhiTexture::RGBA32F; return true;
    case MTLPixelFormatRGB10A2Unorm: *out = QRhiTexture::RGB10A2; return true;
    case MTLPixelFormatR8Unorm: *out = QRhiTexture::R8; return true;
    case MTLPixelFormatRG8Unorm: *out = QRhiTexture::RG8; return true;
    case MTLPixelFormatR16Float: *out = QRhiTexture::R16F; return true;
    case MTLPixelFormatR32Float: *out = QRhiTexture::R32F; return true;
    default: return false;
    }
}

MTLPixelFormat metalFormat(TETextureFormat f)
{
    switch (f) {
    case TETextureFormatR8Unorm: return MTLPixelFormatR8Unorm;
    case TETextureFormatR16Unorm: return MTLPixelFormatR16Unorm;
    case TETextureFormatR16F: return MTLPixelFormatR16Float;
    case TETextureFormatR32F: return MTLPixelFormatR32Float;
    case TETextureFormatRG8Unorm: return MTLPixelFormatRG8Unorm;
    case TETextureFormatRG16Unorm: return MTLPixelFormatRG16Unorm;
    case TETextureFormatRG16F: return MTLPixelFormatRG16Float;
    case TETextureFormatRG32F: return MTLPixelFormatRG32Float;
    case TETextureFormatRG11B10F: return MTLPixelFormatRG11B10Float;
    case TETextureFormatRGB10_A2Unorm: return MTLPixelFormatRGB10A2Unorm;
    case TETextureFormatBGR10_A2Unorm: return MTLPixelFormatBGR10A2Unorm;
    case TETextureFormatRGBA8Unorm: return MTLPixelFormatRGBA8Unorm;
    case TETextureFormatBGRA8Unorm: return MTLPixelFormatBGRA8Unorm;
    case TETextureFormatSRGBA8Unorm: return MTLPixelFormatRGBA8Unorm_sRGB;
    case TETextureFormatSBGRA8Unorm: return MTLPixelFormatBGRA8Unorm_sRGB;
    case TETextureFormatRGBA16Unorm: return MTLPixelFormatRGBA16Unorm;
    case TETextureFormatRGBA16F: return MTLPixelFormatRGBA16Float;
    case TETextureFormatRGBA32F: return MTLPixelFormatRGBA32Float;
    default: return MTLPixelFormatInvalid;
    }
}

MTLTextureSwizzle swizzleOf(TETextureComponentSource s)
{
    switch (s) {
    case TETextureComponentSourceZero: return MTLTextureSwizzleZero;
    case TETextureComponentSourceOne: return MTLTextureSwizzleOne;
    case TETextureComponentSourceRed: return MTLTextureSwizzleRed;
    case TETextureComponentSourceGreen: return MTLTextureSwizzleGreen;
    case TETextureComponentSourceBlue: return MTLTextureSwizzleBlue;
    case TETextureComponentSourceAlpha: return MTLTextureSwizzleAlpha;
    }
    return MTLTextureSwizzleRed;
}

bool isIdentity(TETextureComponentMap m)
{
    return m.r == TETextureComponentSourceRed && m.g == TETextureComponentSourceGreen && m.b == TETextureComponentSourceBlue &&
           m.a == TETextureComponentSourceAlpha;
}

} // namespace

struct ToxMetal::Impl {
    QRhi *rhi = nullptr;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLSharedEvent> copyDone = nil; // signalled by our copies into the input textures
    uint64_t copyValue = 0;
    TEMetalContext *context = nullptr;
    std::map<std::string, std::array<std::shared_ptr<Slot>, 3>> pool;
    std::vector<std::shared_ptr<Slot>> retired; // let go of, kept until TouchEngine's final release
    // The output held: TouchEngine's texture and the semaphore it asked us to wait for
    TETexture *output = nullptr;
    TESemaphore *outputSemaphore = nullptr;
    uint64_t outputWait = 0;
    // QRhi textures over Metal textures TouchEngine gave, by Metal texture, dropped when not used for a while
    struct Wrapped {
        std::unique_ptr<QRhiTexture> texture;
        id<MTLTexture> view = nil; // with the component map's swizzle, if it has one
        unsigned lastUsed = 0;
    };
    std::map<void *, Wrapped> wrapped;
    std::map<void *, id<MTLTexture>> surfaceTextures; // IOSurface outputs, by surface
    unsigned frame = 0;
};

ToxMetal::ToxMetal() : d(std::make_unique<Impl>()) {}

std::unique_ptr<ToxMetal> ToxMetal::create(QRhi *rhi, QString *error)
{
    if (!rhi || rhi->backend() != QRhi::Metal || !rhi->nativeHandles()) return nullptr;
    std::unique_ptr<ToxMetal> t(new ToxMetal);
    auto *handles = static_cast<const QRhiMetalNativeHandles *>(rhi->nativeHandles());
    t->d->rhi = rhi;
    t->d->device = (id<MTLDevice>)handles->dev;
    t->d->queue = [t->d->device newCommandQueue];
    t->d->copyDone = [t->d->device newSharedEvent];
    if (!t->d->queue || !t->d->copyDone) {
        if (error) *error = QStringLiteral("Cannot make a Metal command queue for TouchEngine");
        return nullptr;
    }
    const TEResult r = TEMetalContextCreate(t->d->device, &t->d->context);
    if (r != TEResultSuccess || !t->d->context) {
        if (error) *error = QString::fromUtf8(TEResultGetDescription(r));
        return nullptr;
    }
    return t;
}

ToxMetal::~ToxMetal() { close(); }

TEGraphicsContext *ToxMetal::context() const { return reinterpret_cast<TEGraphicsContext *>(d->context); }

bool ToxMetal::setInput(TEInstance *instance, const char *identifier, QRhiTexture *source, QString *error)
{
    auto fail = [&](const QString &why) {
        if (error) *error = why;
        return false;
    };
    if (!source) { // no picture: the link is emptied
        const TEResult r = TEInstanceLinkSetTextureValue(instance, identifier, nullptr, context());
        return r == TEResultSuccess || TEResultGetSeverity(r) != TESeverityError;
    }
    const QRhiTexture::NativeTexture native = source->nativeTexture();
    id<MTLTexture> from = (__bridge id<MTLTexture>)reinterpret_cast<void *>(quintptr(native.object));
    if (!from) return fail(QStringLiteral("The picture sent to TouchEngine has no Metal texture"));
    const int w = int(from.width), h = int(from.height);
    auto &pool = d->pool[identifier];
    for (std::shared_ptr<Slot> &entry : pool) {
        if (!entry) entry = std::make_shared<Slot>();
        if (entry->users.load() != 0) continue; // TouchEngine is still reading it
        if (entry->texture && (entry->w != w || entry->h != h || entry->format != from.pixelFormat)) {
            // Another size: it must outlive TouchEngine's last use, even after EndUse
            if (entry->handle) TERelease(&entry->handle);
            d->retired.push_back(entry);
            entry = std::make_shared<Slot>();
        }
        if (d->retired.size() >= 64) return fail(QStringLiteral("TouchEngine is retaining resized input textures; reload the component"));
        Slot &slot = *entry;
        if (!slot.texture) {
            MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:from.pixelFormat width:NSUInteger(w)
                                                                                           height:NSUInteger(h) mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModePrivate;
            slot.texture = [d->device newTextureWithDescriptor:desc];
            if (!slot.texture) return fail(QStringLiteral("Cannot allocate a TouchEngine input texture"));
            slot.w = w;
            slot.h = h;
            slot.format = from.pixelFormat;
            // Our pictures hold their bottom row first
            slot.handle = TEMetalTextureCreate(slot.texture, TETextureOriginBottomLeft, kTETextureComponentMapIdentity, Slot::event, &slot);
            if (!slot.handle) return fail(QStringLiteral("Cannot share a texture with TouchEngine"));
        }
        // The copy, on the GPU, signalling an event TouchEngine waits for (the picture was finished by the last frame)
        id<MTLCommandBuffer> cb = [d->queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromTexture:from sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(NSUInteger(w), NSUInteger(h), 1)
                    toTexture:slot.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        const uint64_t value = ++d->copyValue;
        [cb encodeSignalEvent:d->copyDone value:value];
        [cb commit];
        const TEResult set = TEInstanceLinkSetTextureValue(instance, identifier, reinterpret_cast<TETexture *>(slot.handle), context());
        if (TEResultGetSeverity(set) == TESeverityError) return fail(QString::fromUtf8(TEResultGetDescription(set)));
        if (TEInstanceDoesTextureOwnershipTransfer(instance)) {
            TEMetalSemaphore *sem = TEMetalSemaphoreCreate([d->copyDone newSharedEventHandle], nullptr, nullptr);
            if (sem) {
                TEInstanceAddTextureTransfer(instance, reinterpret_cast<TETexture *>(slot.handle), reinterpret_cast<TESemaphore *>(sem), value);
                TERelease(&sem);
            }
        } else {
            [cb waitUntilCompleted]; // nothing makes TouchEngine wait: it is done before the cook starts
        }
        return true;
    }
    return true; // all three still in use: this frame's picture is not sent
}

QRhiTexture *ToxMetal::receive(TEInstance *instance, const char *identifier, QSize *size, bool *flipped, QString *error)
{
    auto fail = [&](const QString &why) -> QRhiTexture * {
        if (error) *error = why;
        return nullptr;
    };
    ++d->frame;
    releaseOutput(instance);
    TETexture *value = nullptr;
    const TEResult got = TEInstanceLinkGetTextureValue(instance, identifier, TELinkValueCurrent, &value);
    if (TEResultGetSeverity(got) == TESeverityError) return fail(QString::fromUtf8(TEResultGetDescription(got)));
    if (!value) return nullptr;
    d->output = value; // released by releaseOutput(), never retained again
    if (TEInstanceHasTextureTransfer(instance, value)) {
        TESemaphore *sem = nullptr;
        uint64_t wait = 0;
        if (TEInstanceGetTextureTransfer(instance, value, &sem, &wait) == TEResultSuccess && sem) {
            d->outputSemaphore = sem;
            d->outputWait = wait;
            if (TESemaphoreGetType(sem) == TESemaphoreTypeMetal) {
                MTLSharedEventHandle *handle = TEMetalSemaphoreGetSharedEventHandle(reinterpret_cast<TEMetalSemaphore *>(sem));
                id<MTLSharedEvent> event = handle ? [d->device newSharedEventWithHandle:handle] : nil;
                // TouchEngine's work on the picture is done before it is read (a wait of the CPU: the frame is finished, so short)
                if (event && ![event waitUntilSignaledValue:wait timeoutMS:1000]) return fail(QStringLiteral("TouchEngine's output is not ready"));
            }
        }
    }
    id<MTLTexture> metal = nil;
    const TETextureType type = TETextureGetType(value);
    if (type == TETextureTypeMetal) {
        metal = TEMetalTextureGetTexture(reinterpret_cast<const TEMetalTexture *>(value));
    } else if (type == TETextureTypeIOSurface) {
        auto *surfaceTexture = reinterpret_cast<TEIOSurfaceTexture *>(value);
        IOSurfaceRef surface = TEIOSurfaceTextureGetSurface(surfaceTexture);
        auto found = d->surfaceTextures.find(surface);
        if (found != d->surfaceTextures.end()) {
            metal = found->second;
        } else if (surface) {
            const MTLPixelFormat format = metalFormat(TEIOSurfaceTextureGetFormat(surfaceTexture));
            if (format == MTLPixelFormatInvalid) return fail(QStringLiteral("TouchEngine gave a texture format NyxMap cannot show"));
            MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:IOSurfaceGetWidth(surface)
                                                                                           height:IOSurfaceGetHeight(surface) mipmapped:NO];
            metal = [d->device newTextureWithDescriptor:desc iosurface:surface plane:0];
            if (metal) d->surfaceTextures[surface] = metal;
        }
    }
    if (!metal) return fail(QStringLiteral("Cannot import the TouchEngine output texture"));
    void *key = (__bridge void *)metal;
    Impl::Wrapped &wrap = d->wrapped[key];
    wrap.lastUsed = d->frame;
    if (!wrap.texture) {
        QRhiTexture::Format format;
        QRhiTexture::Flags flags;
        if (!qrhiFormat(metal.pixelFormat, &format, &flags)) {
            d->wrapped.erase(key);
            return fail(QStringLiteral("TouchEngine gave a texture format NyxMap cannot show"));
        }
        id<MTLTexture> shown = metal;
        const TETextureComponentMap map = TETextureGetComponentMap(value);
        if (!isIdentity(map)) {
            const MTLTextureSwizzleChannels channels = MTLTextureSwizzleChannelsMake(swizzleOf(map.r), swizzleOf(map.g), swizzleOf(map.b), swizzleOf(map.a));
            shown = [metal newTextureViewWithPixelFormat:metal.pixelFormat textureType:MTLTextureType2D levels:NSMakeRange(0, 1)
                                                  slices:NSMakeRange(0, 1) swizzle:channels];
            if (!shown) {
                d->wrapped.erase(key);
                return fail(QStringLiteral("Cannot swizzle the TouchEngine output texture"));
            }
        }
        wrap.view = shown;
        wrap.texture.reset(d->rhi->newTexture(format, QSize(int(shown.width), int(shown.height)), 1, flags));
        if (!wrap.texture->createFrom({quint64(quintptr((__bridge void *)shown)), 0})) {
            d->wrapped.erase(key);
            return fail(QStringLiteral("Cannot use the TouchEngine output texture"));
        }
    }
    // The ones not used for a long time (TouchEngine recycles its own): dropped
    for (auto it = d->wrapped.begin(); it != d->wrapped.end();) {
        if (d->frame - it->second.lastUsed > 240) it = d->wrapped.erase(it);
        else ++it;
    }
    *size = wrap.texture->pixelSize();
    *flipped = TETextureGetOrigin(value) == TETextureOriginTopLeft;
    return wrap.texture.get();
}

bool ToxMetal::outputHeld() const { return d->output != nullptr; }

void ToxMetal::releaseOutput(TEInstance *instance)
{
    if (!d->output) return;
    // The instance asks for the texture back (a transfer), with the semaphore it gave
    if (instance && d->outputSemaphore && TEInstanceDoesTextureOwnershipTransfer(instance))
        TEInstanceAddTextureTransfer(instance, d->output, d->outputSemaphore, d->outputWait);
    if (d->outputSemaphore) TERelease(&d->outputSemaphore);
    TERelease(&d->output);
    d->outputWait = 0;
}

void ToxMetal::close()
{
    if (d->output || d->outputSemaphore) {
        if (d->outputSemaphore) TERelease(&d->outputSemaphore);
        if (d->output) TERelease(&d->output);
    }
    for (auto &entry : d->pool)
        for (auto &slot : entry.second)
            if (slot && slot->handle) TERelease(&slot->handle);
    d->pool.clear();
    d->retired.clear();
    d->wrapped.clear();
    d->surfaceTextures.clear();
    if (d->context) TERelease(&d->context);
}
