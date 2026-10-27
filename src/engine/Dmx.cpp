#include "Dmx.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSettings>
#include <QUdpSocket>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace {

void put16(QByteArray &b, int at, int v)
{
    b[at] = char((v >> 8) & 0xff);
    b[at + 1] = char(v & 0xff);
}

QByteArray padded(const QByteArray &data, int minimum)
{
    QByteArray d = data.left(512);
    if (d.size() < minimum) d.append(QByteArray(minimum - d.size(), '\0'));
    return d;
}

} // namespace

namespace dmx {

QByteArray artDmxPacket(int portAddress, quint8 sequence, const QByteArray &data)
{
    QByteArray d = padded(data, 2);
    if (d.size() & 1) d.append('\0');
    QByteArray p(18, '\0');
    std::memcpy(p.data(), "Art-Net", 8); // with its terminating zero
    p[8] = 0x00; // OpDmx 0x5000, low byte first
    p[9] = 0x50;
    p[10] = 0;   // protocol version 14
    p[11] = 14;
    p[12] = char(sequence);
    p[13] = 0;   // physical port
    p[14] = char(portAddress & 0xff);        // SubUni: sub-net and universe
    p[15] = char((portAddress >> 8) & 0x7f); // Net
    put16(p, 16, int(d.size()));
    return p + d;
}

QByteArray sacnPacket(const QUuid &cid, const QString &sourceName, quint8 priority, quint8 sequence, int universe,
                      const QByteArray &data, bool terminated)
{
    const QByteArray d = padded(data, 1);
    const int total = 126 + int(d.size());
    QByteArray p(126, '\0');
    // Root layer
    put16(p, 0, 0x0010); // preamble size; postamble size 0
    std::memcpy(p.data() + 4, "ASC-E1.17\0\0\0", 12);
    put16(p, 16, 0x7000 | (total - 16));
    put16(p, 20, 0x0004); // VECTOR_ROOT_E131_DATA (4 bytes, high ones zero)
    const QByteArray id = cid.toRfc4122();
    std::memcpy(p.data() + 22, id.constData(), 16);
    // Framing layer
    put16(p, 38, 0x7000 | (total - 38));
    put16(p, 42, 0x0002); // VECTOR_E131_DATA_PACKET
    const QByteArray name = sourceName.toUtf8().left(63);
    std::memcpy(p.data() + 44, name.constData(), size_t(name.size()));
    p[108] = char(std::min<int>(priority, 200));
    put16(p, 109, 0); // no synchronization
    p[111] = char(sequence);
    p[112] = char(terminated ? 0x40 : 0); // options: stream terminated
    put16(p, 113, universe);
    // DMP layer
    put16(p, 115, 0x7000 | (total - 115));
    p[117] = 0x02;       // VECTOR_DMP_SET_PROPERTY
    p[118] = char(0xa1); // address and data type
    put16(p, 119, 0);    // first property address
    put16(p, 121, 1);    // address increment
    put16(p, 123, int(d.size()) + 1);
    p[125] = 0; // start code
    return p + d;
}

QString sacnMulticastAddress(int universe)
{
    return QStringLiteral("239.255.%1.%2").arg((universe >> 8) & 0xff).arg(universe & 0xff);
}

} // namespace dmx

// ---------------------------------------------------------------------------

struct DmxSender::Link {
    DmxOutput settings;
    std::unique_ptr<QUdpSocket> socket;
    std::vector<QHostAddress> destinations; // unicast, or Art-Net's broadcast (none: sACN multicast, by universe)
    std::map<int, quint8> sequence;
    std::chrono::steady_clock::time_point next{};
    QString error;
};

DmxSender::DmxSender()
{
    // sACN receivers tell sources apart by their CID: the same one from one run to the next on this machine
    if (!QCoreApplication::organizationName().isEmpty()) {
        QSettings s;
        m_cid = QUuid(s.value(QStringLiteral("dmx/cid")).toString());
        if (m_cid.isNull()) {
            m_cid = QUuid::createUuid();
            s.setValue(QStringLiteral("dmx/cid"), m_cid.toString());
        }
    } else {
        m_cid = QUuid::createUuid();
    }
}

DmxSender::~DmxSender() { stop(); }

void DmxSender::ensureThread()
{
    if (m_thread.joinable()) return;
    m_quit = false;
    m_thread = std::thread([this] { run(); });
}

void DmxSender::follow(const std::vector<DmxOutput> &outputs)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (outputs == m_outputs && (m_thread.joinable() || outputs.empty())) return;
    m_outputs = outputs;
    m_changed = true;
    if (!outputs.empty()) ensureThread();
    m_cv.notify_all();
}

void DmxSender::submit(DmxFrame frame)
{
    auto f = std::make_shared<const DmxFrame>(std::move(frame));
    std::lock_guard<std::mutex> lk(m_mutex);
    m_frame = std::move(f);
}

void DmxSender::stop()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quit = true;
        m_cv.notify_all();
    }
    if (m_thread.joinable()) m_thread.join();
    std::lock_guard<std::mutex> lk(m_mutex);
    m_outputs.clear();
    m_frame.reset();
    m_states.clear();
}

DmxOutputState DmxSender::state(quint64 output) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const auto it = m_states.find(output);
    return it != m_states.end() ? it->second : DmxOutputState{};
}

void DmxSender::run()
{
    using clock = std::chrono::steady_clock;
    std::map<quint64, Link> links; // only this thread's: the sockets live and die here

    auto send = [&](Link &l, int universe, const QByteArray &data, bool terminated) {
        const DmxOutput &o = l.settings;
        QByteArray packet;
        std::vector<QHostAddress> to = l.destinations;
        quint8 &seq = l.sequence[universe];
        if (o.protocol == DmxOutput::Protocol::ArtNet) {
            if (universe < 0 || universe > 32767) return QStringLiteral("Art-Net universe %1 out of 0..32767").arg(universe);
            seq = quint8(seq % 255 + 1); // 1..255 (0 would turn reordering off)
            packet = dmx::artDmxPacket(universe, seq, data);
        } else {
            if (universe < 1 || universe > 63999) return QStringLiteral("sACN universe %1 out of 1..63999").arg(universe);
            ++seq;
            packet = dmx::sacnPacket(m_cid, o.sourceName, quint8(o.priority), seq, universe, data, terminated);
            if (to.empty()) to.emplace_back(dmx::sacnMulticastAddress(universe));
        }
        QString error;
        for (const QHostAddress &a : to)
            if (l.socket->writeDatagram(packet, a, quint16(o.effectivePort())) != packet.size()) error = l.socket->errorString();
        return error;
    };
    // sACN: receivers let go of the universe at once rather than holding the last values
    auto terminate = [&](Link &l) {
        if (!l.socket || l.settings.protocol != DmxOutput::Protocol::Sacn) return;
        const QByteArray zero(512, '\0');
        for (const auto &[u, s] : l.sequence)
            for (int k = 0; k < 3; ++k) send(l, u, zero, true);
    };
    auto open = [&](const DmxOutput &o) {
        Link l;
        l.settings = o;
        l.socket = std::make_unique<QUdpSocket>();
        const QHostAddress local = o.interfaceAddress.isEmpty() ? QHostAddress(QHostAddress::AnyIPv4)
                                                                : QHostAddress(o.interfaceAddress);
        if (local.isNull() || !l.socket->bind(local, 0)) {
            l.error = QStringLiteral("Cannot send from %1").arg(o.interfaceAddress);
            l.socket.reset();
            return l;
        }
        QNetworkInterface card; // the one holding the local address, if one is chosen
        QHostAddress broadcast(QHostAddress::Broadcast);
        if (!o.interfaceAddress.isEmpty())
            for (const QNetworkInterface &i : QNetworkInterface::allInterfaces())
                for (const QNetworkAddressEntry &e : i.addressEntries())
                    if (e.ip() == local) {
                        card = i;
                        if (!e.broadcast().isNull()) broadcast = e.broadcast();
                    }
        if (o.unicast) {
            QStringList bad;
            for (const QString &ip : o.destinations) {
                const QHostAddress a(ip);
                if (a.isNull()) bad << ip;
                else l.destinations.push_back(a);
            }
            if (!bad.isEmpty()) l.error = QStringLiteral("Not an IP address: %1").arg(bad.join(QStringLiteral(", ")));
            else if (l.destinations.empty()) l.error = QStringLiteral("Unicast: no IP address to send to");
            if (!l.error.isEmpty()) l.socket.reset();
        } else if (o.protocol == DmxOutput::Protocol::ArtNet) {
            l.destinations = {broadcast};
        } else if (card.isValid()) {
            l.socket->setMulticastInterface(card);
        }
        return l;
    };

    std::unique_lock<std::mutex> lk(m_mutex);
    while (!m_quit) {
        if (m_changed) {
            m_changed = false;
            const std::vector<DmxOutput> outputs = m_outputs;
            lk.unlock();
            for (auto it = links.begin(); it != links.end();) {
                const auto o = std::find_if(outputs.begin(), outputs.end(),
                                            [&](const DmxOutput &x) { return x.id == it->first; });
                if (o != outputs.end() && *o == it->second.settings) {
                    ++it;
                    continue;
                }
                terminate(it->second);
                it = links.erase(it);
            }
            for (const DmxOutput &o : outputs)
                if (o.enabled && !links.count(o.id)) links.emplace(o.id, open(o));
            lk.lock();
            m_states.clear();
            for (const DmxOutput &o : outputs) {
                DmxOutputState &s = m_states[o.id];
                const auto l = links.find(o.id);
                if (l == links.end()) continue; // off
                s.level = l->second.error.isEmpty() ? DmxOutputState::Ok : DmxOutputState::Error;
                s.text = l->second.error;
            }
            continue;
        }
        const std::shared_ptr<const DmxFrame> frame = m_frame;
        lk.unlock();
        const auto now = clock::now();
        auto wake = now + std::chrono::milliseconds(100);
        std::map<quint64, std::pair<quint64, QString>> sent; // packets, error
        for (auto &[id, l] : links) {
            if (!l.socket) continue;
            if (now >= l.next) {
                l.next = now + std::chrono::microseconds(qint64(1e6 / l.settings.rate));
                if (frame)
                    for (auto it = frame->lower_bound({id, std::numeric_limits<int>::min()});
                         it != frame->end() && it->first.first == id; ++it) {
                        const QString error = send(l, it->first.second, it->second, false);
                        if (error.isEmpty()) ++sent[id].first;
                        else sent[id].second = error;
                    }
            }
            wake = std::min(wake, l.next);
        }
        lk.lock();
        for (const auto &[id, r] : sent) {
            DmxOutputState &s = m_states[id];
            s.packets += r.first;
            s.level = r.second.isEmpty() ? DmxOutputState::Ok : DmxOutputState::Error;
            s.text = r.second;
        }
        m_cv.wait_until(lk, wake, [this] { return m_quit || m_changed; });
    }
    lk.unlock();
    for (auto &[id, l] : links) terminate(l);
    links.clear();
}
