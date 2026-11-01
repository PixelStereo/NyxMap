#pragma once
#include <QWidget>
#include <QJsonObject>
#include <array>
class Engine;
class MappingView;
class QGridLayout;
class QStackedWidget;
class QToolButton;
class PreviewPicture;
class Scene3DView;
class QUndoStack;

// Four independent preview sources. Composition in the first cell retains mapping tools.
class PreviewWorkspace : public QWidget
{
    Q_OBJECT
public:
    PreviewWorkspace(Engine *engine, MappingView *mapping, QWidget *parent = nullptr);
    ~PreviewWorkspace() override;
    void refreshFrames();
    // What it shows: how many views, each one's source, zoom, pan and fixtures, the mapping view's (a project, a preset)
    QJsonObject state() const;
    void setState(const QJsonObject &o);
    void setUndoStack(QUndoStack *undo);
    void setCurrentLayer(quint64 id); // the 3D views show its handles when it is one of their scene's objects

signals:
    void layerPicked(int index); // clicked in a 3D view

private:
    void setCount(int count);
    void selectSource(int slot, quint64 id, const QString &label);
    Engine *m_engine;
    MappingView *m_mapping;
    QGridLayout *m_grid;
    struct Cell {
        QWidget *widget = nullptr;
        QStackedWidget *pages = nullptr;
        QToolButton *source = nullptr;
        PreviewPicture *picture = nullptr;
        Scene3DView *scene3d = nullptr; // a 3D scene chosen: its 3D view
        quint64 id = 0;
    };
    std::array<Cell, 4> m_cells;
    int m_count = 1;
    class QComboBox *m_countBox = nullptr;
};
