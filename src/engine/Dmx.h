#pragma once
// DMX over the network: Art-Net (ArtDmx) and sACN (ANSI E1.31) packets, and the sender that streams the universes
// pixel mapping builds (Fixtures.h). The sender has a thread of its own: each output sends all of its universes at
// its rate, the latest values each time (lights expect a steady stream), whatever the render's frame rate.

#include "Fixtures.h"

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dmx {
// ArtDmx (Art-Net 4): portAddress 0..32767 (net, sub-net, universe); sequence 0: not used; length 2..512, even
QByteArray artDmxPacket(int portAddress, quint8 sequence, const QByteArray &data);
// E1.31 data packet: universe 1..63999, priority 0..200; terminated: the last packets of a stream. Start code 0: the
// levels; 0xDD: a priority for each channel (0: not this source's — a receiver takes the channel from another source)
QByteArray sacnPacket(const QUuid &cid, const QString &sourceName, quint8 priority, quint8 sequence, int universe,
                      const QByteArray &data, bool terminated = false, quint8 startCode = 0);
inline constexpr quint8 kSacnPerChannelPriority = 0xDD;
// The multicast group of an sACN universe: 239.255.hi.lo
QString sacnMulticastAddress(int universe);
} // namespace dmx

struct DmxOutputState {
    enum Level { Off, Ok, Error };
    Level level = Off;
    QString text;
    quint64 packets = 0; // sent since the output started
};

class DmxSender
{
public:
    DmxSender();
    ~DmxSender();
    DmxSender(const DmxSender &) = delete;
    DmxSender &operator=(const DmxSender &) = delete;

    // --- Any thread
    void follow(const std::vector<DmxOutput> &outputs); // the outputs as they are now (cheap when unchanged)
    // The latest universes, by output and universe, and the channels that are ours (buildDmxOwnership): sACN sends a
    // priority for each channel, about once a second, 0 for the others
    void submit(DmxFrame frame, DmxFrame owned = {});
    void stop(); // sACN streams terminated, the thread joined; follow() starts it again
    DmxOutputState state(quint64 output) const;
    QUuid cid() const { return m_cid; } // sACN: this sender's component id, stable on this machine

private:
    struct Link; // an output's socket and its universes' sequences (the thread's)
    void run();
    void ensureThread();

    QUuid m_cid;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<DmxOutput> m_outputs;               // (m_mutex)
    std::shared_ptr<const DmxFrame> m_frame;        // (m_mutex)
    std::shared_ptr<const DmxFrame> m_owned;        // (m_mutex)
    std::map<quint64, DmxOutputState> m_states;     // (m_mutex)
    bool m_quit = false;                            // (m_mutex)
    bool m_changed = false;                         // outputs changed (m_mutex)
    std::thread m_thread;
};
