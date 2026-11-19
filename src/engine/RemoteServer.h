#pragma once
// The remote: a small web server with two pages for a phone, a tablet or a browser, no installation. Open them at
// http://<this machine>:<port>/control and /show and enter the PIN; each links to the other ("/" leads to /control).
//   /control   the show's controls (ShowControl.h): a page of your own, laid out as you want it (pxs::controls: the page, the server and
//              its protocol are pxs's, shared by the Pixel Stereo applications), live both ways
//   /show      the show desk (ShowDesk.h): GO, the cues and their times, snapshots, presets, timelines, and the layers' values
//
// What it is, and is not: it plays the show and moves live values, and lays out the controls. It cannot open or save the project, and it is
// not OSC: a layer's values, by their OSC address, and nothing else of the namespace. Like OSC, it can move any layer. The PIN, the same-origin
// WebSocket, the Content-Security-Policy and the limits are pxs's (pxs/Server.h).
//
// The desk's protocol, for a client that asks it (the controls' is in pxs/Server.h):
//   client -> {"t":"desk"}                               then:
//   server -> {"t":"catalog","catalog":{..}}             at first, and when the project's lists change
//   server -> {"t":"state","state":{..}}                 ten times a second at most, when it changed
//   client -> {"t":"do","c":..}                          a command (ShowDesk.h)
//   client -> {"t":"layer","id":"<id>"}                  server -> {"t":"layer","id":"<id>","tree":{OSCQuery node}}
//   client -> {"t":"listen"|"ignore","path":"<address>"} server -> {"t":"params","v":{"<address>":[values]}}  what changed
//   server -> {"t":"changed"}                            a layer's values changed shape: its tree again
//   client -> {"t":"write","path":"<address>","args":[values]}

#include <pxs/Server.h>

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "Osc.h"

class Engine;
class EngineBackend;

class RemoteServer : public pxs::controls::Server
{
    Q_OBJECT
public:
    explicit RemoteServer(Engine *engine, QObject *parent = nullptr);
    ~RemoteServer() override;

    QStringList urls() const override; // one address per network interface: the desk and the controls

    // The pages, as served (tests)
    static QByteArray page();
    static QByteArray showPage();

signals:
    void showDriven(); // the desk played the show or moved a layer: the interface follows

protected:
    bool hasShowPage() const override { return true; } // the page links to /show
    bool servePage(const QString &path, QByteArray *html, QByteArray *csp) override;
    bool message(QTcpSocket *client, const QString &type, const QJsonObject &o) override;
    void tick() override;
    void clientGone(QTcpSocket *client) override;

private:
    struct Desk { // what a client has of the show
        bool desk = false;                    // it asked for the desk: its state is pushed
        QByteArray catalog, state;            // what it has of them
        QString treeSeen;                     // the layer values' shape it knows
        QSet<QString> listening;              // the layers' values it shows
        QHash<QString, QVariantList> params;  // and their last values sent
    };
    void onDesk(QTcpSocket *client, const QString &type, const QJsonObject &o);
    void pushDesk();

    Engine *m_e;
    EngineBackend *m_backend;
    QHash<QTcpSocket *, Desk> m_desks;
    QByteArray m_cspShow;
    OscNamespace m_ns; // the layers' values, on this server's thread
    int m_tick = 0;
};
