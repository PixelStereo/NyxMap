#pragma once
#include "Engine.h"
#include <QAbstractScrollArea>
#include <QTimer>

// One painted viewport for any number of timelines; no widgets or keyframe copies per row.
class TimelineActivity : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit TimelineActivity(Engine *engine, QWidget *parent = nullptr);
    void poll();
    void setOnlyTimeline(quint64 id) { m_only = id; poll(); }
    void setSummary(bool on) { m_summary = on; viewport()->update(); }
    void setShowStopped(bool show);
    void setCompact(bool on); // every timeline, on one line: its name and its run
    const std::vector<Engine::AnimationPlayback> &rows() const { return m_rows; }
    QString describe() const;
signals:
    void editRequested(quint64 timeline);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    int rowAt(const QPoint &point) const;
    void select(int row);
    void updateRange();
    Engine *m_engine;
    QTimer m_timer;
    std::vector<Engine::AnimationPlayback> m_rows;
    quint64 m_selected = 0, m_only = 0;
    bool m_summary = false;
    bool m_showStopped = false, m_compact = false;
    int m_rowHeight = 34;
    int m_playing = 0, m_paused = 0, m_total = 0;
};
