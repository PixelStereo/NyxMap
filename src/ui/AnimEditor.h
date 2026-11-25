#pragma once
// The editor of an animation is pxs-qt's (pxs/TimelineEditor.h, pxs::timelineview: the transport, the ruler and zoom, a row per track),
// on what the Engine answers as a TimelineHost. NyxMap's editors (TimelineEditor, in the Timelines window; ParamAnimEditor, in the
// Anim tab and its floating window) derive from these, which add the Engine for what they do besides (undo, layers, sequences).
#include "Engine.h"

#include <pxs/TimelineEditor.h>

using pxs::timelineview::addParamMenu;
using pxs::timelineview::animCurveName;
using pxs::timelineview::animWaveName;
using pxs::timelineview::ColorLane;
using pxs::timelineview::CurveLane;
using pxs::timelineview::EventLane;
using pxs::timelineview::sameAnimation;
using pxs::timelineview::TimeRuler;
using pxs::timelineview::XyPathPreview;

// Keep a whole color beside its component entries in the same Source / Color / FX category
inline std::vector<Engine::AnimParam> animationMenuParams(Engine *engine, quint64 layer)
{
    return pxs::timelineview::animationMenuParams(engine, layer);
}

class AnimEditor : public pxs::timelineview::AnimEditor
{
public:
    AnimEditor(Engine *engine, QUndoStack *undo, Layout layout, bool scrollTracks, QWidget *parent = nullptr)
        : pxs::timelineview::AnimEditor(engine, undo, layout, scrollTracks, parent), m_engine(engine)
    {
    }
    Engine *engine() const { return m_engine; }

protected:
    Engine *m_engine;
};

class TrackRow : public pxs::timelineview::TrackRow
{
public:
    TrackRow(AnimEditor *editor, int index) : pxs::timelineview::TrackRow(editor, index), m_e(editor->engine()) {}

protected:
    Engine *m_e;
};
