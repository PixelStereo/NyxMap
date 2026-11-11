# Camera Inversa

A one-minute audiovisual score for NyxMap. Steel, smoke and an impossible aperture cut against asymmetric bass, dry contact noise and bowed-metal grains.

Open **Camera Inversa.nyxmap**, then press **GO** once in **Show Control** (Shift+6) to run **Reference (1:00)**. For performance, select **Live GO** and fire the ten animated cues yourself. Each new cue interrupts the previous motion. Use **Nero** to cut both picture and sound.

| Time | Cue | Cut |
| --- | --- | --- |
| 0:00 | Fessura | A horizontal slit opens onto steel and smoke. |
| 0:05 | Diaframma | The frame turns square and an iris rushes the lens. |
| 0:12 | Rilievo | The camera jumps wide. The ribs swing into the foreground. |
| 0:19 | Negativo | White replaces black. Fins close across the frame and the bass drops out. |
| 0:25 | Segnale rotto | RGB geometry shears apart over an irregular bass and contact-noise pattern. |
| 0:33 | Vuoto | Three seconds of black and silence. |
| 0:36 | Camera Inversa | The relief returns as a cavity. The camera accelerates through its lip. |
| 0:45 | Caduta | The iris tumbles off-axis and debris cuts across it. |
| 0:53 | Impronta | A vertical aperture breaks open into the last trace of smoke. |
| 1:00 | Nero | End. |

In **Controls** (Shift+5), adjust the aperture level, camera orbit, relief depth, smoke, audio mix and grain shape during a cue. **Smoke turbulence**, **Grain transposition**, **Grain space**, **Rib torsion** and **Strike / hold** keep their settings across cues. Controls are also available over OSC at `/control/<key>`, for example `/control/depth` or `/control/twist`.

The image uses native 3D geometry and lights, with `smoke.fs` from the standard ISF library as its sole shader. The audio is synthesized in NyxMap. Keep the `audio/` folder alongside the project. Regenerate both with `python3 examples/camera-inversa/source/build_show.py` from the repository root.
