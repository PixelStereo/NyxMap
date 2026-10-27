#pragma once
// Reading zip archives (a GDTF file is one): the files stored or deflated in them, read whole in memory. No zip64, no
// encryption. Self-contained: no zlib (not available on every platform Pinol builds on).

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace zip {
QStringList names(const QByteArray &archive, QString *err = nullptr);
// The file of that name (case ignored); empty with *err set when it is missing or unreadable
QByteArray read(const QByteArray &archive, const QString &name, QString *err = nullptr);
// Raw DEFLATE data (RFC 1951) inflated; ok false when it is malformed
QByteArray inflate(const QByteArray &deflated, bool *ok = nullptr);
} // namespace zip
