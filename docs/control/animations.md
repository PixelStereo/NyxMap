# Animation data filters and drivers

In a layer's **Anim** tab, **Add Data Filter…** appears immediately below the waveform or keypoint lane. It offers the same 15 filters as Interactions, including Math, Remap, Clamp, smoothing, Delay, Curve and Envelope. Filters run in order after the animation evaluates its curve or waveform. Switch a filter off to bypass it, use the arrows to reorder it, or × to remove it. These edits support undo/redo and project saving.

For XY animations, choose X or Y to edit that axis's independent filter chain. Color animations apply their chain independently to R, G and B; alpha is unchanged. Event tracks retain their discrete actions and do not run data filters.

Right-click **Amplitude**, **Period**, **Center**, **Phase**, **Duration**, **Speed**, or a filter's numeric setting to add an animation, timeline or interaction controlling it. An amplitude animation runs before the waveform it controls, including nested modulation. Filters retain their own history while their numeric settings change; seeking or restarting resets that history.

In an interaction's target picker, expand the layer's **Animation** parameters to reach its oscillator settings, transport and filters. The composition's **Interactions** parameters expose interaction settings and their filters, allowing one interaction to control another. These addresses are also available to OSC, presets and snapshots. Existing animations must exist before their settings appear as targets.

Right-click a driven value and choose **Go to animation driver**, **Go to interaction driver**, or **Go to timeline driver**. Multiple drivers are listed individually; disabled drivers are marked “off”. A direct layer animation opens in its editor window; an interaction or timeline is selected in its corresponding panel. Timeline navigation is in the **Animate → Timeline** menu.

Animations run first, then interactions, matching the existing engine priority. An interaction changing an oscillator setting affects the following animation evaluation. Mutually dependent interactions are evaluated once per frame in list order; they do not recursively execute.
