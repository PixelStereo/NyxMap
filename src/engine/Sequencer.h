#pragma once
// The sequences (the cue lists) are pxs's (pxs/Sequencer.h, pxs-qt's sequence module), shared with OSCQlock. The Engine is their host
// (SequencerHost.h, EngineSequencerHost in Engine.cpp).
#include <pxs/Sequencer.h>

#include "AnnotatedMutex.h"
#include "FrameTiming.h"
