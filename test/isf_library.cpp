#include "IsfLibrary.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL isf_library: %s\n", why); }
    };
    QTemporaryDir files;
    if (!files.isValid()) return 1;
    const QString system = files.filePath("system"), user = files.filePath("user");
    check(files.isValid() && QDir().mkpath(system) && QDir().mkpath(user), "create isolated shader folders");
    const QByteArray inputs[] = {"[]", "[{\"NAME\":\"inputImage\",\"TYPE\":\"image\"}]",
        "[{\"NAME\":\"startImage\",\"TYPE\":\"image\"},{\"NAME\":\"endImage\",\"TYPE\":\"image\"},"
        "{\"NAME\":\"progress\",\"TYPE\":\"float\",\"DEFAULT\":0}]"};
    auto write = [&](const QString &path, int kind) {
        QFile file(path);
        const QByteArray shader = "/*{\"ISFVSN\":\"2\",\"INPUTS\":" + inputs[kind] + "}*/\nvoid main(){gl_FragColor=vec4(1.0);}";
        return file.open(QIODevice::WriteOnly) && file.write(shader) == shader.size();
    };
    QStringList systemPaths, userPaths;
    for (int kind = 0; kind < 3; ++kind) {
        systemPaths << system + QStringLiteral("/System%1.fs").arg(kind);
        userPaths << user + QStringLiteral("/User%1.fs").arg(kind);
        check(write(systemPaths.back(), kind) && write(userPaths.back(), kind), "create generator, FX and transition headers");
    }
    IsfLibrary bundled(QStringList{});
    bundled.scan();
    IsfLibrary library(QStringList{system});
    QStringList userFolders{user};
#ifdef Q_OS_UNIX
    const QString alias = files.filePath("system_alias");
    check(QFile::link(system, alias), "create a symbolic alias of the system library");
    userFolders << alias;
#endif
    library.setUserFolders(userFolders);
    library.scan();
    auto entries = [](const IsfLibrary &lib, int kind) -> const QVector<IsfEntry> & {
        return kind == 0 ? lib.generators() : kind == 1 ? lib.filters() : lib.transitions();
    };
    auto contains = [](const QVector<IsfEntry> &items, const QString &path) {
        return std::any_of(items.begin(), items.end(), [&](const IsfEntry &item) {
            return QFileInfo(item.path).canonicalFilePath() == QFileInfo(path).canonicalFilePath();
        });
    };
    for (int mask = 0; mask < 8; ++mask) {
        library.setSystemVisibility({bool(mask & 1), bool(mask & 2), bool(mask & 4)});
        for (int kind = 0; kind < 3; ++kind) {
            const bool show = mask & (1 << kind);
            check(contains(entries(library, kind), systemPaths[kind]) == show, "system categories toggle independently");
            check(contains(entries(library, kind), userPaths[kind]), "manually added shaders stay visible");
            check(entries(library, kind).size() == entries(bundled, kind).size() + 1 + int(show),
                  "bundled shaders remain visible and aliases are deduplicated");
            check(library.findByFileName(QFileInfo(systemPaths[kind]).fileName()) == systemPaths[kind],
                  "hidden shaders still resolve for project relinking");
        }
    }
    library.setSystemVisibility({false, false, false});
    library.scan();
    check(!contains(library.generators(), systemPaths[0]) &&
              library.findByFileName("System0.fs") == systemPaths[0], "rescan retains visibility and the full repair index");
    check(QFile::remove(systemPaths[0]), "remove a system fixture");
    library.scan();
    check(library.findByFileName("System0.fs").isEmpty(), "rescan drops removed shaders from the repair index");
    return failures ? 1 : 0;
}
