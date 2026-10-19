# Experimental modulation graph

Open **Interactions → Graph**, add an interaction with **+**, choose its **Target**, then select **Edit Graph…**.
A graph is the source of one existing interaction binding. The standard source/target ranges, master level,
filters and takeover still run after the graph. Existing non-graph interactions keep their behavior.

The default example is `(Audio + OSC) × MIDI`, with a sine fallback when MIDI is below a threshold.

- Audio: live input, 20–180 Hz. Configure the input device in Settings, or choose Program mix in the Audio node.
- OSC: `/control`, argument 0. Initial value is 0.2 until a matching message arrives.
- MIDI: Control Change 7, any input and channel. Initial value is 0.7 until a matching message arrives.
- Below Threshold: threshold 0.2, hysteresis 0.04 (enter below 0.18, leave at 0.22).
- Sine: 0.3 Hz, amplitude 0.4, offset 0.5. It continues running while the primary branch is active.
- Smooth: 150 ms time constant; Clamp limits the final result to 0–1.

Select a node to edit its properties and input connections. Click an output port, then an input port to connect
nodes. Numeric and condition ports are distinct; cycles are rejected. Unconnected inputs are zero / false.
Move nodes by dragging them; Fit and Zoom controls navigate the graph. Use as Output chooses its numeric output.
Changes apply live and go through the project undo stack. The editor also has local Undo.

Graphs are saved in projects and captured by snapshots. Graph interactions can expand in the list to show their
nodes. Layers with active graph bindings have a marker and a Modulated tooltip; an opacity binding also has an
Edit Graph shortcut in the layer inspector. Any target parameter's context menu lists its graph bindings.

## Scope of this test branch

One graph and one numeric output per interaction. MIDI graph sources currently receive CC messages; existing
simple MIDI interactions also retain their other message types. Sources can be combined freely using Add,
Multiply, Below Threshold, Select, Sine, Constant, Smooth and Clamp nodes. Only the source graph is new:
shared patch libraries, multiple named outputs, and target-level Add / Multiply blend modes are not implemented
in this first test. Multiple bindings to one parameter retain the existing interaction ordering.

## Validation

- `pinol_modulation_graph_test`: DAG validation, event values, arithmetic, fallback, hysteresis, smoothing,
  serialization, state preservation on layout edits, invalid graphs and bounded simulation.
- `pinol_audio_interaction_ui_test`: native editor, node properties, add/delete and target menu access.
- `pinol_tests --area interactions`: real engine target writes, OSC/MIDI events, project round trip and snapshot
  reconstruction, alongside the existing interactions regression suite. Requires an OpenGL-capable session.
