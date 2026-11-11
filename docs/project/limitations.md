# Limitations

Known problems of the current version. Planned work is in the [Roadmap](roadmap.md).

- **Parameter control:** competing drivers can overwrite each other without a durable takeover. See [current rules](../control/parameters.md) and [proposed improvements](roadmap.md#parameter-control).
- **Color:** compositing and FX remain in encoded sRGB, not linear light.
  Unknown/live profiles require a declaration; some video tags are unsupported.
  ISF image inputs are not individually color-managed. Some decoder/readback
  paths lose precision. Output conversion does not configure display ICC
  profiles or signal encoding; physical 10-bit output remains unverified.
- **Timing:** long stalls skip feedback/simulation steps. Physical A/V recovery and
  clock stability remain unmeasured. Input acquisition continues while paused.
- **Resources:** soft budgets miss some FFmpeg/driver/GPU allocations. Render-lock
  contention, retained decoder threads and control-event drops under load need
  measurement. Readback waits accumulate per output; slow IO can delay shutdown.
- **Playback and recalls:** FFT startup adds 85 ms at 48 kHz. ProRes 4444/XQ software
  decoding increases CPU load. Recall delays do not cover source/topology changes.
- **Media:** Photoshop files reproduce normal blending only (no other blend modes, clipping, masks,
  effects, adjustment layers or smart objects). An image sequence ends at the first gap in its numbering.
  SVG drawings are not redrawn when scaled. Network streams have no sound, seeking or looping, and the
  macOS package has no SRT. See [Media sources](../visuals/media.md).
- **TouchDesigner:** `.tox` playback needs an installed, paid TouchDesigner/TouchPlayer on Windows/macOS. Licensed GPU playback remains to be verified on hardware. CHOP/DAT/audio are not connected; Collect Files cannot discover external component resources. See [TouchDesigner components](../integrations/touchdesigner.md).
- **3D scenes:** no file animations (skeletons, keyframes) or cameras, no sorting of transparent
  surfaces, no hand editing of mesh points. Reflections only hold what is on screen. Without a graphics
  card, antialiasing, shadows, reflections, glow and fog are off. See [3D scenes](../visuals/3d-scenes.md).
- **Distribution:** macOS packages lack Developer ID signing and notarization.
