#include "Engine.h"
#include "InteractionPanel.h"
#include "Widgets.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QUndoStack>
#include <iostream>
#include <stdexcept>
static void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    theme::applyToApplication(app);
    Engine engine;
    const auto outer = engine.layerId(engine.addGroup("Outer Group"));
    const auto inner = engine.layerId(engine.addGroup("Inner Group"));
    const auto image = engine.layerId(engine.addLayer("Image Layer"));
    auto structure = tree::intoGroup(engine.structure(), {inner}, outer);
    engine.setStructure(tree::intoGroup(structure, {image}, inner));
    QUndoStack undo;
    try {
        InteractionPanel panel(&engine, &undo);
        panel.resize(720, 1100);
        panel.show();
        panel.learnFor(0, "opacity", InteractionSource::Pixels);
        app.processEvents();
        auto *sourceChoice = panel.findChild<QToolButton *>("visionSource");
        auto *tapChoice = panel.findChild<QComboBox *>("visionTap");
        const auto item = [](QMenu *menu, const QString &text) -> QAction * {
            if (menu)
                for (auto *action : menu->actions())
                    if (action->text() == text) return action;
            return nullptr;
        };
        const auto submenu = [&](QMenu *menu, const QString &text) -> QMenu * {
            const auto *action = item(menu, text);
            return action ? action->menu() : nullptr;
        };
        require(sourceChoice && sourceChoice->menu(), "analysis source offers a menu");
        auto *sourceMenu = sourceChoice->menu();
        emit sourceMenu->aboutToShow();
        require(!item(sourceMenu, "Composition") && !item(sourceMenu, "Image Layer"),
                "analysis menu leaves the layer in its group");
        auto *imageChoice = item(submenu(submenu(sourceMenu, "Outer Group"), "Inner Group"), "Image Layer");
        require(imageChoice, "analysis menu follows nested layer groups");
        imageChoice->trigger();
        require(engine.interactions()[0].pixels.layer == image, "choosing a nested source stores its layer ID");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tapChoice = panel.findChild<QComboBox *>("visionTap");
        auto *target = panel.findChild<QToolButton *>("interactionTargetParameter");
        require(target && target->menu(), "mapping target offers a menu");
        emit target->menu()->aboutToShow();
        auto *outerTarget = submenu(target->menu(), "Outer Group");
        auto *innerTarget = submenu(outerTarget, "Inner Group");
        auto *layerTarget = submenu(innerTarget, "Image Layer");
        require(layerTarget, "the target layer is inside its group");
        emit layerTarget->aboutToShow();
        require(submenu(layerTarget, "Layer") && submenu(innerTarget, "Group Parameters") &&
                    !item(target->menu(), "Image Layer"), "target menu keeps both group and layer parameters");

        require(tapChoice && tapChoice->count() == 2 && tapChoice->itemData(0).toInt() == 1 &&
                    tapChoice->itemData(1).toInt() == 2,
                "pre/post FX are the only two analysis taps");
        auto *tabs = panel.findChild<QButtonGroup *>();
        require(tabs && tabs->checkedButton()->text() == "Pixels", "Pixels tab selected");
        auto *learn = panel.findChild<QToolButton *>("interactionLearn");
        require(learn && !learn->isChecked() && !learn->isEnabled(), "Pixels does not start external Learn");
        auto *channel = panel.findChild<QComboBox *>("pixelChannel");
        auto *feature = panel.findChild<QComboBox *>("pixelFeature");
        auto *source = panel.findChild<QToolButton *>("visionSource");
        require(channel && feature && source && channel->count() == 6,
                "pixel channel, feature and image selection widgets");
        channel->setCurrentIndex(3);
        emit channel->activated(3);
        require(engine.interactions()[0].pixels.channel == PixelAnalysis::Channel::Alpha,
                "channel edits reach the engine");
        require(!panel.findChild<QDoubleSpinBox *>("pixelROI_width"), "Pixels hides its ROI fields");
        panel.learnFor(0, "opacity", InteractionSource::Hands);
        app.processEvents();
        require(tabs->checkedButton()->text() == "MediaPipe", "MediaPipe tab selected");
        require(engine.interactions().back().filters.empty(), "new MediaPipe interactions have no default filter");
        HandTracking::Frame tracked;
        tracked.hands[0].valid = tracked.hands[1].valid = true;
        tracked.hands[0].handedness = 1;
        tracked.hands[1].handedness = 0;
        require(HandTracking::handSlot(tracked, 2) == 1 && HandTracking::handSlot(tracked, 3) == 0,
                "left and right routing follows handedness instead of detector order");
        tracked.hands[0].confidence = tracked.hands[1].confidence = 1;
        tracked.hands[0].gesture = QStringLiteral("Thumb_Down");
        tracked.hands[1].gesture = QStringLiteral("Victory");
        using Feature = HandTracking::Feature;
        require(HandTracking::measureSelected(tracked, 0, Feature::GestureThumbDown, .7, 0) == 1 &&
                    HandTracking::measureSelected(tracked, 0, Feature::GestureVictory, .7, 0) == 1,
                "different gestures can be active on two hands at the same time");
        require(HandTracking::measureSelected(tracked, 2, Feature::GestureThumbDown, .7, 0) == 0 &&
                    HandTracking::measureSelected(tracked, 2, Feature::GestureVictory, .7, 0) == 1,
                "explicit left-hand routing ignores the right hand's gesture");
        auto *hand = panel.findChild<QComboBox *>("trackedHand");
        require(hand && hand->findText("Left") >= 0 && hand->findText("Right") >= 0,
                "both hands are selectable by handedness");
        require(!learn->isChecked() && !learn->isEnabled(), "Hands does not start external Learn");
        auto *category = panel.findChild<QComboBox *>("visionCategory");
        require(category && category->findText("Face") >= 0, "Face is a selectable category");
        category->setCurrentIndex(category->findText("Face"));
        emit category->activated(category->currentIndex());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        feature = panel.findChild<QComboBox *>("handFeature");
        require(feature && feature->findText("Face · Jaw open") >= 0 && feature->findText("Pinch strength") < 0,
                "choosing Face shows only face measures");
        require(panel.findChild<QPushButton *>("visionSetup"), "Setup is available for Face");
        require(!panel.findChild<QComboBox *>("trackedHand"), "Face has no hand selector");
        require(!panel.findChild<QLabel *>("visionPreview"), "MediaPipe preview stays in Setup");
        feature->setCurrentIndex(feature->findText("Face · Jaw open"));
        emit feature->activated(feature->currentIndex());
        require(engine.interactions().back().handFeature == HandTracking::Feature::JawOpen, "Face measure reaches the engine");
        std::cout << "Pixels/Hands UI, source and channel editors passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
