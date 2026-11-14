#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

struct InteractionOutput {
    QString protocol;
    QString destination;
    QString address;
    QJsonArray arguments;
    int channel = 1;
    int number = 0;

    QString path() const
    {
        const QJsonObject settings{{"destination", destination}, {"address", address}, {"arguments", arguments},
                                   {"channel", channel}, {"number", number}};
        return QStringLiteral("output/") + protocol + QLatin1Char('/') +
               QString::fromLatin1(QJsonDocument(settings).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding));
    }

    static InteractionOutput fromPath(const QString &path)
    {
        if (!path.startsWith(QLatin1String("output/"))) return {};
        const auto protocol = path.section(QLatin1Char('/'), 1, 1);
        if (protocol != "osc" && protocol != "dmx" && protocol != "midi") return {};
        const auto settings = QJsonDocument::fromJson(
            QByteArray::fromBase64(path.section(QLatin1Char('/'), 2).toLatin1(), QByteArray::Base64UrlEncoding)).object();
        const auto destination = settings.value("destination").toString();
        if (destination.isEmpty()) return {};
        return {protocol, destination, settings.value("address").toString(), settings.value("arguments").toArray(),
                settings.value("channel").toInt(1), settings.value("number").toInt()};
    }
};
