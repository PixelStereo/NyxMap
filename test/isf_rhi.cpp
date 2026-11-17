// QRhi prototype: can every ISF shader be baked for the backends QRhi drives (Metal, Direct3D, Vulkan)?
// Each .fs (and its .vs) goes through IsfInstance::rhiSources (Vulkan-flavoured GLSL 440), then QShaderBaker:
// SPIR-V 1.0 (Vulkan), MSL 2.0 (Metal) and HLSL 5.0 (Direct3D 11/12). No GPU needed.
//
// nyxmap_isf_rhi_test [folder or .fs]...  (default: the shaders that ship with NyxMap, and the tests')
// --verbose: the first error of each shader that fails. Fails when a shader that ships with NyxMap does not bake.
#include "Isf.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QFileInfo>
#include <rhi/qshaderbaker.h>

#include <cstdio>
#include <map>

static QString firstLine(const QString &log)
{
    for (const QString &line : log.split(QLatin1Char('\n')))
        if (line.contains(QLatin1String("ERROR")) || line.contains(QLatin1String("error"))) return line.trimmed();
    return log.section(QLatin1Char('\n'), 0, 0).trimmed();
}

static bool bake(const QString &source, QShader::Stage stage, QString *error)
{
    QShaderBaker baker;
    baker.setSourceString(source.toUtf8(), stage);
    baker.setGeneratedShaders({{QShader::SpirvShader, QShaderVersion(100)},
                               {QShader::MslShader, QShaderVersion(20)},
                               {QShader::HlslShader, QShaderVersion(50)}});
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    const QShader shader = baker.bake();
    if (shader.isValid() && shader.availableShaders().size() == 3) return true;
    *error = firstLine(baker.errorMessage());
    if (error->isEmpty()) *error = QStringLiteral("not every backend was generated");
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    const bool verbose = args.removeAll(QStringLiteral("--verbose")) > 0;
    QString dump; // --dump DIR: the generated GLSL 440 of each shader that fails, to read
    if (const qsizetype i = args.indexOf(QStringLiteral("--dump")); i >= 0 && i + 1 < args.size()) {
        dump = args.takeAt(i + 1);
        args.removeAt(i);
        QDir().mkpath(dump);
    }
    const bool shipped = args.isEmpty();
    if (shipped) args = {QStringLiteral(SOURCE_DIR "/isf"), QStringLiteral(SOURCE_DIR "/test/isf")};

    QStringList files;
    for (const QString &a : args) {
        if (QFileInfo(a).isFile()) files << a;
        else
            for (QDirIterator it(a, {QStringLiteral("*.fs")}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
                files << it.next();
    }
    files.sort();

    int ok = 0, unreadable = 0;
    std::map<QString, int> causes; // first error, by kind
    QElapsedTimer timer;
    timer.start();
    for (const QString &f : files) {
        const IsfInstance::RhiSources src = IsfInstance::rhiSources(f);
        QString error;
        if (!src.error.isEmpty()) {
            ++unreadable; // not a shader this host reads at all
            if (verbose) std::printf("  unreadable %s: %s\n", qPrintable(f), qPrintable(firstLine(src.error)));
            continue;
        }
        const bool v = bake(src.vertex, QShader::VertexStage, &error);
        const bool fr = v && bake(src.fragment, QShader::FragmentStage, &error);
        if (v && fr) {
            ++ok;
            continue;
        }
        QString kind = error;
        kind.remove(QRegularExpression(QStringLiteral("^.*?:\\s*\\d+:\\s*"))); // file:line:
        kind.remove(QRegularExpression(QStringLiteral("'[^']*'")));
        ++causes[(v ? QStringLiteral("fragment: ") : QStringLiteral("vertex: ")) + kind.left(70)];
        if (!dump.isEmpty()) {
            QFile out(dump + QLatin1Char('/') + QFileInfo(f).completeBaseName() + (v ? QStringLiteral(".frag") : QStringLiteral(".vert")));
            if (out.open(QIODevice::WriteOnly)) out.write((v ? src.fragment : src.vertex).toUtf8());
        }
        if (verbose) std::printf("  FAIL %s (%s): %s\n", qPrintable(QFileInfo(f).fileName()), v ? "fragment" : "vertex", qPrintable(error));
    }
    const int read = int(files.size()) - unreadable;
    std::printf("ISF → QRhi: %d of %d shaders bake for Metal, Direct3D and Vulkan (%d not read by NyxMap either), %.1f s\n", ok,
                read, unreadable, timer.elapsed() / 1000.0);
    for (const auto &[cause, count] : causes) std::printf("  %3d  %s\n", count, qPrintable(cause));
    return shipped && ok < read ? 1 : 0;
}
