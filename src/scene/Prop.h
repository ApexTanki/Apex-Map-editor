// Port of alternativa.editor.prop.* (Prop, MeshProp, Sprite3DProp, SpawnPoint, CTFFlagBase,
// ControlPoint, FreeBonusRegion, KillBox). One class with a `kind` instead of a class hierarchy.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Math.h"
#include "render/Texture.h"
#include "scene/Mesh.h"

namespace ae {

enum class PropKind { Mesh, Sprite, Spawn, Flag, ControlPoint, Bonus, Kill };

// Prop.type values used by the exporters.
enum PropType { TYPE_TILE = 1, TYPE_SPAWN = 2, TYPE_BONUS = 3, TYPE_FLAG = 4, TYPE_DOM_CP = 5, TYPE_KILL = 6 };

// Data shared by a library prototype and every clone of it.
struct PropModel {
    MeshRef mesh;                     // main visual object (null for sprites)
    std::vector<MeshRef> collision;   // collision meshes (not named "occ*")
    std::vector<MeshRef> otherObjects;// every non-main object of the 3DS file
    // MeshProp.bitmaps: ordered name -> texture. `hasBitmaps == false` means null.
    std::vector<std::pair<std::string, TextureRef>> bitmaps;
    bool hasBitmaps = false;
    // Sprite3D
    double spriteWidth = 100, spriteHeight = 100, originX = 0.5, originY = 1, spriteScale = 1;
    // Base alpha of the visual object (bonus region / kill box templates use 0.5).
    float objectAlpha = 1.0f;
    // Offset of the visual object inside the prop (box templates are moved by half their size).
    Vec3 objectOffset;
    TextureRef icon;                  // 50x50 library icon

    TextureRef findBitmap(const std::string& name) const {
        for (auto& [n, t] : bitmaps)
            if (n == name) return t;
        return nullptr;
    }
};
using PropModelRef = std::shared_ptr<PropModel>;

class Prop;
using PropRef = std::shared_ptr<Prop>;
using PropWeak = std::weak_ptr<Prop>;

enum class MaterialOverride { None, FillAccept, FillConflict, SpriteAccept, SpriteConflict };

class Prop : public std::enable_shared_from_this<Prop> {
  public:
    static constexpr double BOX_W = 500, BOX_L = 500, BOX_H = 300;  // region / kill box base size

    PropKind kind = PropKind::Mesh;
    int type = TYPE_TILE;
    std::string name, libraryName, groupName;
    PropModelRef model;

    // Transform (AS3 Number == double)
    double x = 0, y = 0, z = 0;
    double rotationZ = 0;
    double scaleX = 1, scaleY = 1, scaleZ = 1;

    // Grid footprint, relative to the position (Prop.distancesX/Y/Z: x = low, y = high).
    Vec2 distancesX, distancesY, distancesZ;
    bool multi = false;
    bool free = true;  // not registered in the occupancy map
    int height = 0;

    bool selected = false;
    bool visible = true;
    float alpha = 1.0f;  // container alpha (cursor ghost = 0.5)
    MaterialOverride materialOverride = MaterialOverride::None;

    // MeshProp / Sprite3DProp
    TextureRef bitmapData;    // base texture currently applied
    std::string textureName;  // "" for DEFAULT
    bool collisionEnabled = true;
    bool showBound = false;   // "plane bounds" outline (key T)

    // FreeBonusRegion
    std::vector<std::string> bonusTypes;  // ordered set
    std::vector<std::string> gameModes;   // ordered set (regions) / list (control points)
    bool parachute = true;
    double shadowLocalZ = 0;
    bool shadowDirty = true;

    // KillBox
    std::string killAction = "kill";

    // ControlPoint
    std::string controlPointName = "A";
    std::vector<PropWeak> spawnPoints;
    // SpawnPoint (domination): linked control point (Prop.data)
    PropWeak linkedControlPoint;

    // Set for props pasted with Space (SceneContainer.onPropMouseDown listener).
    bool clickZListener = false;

    // ---- factory ------------------------------------------------------------------------------
    static PropRef createMesh(PropModelRef model, const std::string& name, const std::string& lib,
                              const std::string& group);
    static PropRef createSprite(PropModelRef model, TextureRef texture, const std::string& name,
                                const std::string& lib, const std::string& group);
    static PropRef createSpawn(PropModelRef model, TextureRef tex, const std::string& name, const std::string& lib,
                               const std::string& group);
    static PropRef createFlag(PropModelRef model, TextureRef tex, const std::string& name, const std::string& lib,
                              const std::string& group);
    static PropRef createControlPoint(PropModelRef model, TextureRef tex, const std::string& name,
                                      const std::string& lib, const std::string& group);
    static PropRef createBonusRegion(const std::string& name, const std::string& lib, const std::string& group);
    static PropRef createKillBox(const std::string& name, const std::string& lib, const std::string& group);

    PropRef clone() const;

    // ---- geometry -------------------------------------------------------------------------------
    // Prop.calculate(): footprint from the visual mesh. When `inScene` is true the vertices are
    // transformed by the prop transform (localToGlobal), as happens for rotated additions.
    void calculate(bool inScene);
    void snapToGrid();
    Matrix4 transformation() const;   // world matrix of the prop container
    Matrix4 objectMatrix() const;     // world matrix of the visual object

    bool isMeshProp() const { return kind == PropKind::Mesh || kind == PropKind::Sprite; }  // "is MeshProp"
    bool isSprite() const { return kind == PropKind::Sprite; }
    bool isBox() const { return kind == PropKind::Bonus || kind == PropKind::Kill; }
    bool hasCollisionParam() const { return kind == PropKind::Mesh; }

    // Rotation setter with the region/kill-box semantics (swap scales instead of rotating).
    void setRotationZ(double value);
    // Prop.rotateClockwise/CounterClockwise (used by spawn ±5°).
    void rotateClockwise();
    void rotateCounterClockwise();
    // Turns the stored footprint (distancesX/Y) by `quarterTurnsCcw` * 90 degrees around the prop position.
    void rotateFootprint(int quarterTurnsCcw);

    // Bonus region / kill box extents
    double minX() const { return x; }
    double minY() const { return y; }
    double minZ() const { return z; }
    double maxX() const { return x + BOX_W * scaleX; }
    double maxY() const { return y + BOX_L * scaleY; }
    double maxZ() const { return z + BOX_H * scaleZ; }
    void setMaxX(double v) { setScaleX((v - x) / BOX_W); }
    void setMaxY(double v) { setScaleY((v - y) / BOX_L); }
    void setMaxZ(double v) { setScaleZ((v - z) / BOX_H); }
    void setScaleX(double v) { scaleX = v; shadowDirty = true; }
    void setScaleY(double v) { scaleY = v; shadowDirty = true; }
    void setScaleZ(double v) { scaleZ = v; shadowDirty = true; }

    // Position setters notify listeners (SceneEvents.PROP_POSITION_CHANGED).
    void setX(double v) { x = v; changed(); }
    void setY(double v) { y = v; changed(); }
    void setZ(double v) { z = v; changed(); }
    void setPosition(double px, double py, double pz) { x = px; y = py; z = pz; changed(); }
    Vec3 position() const { return {x, y, z}; }

    // ---- materials ------------------------------------------------------------------------------
    void select() { selected = true; }
    void deselect() { selected = false; }
    void hide() { visible = false; }
    void show() { visible = true; }
    // MeshProp.textureName setter. Returns false if the name is unknown.
    bool setTextureName(const std::string& name);
    // Texture to render for the visual object (null when drawn as a flat fill).
    TextureRef currentTexture() const;
    // Flat fill colour (0xRRGGBB) when an override fill is active; returns false otherwise.
    bool fillColor(uint32_t& rgb) const;

    // ---- control point links --------------------------------------------------------------------
    void addSpawnPoint(const PropRef& spawn);
    void removeSpawnPoint(const PropRef& spawn);
    std::vector<PropRef> getSpawnPoints() const;
    void unlinkSpawnPoints();

    static std::function<void(Prop*)> onPositionChanged;  // set by the UI

  private:
    void changed();
};

// Prop.floorTo / ceilTo
inline double floorTo(double v, double base) { return std::floor(v / base) * base; }
inline double ceilTo(double v, double base) { return std::ceil(v / base) * base; }

}  // namespace ae
