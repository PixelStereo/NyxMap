#include "Engine.h"
#include "InteractionPanel.h"
#include "AudioSpectrum.h"
#include "ModulationGraphView.h"
#include "Widgets.h"
#include <QApplication>
#include <QTabBar>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QMenu>
#include <QMouseEvent>
#include <QUndoStack>
#include <cstdio>
#include <QDialog>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QTimer>
#include <QPushButton>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QPinchGesture>
#include <QGestureEvent>
#include <QGraphicsPathItem>
#include <QLineF>
#include <cmath>
int main(int argc,char **argv){
    QApplication app(argc,argv);theme::applyToApplication(app);Engine engine;QUndoStack undo;int failures=0;
    auto check=[&](bool ok,const char *why){if(!ok){++failures;std::printf("FAIL audio_interaction_ui: %s\n",why);}};
    auto old=Interaction::fromJson({{"source","internal"},{"source_layer","0"},{"source_param","audio/bass"},{"layer","101"},{"param","opacity"}});
    check(old.source==InteractionSource::Audio && old.audioLow==20 && old.audioHigh==180,"legacy program audio migrates to Audio band");
    old.audioLow=340;old.audioHigh=1240;old.name="Metal";
    auto roundtrip=Interaction::fromJson(old.toJson());
    check(roundtrip.source==InteractionSource::Audio && roundtrip.audioLow==340 && roundtrip.audioHigh==1240 && roundtrip.name=="Metal","custom frequency interval survives project serialization");
    InteractionPanel panel(&engine,&undo);panel.resize(720,1000);panel.show();
    panel.learnFor(0,"opacity",InteractionSource::Audio);app.processEvents();
    auto *tabs=panel.findChild<QTabBar*>();check(tabs&&tabs->tabText(tabs->currentIndex())=="Audio","Audio has its own selected tab");
    auto *spectrum=panel.findChild<AudioSpectrum*>();auto *lo=panel.findChild<QDoubleSpinBox*>("audioLowHz");auto *hi=panel.findChild<QDoubleSpinBox*>("audioHighHz");
    check(spectrum&&lo&&hi,"spectrum and numeric frequency selection shown before target mapping");
    if(lo&&hi){lo->setValue(500);hi->setValue(1500);auto list=engine.interactions();check(list.size()==1&&list[0].audioLow==500&&list[0].audioHigh==1500,"frequency widgets update engine interaction");undo.undo();check(engine.interactions()[0].audioHigh==20000,"band drag/number edits undo together");undo.redo();}
    // The live input: chosen per interaction, undone like any edit, its spectrum shown in place of the program mix's
    auto *input=panel.findChild<QComboBox*>("audioInput");check(input&&input->currentIndex()==0,"an Audio interaction listens to the program mix by default");
    if(input){input->setCurrentIndex(1);emit input->activated(1);app.processEvents();
        check(engine.interactions()[0].audioInput,"Live input is chosen from the Audio interaction");
        check(panel.findChild<AudioSpectrum*>()!=nullptr,"the live input's spectrum is shown");
        undo.undo();check(!engine.interactions()[0].audioInput,"the input choice undoes");undo.redo();}
    QMenu menu;InteractionPanel::fillValueMenu(&menu,&engine,0,{"opacity"});
    bool audioAction=false;for(auto *a:menu.actions())if(a->menu())for(auto *b:a->menu()->actions())if(b->text()=="Audio")audioAction=true;
    check(audioAction,"parameter right-click offers Audio mapping");
    // The editor itself does not require a physical MIDI service in headless CI.
    Interaction graph=Interaction::made(InteractionSource::Graph);graph.enabled=false;graph.param="opacity";
    const auto graphId=engine.addInteraction(graph);
    tabs->setCurrentIndex(int(InteractionSource::Graph));app.processEvents();
    auto *listWidget=panel.findChild<QTreeWidget*>("interactionList");
    if(listWidget&&listWidget->topLevelItemCount())listWidget->setCurrentItem(listWidget->topLevelItem(0));
    app.processEvents();
    check(tabs->tabText(tabs->currentIndex())=="Graph","Graph tab available for patch bindings");
    check(panel.findChild<QPushButton*>("editModulationGraph")!=nullptr,"graph interaction has Edit Graph action");
    QTimer::singleShot(0,&panel,[&]{
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog&&dialog->objectName()=="modulationGraphEditor","native graph editor opens");
        if(!dialog)return;
        auto *view=dialog->findChild<QGraphicsView*>("modulationGraphView");
        check(view&&view->scene()->items().size()>10,"graph nodes and connections rendered");
        if(view){for(auto *item:view->scene()->items())if(item->data(0).toInt()==3){view->scene()->clearSelection();item->setSelected(true);break;}}
        auto *initial=dialog->findChild<QDoubleSpinBox*>("graphInitialvalue");
        check(initial!=nullptr,"MIDI node exposes initial value");
        if(initial){initial->setValue(.1);check(engine.interactions().back().graph.nodes[2].value==.1,"node edit reaches interaction model");}
        auto *add=dialog->findChild<QPushButton*>("graphAddNode");if(add)add->click();
        check(engine.interactions().back().graph.nodes.size()==11,"node addition committed");
        auto *remove=dialog->findChild<QPushButton*>("graphDeleteNode");if(remove)remove->click();
        check(engine.interactions().back().graph.nodes.size()==10,"node deletion committed");
        app.processEvents();
        if(view){
            auto *canvas=dynamic_cast<ModulationGraphView*>(view);
            check(canvas!=nullptr,"graph uses gesture-aware canvas");
            if(canvas){
                const QPoint point=canvas->viewport()->rect().center()+QPoint(60,-30);
                const QPointF anchor=canvas->mapToScene(point);
                const double before=canvas->zoom();
                auto native=[&](Qt::NativeGestureType type,double amount){
                    QNativeGestureEvent gesture(type,QPointingDevice::primaryPointingDevice(),2,point,point,canvas->viewport()->mapToGlobal(point),amount,{});
                    QApplication::sendEvent(canvas->viewport(),&gesture);
                    return gesture.isAccepted();
                };
                check(native(Qt::BeginNativeGesture,0)&&native(Qt::ZoomNativeGesture,.25),"native trackpad pinch accepted");
                check(std::abs(canvas->zoom()-before*1.25)<1e-9,"pinch expands graph");
                check(QLineF(anchor,canvas->mapToScene(point)).length()<3,"pinch keeps scene under gesture center");
                native(Qt::ZoomNativeGesture,-.2);native(Qt::EndNativeGesture,0);
                check(std::abs(canvas->zoom()-before)<1e-9,"pinch contracts graph");
                canvas->viewport()->grabGesture(Qt::PinchGesture);
                QPinchGesture pinch;pinch.setCenterPoint(point);pinch.setLastScaleFactor(1);pinch.setScaleFactor(1.2);pinch.setTotalScaleFactor(1.2);pinch.setChangeFlags(QPinchGesture::ScaleFactorChanged);
                QGestureEvent fallback({&pinch});QApplication::sendEvent(canvas->viewport(),&fallback);
                check(std::abs(canvas->zoom()-before*1.2)<1e-9,"Qt pinch fallback expands graph");
                pinch.setScaleFactor(1.1);pinch.setLastScaleFactor(1.2);pinch.setTotalScaleFactor(1.32);
                QGestureEvent nextPinch({&pinch});QApplication::sendEvent(canvas->viewport(),&nextPinch);
                check(std::abs(canvas->zoom()-before*1.32)<1e-9,"successive pinch updates use total magnification once");
                canvas->viewport()->ungrabGesture(Qt::PinchGesture);
                canvas->zoomAt(point,1e9);check(std::abs(canvas->zoom()-3.5)<1e-9,"maximum zoom bounded");
                canvas->zoomAt(point,1e-9);check(std::abs(canvas->zoom()-.2)<1e-9,"minimum zoom bounded");
                canvas->zoomAt(point,-2);check(std::abs(canvas->zoom()-.2)<1e-9,"invalid scale cannot flip graph");
                check(canvas->viewportUpdateMode()==QGraphicsView::FullViewportUpdate,"full canvas repaint avoids port trails");
                QGraphicsRectItem *audioNode=nullptr;QGraphicsPathItem *audioWire=nullptr;
                for(auto *item:canvas->scene()->items()){
                    if(item->data(0).toInt()==1)audioNode=dynamic_cast<QGraphicsRectItem*>(item);
                    if(item->data(1).toInt()==1)audioWire=dynamic_cast<QGraphicsPathItem*>(item);
                }
                check(audioNode&&audioNode->boundingRect().contains(QPointF(-6,82))&&audioNode->boundingRect().contains(QPointF(audioNode->rect().right()+6,audioNode->rect().bottom()-18)),"painted ports stay within declared item bounds");
                if(audioNode&&audioWire){
                    const QPointF old=audioNode->pos();audioNode->setPos(old+QPointF(40,20));
                    const auto start=audioWire->path().elementAt(0);
                    const auto port=audioNode->mapToScene(QPointF(audioNode->rect().right(),audioNode->rect().bottom()-18));
                    check(QLineF(QPointF(start.x,start.y),port).length()<.01,"cable follows node before drag release");
                    audioNode->setPos(old);
                }
                canvas->fitGraph(canvas->scene()->itemsBoundingRect());
            }
        }
        app.processEvents();
        if(argc>2)dialog->grab().save(QString::fromLocal8Bit(argv[2]));
        dialog->accept();
    });
    panel.openGraph(graphId);app.processEvents();
    QMenu graphMenu;InteractionPanel::fillValueMenu(&graphMenu,&engine,0,{"opacity"});
    bool graphAction=false;for(auto *a:graphMenu.actions())if(a->text().startsWith("Edit Graph"))graphAction=true;
    check(graphAction,"target parameter menu opens existing graph");
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);app.processEvents();
    if(argc>1)panel.grab().save(QString::fromLocal8Bit(argv[1]));
    std::printf("audio_interaction_ui: %d failures\n",failures);return failures?1:0;
}
