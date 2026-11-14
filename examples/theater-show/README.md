# Theater Show

The same set first reads as an archive corridor. Its practical lights fail, a blade of light finds the seam in the shutters, and the wall opens on a dark basin. First light reveals the landscape. The final image leaves the performer in front of the horizon.

Open **Theater Show.nyxmap**, select **Theater Show · Manual GO** in the sequencer and show the composition output. The project opens at blackout. Press **GO** once for each row.

| GO | Cue | Standby |
| --- | --- | --- |
| 01 | Line check | Check the six Zone outlines against the set |
| 02 | House out | House clear |
| 03 | The archive | Performer’s first step |
| 04 | Power failure | Hand touches the wall |
| 05 | The wall parts | Performer steps back |
| 06 | First light | Performer turns out |
| 07 | The last image | After the final line |
| 08 | Final blackout | Stage manager’s call |

Six Zones map two shutters, two angled wings, the lintel and the apron. On each shutter, the archive and the moving light have separate layers linked to the same Zone. **The wall parts** recalls a 2.6-second Zone pose while the rear plate appears. **Power failure** starts a snapshot and a light sweep in a single GO. All six archive layers use the same **Archive Atlas.jpg**, a 3840 × 2160 image. Select an **Archive** layer and open **ROI** to see its slice of the image. The ROI selects the source pixels. The Zone places and warps that slice on the set. The shader supplies the calibration grid, light sweep, landscape and reflection.

The project uses NyxMap’s Mapping Zones. Keep `Archive Atlas.jpg` and `Scenic Archive.fs` beside the project. Select a Zone row to align its corners with the physical set. **Line check** recalls the shutters’ closed pose. Update **Shutters · closed** and **Shutters · open** after adjusting their horizontal positions for a different set.

References for the use of architectural depth and projection on moving scenery.

- [The Lehman Trilogy · Luke Halls Studio](https://lukehalls.com/case-studies/the-lehman-trilogy-nyc/)
- [Courville · VYV and Ex Machina](https://www.vyv.ca/portfolio/ex-machina-courville-2/)
