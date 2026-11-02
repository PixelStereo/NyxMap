#pragma once
#include <QHash>
#include <QJsonObject>
#include <QWidget>

class Engine;
class QComboBox;
class QGridLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QVBoxLayout;

// The Show Control tab: a desk to play the show from, the same one as the web page at /show (ShowPage.h). The selected sequence,
// its previous, current and next cues with their times, GO, GO BACK and the stops, the cue stack; the snapshots, the presets and
// the timelines on their pages. It reads and commands the show through ShowDesk.h, and only looks while it is shown.
class ShowControlPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ShowControlPanel(Engine *engine, QWidget *parent = nullptr);
    void refresh(); // the show as it is now

signals:
    void layersRequested();

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    struct Card {
        class CueFill *fill = nullptr;
        QLabel *continuation = nullptr, *label = nullptr, *text = nullptr, *description = nullptr, *phase = nullptr, *times = nullptr;
    };
    struct TimelineRow {
        QLabel *clock = nullptr;
        QProgressBar *progress = nullptr;
    };
    QWidget *makeCard(const QString &role, const QString &heading, const QString &edge, const QString &tint);
    void command(const QJsonObject &o);
    void rebuildLists();
    void rebuildCues();
    void markCues();
    void renderCard(const QString &role, const QJsonObject &values);

    Engine *m_e;
    QTimer *m_timer = nullptr;
    int m_tick = 0;
    QJsonObject m_catalog;
    quint64 m_selected = 0;
    QHash<QString, Card> m_cards;
    QHash<int, QPushButton *> m_cueButtons; // by the cue's number
    int m_currentCue = -1, m_nextCue = -1;
    QHash<QString, TimelineRow> m_timelines;
    QComboBox *m_sequence = nullptr;
    QLabel *m_running = nullptr, *m_elapsed = nullptr, *m_remaining = nullptr;
    QProgressBar *m_phase = nullptr;
    QPushButton *m_go = nullptr, *m_back = nullptr, *m_stop = nullptr, *m_stopAll = nullptr;
    QVBoxLayout *m_cues = nullptr, *m_timelineList = nullptr;
    QGridLayout *m_snapshots = nullptr, *m_presets = nullptr;
    qint64 m_stopAllArmed = 0;
    bool m_filling = false;
};
