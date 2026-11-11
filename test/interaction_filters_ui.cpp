// An interaction's filters in its settings, as a layer's effects: added from "+ Filter", each on or off, moved,
// removed, a Math filter's operation chosen, a Curve's editor shown — each one undo step. No Invert any more.
#include "Engine.h"
#include "AnimEditor.h"
#include "InteractionPanel.h"
#include "TimelinePanel.h"
#include "TimelineActivity.h"
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include "PresetEditor.h"
#include "Widgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QInputDialog>
#include <QTimer>
#include <QMenu>
#include <QPointer>
#include <QMouseEvent>
#include <QProgressBar>
#include <QTreeWidget>
#include <QLabel>
#include <QToolButton>
#include <QUndoStack>
#include <cstdio>

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    theme::applyToApplication(app);
    Engine engine;
    QUndoStack undo;
    int failures = 0;
    auto check = [&](bool ok, const char *why) { if (!ok) { ++failures; std::printf("FAIL interaction_filters_ui: %s\n", why); } };
    PresetEditor presets(&engine, &undo); // as in the application: it watches the values shown
    InteractionPanel panel(&engine, &undo);
    panel.resize(720, 1400);
    panel.show();
    panel.learnFor(0, "opacity", InteractionSource::Osc);
    auto settle = [&] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        app.processEvents();
    };
    settle();
    auto filters = [&] { return engine.interactions().back().filters; };
    auto add = [&](const char *kind) {
        auto *button = panel.findChild<QToolButton *>("addFilter");
        if (!button || !button->menu()) return false;
        for (QAction *a : button->menu()->actions())
            if (a->text() == QLatin1String(kind)) {
                a->trigger();
                settle();
                return true;
            }
        return false;
    };

    bool invert = false;
    for (auto *box : panel.findChildren<QCheckBox *>()) invert = invert || box->text() == "Invert";
    check(!invert, "no Invert: a filter does it");
    check(filters().empty() && panel.findChild<QToolButton *>("addFilter"), "none at first, + Filter offered");

    check(add("Math") && add("Clamp"), "add Math, then Clamp");
    check(filters().size() == 2 && filters()[0].kind == InteractionFilter::Math && filters()[1].kind == InteractionFilter::Clamp,
          "in the order they were added");
    if (auto *op = panel.findChild<QComboBox *>("filterOp0")) {
        op->setCurrentIndex(int(MathOp::OneMinus));
        emit op->activated(int(MathOp::OneMinus));
        settle();
    }
    check(filters()[0].op == MathOp::OneMinus, "Math › 1 − x");
    check(!panel.findChild<QWidget *>("filter0a"), "1 − x has no value to set");
    auto *rename = panel.findChild<QLabel *>("filterName0");
    check(rename != nullptr, "a filter has a name label");
    if (rename) {
        QTimer::singleShot(0, &panel, [] {
            if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                dialog->setTextValue("Invert signal");
                dialog->accept();
            }
        });
        const QPoint at = rename->rect().center();
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, at, rename->mapToGlobal(at),
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(rename, &doubleClick);
        settle();
        const auto *label = panel.findChild<QLabel *>("filterName0");
        check(filters()[0].enabled && filters()[0].name == "Invert signal" && label && label->text().contains("Invert signal (Math)"),
              "renaming shows both purpose and type");
        undo.undo();
        settle();
        check(filters()[0].name.isEmpty() && filters()[0].op == MathOp::OneMinus, "renaming undoes without changing settings");
    }
    auto select = [&](int k) { // a click on a filter (its frame, not a field)
        if (auto *block = panel.findChild<QWidget *>(QStringLiteral("filterBlock%1").arg(k))) {
            const QPointF at(block->width() - 4, 4);
            QMouseEvent press(QEvent::MouseButtonPress, at, block->mapToGlobal(at.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(block, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, at, block->mapToGlobal(at.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(block, &release);
        }
        settle();
    };
    select(1);
    if (auto *up = panel.findChild<QToolButton *>("filterUp")) up->click();
    settle();
    check(filters()[0].kind == InteractionFilter::Clamp && filters()[1].kind == InteractionFilter::Math, "Clamp moved before Math");
    undo.undo();
    settle();
    check(filters()[0].kind == InteractionFilter::Math, "the move undoes");
    if (auto *on = panel.findChild<QCheckBox *>("filterOn0")) on->setChecked(false);
    settle();
    check(!filters()[0].enabled, "a filter switched off");
    select(1);
    if (auto *remove = panel.findChild<QToolButton *>("removeFilter")) remove->click();
    settle();
    check(filters().size() == 1 && filters()[0].kind == InteractionFilter::Math, "Clamp removed");
    undo.undo();
    settle();
    check(filters().size() == 2, "the removal undoes");
    check(panel.findChild<QWidget *>("filtersFrame") != nullptr, "the filters in their frame");

    auto *tabs = panel.findChild<QTabWidget *>("interactionTargetTabs");
    check(tabs && tabs->count() == 1 && tabs->tabText(0) == "Target 1", "Target 1 exists by default");
    check(panel.findChild<QWidget *>("sharedFiltersFrame"), "shared filters have their own frame");
    if (tabs) {
        QTimer::singleShot(0, &panel, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                for (auto *action : menu->actions())
                    if (action->text() == "Add Target") { action->trigger(); break; }
                menu->close();
            }
        });
        tabs->tabBar()->customContextMenuRequested(tabs->tabBar()->tabRect(0).center());
        settle();
        tabs = panel.findChild<QTabWidget *>("interactionTargetTabs");
        check(tabs && tabs->count() == 2 && tabs->currentIndex() == 1, "right-click adds and opens Target 2");
        undo.undo();
        settle();
    }
    // Switching protocols preserves the complete mapping and remains undoable.
    auto changeSource = [&](InteractionSource kind) {
        auto *button = panel.findChild<QToolButton *>("interactionSource");
        if (!button || !button->menu()) return false;
        for (auto *action : button->menu()->actions()) {
            if (action->text() == interactionSourceName(kind)) {
                action->trigger();
                settle();
                return true;
            }
        }
        return false;
    };
    const auto original = engine.interactions().back();
    check(changeSource(InteractionSource::Keyboard), "OSC can become keyboard");
    check(changeSource(InteractionSource::Osc), "keyboard can become OSC");
    const auto changed = engine.interactions().back();
    check(changed.id == original.id && changed.layer == original.layer && changed.param == original.param &&
              changed.filters.size() == original.filters.size() && changed.filters[0].kind == original.filters[0].kind,
          "source switch preserves identity, destination and filters");
    undo.undo();
    settle();
    check(engine.interactions().back().source == InteractionSource::Keyboard, "source switch undoes");
    undo.redo();
    settle();
    check(engine.interactions().back().source == InteractionSource::Osc, "source switch redoes");

    // Any value: a filter's setting (a number field, no slider and no bounds)
    if (auto *clampMax = panel.findChild<QDoubleSpinBox *>("filter1b")) clampMax->setValue(250);
    settle();
    check(filters()[1].kind == InteractionFilter::Clamp && filters()[1].b == 250, "a setting beyond its usual range");
    bool ranges = false, toggle = false; // a range is a Remap, a toggle a Toggle: no fields of their own
    for (auto *l : panel.findChildren<QLabel *>()) ranges = ranges || (l->isVisible() && (l->text() == "Source range" || l->text() == "Target range"));
    for (auto *box : panel.findChildren<QCheckBox *>()) toggle = toggle || (box->isVisible() && box->text() == "Toggle");
    check(!ranges && !toggle, "no ranges nor toggle of their own: filters");


    check(add("Curve") && filters().size() == 3 && panel.findChild<CurveLane *>("filterCurve2"), "a Curve, with its editor");
    check(panel.findChild<QProgressBar *>("filterMeter0") && panel.findChild<QProgressBar *>("filterMeter2") &&
              panel.findChild<QLabel *>("filterValue1"),
          "each filter shows what comes out of it");
    if (auto *list = panel.findChild<QTreeWidget *>()) check(list->columnCount() == 3, "no Value column in the list");
    // A Remap from +: from what the source gives (OSC: 0..1) to what the target takes (the composition's opacity: 0..1)
    check(add("Remap") && filters().size() == 4 && filters()[3].kind == InteractionFilter::Remap && filters()[3].a == 0 &&
              filters()[3].b == 1 && filters()[3].c == 0 && filters()[3].d == 1,
          "a Remap starts from the source's range to the target's");
    check(panel.findChild<QDoubleSpinBox *>("filter3d") != nullptr, "its four ends as numbers");
    {
        auto list = filters();
        list.pop_back();
        Interaction g = engine.interactions().back();
        g.filters = list;
        engine.setInteraction(g);
        settle();
    }
    // Drawn on: a key where it is clicked, then moved by a drag
    auto mouse = [&](QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButtons buttons) {
        QMouseEvent ev(type, pos, w->mapToGlobal(pos.toPoint()), Qt::LeftButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(w, &ev);
    };
    if (auto *lane = panel.findChild<CurveLane *>("filterCurve2")) {
        const QPointF mid(lane->width() * 0.5, lane->height() * 0.2);
        mouse(lane, QEvent::MouseButtonPress, mid, Qt::LeftButton);
        mouse(lane, QEvent::MouseButtonRelease, mid, Qt::NoButton);
        settle();
        check(filters()[2].curve.keys.size() == 3, "a click on the curve adds a key");
        lane = panel.findChild<CurveLane *>("filterCurve2");
        const auto before = filters()[2].curve.keys;
        if (lane && before.size() == 3) {
            const QPointF from(lane->width() * 0.5, lane->height() * 0.2), to(lane->width() * 0.5, lane->height() * 0.7);
            mouse(lane, QEvent::MouseButtonPress, from, Qt::LeftButton);
            for (int k = 1; k <= 5; ++k) mouse(lane, QEvent::MouseMove, from + (to - from) * (k / 5.0), Qt::LeftButton);
            mouse(lane, QEvent::MouseButtonRelease, to, Qt::NoButton);
            settle();
            const auto after = filters()[2].curve.keys;
            check(after.size() == 3 && after[1].v < before[1].v - 0.2, "a key dragged down");
        }
    }

    // A curve with keys a thousandth from its ends (as drawn in a show to cut at the end of a video): its top right
    // key dragged. Each move is an edit, the list is made again: the curve dragged stays the one under the mouse
    {
        Interaction g = engine.interactions().back();
        InteractionStage c = InteractionStage::made(InteractionFilter::Curve);
        c.curve.keys = {AnimKey{0, 0, 0}, AnimKey{0.001, 0.995, 3}, AnimKey{0.999, 1.005, 3}, AnimKey{1, 0, 3}};
        g.filters = {c};
        engine.setInteraction(g);
        settle();
        auto *lane = panel.findChild<CurveLane *>("filterCurve0");
        check(lane != nullptr, "its curve shown");
        if (lane) {
            // Where the key at 0.999 (1.005) is: hovering it names it (its time and value), nothing is edited
            QPointF at(-1, -1);
            for (int y = 1; y < lane->height() / 2 && at.x() < 0; ++y)
                for (int x = lane->width() - 1; x > lane->width() / 2 && at.x() < 0; --x) {
                    mouse(lane, QEvent::MouseMove, QPointF(x, y), Qt::NoButton);
                    const QString tip = lane->toolTip();
                    if (tip.contains(QStringLiteral("Drag: move")) && tip.contains(QStringLiteral("→ 1.0"))) at = QPointF(x, y);
                }
            check(at.x() >= 0, "its key can be taken");
            const QPointer<CurveLane> held(lane);
            mouse(lane, QEvent::MouseButtonPress, at, Qt::LeftButton);
            for (int k = 1; k <= 5; ++k) mouse(lane, QEvent::MouseMove, at + QPointF(-30 * k, 10 * k), Qt::LeftButton);
            check(held && held->isVisible(), "the curve dragged is not replaced while it is dragged");
            mouse(lane, QEvent::MouseButtonRelease, at + QPointF(-150, 50), Qt::NoButton);
            settle();
            const auto keys = filters()[0].curve.keys;
            check(keys.size() == 4 && keys[2].t < 0.9 && keys[2].v < 0.9, "its key moved where it was dragged");
        }
    }

    if (argc > 1) panel.grab().save(QString::fromLocal8Bit(argv[1]));
    std::printf("interaction_filters_ui: %d failures\n", failures);

    {
        Animation a; a.name = "Fold test";
        const int index = engine.addAnimation(a);
        TimelineWindow timelines(&engine, &undo);
        timelines.selectTimeline(engine.animation(index).id);
        auto *fold = timelines.editor()->findChild<QPushButton *>("timelineFold");
        auto *pages = timelines.findChild<QStackedWidget *>("timelineWorkspace");
        check(fold && pages, "timeline fold control exists");
        if (fold && pages) {
            fold->click();
            check(pages->currentWidget() != timelines.editor(), "individual timeline folds");
            auto *expand = pages->currentWidget()->findChild<QPushButton *>("timelineExpand");
            check(expand != nullptr, "folded timeline can expand");
            if (expand) expand->click();
            check(pages->currentWidget() == timelines.editor(), "timeline expands without replacement");
            auto *all = timelines.listPanel()->findChild<QPushButton *>("timelinesOverview");
            all->click();
            auto *activity = qobject_cast<TimelineActivity *>(pages->currentWidget());
            check(activity && !activity->rows().empty(), "fold all retains timeline overview");
        }
    }
    return failures ? 1 : 0;
}
