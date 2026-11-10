# Editing snapshots, presets, and sequences

## Include, exclude, or remove

The checkbox in a snapshot's content tree controls **inclusion on recall**. Unchecking a layer, interaction, fixture, or composition keeps its stored contents but leaves that object alone when recalled. It does not delete the stored contents or the live layer.

Select rows in the contents tree and use **Remove elements**, Delete/Backspace, or the context menu to actually remove their stored data. Shift and Ctrl/Cmd allow multiple selection. Selecting a category selects its descendants; selecting a layer removes all its stored contents. Structural source, effect-chain, mask, mesh, routing and animation-list entries represent complete definitions.

Removing contents makes the snapshot **Partial**, indicated in its inspector. Partial snapshots leave unmentioned layers and interactions alone, including objects created later. Ordinary, unedited full snapshots retain their existing full-scene behavior. Removed properties do not reset to defaults. A parameter-only fragment cannot recreate a missing source; include its source definition when recreation is required.

**Update** on a partial snapshot updates only the owners, values and definitions it still holds. Removed items do not reappear. The preset's Update follows the same existing-values rule. Stored edits leave the live composition untouched until a recall.

## Duplicate, copy, and paste

Use **Duplicate** in a snapshot/preset's menu or its duplicate button; Ctrl/Cmd+D also works in the library. Copies receive new identities, so existing sequence targets continue to point to their original objects. Snapshot copies stay beside their original in its group. These operations can be undone/redone.

In the snapshot/preset **contents** tree:

- Select one or more elements, or a whole layer/category.
- Use the Copy, Cut, Paste buttons, context menu, or Ctrl/Cmd+C/X/V.
- Select the destination snapshot or preset and paste. Existing matching elements are replaced; unrelated stored content is retained.
- Per-value delay, transition time and easing travel with the copied value. Paste into an empty snapshot produces a partial snapshot.

The same typed clipboard transfers supported values, structural definitions, animations and interactions between snapshots and presets. It uses the original stable layer/interaction/fixture identities: use it within the same project. It does not clone or retarget live layers. Source/FX/mask definitions remain whole definitions, as in the preset inspector; their existing structural recall rules apply. Layer visibility explicitly copied into a preset is a cut, with an optional delay.

Library Delete removes the entire snapshot/preset. Contents-tree Delete removes only selected stored elements. Copy/paste shortcuts are scoped to their tree/table, leaving text editors' ordinary clipboard behavior available. All new content edits and duplicates support the main undo stack.

## Edit a timecoded sequence

Open **Sequences** and enable **Edit mode · mute TC / AUTO** for the sequence you want to edit.

- Its pending pre-waits and follows are stopped.
- Incoming timecode no longer triggers its cues; the camera/audio/timecode source itself keeps running.
- Manual GO or playing a step auditions one cue immediately, without its pre-wait, tracking, or automatic continuation.
- Disabling Edit mode resumes at the clock's current position. Cues crossed while muted are not replayed; future crossings trigger normally.

The mode is per sequence and saved in the project. Other running sequences remain independent. It is an editing control, not a global pause or a mute of timecode outputs.

## Steps and action groups

The steps table provides multi-selection Copy/Cut/Paste and Delete/Backspace. **Duplicate** or Ctrl/Cmd+D copies the selected steps after the last selected row. Duplicates keep their waits, targets, text and actions, receive fresh identities, and clear their absolute timecodes to avoid two cues firing at the same timestamp. Clipboard paste preserves the stored timecodes, so check them when pasting into the same sequence.

**+ Step** inserts after the current row; Insert and the context menu also offer insertion before/after. Existing step identities follow moves/insertions. Step definition edits support Undo/Redo.

A **group** holds steps that start on one GO and overlap, like a Timeline Group in QLab. Select consecutive steps and press **Group** (or ⌘G, or right-click › **Create Group**): the group takes the first one's pre-wait, timecode and description, the last one's post-wait and what comes next. With nothing selected it makes a new group. In the list a group is a row with a triangle (a click on it, or a double-click on the row, folds it: only what is shown changes, not how it plays), its steps under it, numbered 1.1, 1.2, with a green line down them.

A step of a group is edited like any other: its target (drop a snapshot or a timeline on it; on the group's row, a new step), its description, its **Time** cell. Its pre-wait counts from the group's start, after the group's own pre-wait; those with the same time dispatch in list order during the same cue-clock turn. If they set the same property, the later one wins. AUTO on the group waits for the last of its steps to end, its post-wait included, a timeline played being followed in real time. A group holds up to 64 steps, and no other group.

A group can instead play **one GO per step** (QLab's *Start First And Enter*): choose it with right-click › **Group mode**, or make one with **Create Group, one GO per step**. Its steps stay steps like any other, with their own number, timecode, quantize and **Next**; each GO plays the next one, then the step after the group. The group's row only gathers and folds them, with a blue line down them. Changing the mode of a group keeps its steps and their waits.

Drag rows to move them: between two rows, into a group (on the lower half of its row, or between its steps), or out of it; a green line shows where they will go. ▲ and ▼ do the same one row at a time. A group left with no step goes. **Ungroup** turns a group into consecutive steps with AUTO between them, adding the group's pre-wait to the first and its post-wait and continuation to the last: they then play one after the other. Undo restores it.

## The library's lists

Snapshots, Presets and Timelines share the same bar above their list: **Group**, **+**, **−**, **Update** (snapshots and presets),
**⧉** and **PLAY**. A group is made with **Group**, renamed by a double-click or Return, and filled by dragging rows into it; **−**
on a selected group takes it away and keeps what it holds.

A preset can hold any value: click its name in the inspector in Edit mode (Enable and Solo too), right-click it, or use **Add
elements** above the preset's values, which lists the composition, every layer and the interactions.

The **≡ All** button of the Timelines list folds all the timelines: each one's run on a line, with its time and its transport.
Double-click a line to edit that timeline.

## Keys of the lists

The same keys work in the Snapshots, Presets and Timelines lists and in a sequence's steps, while that list has the focus:

| Key | Does |
| :--- | :--- |
| Return, double-click | Rename the row (edit the cell, in the steps). Neither plays: Return ends what is typed, and a double-click is too near a click to send a look out by accident. |
| ⌘Return / Ctrl+Return | Play the row. |
| Delete, Backspace | Delete the row (undoable). |
| ⌘D / Ctrl+D | Duplicate the row. |
| ⌘G / Ctrl+G | A new group; in the steps, one step of the steps selected. |

A held key does nothing more than one press. Space stays GO and Shift+Space GO BACK everywhere; in the steps, double-clicking a step's number plays it.

The field above each library list narrows it to the names holding what is typed (whatever its case and accents) or to a number; Esc empties it.

## A step's times

The **Time** column shows a step's pre-wait (yellow), action (green) and post-wait (blue, counted only with AUTO) on one bar, each with its share of the step's time; a part's number shows when it fits. The whole time is at the end, or the time gone by while the step plays. The tooltip tells all three. Double-click the cell (or Return on it) to change the waits: Return or a click away keeps them, Esc does not. The bar of each step is at its own scale: compare steps by the whole time, not by the length of their bars.

## Next: GO or AUTO

A step's **Next** cell (double-click or Return) says how the next step starts: **GO** waits for a GO; **AUTO** goes on once this step's action is over, plus its post-wait, following a timeline's real time (a pause or a speed change moves it, and an endless one holds it). Steps that must overlap go in a group, each with its own waits. A project's old Follow steps are read as AUTO, which no longer overlaps them.
