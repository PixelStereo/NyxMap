#pragma once
// The name of a copy: the next number that is free, never "copy". "Layer" gives "Layer 2", "Layer 2" gives "Layer 3"
// (or the next one no other has), so a list of duplicates reads as a series.
#include <QRegularExpression>
#include <QString>
#include <QStringList>

inline QString numberedName(const QString &name, const QStringList &taken)
{
    static const QRegularExpression tail(QStringLiteral("^(.*\\S)\\s+(\\d{1,6})$"));
    QString base = name.trimmed();
    int n = 1;
    if (const QRegularExpressionMatch m = tail.match(base); m.hasMatch()) {
        base = m.captured(1);
        n = m.captured(2).toInt();
    }
    for (int k = n + 1;; ++k) {
        const QString candidate = base.isEmpty() ? QString::number(k) : base + QLatin1Char(' ') + QString::number(k);
        if (!taken.contains(candidate)) return candidate;
    }
}
