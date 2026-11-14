# 3D scenes

A **3D Scene** (**Layer › New 3D Scene**, or a model dropped onto a layer) is a group whose picture is its models seen by its camera, under its lights. It holds only 3D objects, and they stay in it. Each object is a layer: shown, soloed, animated and recalled like any other. Its values are parameters (`model/…`, `light/…`; the scene's `camera/…`, `scene/…`), so they can be driven by animations, interactions, timelines and [OSC](../reference/osc.md). See `examples/pbr/` in the repository.

## Models and primitives

- **Files:** FBX, glTF/GLB and OBJ (read with ufbx and cgltf).
- **Primitives** from **Media Bin › 3D Primitives**: cube, open cube, plane, disc, cylinder, open cylinder, cone, pyramid, sphere, dome, torus, capsule — all unwrapped.

A primitive starts with as few triangles as its shape needs (a cube is 12, a plane 2, a sphere 576). `model/detail` (1–8) cuts it finer; a bend or a displacement take what they need on their own.

A model can:

- show another layer's picture over its UVs (its 3D tab › **Surface › Picture**; lit, or unlit: as it is);
- bend across its width (`model/bend`: a curved screen);
- have its points pushed along their normals by a layer's luminance (**Surface › Displace**; `model/displace/amount`, `model/displace/mid`).

`model/shading` shows a model as made, smooth (its normals averaged where faces meet) or flat (its facets show).

### Instances

An **Instance** (**Layer › New Instance of the Selected Model**) shows another model's meshes and material: a primitive, an FBX, an OBJ or a glTF. Its triangles are not built or loaded again, and editing the original (its material, picture, displacement, shading or detail) changes every instance of it. An instance has its own place (Spatial tab), its own 3D FX (Transform 3D, Follow Path) and, with **Own Material** (`model/instance/own_material`), its own colour, metal and roughness. An instance of an instance shows the original; its Source tab sets which model it shows.

## Materials

Lighting is physically based, in linear light, with ACES tone mapping.

- glTF and FBX materials use their base color, metal, roughness, normal, occlusion and emission maps, alpha cut-out and blending (blended surfaces are not sorted).
- A primitive's material is set by `model/material/color`, `model/material/metal` and `model/material/rough`.

## Lights

Up to 8 lights per scene: directional, point, spot and area.

- A spot's `light/falloff` goes from a sharp edge (0) to a gradient from its middle (1).
- An area light is a rectangle lit on the side it faces, soft-shadowed as wide as it is.
- A light can look at a point or follow a 3D object of its scene (`light/lookat`, instead of its yaw and pitch).
- A light can travel around a circle or a rectangle in 3D (`light/path/…`): its position is the shape's middle, `light/path/rotation/…` tilts it, `light/path/position` places it on the way (0 to 1) and `light/path/speed` moves it on its own.

## Shadows and effects

The models cast shadows (`light/shadow`, `light/softness`) from up to four spots and directional lights and two point lights, all around those, except what is far larger than the others (a floor, a wall), which only receives them. Shadows are sharp where what stops the light is near, soft where it is far. Their maps are drawn again only when something they show moves.

- `scene/ao` and `scene/ao_radius` darken where surfaces meet.
- `scene/reflections` shows the other models in smooth surfaces. Both work from what the camera sees: a reflection only holds what is on screen, and fades on surfaces that face the camera.
- Without an environment, `scene/studio` gives the models a photo studio's panels to reflect.

Each scene has a look (**Scene** tab): a glow around what shines above its threshold, contrast, saturation, temperature and a vignette; and fog, with its density, height and reach, in which the lights show as beams stopped by the models (each light's **In the Fog**).

## Environment and HDR

A scene's environment is a layer's picture — an HDRI (`.hdr` or `.exr`, loaded in an image layer that can stay hidden), a video or a generator — equirectangular, taken at any stage of its FX (**Scene tab › Environment**). It lights the models (diffuse, and reflections as sharp as the surfaces are smooth) and can be shown behind them (`scene/environment/intensity`, `rotation`, `show`, `blur`).

An HDR image layer (`.hdr`, `.exr`, half or float) keeps its file as read, linear and unclipped: its **HDR file** stage (chosen by default) lights the scene with its full range (a sun far above 1), while its own picture in the composition stays clipped to 0–1 like any layer's (8 or 16-bit normalized). The environment's levels are blurred copies, not convolved.

## Path

A **Path** (**Layer › New Path**, or **Media Bin › 3D Primitives › Path** dropped onto a scene) is a set of points laid out by a rule, as a SOP in TouchDesigner: **circle**, **rectangle**, **line**, **grid**, **spiral**, **sphere** or **random** (in a box). Its 3D tab sets its kind, how many points (`path/points`; a grid's `path/columns` and `path/rows`), its size (`path/width`, `depth`, `height`), a spiral's turns, the random points' seed, and its place (`path/position`, `rotation`, `scale`), under its Merge 3Ds like any 3D object.

It is drawn in the 3D view (its points and lines), not in the scene's picture; **Shown in the 3D View** hides it there too.

A circle, a rectangle, a line or a spiral is also a **path**, which 3D objects travel along with the **Follow Path** 3D FX. A closed path (circle, rectangle) goes around; an open one (line, spiral) goes to its end and back.

## Follow Path (3D FX)

A model, a Merge 3D or an Empty, a camera or a light has a **Follow Path** 3D FX (its **FX** tab): choose a Path of the scene, turn it on (`follow/enabled`), place the object on it (`follow/position`, 0 to 1) and set it moving (`follow/speed`, turns per second). **Turned Along It** (`follow/orient`) turns the object to face the way it goes. A camera or a light sits on the path's point; a model's or a Merge 3D's own transform applies from there, so a Merge 3D on a path carries everything it holds.

## Cameras

A **Camera** (**Layer › Add Camera to 3D Scene**, or **Media Bin › 3D Primitives › Camera** dropped onto a scene) is a 3D object: a scene can have several. The scene is seen through the one chosen in its Scene tab › **Seen Through** (the first camera added is chosen), or through its own orbit camera. A camera looks at a point or at any 3D object of its scene (**Look At** › Object: an Empty, a primitive, an FBX or OBJ model, a light…), or points by its yaw and pitch (`camera3d/…`), and can follow a Path with its Follow Path FX. The 3D view draws every camera, the one the scene is seen through brighter.

## Empty

An **Empty** (**Layer › New Empty**, or **Media Bin › 3D Primitives › Empty**) is a Merge 3D: a point of the scene with its axes, to place objects under, for a light or a camera to look at, or to carry objects along a Path.

## Merge 3D

A **Merge 3D** (**Layer › New Merge 3D**) is a transform for what it holds, as in a node graph: models, lights and other Merge 3Ds, in a 3D scene only. It is not rendered: its scene draws what it holds, moved, turned and scaled by `merge/position`, `rotation` and `scale`, its own and those above it. A hidden one hides all it holds.

## The 3D view

A preview cell showing a 3D scene (its source menu: "… (3D view)") is a 3D view with its own camera:

| Action | Input |
|---|---|
| Turn | Drag |
| Pan | Shift-drag or middle button |
| Distance | Wheel |
| Frame everything | H or Home |
| Frame the selection | F |
| Move / turn / scale along X, Y, Z | W / E / R, then the handles (one undo step each) |

It draws the ground grid, the scene's lights and camera, and its models in flat colours with their edges (**Simple**, the lightest) or with their colours and pictures but no light on them (**Unlit**), chosen by its button. The scene's own picture is in the output, as it will be seen. A click picks a model or a light. **View from Camera** and **Camera = View** exchange the view's and the scene's cameras.

Lights are drawn too: a lamp's head and its cone for a spot (down to the ground, its falloff dashed inside), a sun with parallel rays for a directional light, a bulb for a point light. A light's look-at target is a dashed line, its path is drawn, and a Merge 3D is a diamond with a line to each thing it holds.

A scene shows its triangle count in the layer list, its inspector and its 3D view: those its last frame computed, in all its passes. What the camera or a light does not see is not drawn, and a scene where nothing moves keeps its picture (except with a layer's picture on a model or around the scene, which can change at any frame).

## Quality

The Composition tab's **3D Quality** lightens all the scenes of a project when needed: `quality3d/antialiasing` (off, 2x, 4x, 8x), `shadows`, `contact` and `reflections` (off, low, medium, high), plus glow and fog.

Without a graphics card (a renderer running on the processor), a scene leaves out antialiasing, shadows, contact shadows, soft shadows' searches, reflections, glow and fog.

## Not yet supported

- The file's animations (skeletons, keyframes) and cameras.
- Sorting of transparent surfaces.
- Editing a mesh's points by hand.
