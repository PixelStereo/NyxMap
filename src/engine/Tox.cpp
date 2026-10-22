#include "Tox.h"
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
std::mutex factoryMutex;
ToxFactory factory;
}

QString touchEngineRequirements()
{
    return QStringLiteral("TouchEngine requires an installed TouchDesigner or TouchPlayer with an Educational, Commercial or Pro license. .tox playback is available on Windows and macOS.");
}

void ToxSettings::sanitize()
{
    fps = std::isfinite(fps) ? std::clamp(fps, 1.0, 120.0) : 30;
}

QJsonObject ToxSettings::toJson() const
{
    QJsonObject tops;
    for (auto it = inputs.cbegin(); it != inputs.cend(); ++it) tops[it.key()] = QString::number(it.value());
    return {{"output", output}, {"fps", fps}, {"playing", playing}, {"values", values}, {"inputs", tops}};
}

ToxSettings ToxSettings::fromJson(const QJsonObject &j)
{
    ToxSettings s;
    s.output = j.value("output").toString();
    s.fps = j.value("fps").toDouble(30);
    s.playing = j.value("playing").toBool(true);
    s.values = j.value("values").toObject();
    const QJsonObject tops = j.value("inputs").toObject();
    for (auto it = tops.begin(); it != tops.end(); ++it) s.inputs[it.key()] = it.value().toString().toULongLong();
    s.sanitize();
    return s;
}

QString ToxControl::path(int component) const
{
    QString p = QStringLiteral("tox/param/") + QString::fromLatin1(QUrl::toPercentEncoding(id));
    if (count > 1) p += QLatin1Char('/') + (color && component < 4 ? QString(QChar("rgba"[component]))
                                              : component < 4 ? QString(QChar("xyzw"[component]))
                                                              : QString::number(component + 1));
    return p;
}

QVariant ToxControl::value(const ToxSettings &s, int component) const
{
    QJsonValue v = s.values.value(id);
    if (v.isUndefined()) v = initial;
    if (count > 1) {
        const QJsonArray a = v.toArray();
        return component >= 0 && component < a.size() ? a[component].toVariant() : QVariant(0.0);
    }
    return v.toVariant();
}

void ToxControl::setValue(ToxSettings &s, int component, const QVariant &value) const
{
    if (count <= 1) s.values[id] = QJsonValue::fromVariant(value);
    else {
        QJsonArray a = s.values.contains(id) ? s.values.value(id).toArray() : initial.toArray();
        while (a.size() < count) a.append(0.0);
        if (component >= 0 && component < count) a[component] = QJsonValue::fromVariant(value);
        s.values[id] = a;
    }
}

void setToxFactoryForTests(ToxFactory f)
{
    std::lock_guard<std::mutex> lock(factoryMutex);
    factory = std::move(f);
}

std::unique_ptr<ToxPlayer> createNativeToxPlayer(const QString &, const ToxSettings &);

std::unique_ptr<ToxPlayer> createToxPlayer(const QString &path, const ToxSettings &settings)
{
    ToxFactory f;
    {
        std::lock_guard<std::mutex> lock(factoryMutex);
        f = factory;
    }
    return f ? f(path, settings) : createNativeToxPlayer(path, settings);
}
