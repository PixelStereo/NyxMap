# Ghost Arcade effects

ISF 2.0 versions of the GLSL effects of [Ghost Arcade](https://github.com/riskcapital/ghost-arcade) by
riskcapital, generated from its source at commit `a2c62b104fab` by `scripts/port_ghost_arcade.py` and checked to compile.

**Licence.** Ghost Arcade is under the GNU AGPL v3. These shaders are adapted from its code and stay under the
GNU AGPL v3, like NyxMap. Each file says so in its header (`CREDIT`, `LICENSE`).

**What changed in the adaptation.** The layer texture is the ISF `inputImage`; Ghost Arcade's effect parameters are
ISF inputs with its ranges and labels; uniforms Ghost Arcade sets from code become inputs with a range around their
default, except the ones its engine drives from audio, which are fixed at their default. Names that desktop GLSL
reserves are renamed. The effect logic is unchanged.

**Feedback effects.** The effects that read their own previous output (Feedback Zoom, Reaction Diffusion, Pixel
Sand…) draw into a persistent ISF buffer, `feedbackBuffer`, which holds the previous frame when read, then show it
in a second pass. `uHasFeedback` is false on the first frame, as in Ghost Arcade.

**3D effects.** The Geometric 3D and Advanced 3D effects (String Orb, Disco Mirror Ball, Tube Lattice, Lissajous Knot,
Geometric Tile Pro…) map the layer onto 3D objects by ray casting in the fragment shader: they are ISF filters like
the others, with Ghost Arcade's shared GLSL blocks inlined.

**Not ported (3)** — no GLSL shader to adapt:

- `gpuFluidSim`: WebGPU compute fluid simulation
- `eulerianMagnify`: temporal band-pass over many previous frames, run outside the shader
- `auraField`: needs the person-segmentation mask of the phone camera
