#pragma once
// What a show desk needs of a show, whoever draws it: the Show Control tab and the web page at /show (RemoteServer.h).
// The same two documents are published by OSCQuery (/show/catalog, /show/state), for a controller of its own.
#include <QJsonObject>

class Engine;

namespace showdesk {
// What the project holds: {version, snapshots, presets, timelines, sequences (with their cues), layers}
QJsonObject catalog(Engine *e);
// Where the show is: {version, values: the selected sequence's state (SequenceFeedback.h) by address, timelines, sequences}
QJsonObject state(Engine *e);
// A command, by stable id, from any thread: {"c":"go"|"back"|"stop"|"select","id":<sequence>}, {"c":"stop_all"},
// {"c":"cue","id":<sequence>,"cue":<cue>}, {"c":"snapshot"|"preset","id":..},
// {"c":"timeline","id":..,"do":"play"|"pause"|"stop"|"rewind"}. False: refused, or nothing of that id.
bool command(Engine *e, const QJsonObject &o);
} // namespace showdesk
