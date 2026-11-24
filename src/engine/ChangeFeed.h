#pragma once
// Who is told what was written, for the OSC feedback: a parameter written (its holder and its address), a layer whose state changed, an
// address (a clock, a transport). One observer at a time, notified from any thread, the engine's lock often held: it notes and goes.
//
// The Qt face of pxs-core's ChangeFeed (pxs::core::ChangeFeed, one implementation for every application): the application has one,
// and the parameters declared in this application tell it (Parameter.h).

#include <pxs/Convert.h>
#include <pxs/core/feed/change_feed.h>

#include <QString>

inline pxs::core::ChangeFeed &changeFeed()
{
    static pxs::core::ChangeFeed feed;
    return feed;
}

class ChangeFeed
{
public:
    class Observer
    {
    public:
        virtual ~Observer() = default;
        virtual void parameterWritten(quint64 holder, const QString &path) = 0;
        virtual void layerChanged(quint64 layer) = 0;
        virtual void addressChanged(const QString &address) = 0;
    };

    static void observe(Observer *o)
    {
        auto &a = adapter();
        a.target.store(o, std::memory_order_release);
        changeFeed().observe(o ? &a : nullptr);
    }
    static void ignore(Observer *o)
    {
        auto &a = adapter();
        Observer *expected = o;
        if (a.target.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) changeFeed().ignore(&a);
    }
    static bool listening() { return changeFeed().listening(); }
    static void parameter(quint64 holder, const QString &path) { changeFeed().parameter(holder, pxs::convert::toUtf8(path)); }
    static void layer(quint64 id) { changeFeed().layer(id); }
    static void address(const QString &address) { changeFeed().address(pxs::convert::toUtf8(address)); }

private:
    struct Adapter final : pxs::core::ChangeFeed::Observer {
        std::atomic<::ChangeFeed::Observer *> target{nullptr};
        void parameterWritten(std::uint64_t holder, std::string_view path) override
        {
            if (::ChangeFeed::Observer *o = target.load(std::memory_order_acquire)) o->parameterWritten(holder, pxs::convert::fromUtf8(std::string(path)));
        }
        void layerChanged(std::uint64_t layer) override
        {
            if (::ChangeFeed::Observer *o = target.load(std::memory_order_acquire)) o->layerChanged(layer);
        }
        void addressChanged(std::string_view address) override
        {
            if (::ChangeFeed::Observer *o = target.load(std::memory_order_acquire)) o->addressChanged(pxs::convert::fromUtf8(std::string(address)));
        }
    };
    static Adapter &adapter()
    {
        static Adapter a;
        return a;
    }
};
