#pragma once
// ISF shader library: folder bundled with the application, standard system folders
// (/Library/Graphics/ISF on macOS) and folders added by the user.

#include <QString>
#include <QStringList>
#include <QVector>

struct IsfEntry {
    QString name, path, description;
    QStringList categories;
    bool isFilter = false;
    bool isSystem = false;
};

class IsfLibrary
{
public:
    struct SystemVisibility {
        bool generators = true, filters = true, transitions = true;
    };

    explicit IsfLibrary(const QStringList &standardFolders = systemFolders());
    void setUserFolders(const QStringList &f) { m_userFolders = f; }
    QStringList userFolders() const { return m_userFolders; }
    QStringList allFolders() const;
    SystemVisibility systemVisibility() const { return m_systemVisibility; }
    void setSystemVisibility(SystemVisibility visibility); // presentation only; no disk scan

    void scan();
    const QVector<IsfEntry> &generators() const { return m_generators; }
    const QVector<IsfEntry> &filters() const { return m_filters; }
    const QVector<IsfEntry> &transitions() const { return m_transitions; } // startImage, endImage, progress

    // Includes hidden shaders when repairing a moved project's paths.
    QString findByFileName(const QString &fileName) const;

    static QString bundledFolder();
    static QStringList systemFolders();

private:
    void updateVisible();
    QStringList m_systemFolders;
    QStringList m_userFolders;
    SystemVisibility m_systemVisibility;
    QVector<IsfEntry> m_allGenerators, m_allFilters, m_allTransitions;
    QVector<IsfEntry> m_generators, m_filters, m_transitions;
};
