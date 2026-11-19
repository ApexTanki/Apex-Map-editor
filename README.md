# Apex Editor

> A remake of the Alternativa3D map editor written in C++ with an OpenGL renderer.

A native **OpenGL 3.3 Core** rewrite of the Tanki Online map editor
([alternativa-editor-mod](https://github.com/tubixpvp/alternativa-editor-mod), ActionScript 3 / Adobe AIR).
It assembles maps from prop libraries (tiles, sprites, spawn points, flags, bonus regions,
kill zones, domination points). The goal is to keep the original's interface, controls,
grid/snapping rules and file formats while dropping the AIR runtime.

Written in C++17 with GLFW, Dear ImGui, pugixml, nlohmann/json and stb.

## Download

Get `ApexEditor-<version>-win64.zip` from the **Releases** page, unzip it and run
`ApexEditor.exe`. Keep the `assets` folder next to the exe. It needs Windows 10/11
and a GPU with OpenGL 3.3. No extra runtime is required.

## Features (phase 1)

- 3D workspace with the original camera rig: pan, orbit, zoom, keyboard flight and Ctrl+0 reset
- Prop libraries: recursive folder loading (`library.xml`, `images.xml`, 3DS meshes, sprites,
  opacity maps), recent folders, drag & drop. Loading runs in parallel and decodes each
  texture file only once.
- Built-in functional library: spawn points, CTF flags, bonus regions, kill boxes and
  domination points (Ctrl+click linking)
- Selection (click, Shift/Alt modifiers, rubber band), drag move, Shift-drag copy, vertical
  move, 90°/45° rotation, delete, hide/show, select-all, conflict selection
- Grid snapping (full and half cell) and the occupancy map with the three intersection modes
- Undo/redo journal
- Collision-geometry view, bound boxes, plane bounds, layers window
- Property panels: textures, bonus regions, kill boxes, control points, plus prop parameters
- Map import and export in XML **1.0** and **1.0.Light**, including collision primitives
- English / Russian UI (`assets/localization.json`)

Not yet ported: import of XML 2.0/3.0 maps (they fall back to the 1.0 importer with a warning),
XML 3.0 and HTML5 binary export, and batch processing. See `docs/SPEC.md` for the full
reverse-engineered behaviour of the original.

## Controls

| Input | Action |
|---|---|
| Left click on empty space | Place the cursor prop / clear the selection |
| Space | Paste the selected library prop at the cursor |
| Click / Shift+click / Shift+Alt+click | Select / add / remove |
| Drag, arrows | Move the selection (or the cursor) |
| Shift+drag | Copy and move |
| V+drag, W, S | Move vertically |
| Z / X | Rotate counter-clockwise / clockwise |
| Delete, C | Delete the selection |
| Esc | Clear the selection |
| K | Toggle half-cell snapping |
| Q | Select conflicting props |
| T / Shift+T | Show / hide plane bounds |
| Middle or right drag | Pan |
| Alt + middle/right drag | Orbit |
| Wheel, Ctrl+= / Ctrl+-, numpad +/- | Zoom |
| Ctrl+0 | Reset the camera direction |
| Ctrl+click spawn `dom`, then Ctrl+click a control point | Link a domination spawn |

The menu shortcuts are the same as in the original (Ctrl+N, Ctrl+I, Ctrl+T, Ctrl+Z/Y, G, L, N, ...).

## Building

Requirements: CMake 3.20+ and Visual Studio 2022 (MSVC). Dependencies are downloaded by CMake.

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\ApexEditor.exe
```

The `assets` folder is copied next to the executable after each build.

### Command-line options

```
--lib <dir>            load prop libraries under <dir> (repeatable)
--map <file.xml>       import a map after the libraries are loaded
--select lib/grp/prop  put a library prop on the cursor
--collision            start in collision-geometry view
--export <file.xml>    export the scene as XML 1.0 and quit
--screenshot <png>     save a screenshot after loading and quit
```

## Project layout

```
src/gl/       OpenGL 3.3 function loader
src/core/     math (Alternativa-compatible Matrix4), utilities, error log
src/io/       3DS parser
src/scene/    props, occupancy map, layers, undo journal, camera rig, picking, main scene
src/lib/      library manager (parallel loader) and the functional library
src/map/      XML import/export and collision primitives
src/editor/   workspace input handling (port of SceneContainer)
src/render/   textures, framebuffers, scene renderer
src/ui/       ImGui application shell, localization, settings, native dialogs
docs/SPEC.md  reverse-engineered specification of the original editor
```

## License

MIT (see `LICENSE`). The files in `assets/` come from the original AlternativaEditor project
and remain the property of their respective owners.
