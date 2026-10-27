#pragma once
// GDTF (General Device Type Format, DIN SPEC 15800) fixtures as pixel mapping types (Fixtures.h): one type per DMX
// mode of the file. A .gdtf file is a zip holding description.xml (and 3D models, wheel pictures: not read).
//
// What is kept is what pixel mapping drives: the channels of intensity and colour (Dimmer, ColorAdd_*, ColorRGB_*,
// ColorSub_C/M/Y), each with its pixel; every other channel (pan, tilt, gobo, strobe, ...) sends its default value.
// Pixels: the geometry references of the mode's geometry (a template channel repeated for each, at its break's
// offset), laid out in a grid by their positions; without references, each geometry carrying colour channels is a
// pixel (a single one: the whole fixture is one pixel). Only DMX break 1 is patched.

#include "Fixtures.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <vector>

namespace gdtf {
struct Import {
    std::vector<FixtureType> types; // one per DMX mode
    QStringList warnings;
    QString error; // nothing could be read
};
Import readFile(const QString &path);
Import readArchive(const QByteArray &archive, const QString &fileName = {});
Import readDescription(const QByteArray &xml, const QString &fileName = {}); // description.xml alone
} // namespace gdtf
