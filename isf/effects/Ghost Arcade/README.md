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

**Not ported (17)** — they need what ISF does not provide (WebGPU compute, 3D meshes, a camera mask):

- `gpuFluidSim`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `eulerianMagnify`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `blobTrack`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `blobContour`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `blobHeatmap`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `stringOrb`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `sphereWireframe`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `voxelCubeCluster`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `mobiusLattice`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `crystalShardField`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `tubeLattice`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `discoMirrorBall`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `lissajousKnot`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `helixParticleStream`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `donutConstellation`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `geometricTilePro`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `auraField`: needs other textures: uNativeMask
