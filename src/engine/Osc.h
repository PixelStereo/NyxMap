#pragma once
// OSC control and OSCQuery publication of the whole namespace (composition, layers and groups,
// sources, transport, ISF parameters, color, spatial, effects).
//
//  - OSC over UDP (messages and bundles, address patterns with * ? [] {}): writes values;
//  - OSCQuery (Vidvox proposal) over HTTP on another port: GET / returns the tree as JSON
//    (FULL_PATH, CONTENTS, TYPE, VALUE, RANGE, ACCESS, DESCRIPTION, CLIPMODE), GET /path?VALUE an attribute,
//    GET /?HOST_INFO the server description;
//  - WebSocket on the HTTP port: LISTEN / IGNORE commands, value updates sent as binary OSC messages,
//    OSC messages accepted as binary frames.
//
// Everything is hierarchical and lowercase (layer, group and ISF names aside); a parameter that switches something
// on or off is the child "enable" of what it switches (color/tint/enable, effect/<fx>/enable); the parameters of a shader are under param/.
// Layers are addressed by name, groups contain their members: /layer/<group>/layer/<layer>/opacity.
// Names are made OSC-safe (spaces and reserved characters become '_') and unique among siblings ("_2").
// A locked layer refuses every write except enable, locked and the transport (play, restart, position).
// Writes do not go through the undo stack (show control); edited() tells the interface to refresh.
// When layers, sources or effects change, WebSocket clients receive PATH_CHANGED and fetch the tree again.
// The server is announced by zeroconf (_oscjson._tcp for OSCQuery, _osc._udp for OSC).

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <functional>
#include <map>
#include <vector>

class Engine;
class QTcpServer;
class QTcpSocket;
class QUdpSocket;

#include "OscCodec.h"
#include <pxs/Convert.h>
#include <pxs/core/tree/oscquery.h>
#include <pxs/core/tree/tree.h>

// One node of the namespace (container or method): a pxs-core node, which says what OSCQuery says (type, access, range,
// description) and how to read and write the value (get, set)
using OscNode = pxs::core::tree::Node;

// The getters and setters are written on Qt types (QVariantList); the node says them on pxs-core's values (pxs-qt's convert)
using pxs::convert::getter;
using pxs::convert::setter;

// The namespace built from the engine state; rebuilt when the structure changes.
class OscNamespace
{
public:
    explicit OscNamespace(Engine *e) : m_e(e) {}

    void refresh();                         // rebuilds if the structure signature changed
    int generation() const { return m_generation; } // counts the rebuilds
    QString signature() const;              // layers, sources, effects and parameters (not values)
    const OscNode *node(const QString &path) const;
    QStringList paths() const;              // every method (node with a type)
    // Every readable method, in order, without finding each by its path (the node is at hand)
    void forEachReadable(const std::function<void(const QString &, const OscNode &)> &visit) const;
    // Where a parameter of a holder is (by its name, by its id, and at its common address), those that exist
    QStringList addressesOf(quint64 layer, const QString &path) const;
    // Every readable method at an address and under it
    void forEachUnder(const QString &prefix, const std::function<void(const QString &, const OscNode &)> &visit) const;
    QStringList layerPrefixes(quint64 layer) const; // a layer's addresses (by name, by id)
    QJsonObject toJson(const QString &path) const // node and its contents (OSCQuery)
    {
        return pxs::convert::jsonObjectFromText(pxs::core::tree::describeJson(m_tree, pxs::convert::toUtf8(path)));
    }
    QStringList problems() const // what the namespace lacks of what every pxs tree must have (for the tests)
    {
        QStringList out;
        for (const std::string &p : m_tree.check()) out << pxs::convert::fromUtf8(p);
        return out;
    }
    // The OSC address of a value (a layer's id, 0: the composition — "interaction/<id>/…": an interaction's), as
    // built now ("": it has none)
    QString addressOf(quint64 layer, const QString &path) const;
    static QString addressOf(Engine *e, quint64 layer, const QString &path); // with a namespace made for it
    static QString paramPath(const QString &param); // where a layer parameter is under its layer (spatial/position/x for position/x)
    static QJsonValue jsonValue(const OscNode &n, const QVariant &v, int k)
    {
        const char tag = k >= 0 && std::size_t(k) < n.type.size() ? n.type[std::size_t(k)] : 'f';
        return pxs::convert::jsonValueFromText(pxs::core::tree::valueJson(tag, pxs::convert::toValue(v)));
    }

private:
    void build();
    void complete(); // the range and default of the numbers that were not declared where they are made
    void addShowControl();
    OscNode &add(const QString &path, const QString &type, int access, const QString &description);
    void addLayer(const QString &prefix, quint64 id);

    Engine *m_e;
    QString m_signature;
    int m_generation = 0;
    pxs::core::tree::Tree m_tree;        // the address space (pxs-core's); the OSCQuery description is derived from it
    std::map<QString, OscNode *> m_index; // the nodes that were added, by address, in order (the order the visits keep)
    QHash<quint64, QString> m_prefixOf; // a layer's address
    QHash<quint64, QString> m_interactionPrefix; // an interaction's ("/interaction/<name>")
};

class OscServer : public QObject
{
    Q_OBJECT
public:
    explicit OscServer(Engine *engine, QObject *parent = nullptr);
    ~OscServer() override;

    // Ports 0: chosen by the system (tests). Returns false if a port could not be opened.
    // announce: zeroconf (_oscjson._tcp, _osc._udp)
    // listen: the address of the network interface to listen on ("": all of them, "127.0.0.1": this computer only)
    bool start(quint16 oscPort, quint16 queryPort, const QString &name = QStringLiteral("NyxMap"), bool announce = true,
               const QString &listen = QString());
    void stop();
    bool isRunning() const;
    quint16 oscPort() const;
    quint16 queryPort() const;
    QString status() const { return m_status; }

    // Entry points, also used directly by the tests
    int handlePacket(const QByteArray &packet); // number of values written
    bool handleMessage(const osc::Message &m);
    // HTTP answer to a GET (status code and JSON body)
    QByteArray httpGet(const QString &target, int *status);

signals:
    void edited(); // values changed through OSC: the interface refreshes
    void eventObserved(const QString &channel, const QString &message);

private:
    struct Client {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        bool websocket = false;
        QSet<QString> listening;
        QHash<QString, QVariantList> sent; // last value sent per listened path
    };
    void onUdp();
    void onConnection();
    void onData(QTcpSocket *s);
    void onWsFrame(Client &c, int opcode, const QByteArray &payload);
    void sendWs(QTcpSocket *s, int opcode, const QByteArray &payload);
    void pushListened();
    QJsonObject hostInfo() const;

    Engine *m_e;
    OscNamespace m_ns;
    QUdpSocket *m_udp = nullptr;
    QTcpServer *m_http = nullptr;
    QHash<QTcpSocket *, Client> m_clients;
    QTimer m_listenTimer;
    QString m_name, m_status;
    QString m_treeSignature; // tree last seen by the WebSocket clients (PATH_CHANGED when it changes)
    class Zeroconf *m_zeroconf = nullptr;
};
