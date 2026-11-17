// Hardware frames are not taken by QRhi here (RhiVideoSurface.h): they come through the CPU
#include "RhiVideoSurface.h"

struct RhiVideoSurface::Impl {};

RhiVideoSurface::RhiVideoSurface(QRhi *) : d(std::make_unique<Impl>()) {}
RhiVideoSurface::~RhiVideoSurface() = default;
bool RhiVideoSurface::available(QRhi *) { return false; }
AVBufferRef *RhiVideoSurface::decoderDevice(QRhi *) { return nullptr; }
bool RhiVideoSurface::take(QRhiCommandBuffer *, const VideoFrame &) { return false; }
QRhiTexture *RhiVideoSurface::plane(int) const { return nullptr; }
