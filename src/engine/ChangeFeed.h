#pragma once
// Who is told that something an OSC client can read has changed (the OSC feedback). Nothing polls: what writes a
// parameter, a control or a layer's state says so, the one who listens (if any) hears it, and reads only what was named.
// The calls come from any thread, often with the engine's lock held: they only note it and return.
#include <QString>
#include <atomic>
#include <thread>

class ChangeFeed
{
public:
    class Observer
    {
    public:
        virtual ~Observer() = default;
        // A parameter was written: its holder (a layer's id; 0: the composition, an interaction, the show, a fixture) and its
        // address in the holder
        virtual void parameterWritten(quint64 holder, const QString &path) = 0;
        // Everything of a layer's own (its flags, its source, its transport) may have changed
        virtual void layerChanged(quint64 layer) = 0;
        // What is at this OSC address, and below it, may have changed
        virtual void addressChanged(const QString &address) = 0;
    };
    // One listener at a time (the feedback)
    static void observe(Observer *o) { s_observer.store(o, std::memory_order_release); }
    static void ignore(Observer *o)
    {
        Observer *expected = o;
        s_observer.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        while (s_calls.load(std::memory_order_acquire) > 0) std::this_thread::yield(); // a call that already took it is over
    }
    static bool listening() { return s_observer.load(std::memory_order_acquire) != nullptr; }
    static void parameter(quint64 holder, const QString &path)
    {
        Call call;
        if (Observer *o = s_observer.load(std::memory_order_acquire)) o->parameterWritten(holder, path);
    }
    static void layer(quint64 id)
    {
        Call call;
        if (Observer *o = s_observer.load(std::memory_order_acquire)) o->layerChanged(id);
    }
    static void address(const QString &address)
    {
        Call call;
        if (Observer *o = s_observer.load(std::memory_order_acquire)) o->addressChanged(address);
    }

private:
    struct Call { // a notification on its way: the listener is not destroyed under it
        Call() { s_calls.fetch_add(1, std::memory_order_acq_rel); }
        ~Call() { s_calls.fetch_sub(1, std::memory_order_acq_rel); }
    };
    static inline std::atomic<Observer *> s_observer{nullptr};
    static inline std::atomic<int> s_calls{0};
};
