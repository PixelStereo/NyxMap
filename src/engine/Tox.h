#pragma once
// TouchDesigner components: an asynchronous TouchEngine instance per source.
// The SDK stays behind this interface; projects and tests also work without it.
#include "LiveInput.h"
#include "Parameter.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <functional>

struct ToxSettings {
    QString output; // stable TOP link identifier; empty selects the first TOP
    double fps = 30;
    bool playing = true;
    QJsonObject values; // link identifiers -> scalar or vector values, including while loading
    QMap<QString, quint64> inputs; // TOP input identifier -> NyxMap layer ID (0: disconnected)
    void sanitize();
    QJsonObject toJson() const;
    static ToxSettings fromJson(const QJsonObject &json);
};

struct ToxControl {
    QString id, label;
    ParamInfo::Type type = ParamInfo::Type::Float;
    int count = 1;
    bool color = false;
    QJsonValue initial;
    std::vector<double> minimum, maximum;
    QStringList choices;
    QString path(int component = 0) const;
    QVariant value(const ToxSettings &settings, int component = 0) const;
    void setValue(ToxSettings &settings, int component, const QVariant &value) const;
};

class ToxPlayer : public LiveInput
{
public:
    virtual void configure(const ToxSettings &settings) = 0; // mailbox only, any thread
    virtual void advance(double seconds) {} // render thread; elapsed show time, already paused/scaled by Engine
    virtual void reload() = 0;
    virtual void pulse(const QString &identifier) = 0;
    virtual std::vector<ToxControl> controls() const = 0;
    virtual std::vector<std::pair<QString, QString>> outputs() const = 0; // identifier, label
    virtual std::vector<std::pair<QString, QString>> inputs() const = 0;
    // Render thread, before update(): borrowed until update returns. The backend
    // copies to an owned GPU buffer before handing it to an asynchronous cook.
    virtual void setInputTexture(const QString &identifier, GLuint texture, int width, int height) = 0;
    virtual quint64 revision() const = 0; // schema changed: rebuild parameter declarations and the inspector
    virtual QString error() const = 0; // fatal runtime error; loading/paused are status, not errors
};

using ToxFactory = std::function<std::unique_ptr<ToxPlayer>(const QString &, const ToxSettings &)>;
void setToxFactoryForTests(ToxFactory factory);
std::unique_ptr<ToxPlayer> createToxPlayer(const QString &path, const ToxSettings &settings);
bool touchEngineCompiledIn();
QString touchEngineRequirements();
