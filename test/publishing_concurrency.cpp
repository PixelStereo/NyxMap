#include "Publishing.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <atomic>
#include <cstdio>
#include <thread>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QOpenGLContext context;
    if (!context.create()) return 1;
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    if (!context.makeCurrent(&surface)) return 1;
    Publishing publishing([] {});
    std::atomic<bool> ready{false}, done{false};
    std::atomic<unsigned> reads{0}, failures{0};
    std::thread reader([&] {
        ready = true;
        do {
            for (quint64 id = 1; id <= 256; ++id) {
                const auto state = publishing.state(id, PublishKind::Ndi);
                if (state.level != PublishState::Off) ++failures;
                ++reads;
            }
        } while (!done.load());
    });
    while (!ready.load()) std::this_thread::yield();
    for (int round = 0; round < 8; ++round)
        for (quint64 id = 1; id <= 256; ++id) publishing.follow({{id, PublishSettings{}}});
    publishing.release();
    done = true; reader.join();
    std::printf("publishing_concurrency: %u state reads, %u failures\n", reads.load(), failures.load());
    return failures == 0 && reads > 0 ? 0 : 1;
}
