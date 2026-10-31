#include "Engine.h"
#include "InteractionPanel.h"
#include "AudioSpectrum.h"
#include "Widgets.h"
#include <QApplication>
#include <QTabBar>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QMenu>
#include <QMouseEvent>
#include <QUndoStack>
#include <QToolButton>
#include <cmath>
#include <cstdio>
int main(int argc,char **argv){
    QApplication app(argc,argv);theme::applyToApplication(app);Engine engine;QUndoStack undo;int failures=0;
    auto check=[&](bool ok,const char *why){if(!ok){++failures;std::printf("FAIL audio_interaction_ui: %s\n",why);}};
    Interaction old=Interaction::made(InteractionSource::Audio);old.layer=101;old.param="opacity";
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
    QAction *audioAction=nullptr;
    for(auto *a:menu.actions())if(a->text()=="Animate"&&a->menu())
        for(auto *b:a->menu()->actions())if(b->text()=="Interaction"&&b->menu())
            for(auto *protocol:b->menu()->actions())if(protocol->text()=="Audio")audioAction=protocol;
    check(audioAction,"parameter right-click offers Audio mapping under Animate / Interaction");
    const auto beforeMapping=engine.interactions().size();
    if(audioAction)audioAction->trigger();
    check(engine.interactions().size()==beforeMapping+1&&engine.interactions().back().source==InteractionSource::Audio&&
          engine.interactions().back().param=="opacity","nested Audio action maps the clicked parameter");
    undo.undo();check(engine.interactions().size()==beforeMapping,"mapping created through the nested menu is undoable");
    // Learn says when it listens
    if(auto *learn=panel.findChild<QToolButton*>("interactionLearn")){
        learn->setChecked(true);check(learn->text()==QStringLiteral("● Learning…"),"Learn on says it is listening");
        learn->setChecked(false);check(learn->text()==QStringLiteral("Learn"),"Learn off says Learn");
    }else check(false,"the Learn button");
    // The band: dragged by its middle it moves, as wide (in octaves), held within 20 Hz – 20 kHz; by a boundary, that one
    {
        AudioSpectrum s([]{return AudioAnalysis::Spectrum{};});s.resize(420,160);s.show();app.processEvents();
        double lo=0,hi=0;QObject::connect(&s,&AudioSpectrum::bandEdited,[&](double a,double b){lo=a;hi=b;});
        auto x=[](double hz){return 10+std::log(hz/20.)/std::log(1000.)*400;}; // the plot: 10 px in, 400 px wide
        auto mouse=[&](QEvent::Type t,double px){
            QMouseEvent e(t,QPointF(px,60),QPointF(px,60),t==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                          t==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(&s,&e);};
        auto dragFrom=[&](double from,double to){mouse(QEvent::MouseButtonPress,from);mouse(QEvent::MouseMove,to);mouse(QEvent::MouseButtonRelease,to);};
        s.setBand(200,2000);const double mid=x(std::sqrt(200.*2000.));
        dragFrom(mid,mid+40);
        check(lo>250&&std::abs(hi/lo-10)<0.01,"dragged by its middle, the band moves, as wide");
        s.setBand(200,2000);dragFrom(mid,mid+1000);
        check(std::abs(hi-20000)<0.5&&std::abs(hi/lo-10)<0.01,"moved to the top, it stops at 20 kHz, as wide");
        s.setBand(200,2000);dragFrom(x(2000),x(4000));
        check(std::abs(lo-200)<0.5&&std::abs(hi-4000)<5,"dragged by a boundary, that one alone moves");
    }
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);app.processEvents();
    if(argc>1)panel.grab().save(QString::fromLocal8Bit(argv[1]));
    std::printf("audio_interaction_ui: %d failures\n",failures);return failures?1:0;
}
