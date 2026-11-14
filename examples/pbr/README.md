# PBR

Open `PBR.nyxmap`. The startup sequence plays the **PBR Wave, Light & Camera** timeline. Choose **PBR Scene (3D view)** in a preview's source menu to see the animated rim light and the spot cone.

- **Wave** in **Controls** shows the four-second sine. The 24 Merge 3D groups each move a row of 12 spheres together. Each row maps the signal to its Y position with a different delay.
- **Wave Amplitude** scales the lift of all 288 spheres. At 0 they sit on the floor. At 1 the wave has its full height. The orange point light moves side to side throughout.
- The camera follows a 24-second loop from a wide view into a low tracking shot, then rises above the grid.
- Right-click a slider in **Controls** and choose **Animate → Timeline** to create a track, add the slider to a timeline, or open an existing timeline driver.

All 288 spheres use the built-in Sphere primitive and share an 18-material palette. The diamond plate and wood materials were imported from glTF 2.0, with base color, normal, occlusion, roughness and metallic maps stored in the project.

To reuse a glTF surface on a built-in primitive, open **Materials**, choose **Import glTF…**, and pick a material from a `.gltf` or `.glb` file. Select the primitive and click **Assign to selected 3D object**. The imported maps are stored in the project. The importer uses the metallic-roughness surface and its first UV set. The project material does not reproduce glTF transparency, per-texture transforms, or advanced material extensions.

The wave mappings use named filters. Double-click a filter name to edit its label, or clear the name to display its type again.

The bundled 1K maps are from Poly Haven and released under CC0. The two glTF files and the `textures` folder are included as material import samples. [Metal Plate](https://polyhaven.com/a/metal_plate) · [Wood Table 001](https://polyhaven.com/a/wood_table_001) · [CC0 license](https://polyhaven.com/license)

```bash
NyxMap --render preview.png --frames 3 --strict PBR.nyxmap
```
