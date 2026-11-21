#pragma once
// The editor of an animation (Anim.h), shared by the timelines of the show (TimelineEditor, in the Timelines window)
// and by the animations of a layer's numbers (ParamAnimEditor, in the Anim tab of the inspector and in their own
// floating window): the transport, the duration, the speed and the loop; a ruler and the time zoom; and one row per
// track — its settings (keys or a wave) and its lane, where the curve is drawn and edited.
//
// AnimEditor holds what is common. A subclass says where the animation lives (fetch, store, control) and what a
// track's settings show besides the common ones (makeRow: a TrackRow subclass).

#include "Engine.h"
#include <pxs/CurveLane.h>

#include <QWidget>
#include <functional>
#include <limits>
#include <map>
#include <vector>

class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollBar;
class QSlider;
class QUndoStack;
class QVBoxLayout;
class NumberBox;
class IntBox;
class AnimEditor;

class QMenu;

// A sampled projection of both axis curves over the current pass. Seeking here
// uses the same transport as the time ruler; it never changes the keys.
class XyPathPreview : public QWidget
{
    Q_OBJECT
public:
    explicit XyPathPreview(QWidget *parent = nullptr);
    void setTrack(const AnimTrack &track, double duration,
                  const std::array<std::pair<double, double>, 2> &ranges);
    void setPlayhead(double position, double played, bool backwards, bool running);
    void setLive(const std::array<double, 2> &captured, const std::array<double, 2> &live);
    std::array<double, 2> valueAt(double position) const;
signals:
    void seekRequested(double position);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    void updateCaptures();
    void samplePath();
    void seekAt(QPointF position);
    QRectF plotRect() const;
    QPointF mapPoint(const std::array<double, 2> &value) const;
    AnimTrack m_track;
    double m_duration = 4, m_position = 0, m_origin = 0;
    bool m_backwards = false, m_running = false, m_dirty = true, m_dragging = false;
    std::array<double, 2> m_live{0, 0}, m_captured{NAN, NAN};
    std::array<std::pair<double, double>, 2> m_ranges{{{0, 1}, {0, 1}}}, m_bounds = m_ranges;
    std::vector<std::array<double, 2>> m_samples;
};

// One RGB track: the strip uses exactly the same interpolation as playback.
class ColorLane : public QWidget
{
    Q_OBJECT
public:
    explicit ColorLane(QWidget *parent = nullptr);
    void setTrack(const AnimTrack &track, double duration);
    void setView(double t0, double t1);
    void setPlayhead(double position, bool shown);
    void setLive(const std::array<double, 3> &rgb);
    void setCurrentColor(std::function<std::array<double, 3>()> f) { m_current = std::move(f); }
signals:
    void edited(const AnimTrack &track, int gesture);
    void zoomTime(double factor, double anchor);
    void panTime(double seconds);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    double xOf(double time) const;
    double tOf(double x) const;
    int keyAt(QPointF pos) const;
    AnimTrack shown() const;
    void editColor(int key, double time = 0);
    void removeKey(int key);
    AnimTrack m_track;
    std::array<double, 3> m_live = AnimTrack().capturedRgb;
    double m_duration = 4, m_t0 = 0, m_t1 = 4, m_playhead = 0;
    bool m_shown = false, m_panning = false, m_add = false;
    int m_selected = -1, m_drag = -1, m_gesture = 0;
    QPointF m_press;
    double m_pressTime = 0;
    std::function<std::array<double, 3>()> m_current;
};

// One event track: a flag at each key's time, where its action fires. A click adds a flag, a flag is dragged in time;
// double-click or Delete removes it, right-click: its time, delete.
class EventLane : public QWidget
{
    Q_OBJECT
public:
    explicit EventLane(QWidget *parent = nullptr);
    // `target`: what its flags drive — an action (fired), or a switch, a choice, a number (each flag sets its value)
    void setTrack(const AnimTrack &track, double duration, const ParamInfo &target = ParamInfo{ {}, {}, ParamInfo::Type::Trigger });
    void setView(double t0, double t1);
    void setPlayhead(double position, bool shown);
    QString valueText(int key) const; // what a flag sets ("On", "add", "12.5"); empty: an action
signals:
    void edited(const AnimTrack &track, int gesture);
    void zoomTime(double factor, double anchor);
    void panTime(double seconds);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    double xOf(double time) const;
    double tOf(double x) const;
    int keyAt(QPointF pos) const;
    void addKey(double time);
    void removeKey(int key);
    void setKeyValue(int key, double v);
    bool valued() const { return m_target.type != ParamInfo::Type::Trigger; }
    AnimTrack m_track;
    ParamInfo m_target{{}, {}, ParamInfo::Type::Trigger};
    double m_duration = 4, m_t0 = 0, m_t1 = 4, m_playhead = 0;
    bool m_shown = false, m_panning = false, m_add = false;
    int m_selected = -1, m_drag = -1, m_gesture = 0;
    QPointF m_press;
    double m_pressTime = 0;
};

// The name of a wave, of a key's curve
QString animWaveName(AnimWave w);
using pxs::curveview::animCurveName;
// Numbers by category in a menu: "Spatial › Rotation" is "Rotation" in the sub-menu "Spatial". `item` adds the entry of
// each number (into: its menu; text: its name there).
void addParamMenu(QMenu *menu, const std::vector<Engine::AnimParam> &params,
                  const std::function<void(QMenu *into, const Engine::AnimParam &p, const QString &text)> &item,
                  bool nested = false);
// Keep a whole color beside its component entries in the same Source / Color / FX category.
std::vector<Engine::AnimParam> animationMenuParams(Engine *engine, quint64 layer);

// The curve of a track (click, drag, draw, bend) is pxs's
using pxs::curveview::CurveLane;

// The seconds over the lanes; a click or a drag seeks (the values follow at once)
class TimeRuler : public QWidget
{
    Q_OBJECT
public:
    explicit TimeRuler(QWidget *parent = nullptr);
    void setDuration(double d);
    void setView(double t0, double t1);
    void setPlayhead(double position, bool shown);
    QSize sizeHint() const override { return QSize(500, 22); }

signals:
    void seekRequested(double t);
    void zoomTime(double factor, double anchor);
    void panTime(double seconds);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    double tOf(double x) const;
    double m_duration = 4, m_t0 = 0, m_t1 = 4, m_playhead = 0;
    bool m_shown = false;
};

// A track: its settings (keys or a wave, and the wave's period, phase, center, amplitude) and its lane. A subclass
// adds its own settings above them (buildHeader) and fills them (fill).
class TrackRow : public QWidget
{
public:
    TrackRow(AnimEditor *editor, int index);
    void build(); // the widgets: called once, right after construction (buildHeader is virtual)
    void setPlayhead(double pos, double played, bool backwards, bool shown);
    void setLive(double captured, const std::array<double, 3> &capturedRgb, const std::array<double, 2> &capturedXy, bool running);
    void applyView();                            // the editor's zoom
    std::pair<double, double> fullRange() const;
    virtual void fill(); // the widgets from the track

protected:
    virtual void buildHeader(QGridLayout *) {} // above the common settings
    AnimTrack &track();
    const AnimTrack &track() const;
    AnimTrack &editedTrack();
    const AnimTrack &editedTrack() const;
    void updateLane();
    void changed(bool lane = true, const QString &mergeKey = {}); // the track edited: to the engine
    std::pair<double, double> range() const;
    double currentValue() const;
    AnimEditor *m_ed;
    Engine *m_e;
    int m_i;
    bool m_filling = false;

private:
    QComboBox *m_kind = nullptr, *m_wave = nullptr;
    QComboBox *m_axis = nullptr;
    class QCheckBox *m_axisOn = nullptr;
    int m_axisIndex = 0;
    QWidget *m_osc = nullptr;
    NumberBox *m_period = nullptr, *m_center = nullptr, *m_amp = nullptr, *m_phase = nullptr;
    QPushButton *m_reseed = nullptr;
    QPushButton *m_startCurrent = nullptr;
    QPushButton *m_showPath = nullptr;
    XyPathPreview *m_pointPreview = nullptr;
    void updateStartMode();
    void rebuildFilters();
    void showFilterSignal(); // what comes in and what each filter makes of it, now
    QWidget *m_filters = nullptr;
    std::vector<std::pair<class QSlider *, QLabel *>> m_filterMeters; // the input, then each filter's output
    class QTimer *m_filterTimer = nullptr;
    QVBoxLayout *m_outer = nullptr;
    pxs::curveview::CurveLane *m_lane = nullptr;
    ColorLane *m_colorLane = nullptr;
    EventLane *m_eventLane = nullptr;
};

class AnimEditor : public QWidget
{
    Q_OBJECT
public:
    // Side: a track's settings left of its lane (a wide window); Stacked: above it (the narrow inspector)
    enum class Layout { Side, Stacked };
    AnimEditor(Engine *engine, QUndoStack *undo, Layout layout, bool scrollTracks, QWidget *parent = nullptr);
    // m_edit from the engine (unless it is the same), the rows rebuilt. Call it once the subclass is built.
    void reload();
    void poll(); // where it is: the playhead, the time, the buttons
    bool isCommitting() const { return m_committing; } // its own edit is going to the engine
    Engine *engine() const { return m_engine; }
    Layout layout() const { return m_layout; }
    int headerWidth() const { return m_layout == Layout::Side ? kHeader : 0; }
    // The numbers a track can drive on its layer, and the range of its number
    const std::vector<Engine::AnimParam> &paramsOf(quint64 layer);
    std::pair<double, double> rangeOf(const AnimTrack &t);
    void setLaneHeight(int px) { m_laneHeight = px; }
    int laneHeight() const { return m_laneHeight; }
    double valueDuration() const { return m_edit.duration; }

signals:
    void edited(); // changed: the project is modified

protected:
    friend class TrackRow;
    static constexpr int kHeader = 300; // width of a track's settings, left of its lane (Side)
    // Where the animation lives
    virtual bool fetchPlayback(Engine::AnimationPlayback *a) const = 0;
    virtual bool fetch(Animation *a) const = 0; // as the engine has it now (false: none)
    // m_edit replacing `before`, as an undo step. Edits with the same non-empty key that follow one another (a drag,
    // the arrows of a number) form one step.
    virtual void store(const Animation &before, const Animation &after, const QString &text, const QString &mergeKey) = 0;
    virtual void control(AnimAction action, double time = 0) = 0;
    virtual TrackRow *makeRow(int index) { return new TrackRow(this, index); }
    virtual void bindDriverWidget(QWidget *, int, const QString &) {}
    virtual void syncDrivenValues() {}
    virtual void loaded() {} // after a reload (the subclass's own widgets)
    void commit(const QString &text, const QString &mergeKey = {});
    void rebuildRows();
    void forgetView(); // another animation: shown whole on the next reload
    void forgetParams() { m_params.clear(); } // the layers changed: their numbers are read again
    void setEditorEnabled(bool on);
    QVBoxLayout *mainLayout() const { return m_main; }
    struct LaneView {
        int axis = 0;
        bool xyPreview = false;
        double zoom = 1;                                          // values: the whole range / zoom
        double center = std::numeric_limits<double>::quiet_NaN(); // nan: the middle of the range
    };
    LaneView &laneView(int track);
    void viewOfLane(int track, double lo, double hi, double *vlo, double *vhi);
    void applyView(); // the time window and the lanes' value windows to the ruler, the scroll bar, the lanes
    void zoomTime(double factor, double anchor);
    void panTime(double seconds);
    void setTimeZoom(double factor); // around the middle of what is shown
    void fitView();   // the time from the first key to the last, each lane from its lowest value to its highest
    void resetView();

    Engine *m_engine;
    QUndoStack *m_undo;
    Layout m_layout;
    Animation m_edit;
    bool m_has = false; // there is an animation to edit
    bool m_filling = false, m_committing = false;
    double m_t0 = 0, m_t1 = 4; // seconds shown
    std::vector<LaneView> m_laneViews;
    std::vector<TrackRow *> m_rows;
    QPushButton *m_play, *m_pause, *m_stop, *m_rewind, *m_fit, *m_all;
    QSlider *m_position, *m_zoomX = nullptr, *m_zoomY = nullptr;
    QScrollBar *m_scrollX;
    QLabel *m_time, *m_totalTime, *m_playbackState;
    NumberBox *m_duration, *m_speed;
    QComboBox *m_loop;
    IntBox *m_repeat;
    TimeRuler *m_ruler;
    QWidget *m_body;          // all but what a subclass adds around it
    QVBoxLayout *m_tracks;    // the rows (then a stretch when they scroll)
    bool m_scrollTracks;

private:
    QVBoxLayout *m_main;
    std::map<quint64, std::vector<Engine::AnimParam>> m_params; // by layer, until the next reload
    bool m_freshView = true;
    int m_laneHeight = 96;
};

// Is `a` the same animation as `b`, as edited (its transport where it is left out)?
bool sameAnimation(const Animation &a, const Animation &b);
