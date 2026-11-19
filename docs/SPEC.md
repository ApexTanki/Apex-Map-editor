# AlternativaEditor → Apex Editor (OpenGL 3.3 Core port): reverse-engineered specification

Source of truth: the ActionScript 3 / Flex AIR project in `../src` (Alternativa3D 7.11).
This document records **what the original does**, so the C++ port can match it.
Notation: `file.as:symbol` points at the original implementation.

---------------------------------------------------------------------------------------------------
## 1. Coordinate system, units, camera

* World: right-handed, **Z up**. Units are game units (1 grid cell = 500 × 500 × 300).
* Object transform (Alternativa `Object3D`): `M = T(x,y,z) · Rz · Ry · Rx · S(sx,sy,sz)`.
  Props only ever use `x,y,z,rotationZ` (+ `scaleX/Y/Z` for bonus regions and kill boxes).
* Camera space (Alternativa): X right, **Y down**, Z forward. Pixel offset of a point = `x·f/z`,
  with focal length `f = sqrt((w/2)² + (h/2)²) / tan(fov/2)`, `fov = π/2` (a *diagonal* FOV),
  near = 1, far = 1 000 000.
* Camera rig (`CursorScene.initControllers`): `root → container → camera`.
  * `container` = pivot. It has its own position and `rotationX/rotationZ` (pitch/yaw), driven by
    `containerController` (a `WalkController`).
  * `camera` is a child with local position = `cameraController.coords` and local
    `rotationX = -120°` (looks along +Y, 30° down). Initial local position `(250, -7800, 4670)`.
* `getCameraFacing()` (`EditorScene.as`) classifies the camera's world X axis (`a` = Xx, `e` = Xy):
  `a > 1/√2 → Y`, `a < -1/√2 → -Y`, else `e > 0 → -X`, else `X`.
  `CameraFacing(sin,cos)` rotates screen arrow vectors into world: X(-1,0), Y(0,1), -X(1,0), -Y(0,-1).

### Camera controls (`SceneContainer.as`, `WalkController.as`, `ObjectController.as`)
| Input | Effect |
|---|---|
| Middle **or right** drag (no Alt) | Pan: camera position += camX·10·(prevX−x) + camY·10·(prevY−y), in world space, then converted to container-local |
| Alt + middle/right press | Orbit start: pivot = centre of the selected props' footprint (`getPropsGroupCenter`) at the selected prop's z, else `camera.localToGlobal(0,0,cameraDistance)`. The container moves to the pivot and the camera's world position is preserved. Then mouse-look rotates the container while held |
| Mouse-look | `rotZ = startRotZ + (startMouseX − mouseX)·π/360`, `rotX = startRotX + (startMouseY − mouseY)·π/360`, container rotX clamped to ±90° |
| Wheel | `zoom(±3)`, with Shift `±20` |
| Numpad + / −, Ctrl+= / Ctrl+− | `zoom(±3)` |
| `zoom(step)` | Toward the selected prop (if any) or 1000 units along the view direction. Move distance = `min(len−10, step·100)` along that direction (camera-local coords) |
| N / M | Container yaw left/right at 1 rad/s |
| Numpad 9 / 3 | Container pitch down/up at 1 rad/s (±90° clamp) |
| Numpad 4/6/8/2 | Container moves left/right/forward/back in the horizontal plane by its heading. Speed 2000 u/s; velocity accumulates by `speed` per frame up to `speed`, decays `×3^(−10·dt)` when released |
| Ctrl+0 | Camera local rotationX = −120°, local position (250,−7800,4670); container untouched |

`cameraDistance` = length of camera-local position (recomputed on Ctrl+0 and at start).

---------------------------------------------------------------------------------------------------
## 2. Grid and snapping (`EditorScene.as`, `Prop.as`)

* Constants: `hBase = 250`, `hBase2 = 500`, `vBase = 300`; H-res 500 / half 250; V-res 300 / half 150.
* `snapByHalf` toggled by **K** (global).
* Grid plane: 7500 × 7500 centred at (250,250,0); lines every 500 u, 10 u thick, colour `#909090`,
  15 lines per axis; it is not pickable. Toggled by the toolbar button, menu, or **G**.
* `Prop.snapToGrid()`:
  * half mode: `x = floor(x/250)·250`, `y` same, `z = floor(z/150)·150`
  * full mode: if not `multi`: subtract 250 from x and y; `x = ceil(x/500)·500`, `y` same;
    `z = floor(z/300)·300`; if not multi: add 250 back.
* Footprint `distancesX/Y/Z` (`Prop.calculate`): over all vertex pairs, find the pair with max
  squared distance on each axis, then `calcDistance(pos, minV, maxV, base)` (floor to base) →
  `(lo, hi)` offsets relative to the prop position. `height` = max Z extent.
  `multi = |dx|/500 > 1 || |dy|/500 > 1`.
  Spawn/flag/control-point footprints are fixed: X,Y ∈ [−250,250], Z ∈ [0,300]. Sprites have zero footprint.

### Occupancy map (`OccupyMap.as`)
* A sparse map of cells keyed by (x, y, z) world coordinates. Steps: 250 in X/Y and 300 in Z,
  iterating from `pos+lo` while `< pos+hi`.
* `occupy(prop)` only if `prop.free`, then sets `free=false`. `free(prop)` is the reverse.
* `isConflict(p)`: some cell holds another prop, or holds more than one prop.
* `isConflictGroup(p)`: some cell holds another prop with the same `groupName`.
* `getConflictProps()`: every prop in a cell holding more than one prop (key **Q** selects them).
* Intersection mode (Options menu): ANY (no checks), GROUP (same group denied), NONE (all denied,
  the default). On a conflict after add/move/copy (snap on) the app asks
  "location occupied, keep?" (Yes/No); **No** undoes the last journal entry without making it redoable.

---------------------------------------------------------------------------------------------------
## 3. Props (`prop/*.as`)

| Class | type | Notes |
|---|---|---|
| `MeshProp` | TILE(1) | 3DS mesh, `bitmaps` = named texture set, `textureName`, collision meshes (non-`occ*` objects of the 3DS), `collisionEnabled` (default true) |
| `Sprite3DProp` | TILE(1) | Screen-aligned billboard, `100·scale` world units, origin (originX, originY) of the image, texture "DEFAULT" |
| `SpawnPoint` | SPAWN(2) | Rotates by ±5° per X/Z key press |
| `CTFFlagBase` | FLAG(4) | red_flag / blue_flag |
| `ControlPoint` | DOM_CP(5) | Box 100×100×400 magenta; letter A–Z, game modes ⊂ {dom, sge}; linked spawn points |
| `FreeBonusRegion` | BONUS(3) | Box 500×500×300 with min corner at the origin, colour `#FFAF00`, alpha 0.5, plus a "shadow" plane projected onto the scene below; bonus types and game modes; `parachute` |
| `KillBox` | KILL(6) | Box 500×500×300, colour `#F010F5`, alpha 0.5; action ∈ {kill, kick, block} |

* Region and kill-box rotation is replaced by swapping scaleX/scaleY on multiples of 90°.
  `min = position`, `max = position + base·scale`.
* Materials: default texture; **selected** = texture multiplied by the selection mask (`_assets/73.jpg`,
  scaled by `width/maskWidth`); **no-collision** = texture with `no_collision_texture.png` drawn over
  it, stretched; **invisible** texture name `"invisible"`.
* Cursor ghost: alpha 0.5. Its material is overridden by a flat green `#00FF00` (free) or
  red `#FF0000` (conflict) fill. For sprites it is the texture hard-lit by the green/red mask.
  With snapping off it shows its real material.
* Collision view (Ctrl+2 / toolbar): mesh props hide their visual mesh and show collision meshes in
  flat `#FF7F7F`. Collision-mesh naming: `plane*` → rect, `box*` → box, `tri*` → triangle; `occ*` ignored.
* Plane bounds (**T** / **Shift+T**): for mesh props with `height == 0`, show a blue footprint outline.
* Bound boxes overlay (Ctrl+B): green 2D projection of each visible mesh prop's AABB.

### Functional library (`FunctionalProps.as`) – library name `Functional`
| Group | Props |
|---|---|
| `Flags` | `blue_flag`, `red_flag` (flag.3ds + blue/red jpg) |
| `special-geometry` | `special-geometry` (KillBox) |
| `Spawn Points` | `dm` (yellow `#E2BE01`), `blue` (`#0C01FF`... `786943` = `#0C00FF`), `red` (`#E44700`) — spawn.3ds |
| `Bonus Regions` | `free-bonus` |
| `Domination` | `dom-keypoint` (ControlPoint), `dom`, `dom-red`, `dom-blue` spawns |

Layers (`FunctionalProps.getPropLayer`): spawn `dm` → `dm`; other spawns and flags → `tdm`;
Domination → `dom`; bonus regions → `bonuses` plus one layer per enabled game mode; special → `special`.
The Layers window (L) toggles `dm, tdm, dom, bonuses, special`. A prop is visible if any of its layers is visible.

---------------------------------------------------------------------------------------------------
## 4. Selection and editing (`SceneContainer.as`, `MainScene.as`)

Mouse in the workspace (LMB):
* **Press on a selectable prop** (no Ctrl): Shift → add (Shift+Alt → remove); otherwise if not
  selected → deselect all + select; if already selected → make it the main prop. Sets `propMouseDown`
  and hides the cursor ghost.
* **Drag** (after 20 px Manhattan movement) with `propMouseDown`: moves the selection on the
  horizontal plane through the main prop's z (`projectViewPointToPlane`). With **V** held it moves
  vertically on the X or Y plane depending on camera facing. Snaps if snap is on (mesh props
  always snap). **Shift** at drag start: duplicate the selection (COPY) and drag the copies.
* **Drag on empty space** (>3 px both axes): rubber-band rectangle. Plain → replaces the selection
  with props whose *origin* projects inside the rectangle. Shift → adds on release; Shift+Alt → removes on release.
* **Click on empty space** (<3 px): moves the cursor ghost to the mouse on the plane z = ghost.z,
  snaps, shows the ghost and deselects everything.
* **Ctrl+click** spawn `dom`, then Ctrl+click a control point → link (green lines drawn while the dom layer is visible).

Keys: **Space** paste the prefab at the ghost (or apply the selected texture when the property panel is
open); arrows move the ghost or selection by one cell along the camera-facing axes; **W/S** move up/down
one vertical cell; **Z/X** rotate CCW/CW by 90° (45° in half mode) around the group centre (spawn: ±5°);
**Delete/C** delete the selection (the ghost jumps to it); **Esc** deselect (the ghost jumps to the main
prop); **Q** select conflicting props; **K** toggle half grid; **T/Shift+T** plane bounds.

Undo journal (`EventJournal.as`): ADD, DELETE, ROTATE(bool cw), MOVE(delta), COPY. Undo/redo of MOVE
applies `-=delta` then negates the delta. ROTATE flips the stored direction. Adding a new event clears redo.

Selectable-type filter (toolbar combo): ALL / TILE (MeshProp) / SPRITE / FUNCTIONAL.
Hide selected / Show all buttons. Property panel toggle (textures icon) shows the bottom panel
(textures | bonus | kill | control point) and the left "prop parameters" panel (name, group, library,
collision checkbox, position X/Y/Z, rotation Z in degrees).

---------------------------------------------------------------------------------------------------
## 5. Prop libraries (`LibraryManager.as`, `PropsLibrary.as`, `MeshLoader.as`, `SpriteLoader.as`)

* Load from a directory: recursively find folders containing `library.xml`.
* Optional `images.xml`: `<image name="x" new-name="y" alpha="z"/>` redirects texture files.
* `library.xml`: `<library name>` → `<prop-group name>` (only first-level groups are used) →
  `<prop name>` with either
  * `<mesh file object?><texture name diffuse-map opacity-map?/>*</mesh>`. Without `<texture>`, the
    first 3DS texture material is used as "DEFAULT".
  * `<sprite file alpha? origin-x=0.5 origin-y=1 scale=1/>`
* An opacity map's **red channel** becomes the alpha (resized to the diffuse size if different).
  A missing texture is replaced by a 20×20 magenta/black checker and reported.
* Prop key = `library + group + name`. Recent directories (max 3) are persisted.
* Drag & drop: a single `.xml` file → import map; folders → load libraries.

### 3DS (`Parser3DS.as`)
Chunks: 0x4D4D main, 0x3D3D scene, 0xAFFF material (name 0xA000, colour 0xA020, diffuse map 0xA200,
opacity map 0xA210 → filename 0xA300 (lower-cased), scale/offset/rotation 0xA354/56/58/5A/5C), 0x4000
object → 0x4100 trimesh → 0x4110 verts, 0x4140 uvs (v = 1 − v), 0x4160 matrix, 0x4120 faces → 0x4130
face material, 0x4150 smoothing. 0xB000 keyframer: nodes 0xB002.. → 0xB010 name/parent,
0xB013 pivot, 0xB020 pos, 0xB021 rot (angle-axis), 0xB022 scale.
With keyframer data, vertices are multiplied by the inverse mesh matrix minus the pivot, and the object
transform comes from the keyframes. Object order follows keyframer node order.

---------------------------------------------------------------------------------------------------
## 6. Map files

Import (`TanksXmlImporter.as`): `<map version>` → "1.0.Light" (V1), "1.0" (V1Full = V1 + kill zones),
"2.0", "3.0" (others fall back to 1.0 with an alert). Tiles are processed in batches of 100 with
progress. Unknown props are reported in the error window.

V1 schema (export `TanksXmlExporterV1Lite/Full`):
```xml
<map version="1.0.Light|1.0">
  <static-geometry>
    <prop library-name group-name name [free="true"] [noCollision="true"]>
      <rotation><z>%.6f</z></rotation><texture-name>..</texture-name>
      <position><x>%.3f</x><y/><z/></position></prop>
  </static-geometry>
  <collision-geometry> collision-plane|collision-box|collision-triangle (id=0, %.3f, rot %.6f) </collision-geometry>
  <spawn-points><spawn-point type [free]><rotation><z/></rotation><position/></spawn-point></spawn-points>
  <bonus-regions><bonus-region name parachute [free]><rotation/><min/><max/><position/><bonus-type/>*<game-mode/>*</bonus-region></bonus-regions>
  <special-geometry><special-box type [free]><minX/>..<maxZ/><action/></special-box></special-geometry>  (Full only)
  <ctf-flags><flag-red><x/><y/><z/></flag-red><flag-blue/></ctf-flags>
  <dom-keypoints><dom-keypoint free name modes="dom,sge"><position/><spawn-point/>*</dom-keypoint></dom-keypoints>
</map>
```
Collision primitives are computed per prop type and cached (`CollisionRect/Box/Triangle.as`), then
transformed by the prop matrix; rotations come from `Matrix4.getRotations`.
Also: V3 export, HTML5 binary export (`MapBinaryExporter`), and batch processing (re-export every map in a folder).

---------------------------------------------------------------------------------------------------
## 7. UI layout (`AlternativaEditor.mxml`)
```
┌ Menu: File | Edit | View | Library | Options | Help ─────────────────────────────┐
├ ControlBar 26px: [snap][collision][grid][properties][hide sel][show all] "Select:" [ALL▼]
├──────────────────────────────────────────────────────┬─────────────── 300px ──────┤
│ (Prop params 200px, when properties on) │ Workspace  │ Preview panel (300px high) │
│                                         │ (3D view,  ├────────────────────────────┤
│                                         │ axis gizmo │ [library combo]            │
│                                         │ bottom-left│ Props: accordion of groups │
├─────────────────────────────────────────┴────────────│ with 50×50 icon tiles      │
│ Properties panel 200px (textures / bonus / kill / CP)│                            │
└──────────────────────────────────────────────────────┴────────────────────────────┘
```
Menu shortcuts: Ctrl+N new, Ctrl+I import, Ctrl+A append, Ctrl+T export Flash V1, Ctrl+Z/Y undo/redo,
Ctrl+Alt+C clear, Shift+A select all, Ctrl+1/2 visual/collision, Ctrl+B bound boxes, G grid, L layers,
Ctrl+Alt+L clear & load library, Ctrl+L load library, N snapping. Languages from `localization.json`.
Preview: the camera orbits the prefab at 1°/frame. Icons: a 50×50 render at a 135° yaw.
