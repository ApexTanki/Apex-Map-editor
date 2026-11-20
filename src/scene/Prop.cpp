#include "scene/Prop.h"

#include <algorithm>
#include <cmath>

#include "scene/Grid.h"

namespace ae {

std::function<void(Prop*)> Prop::onPositionChanged;

void Prop::changed() {
    if (isBox()) shadowDirty = true;
    if (onPositionChanged) onPositionChanged(this);
}

// ---------------------------------------------------------------------------------------------
// Factories

static void fixedFootprint(Prop& p) {
    // SpawnPoint / CTFFlagBase / ControlPoint.calculate()
    p.distancesX = {-grid::hBase, grid::hBase};
    p.distancesY = {-grid::hBase, grid::hBase};
    p.distancesZ = {0, grid::vBase};
    p.height = -1;
}

PropRef Prop::createMesh(PropModelRef model, const std::string& name, const std::string& lib,
                         const std::string& group) {
    auto p = std::make_shared<Prop>();
    p->kind = PropKind::Mesh;
    p->type = TYPE_TILE;
    p->name = name;
    p->libraryName = lib;
    p->groupName = group;
    p->model = std::move(model);
    p->calculate(false);
    return p;
}

PropRef Prop::createSprite(PropModelRef model, TextureRef texture, const std::string& name,
                           const std::string& lib, const std::string& group) {
    auto p = std::make_shared<Prop>();
    p->kind = PropKind::Sprite;
    p->type = TYPE_TILE;
    p->name = name;
    p->libraryName = lib;
    p->groupName = group;
    p->model = std::move(model);
    // Sprite3DProp.initBitmapData: bitmaps = {DEFAULT: texture}
    p->model->bitmaps.clear();
    p->model->hasBitmaps = true;
    if (texture) {
        p->model->bitmaps.emplace_back("DEFAULT", texture);
        p->bitmapData = texture;
    }
    p->calculate(false);
    return p;
}

static PropRef makeFunctional(PropKind kind, int type, PropModelRef model, TextureRef tex, const std::string& name,
                              const std::string& lib, const std::string& group) {
    auto p = std::make_shared<Prop>();
    p->kind = kind;
    p->type = type;
    p->name = name;
    p->libraryName = lib;
    p->groupName = group;
    p->model = std::move(model);
    p->bitmapData = std::move(tex);
    fixedFootprint(*p);
    return p;
}

PropRef Prop::createSpawn(PropModelRef model, TextureRef tex, const std::string& name, const std::string& lib,
                          const std::string& group) {
    return makeFunctional(PropKind::Spawn, TYPE_SPAWN, std::move(model), std::move(tex), name, lib, group);
}
PropRef Prop::createFlag(PropModelRef model, TextureRef tex, const std::string& name, const std::string& lib,
                         const std::string& group) {
    return makeFunctional(PropKind::Flag, TYPE_FLAG, std::move(model), std::move(tex), name, lib, group);
}
PropRef Prop::createControlPoint(PropModelRef model, TextureRef tex, const std::string& name,
                                 const std::string& lib, const std::string& group) {
    auto p = makeFunctional(PropKind::ControlPoint, TYPE_DOM_CP, std::move(model), std::move(tex), name, lib, group);
    p->controlPointName = "A";
    p->gameModes = {"dom"};
    return p;
}

static PropModelRef boxModel() {
    static PropModelRef m;
    if (!m) {
        m = std::make_shared<PropModel>();
        m->mesh = primitives::box(Prop::BOX_W, Prop::BOX_L, Prop::BOX_H);
        m->objectOffset = {Prop::BOX_W / 2, Prop::BOX_L / 2, Prop::BOX_H / 2};
        m->objectAlpha = 0.5f;
    }
    return m;
}

PropRef Prop::createBonusRegion(const std::string& name, const std::string& lib, const std::string& group) {
    static TextureRef tex = image::solid(0xFFAF00);  // 16756224
    auto p = std::make_shared<Prop>();
    p->kind = PropKind::Bonus;
    p->type = TYPE_BONUS;
    p->name = name;
    p->libraryName = lib;
    p->groupName = group;
    p->model = boxModel();
    p->gameModes = {"dm", "tdm", "ctf", "dom", "asl", "sge", "jgr", "rgb"};
    p->bonusTypes = {"damageup"};
    p->bitmapData = tex;
    p->calculate(false);
    return p;
}

PropRef Prop::createKillBox(const std::string& name, const std::string& lib, const std::string& group) {
    static TextureRef tex = image::solid(0xF010F5);  // 15732981
    auto p = std::make_shared<Prop>();
    p->kind = PropKind::Kill;
    p->type = TYPE_KILL;
    p->name = name;
    p->libraryName = lib;
    p->groupName = group;
    p->model = boxModel();
    p->bitmapData = tex;
    p->calculate(false);
    return p;
}

PropRef Prop::clone() const {
    auto p = std::make_shared<Prop>();
    p->kind = kind;
    p->type = type;
    p->name = name;
    p->libraryName = libraryName;
    p->groupName = groupName;
    p->model = model;
    p->distancesX = distancesX;
    p->distancesY = distancesY;
    p->distancesZ = distancesZ;
    p->multi = multi;
    p->height = height;
    p->bitmapData = bitmapData;  // the clone picks up the source's current base texture
    switch (kind) {
        case PropKind::Mesh:
            p->textureName = textureName;
            p->collisionEnabled = collisionEnabled;
            break;
        case PropKind::Sprite:
            break;  // Sprite3DProp.clone copies neither textureName nor collision state
        case PropKind::ControlPoint:
            // ControlPoint.clone does not copy the letter, modes or links.
            p->controlPointName = "A";
            p->gameModes = {"dom"};
            break;
        case PropKind::Bonus:
            p->scaleX = scaleX;
            p->scaleY = scaleY;
            p->scaleZ = scaleZ;
            p->z = z;
            p->bonusTypes = bonusTypes;
            p->gameModes = gameModes;
            // parachute is not copied by FreeBonusRegion.clone (stays true)
            break;
        case PropKind::Kill:
            p->killAction = killAction;
            p->scaleX = scaleX;
            p->scaleY = scaleY;
            p->scaleZ = scaleZ;
            p->z = z;
            break;
        default:
            break;
    }
    return p;
}

// ---------------------------------------------------------------------------------------------
// Geometry

// Prop.calcDistance
static Vec2 calcDistance(double pos, double v1, double v2, double base) {
    Vec2 r;
    v2 = floorTo(v2, base);
    v1 = floorTo(v1, base);
    if (v2 == 0 && v1 == 0) {
        r.x = 0;
        r.y = base;
    } else if (v2 > v1) {
        if (v1 == 0) {
            r.x = 0;
            r.y = (double)(int)(v2 - pos);
        } else {
            r.x = (double)(int)(v1 - pos);
            r.y = (double)(int)(v2 - pos);
        }
    } else if (v2 == 0) {
        r.x = 0;
        r.y = (double)(int)(v1 - pos);
    } else {
        r.x = (double)(int)(v2 - pos);
        r.y = (double)(int)(v1 - pos);
    }
    return r;
}

void Prop::calculate(bool inScene) {
    if (kind == PropKind::Spawn || kind == PropKind::Flag || kind == PropKind::ControlPoint) {
        fixedFootprint(*this);
        return;
    }
    if (kind == PropKind::Sprite) {
        distancesX = distancesY = distancesZ = {};
        multi = false;
        height = -1;
        return;
    }
    if (!model || !model->mesh) return;
    const MeshObject& mesh = *model->mesh;
    const size_t n = mesh.vertexCount();
    std::vector<Vec3> verts(n);
    Matrix4 m = transformation();
    for (size_t q = 0; q < n; ++q) verts[q] = inScene ? m.transform(mesh.vertex(q)) : mesh.vertex(q);

    // Largest pairwise separation along each axis. Equivalent to the original O(n^2) scan
    // (first strictly-greater pair wins), but done with min/max for speed.
    double bestX = 0, bestY = 0, bestZ = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, z1 = 0, z2 = 0;
    if (n >= 2) {
        auto axisPair = [&](int axis, double& best, double& a1, double& a2) {
            size_t iMin = 0, iMax = 0;
            auto get = [&](size_t q) { return axis == 0 ? verts[q].x : axis == 1 ? verts[q].y : verts[q].z; };
            for (size_t q = 1; q < n; ++q) {
                if (get(q) < get(iMin)) iMin = q;
                if (get(q) > get(iMax)) iMax = q;
            }
            double d = get(iMax) - get(iMin);
            if (d * d > 0) {
                best = d * d;
                a1 = get(std::min(iMin, iMax));
                a2 = get(std::max(iMin, iMax));
            }
        };
        axisPair(0, bestX, x1, x2);
        axisPair(1, bestY, y1, y2);
        axisPair(2, bestZ, z1, z2);
    }
    height = (int)std::sqrt(bestZ);
    distancesX = calcDistance(x, (double)(int)x1, (double)(int)x2, grid::hBase);
    distancesY = calcDistance(y, (double)(int)y1, (double)(int)y2, grid::hBase);
    distancesZ = calcDistance(z, (double)(int)z1, (double)(int)z2, grid::vBase);
    if (std::abs((int)x2 - (int)x1) / grid::hBase2 > 1 || std::abs((int)y1 - (int)y2) / grid::hBase2 > 1)
        multi = true;
}

void Prop::snapToGrid() {
    if (grid::snapByHalf) {
        x = floorTo(x, grid::H_RES_2);
        y = floorTo(y, grid::H_RES_2);
        z = floorTo(z, grid::V_RES_2);
    } else {
        if (!multi) {
            x -= grid::H_RES_2;
            y -= grid::H_RES_2;
        }
        x = ceilTo(x, grid::H_RES_1);
        y = ceilTo(y, grid::H_RES_1);
        z = floorTo(z, grid::V_RES_1);
        if (!multi) {
            x += grid::H_RES_2;
            y += grid::H_RES_2;
        }
    }
    changed();
}

Matrix4 Prop::transformation() const { return Matrix4::compose(x, y, z, 0, 0, rotationZ, scaleX, scaleY, scaleZ); }

Matrix4 Prop::objectMatrix() const {
    Matrix4 m = transformation();
    if (model && model->mesh) {
        Matrix4 local = model->mesh->localMatrix();
        local.d += model->objectOffset.x;
        local.h += model->objectOffset.y;
        local.l += model->objectOffset.z;
        m = Matrix4::mul(m, local);
    }
    return m;
}

void Prop::setRotationZ(double value) {
    if (isBox()) {
        // FreeBonusRegion / KillBox: rotation is emulated by swapping the X/Y scale.
        if (std::abs(value - rotationZ) < 0.0001) return;
        if (std::fmod(std::abs(rotationZ - value), PI / 2) < 0.0001) {
            std::swap(scaleX, scaleY);
            shadowDirty = true;
        }
        return;
    }
    rotationZ = value;
    changed();
}

void Prop::rotateFootprint(int quarterTurnsCcw) {
    for (int i = ((quarterTurnsCcw % 4) + 4) % 4; i > 0; --i) {
        // One counter-clockwise quarter turn maps (x, y) to (-y, x): the new X range is the old Y range
        // mirrored, and the new Y range is the old X range.
        Vec2 t = distancesY;
        distancesY = distancesX;
        distancesX = {-t.y, -t.x};
    }
}

void Prop::rotateCounterClockwise() {
    if (kind == PropKind::Spawn) {
        setRotationZ(rotationZ + DEG5);
        return;
    }
    Vec2 t = distancesY;
    distancesY = distancesX;
    distancesX = {-t.y, -t.x};
    setRotationZ(rotationZ + DEG90);
}

void Prop::rotateClockwise() {
    if (kind == PropKind::Spawn) {
        setRotationZ(rotationZ - DEG5);
        return;
    }
    Vec2 t = distancesX;
    distancesX = distancesY;
    distancesY = {-t.y, -t.x};
    setRotationZ(rotationZ - DEG90);
}

// ---------------------------------------------------------------------------------------------
// Materials

bool Prop::setTextureName(const std::string& tn) {
    if (!isMeshProp()) return false;
    textureName = tn;
    TextureRef bmp;
    bool found = true;
    if (tn == "invisible") {
        bmp = TextureRegistry::instance().invisible();
    } else if (model && model->hasBitmaps) {
        bmp = model->findBitmap(tn);
        found = (bool)bmp;
    } else {
        found = false;
    }
    if (textureName == "DEFAULT") textureName = "";
    if (bmp) bitmapData = bmp;
    return found;
}

TextureRef Prop::currentTexture() const {
    switch (materialOverride) {
        case MaterialOverride::FillAccept:
        case MaterialOverride::FillConflict:
            return nullptr;
        case MaterialOverride::SpriteAccept:
            return TextureRegistry::instance().cursor(bitmapData, true);
        case MaterialOverride::SpriteConflict:
            return TextureRegistry::instance().cursor(bitmapData, false);
        default:
            break;
    }
    if (!bitmapData) return nullptr;
    if (selected) return TextureRegistry::instance().selected(bitmapData);
    if (kind == PropKind::Mesh && textureName != "invisible" && !collisionEnabled)
        return TextureRegistry::instance().noCollision(bitmapData);
    return bitmapData;
}

bool Prop::fillColor(uint32_t& rgb) const {
    if (materialOverride == MaterialOverride::FillAccept) {
        rgb = 0x00FF00;
        return true;
    }
    if (materialOverride == MaterialOverride::FillConflict) {
        rgb = 0xFF0000;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Control point links

void Prop::addSpawnPoint(const PropRef& spawn) {
    for (auto& w : spawnPoints)
        if (w.lock() == spawn) return;
    spawnPoints.push_back(spawn);
    spawn->linkedControlPoint = weak_from_this();
}

void Prop::removeSpawnPoint(const PropRef& spawn) {
    for (auto it = spawnPoints.begin(); it != spawnPoints.end(); ++it) {
        if (it->lock() == spawn) {
            spawnPoints.erase(it);
            spawn->linkedControlPoint.reset();
            return;
        }
    }
}

std::vector<PropRef> Prop::getSpawnPoints() const {
    std::vector<PropRef> out;
    for (auto& w : spawnPoints)
        if (auto s = w.lock()) out.push_back(s);
    return out;
}

void Prop::unlinkSpawnPoints() {
    for (auto& s : getSpawnPoints()) removeSpawnPoint(s);
}

}  // namespace ae
