# Ghost Arcade effects

ISF 2.0 versions of the single-pass effects of [Ghost Arcade](https://github.com/riskcapital/ghost-arcade) by
riskcapital, generated from its source at commit `a2c62b104fab` by `scripts/port_ghost_arcade.py` and checked to compile.

**Licence.** Ghost Arcade is under the GNU AGPL v3. These shaders are adapted from its code and stay under the
GNU AGPL v3, like NyxMap. Each file says so in its header (`CREDIT`, `LICENSE`).

**What changed in the adaptation.** The layer texture is the ISF `inputImage`; Ghost Arcade's effect parameters are
ISF inputs with its ranges and labels; uniforms Ghost Arcade sets from code become inputs with a range around their
default, except the ones its engine drives from audio, which are fixed at their default. Names that desktop GLSL
reserves are renamed. The effect logic is unchanged.

**Not ported (28)** — they need what a single ISF pass does not provide:

- `gpuFluidSim`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `eulerianMagnify`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `blobTrack`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `blobContour`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `blobHeatmap`: no standalone GLSL shader (WebGPU, multi-pass or generated in code)
- `bloom`: passes the texture to a function
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
- `feedbackZoom`: needs other textures: uFeedback
- `opticalFlowDatamosh`: needs other textures: uFeedback
- `reactionDiffusion`: needs other textures: uFeedback
- `pixelSand`: needs other textures: uFeedback
- `laserSlice`: needs other textures: uFeedback
- `auraField`: needs other textures: uNativeMask
- `glitchQuilt`: needs other textures: uFeedback
- `cellularAutomataBurn`: needs other textures: uFeedback
- `analogFeedbackRack`: needs other textures: uFeedback
- `mirrorShards`: needs other textures: uFeedback
- `ghostExposure`: needs other textures: uFeedback
