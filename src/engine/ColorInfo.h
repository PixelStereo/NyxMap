#pragma once
#include <QColorSpace>
#include <QMetaEnum>
#include <QString>
#include <QStringList>
#include <algorithm>

// Describe tags, never infer a profile from sample depth or file extension.
inline QString imageColorSpaceName(const QColorSpace &space)
{
    if (!space.isValid()) return QStringLiteral("Unknown (untagged)");
    const QString description = space.description().simplified();
    if (!description.isEmpty()) return description;
    const auto primaries = QMetaEnum::fromType<QColorSpace::Primaries>();
    const char *name = primaries.valueToKey(int(space.primaries()));
    const char *curve = QMetaEnum::fromType<QColorSpace::TransferFunction>().valueToKey(int(space.transferFunction()));
    return QStringLiteral("%1 / %2").arg(name ? QString::fromLatin1(name) : QStringLiteral("Custom primaries"),
                                        curve ? QString::fromLatin1(curve) : QStringLiteral("Custom transfer"));
}
inline QString imageColorDescription(const QColorSpace &space)
{
    if (!space.isValid()) return QStringLiteral("Color space: Unknown (untagged)");
    auto name = [](auto value) {
        const char *key = QMetaEnum::fromType<decltype(value)>().valueToKey(int(value));
        return key ? QString::fromLatin1(key) : QStringLiteral("Custom");
    };
    QString transfer = name(space.transferFunction());
    if (space.transferFunction() == QColorSpace::TransferFunction::Gamma)
        transfer += QStringLiteral(" %1").arg(space.gamma(), 0, 'g', 4);
    return QStringLiteral("Color space: %1 · primaries: %2 · transfer: %3")
        .arg(imageColorSpaceName(space), name(space.primaries()), transfer);
}

// Order matches the output shader's transfer/gamut modes. "same" is resolved
// by the caller; existing projects default to unchanged output with an explicit
// sRGB assumption for their rendered RGB if a conversion is later requested.
inline QStringList outputColorSpaceKeys() { return {"srgb", "linear709", "displayp3", "rec709-g24"}; }
inline QStringList outputColorSpaceLabels() {
    return {"sRGB", "Linear Rec.709", "Display P3", "Rec.709 / Gamma 2.4"};
}
inline int outputColorSpaceIndex(const QString &key) {
    if (key == QLatin1String("linear709")) return 1;
    if (key == QLatin1String("displayp3")) return 2;
    if (key == QLatin1String("rec709-g24")) return 3;
    return 0;
}
