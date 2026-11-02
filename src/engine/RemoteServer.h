#pragma once
// The remote: a small web server with two pages for a phone, a tablet or a browser, no installation. Open them at
// http://<this machine>:<port>/control and /show and enter the PIN; each links to the other ("/" leads to /control).
//   /control   the show's controls (ShowControl.h): sliders, switches, selectors, hold buttons, color pickers, live both ways
//   /show      the show desk (ShowDesk.h): GO, the cues and their times, snapshots, presets, timelines, and the layers' values
//
// What it is, and is not: it plays the show and moves live values. It cannot open, save or edit the project, and it is not OSC:
// a layer's values, by their OSC address, and nothing else of the namespace. Like OSC, it can move any layer. It asks a
// code (the "PIN": 4 to 32 letters or digits, chosen by the operator, or none at all), compared in constant time; a machine that
// gets it wrong eight times is refused for a minute. Without a code anyone on the network may move the controls.
// A WebSocket is accepted only from the page this server served (same origin). The page's script and style are the only
// ones its Content-Security-Policy allows. There is no encryption: the PIN and the values cross the network in clear, as OSC
// does: use it on the show's network, not on a shared one.
//
// Protocol (text frames, JSON), for those who want their own client:
//   GET /info          {"pin":true|false}: whether a code is asked
//   GET /ws?pin=<PIN>  upgrade to a WebSocket (no pin= when none is asked)
//   server -> {"t":"controls","title":..,"list":[{id,name,key,kind,min,max,step,unit,choices,value,rgb}]}   at connection, and
//             when a control is added, removed or shaped
//   server -> {"t":"values","v":{"<id>":<number>|[r,g,b]}}                                                  what changed
//   client -> {"t":"set","id":"<id>","v":<number>}      client -> {"t":"color","id":"<id>","rgb":[r,g,b]}
// The desk, for a client that asks it:
//   client -> {"t":"desk"}                               then:
//   server -> {"t":"catalog","catalog":{..}}             at first, and when the project's lists change
//   server -> {"t":"state","state":{..}}                 ten times a second at most, when it changed
//   client -> {"t":"do","c":..}                          a command (ShowDesk.h)
//   client -> {"t":"layer","id":"<id>"}                  server -> {"t":"layer","id":"<id>","tree":{OSCQuery node}}
//   client -> {"t":"listen"|"ignore","path":"<address>"} server -> {"t":"params","v":{"<address>":[values]}}  what changed
//   server -> {"t":"changed"}                            a layer's values changed shape: its tree again
//   client -> {"t":"write","path":"<address>","args":[values]}

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <vector>

#include "Osc.h"

class Engine;
class QTcpServer;
class QTcpSocket;

class RemoteServer : public QObject
{
    Q_OBJECT
public:
    explicit RemoteServer(Engine *engine, QObject *parent = nullptr);
    ~RemoteServer() override;

    // Port 0: chosen by the system (tests). `pin` empty: no code asked. `listen`: the interface's address ("": all). False with
    // status() when it cannot
    bool start(quint16 port, const QString &pin, const QString &listen = QString());
    void stop();
    bool isRunning() const;
    quint16 port() const;
    QString status() const { return m_status; }
    int clientCount() const;
    // Where to point the phone: one address per network interface
    QStringList urls() const;
    // A random PIN of `digits` digits
    static QString makePin(int digits = 6);
    static bool pinValid(const QString &pin); // 4 to 32 letters or digits (an empty one is "no code": start() takes it)

    // The pages, as served (tests)
    static QByteArray page();
    static QByteArray showPage();

signals:
    void clientsChanged(int count);
    void controlMoved(); // a control was set from a client: the interface follows
    void showDriven();   // the desk played the show or moved a layer: the interface follows

private:
    struct Client {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        bool websocket = false;
        quint64 controlsSeen = ~0ull;
        QHash<quint64, QList<double>> sent; // the values the client has
        double budget = 300;                  // messages it may still send this second
        bool desk = false;                    // the show desk's page
        QByteArray catalog, state;            // what it has of them
        QString treeSeen;                     // the layer values' shape it knows
        QSet<QString> listening;              // the layers' values it shows
        QHash<QString, QVariantList> params;  // and their last values sent
    };
    void onConnection();
    void onData(QTcpSocket *s);
    void onFrame(Client &c, int opcode, const QByteArray &payload);
    void push();
    void pushDesk();
    void onDesk(Client &c, const QString &type, const QJsonObject &o);
    void send(QTcpSocket *s, const QByteArray &frame) const;
    QByteArray controlsMessage() const;
    bool authorized(const QByteArray &presented) const;

    Engine *m_e;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Client> m_clients;
    QTimer m_timer;
    QString m_pin, m_status;
    QByteArray m_csp, m_cspShow;
    OscNamespace m_ns; // the layers' values, on this server's thread
    int m_tick = 0;
    struct Attempts {
        int failures = 0;
        qint64 until = 0; // ms since epoch
    };
    QHash<QString, Attempts> m_attempts; // by address
};
