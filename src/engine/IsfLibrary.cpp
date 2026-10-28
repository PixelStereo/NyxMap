#include "IsfLibrary.h"
#include "Isf.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

namespace {
QString pathKey(const QString &path)
{
    const QFileInfo file(path);
    QString key = file.canonicalFilePath();
    if (key.isEmpty()) key = QDir::cleanPath(file.absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
}

IsfLibrary::IsfLibrary(const QStringList &standardFolders) : m_systemFolders(standardFolders) {}

QString IsfLibrary::bundledFolder()
{
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        app + "/isf",                     // Windows / Linux: next to the executable
        app + "/../Resources/isf",        // macOS: NyxMap.app/Contents/Resources/isf
        app + "/../share/nyxmap/isf",   // Linux, installed
        app + "/../isf",                  // build folder
        app + "/../../isf",
    };
    for (const QString &c : candidates)
        if (QFileInfo(c).isDir()) return QDir(c).absolutePath();
    return {};
}

QStringList IsfLibrary::systemFolders()
{
    QStringList f;
#ifdef Q_OS_MACOS
    f << "/Library/Graphics/ISF" << QDir::homePath() + "/Library/Graphics/ISF";
#elif defined(Q_OS_WIN)
    f << "C:/ProgramData/ISF";
#else
    f << "/usr/share/isf" << QDir::homePath() + "/.local/share/isf";
#endif
    return f;
}

QStringList IsfLibrary::allFolders() const
{
    QStringList all;
    QSet<QString> seen;
    auto add = [&](const QString &folder) {
        if (folder.isEmpty() || !QFileInfo(folder).isDir()) return;
        const QString key = pathKey(folder);
        if (seen.contains(key)) return;
        seen.insert(key);
        all << folder;
    };
    const QString b = bundledFolder();
    add(b);
    for (const QString &s : m_systemFolders) add(s);
    for (const QString &u : m_userFolders) add(u);
    return all;
}

void IsfLibrary::scan()
{
    m_allGenerators.clear();
    m_allFilters.clear();
    m_allTransitions.clear();
    QSet<QString> system;
    for (const QString &folder : m_systemFolders) system.insert(pathKey(folder));
    const QString bundled = bundledFolder();
    QSet<QString> seen;
    for (const QString &folder : allFolders()) {
        const bool isSystem = (bundled.isEmpty() || pathKey(folder) != pathKey(bundled)) && system.contains(pathKey(folder));
        QDirIterator it(folder, {"*.fs", "*.frag"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = QFileInfo(it.next()).absoluteFilePath();
            const QString key = pathKey(path);
            if (seen.contains(key)) continue;
            seen.insert(key);
            IsfInstance::Header h = IsfInstance::readHeader(path);
            if (!h.ok) continue;
            IsfEntry e;
            e.name = h.name;
            e.path = path;
            e.description = h.description;
            e.categories = h.categories;
            e.isFilter = h.isFilter;
            e.isSystem = isSystem;
            (h.isTransition ? m_allTransitions : e.isFilter ? m_allFilters : m_allGenerators).push_back(e);
        }
    }
    auto byName = [](const IsfEntry &a, const IsfEntry &b) { return a.name.localeAwareCompare(b.name) < 0; };
    std::sort(m_allGenerators.begin(), m_allGenerators.end(), byName);
    std::sort(m_allFilters.begin(), m_allFilters.end(), byName);
    std::sort(m_allTransitions.begin(), m_allTransitions.end(), byName);
    updateVisible();
}

void IsfLibrary::setSystemVisibility(SystemVisibility visibility)
{
    m_systemVisibility = visibility;
    updateVisible();
}

void IsfLibrary::updateVisible()
{
    auto visible = [](QVector<IsfEntry> entries, bool showSystem) {
        if (!showSystem) entries.erase(std::remove_if(entries.begin(), entries.end(),
            [](const IsfEntry &entry) { return entry.isSystem; }), entries.end());
        return entries;
    };
    m_generators = visible(m_allGenerators, m_systemVisibility.generators);
    m_filters = visible(m_allFilters, m_systemVisibility.filters);
    m_transitions = visible(m_allTransitions, m_systemVisibility.transitions);
}

QString IsfLibrary::findByFileName(const QString &fileName) const
{
    for (const IsfEntry &e : m_allGenerators) if (QFileInfo(e.path).fileName() == fileName) return e.path;
    for (const IsfEntry &e : m_allFilters) if (QFileInfo(e.path).fileName() == fileName) return e.path;
    for (const IsfEntry &e : m_allTransitions) if (QFileInfo(e.path).fileName() == fileName) return e.path;
    return {};
}
