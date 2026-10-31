#pragma once
#include "Anim.h"
#include <QUrl>

// Encode the complete driven path, including '/' and '%': nested drivers remain unambiguous.
inline QString animationDriverPrefix(const QString &target, int axis = -1)
{
    return QStringLiteral("animation/%1/").arg(QString::fromLatin1(QUrl::toPercentEncoding(target))) +
           (axis < 0 ? QString() : QStringLiteral("axes/%1/").arg(axis));
}
inline AnimTrack *animationDriverTrack(std::vector<Animation> &animations, const QString &target, int axis)
{
    for (auto &a : animations)
        if (!a.tracks.empty() && a.tracks.front().param == target) {
            auto &t = a.tracks.front();
            return axis < 0 ? &t : (axis < int(t.axes.size()) ? &t.axes[size_t(axis)] : nullptr);
        }
    return nullptr;
}
