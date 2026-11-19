#include "RemoteServer.h"
#include "Engine.h"
#include "RemotePage.h"
#include "ShowDesk.h"
#include "ShowPage.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <cmath>

// What the pxs server asks of the show: its controls, as the engine has them, and what the page sets
class EngineBackend final : public pxs::controls::Backend
{
public:
    explicit EngineBackend(Engine *engine) : m_e(engine) {}
    std::vector<pxs::controls::Control> controls() const override { return m_e->controls(); }
    std::optional<pxs::controls::Control> control(quint64 id) const override { return m_e->control(id); }
    quint64 revision() const override { return m_e->controlsRevision(); }
    QString title() const override
    {
        const QString name = QFileInfo(m_e->projectPath()).completeBaseName();
        return name.isEmpty() ? QStringLiteral("NyxMap") : name;
    }
    bool setValue(quint64 id, double v) override { return m_e->setControlValue(id, v); }
    bool setPoint(quint64 id, double x, double y) override { return m_e->setControlPoint(id, x, y); }
    bool setPoints(quint64 id, const std::vector<double> &xy) override { return m_e->setControlPoints(id, xy); }
    bool setMulti(quint64 id, const std::vector<double> &v) override { return m_e->setControlMulti(id, v); }
    bool setAdsr(quint64 id, const std::array<double, 4> &v) override { return m_e->setControlAdsr(id, v); }
    bool setColor(quint64 id, double r, double g, double b) override { return m_e->setControlColor(id, r, g, b); }
    bool setLayouts(const QHash<quint64, std::array<int, 4>> &places) override { return m_e->setControlLayouts(places); }
    bool update(quint64 id, const pxs::controls::Control &changed) override { return m_e->updateControl(id, changed); }
    bool remove(quint64 id) override { return m_e->removeControl(id); }

private:
    Engine *m_e;
};

QByteArray RemoteServer::page() { return pxs::controls::Server::page(QStringLiteral("NyxMap remote")); }

QByteArray RemoteServer::showPage()
{
    return QByteArray("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
                      "<title>NyxMap show control</title><style>") +
           show_page::kStyle + "</style></head><body>" + show_page::kBody + "<script>" + show_page::kScript + "</script></body></html>";
}

RemoteServer::RemoteServer(Engine *engine, QObject *parent)
    : pxs::controls::Server(nullptr, parent), m_e(engine), m_backend(new EngineBackend(engine)), m_ns(engine)
{
    setBackend(m_backend);
    setPageTitle(QStringLiteral("NyxMap remote"));
    m_cspShow = policy(show_page::kScript, show_page::kStyle);
}

RemoteServer::~RemoteServer()
{
    stop();
    delete m_backend;
}

// The desk and the controls: one address each, per network interface
QStringList RemoteServer::urls() const
{
    QStringList out;
    for (const QString &control : pxs::controls::Server::urls()) {
        out << control.chopped(8) + QStringLiteral("/show");
        out << control;
    }
    return out;
}

bool RemoteServer::servePage(const QString &path, QByteArray *html, QByteArray *csp)
{
    if (path != QLatin1String("/show")) return false;
    *html = showPage();
    *csp = m_cspShow;
    return true;
}

bool RemoteServer::message(QTcpSocket *client, const QString &type, const QJsonObject &o)
{
    onDesk(client, type, o);
    return true;
}

void RemoteServer::tick() { pushDesk(); }

void RemoteServer::clientGone(QTcpSocket *client) { m_desks.remove(client); }

void RemoteServer::onDesk(QTcpSocket *client, const QString &type, const QJsonObject &o)
{
    Desk &c = m_desks[client];
    const QLatin1String layerRoot("/show/layer/by-id/");
    const QString path = o.value("path").toString();
    if (type == QLatin1String("desk")) {
        c.desk = true;
        leaveControls(client); // the desk has its own pages: the controls are not pushed to it
        c.catalog = QJsonDocument(showdesk::catalog(m_e)).toJson(QJsonDocument::Compact); // its first picture
        sendText(client, "{\"t\":\"catalog\",\"catalog\":" + c.catalog + "}");
    } else if (type == QLatin1String("do")) {
        if (showdesk::command(m_e, o)) emit showDriven();
    } else if (type == QLatin1String("layer")) {
        const QString id = o.value("id").toString();
        if (id.toULongLong() == 0) return;
        m_ns.refresh();
        c.treeSeen = m_ns.signature();
        sendText(client, QJsonDocument(QJsonObject{{"t", "layer"}, {"id", id},
            {"tree", m_ns.toJson(layerRoot + id)}}).toJson(QJsonDocument::Compact));
    } else if (!path.startsWith(layerRoot)) {
        return; // a layer's values, nothing else of the namespace
    } else if (type == QLatin1String("listen")) {
        if (c.listening.size() < 256) c.listening.insert(path);
        c.params.remove(path);
    } else if (type == QLatin1String("ignore")) {
        c.listening.remove(path);
        c.params.remove(path);
    } else if (type == QLatin1String("write")) {
        m_ns.refresh();
        const OscNode *node = m_ns.node(path);
        if (!node || !node->set || !(node->access & 2)) return;
        const QJsonArray args = o.value("args").toArray();
        const bool trigger = node->type == QLatin1String("N");
        if (!trigger && args.size() != node->type.size()) return;
        QVariantList values;
        for (int k = 0; !trigger && k < args.size(); ++k) {
            const QChar tag = node->type[k];
            const QJsonValue v = args[k];
            if (tag == QLatin1Char('s')) {
                if (!v.isString()) return;
                values << v.toString();
            } else if (tag == QLatin1Char('T') || tag == QLatin1Char('F')) {
                if (!v.isBool() && !v.isDouble()) return;
                values << (v.isBool() ? v.toBool() : v.toDouble() != 0);
            } else {
                if (!v.isDouble() || !std::isfinite(v.toDouble())) return;
                values << (tag == QLatin1Char('i') ? QVariant(int(std::lround(v.toDouble()))) : QVariant(v.toDouble()));
            }
        }
        if (node->set(values)) emit showDriven();
    }
}

// The desks: the project's lists when they changed (twice a second), the show's state, the values shown
void RemoteServer::pushDesk()
{
    if (m_desks.isEmpty()) return;
    ++m_tick;
    QByteArray state, catalog;
    if (m_tick % 2 == 0) state = QJsonDocument(showdesk::state(m_e)).toJson(QJsonDocument::Compact);
    const bool slow = m_tick % 10 == 0;
    if (slow) catalog = QJsonDocument(showdesk::catalog(m_e)).toJson(QJsonDocument::Compact);
    QString signature;
    for (QTcpSocket *client : websockets()) {
        const auto found = m_desks.find(client);
        if (found == m_desks.end() || !found->desk) continue;
        Desk &c = *found;
        if (slow && c.catalog != catalog) {
            c.catalog = catalog;
            sendText(client, "{\"t\":\"catalog\",\"catalog\":" + catalog + "}");
        }
        if (!state.isEmpty() && c.state != state) {
            c.state = state;
            sendText(client, "{\"t\":\"state\",\"state\":" + state + "}");
        }
        if (c.listening.isEmpty()) continue;
        if (signature.isEmpty()) {
            m_ns.refresh();
            signature = m_ns.signature();
        }
        if (c.treeSeen != signature) {
            c.treeSeen = signature;
            c.listening.clear();
            c.params.clear();
            sendText(client, "{\"t\":\"changed\"}");
            continue;
        }
        QJsonObject changed;
        for (const QString &path : std::as_const(c.listening)) {
            const OscNode *node = m_ns.node(path);
            if (!node || !node->get || !(node->access & 1)) continue;
            const QVariantList now = node->get();
            auto seen = c.params.find(path);
            if (seen != c.params.end() && seen.value() == now) continue;
            c.params.insert(path, now);
            QJsonArray values;
            for (int k = 0; k < now.size(); ++k) values.append(OscNamespace::jsonValue(*node, now[k], k));
            changed[path] = values;
        }
        if (!changed.isEmpty())
            sendText(client, QJsonDocument(QJsonObject{{"t", "params"}, {"v", changed}}).toJson(QJsonDocument::Compact));
    }
}

