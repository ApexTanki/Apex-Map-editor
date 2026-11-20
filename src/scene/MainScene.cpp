#include "scene/MainScene.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

#include "lib/FunctionalProps.h"
#include "scene/Grid.h"

namespace ae {

MainScene::MainScene() { setSelectableFilter(0); }

// ---------------------------------------------------------------------------------------------
// Selectable filter (AlternativaEditor.onSelectionTypeChange)

void MainScene::setSelectableFilter(int index) { filterIndex_ = index; }

bool MainScene::isSelectableProp(const Prop& p) const {
    if (!p.visible) return false;
    switch (filterIndex_) {
        case 0:  // DEFAULT_SELECTABLE_TYPES: everything the editor creates
            return true;
        case 1:  // [MeshProp]  (exact class match: sprites are excluded)
            return p.kind == PropKind::Mesh;
        case 2:  // [Sprite3DProp]
            return p.kind == PropKind::Sprite;
        case 3:  // functional
            return p.kind == PropKind::Spawn || p.kind == PropKind::Flag || p.kind == PropKind::ControlPoint ||
                   p.kind == PropKind::Bonus || p.kind == PropKind::Kill;
    }
    return true;
}

bool MainScene::contains(const Prop* p) const {
    for (auto& c : children)
        if (c.get() == p) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------
// Adding / deleting

PropRef MainScene::addProp(const PropRef& src, const Vec3& pos, double rotation, bool cloneIt, bool occupy) {
    PropRef p;
    if (cloneIt) {
        p = src->clone();
        p->setRotationZ(rotation);
    } else {
        p = src;
    }
    children.push_back(p);
    if (rotation != 0 && cloneIt) p->calculate(true);
    p->x = pos.x;
    p->y = pos.y;
    p->z = pos.z;
    p->shadowDirty = true;
    changed = true;
    if (snapMode && occupy) occupyMap.occupy(*p);
    addPropToLayer(p);
    return p;
}

PropSet MainScene::deleteProps(const PropSet* set) {
    PropSet source;
    if (!set) {
        source = selectedProps;
        selectedProp_ = nullptr;
    } else {
        source = *set;
    }
    PropSet copy = source;
    for (const PropRef& p : copy) {
        children.erase(std::remove(children.begin(), children.end(), p), children.end());
        occupyMap.free(*p);
        if (selectedProps.has(p)) deselectProp(p, false);
        layers.removeProp(p.get());
        if (p->kind == PropKind::ControlPoint) {
            p->unlinkSpawnPoints();
        } else if (p->kind == PropKind::Spawn && p->name == functional::DOMINATION_SPAWN) {
            if (auto cp = p->linkedControlPoint.lock()) cp->removeSpawnPoint(p);
        }
    }
    requestPanels();
    propMouseDown = false;
    changed = true;
    return copy;
}

void MainScene::clear() {
    selectedProp_ = nullptr;
    selectedProps.clear();
    for (auto& p : children) p->selected = false;
    occupyMap.clear();
    layers.clear();
    children.clear();
    hiddenProps_.clear();
    domSpawnPoint_.reset();
    requestPanels();
}

std::vector<PropRef> MainScene::dominationPoints() const {
    std::vector<PropRef> out;
    for (auto& c : children)
        if (c->kind == PropKind::ControlPoint) out.push_back(c);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Layers

void MainScene::addPropToLayer(const PropRef& p, const std::string& layerIn) {
    std::string layer = layerIn;
    if (layer.empty()) {
        if (p->kind == PropKind::Bonus)
            for (auto& mode : p->gameModes) layers.addProp(mode, p.get());
        layer = functional::getPropLayer(*p);
    }
    if (!layer.empty()) {
        layers.addProp(layer, p.get());
        checkPropVisibility(p);
    }
}

void MainScene::checkPropVisibility(const PropRef& p) {
    bool vis = false;
    for (Layer* l : layers.layersContaining(p.get()))
        if (l->visible) vis = true;
    if (!vis)
        hidePropInternal(p);
    else
        showPropInternal(p);
}

void MainScene::hidePropInternal(const PropRef& p) {
    if (!p->free) occupyMap.free(*p);
    p->free = true;
    p->hide();
}

void MainScene::showPropInternal(const PropRef& p) {
    p->show();
    occupyMap.occupy(*p);
}

void MainScene::setLayerVisible(const std::string& name, bool visible) {
    Layer& l = layers.get(name);
    l.visible = visible;
    for (auto& c : std::vector<PropRef>(children))
        if (l.props.count(c.get())) checkPropVisibility(c);
}

void MainScene::onLayerContentChange(const PropRef& p, const std::string& layer, bool containsProp) {
    if (containsProp) {
        addPropToLayer(p, layer);
    } else if (!layer.empty()) {
        layers.removeProp(p.get(), layer);
        checkPropVisibility(p);
    }
}

void MainScene::dominationLinkEnd(const PropRef& cp) {
    if (!domSpawnPoint_) return;
    if (auto old = domSpawnPoint_->linkedControlPoint.lock()) old->removeSpawnPoint(domSpawnPoint_);
    cp->addSpawnPoint(domSpawnPoint_);
    domSpawnPoint_.reset();
}

// ---------------------------------------------------------------------------------------------
// Selection

void MainScene::onPropMouseDown(const PropRef& target, bool shift, bool alt, bool ctrl) {
    if (ctrl || !target) return;
    if (!isSelectableProp(*target)) return;
    bool wasSelected = target->selected;
    if (shift) {
        if (alt) {
            if (wasSelected) deselectProp(target, true);
        } else if (!wasSelected) {
            selectProp(target, true);
        }
    } else if (!wasSelected) {
        deselectProps();
        selectProp(target, true);
    } else {
        selectedProp_ = target;
    }
    propMouseDown = true;
}

void MainScene::deselectProps() {
    while (!selectedProps.empty()) deselectProp(selectedProps.peek(), false);
    selectedProps.clear();
    selectedProp_ = nullptr;
    requestPanels();
}

void MainScene::deselectProp(const PropRef& p, bool updatePanels) {
    p->deselect();
    selectedProps.remove(p);
    if (p == selectedProp_) selectedProp_ = nullptr;
    requestPanels();
    if (!selectedProps.empty()) {
        selectedProp_ = selectedProps.peek();
        if (updatePanels) requestPanels();
    }
}

void MainScene::selectProps(const PropSet& newSel, bool updatePanels) {
    for (const PropRef& p : std::vector<PropRef>(selectedProps.items())) {
        if (!newSel.has(p)) {
            selectedProps.remove(p);
            p->deselect();
            if (p == selectedProp_) selectedProp_ = nullptr;
        }
    }
    for (const PropRef& p : newSel) {
        if (!selectedProps.has(p) && isSelectableProp(*p)) {
            selectedProps.add(p);
            p->select();
            selectedProp_ = p;
        }
    }
    if (updatePanels && selectedProp_) requestPanels();
}

void MainScene::selectProp(const PropRef& p, bool updatePanels) {
    if (!isSelectableProp(*p)) return;
    p->select();
    selectedProps.add(p);
    selectedProp_ = p;
    if (updatePanels) requestPanels();
}

PropSet MainScene::getPropsUnderRect(double rx, double ry, double rw, double rh, bool select, bool changeState) {
    PropSet out;
    for (const PropRef& p : children) {
        if (!isSelectableProp(*p)) continue;
        Vec3 s = camera.projectGlobal(p->position(), viewport);
        if (s.x >= rx && s.x <= rx + rw && s.y >= ry && s.y <= ry + rh) {
            if (changeState) {
                if (select) {
                    if (!p->selected) p->select();
                } else if (p->selected) {
                    p->deselect();
                }
            }
            out.add(p);
        }
    }
    return out;
}

void MainScene::selectAll() {
    PropSet all;
    for (auto& p : children)
        if (isSelectableProp(*p)) all.add(p);
    selectProps(all, true);
}

void MainScene::selectConflictingProps() {
    PropSet set;
    for (Prop* raw : occupyMap.getConflictProps())
        for (auto& c : children)
            if (c.get() == raw) set.add(c);
    selectProps(set, true);
}

// ---------------------------------------------------------------------------------------------
// Editing

void MainScene::moveSelectedPropsByMouse(bool vertical, double mx, double my) {
    if (!selectedProp_) return;
    for (auto& p : selectedProps) occupyMap.free(*p);
    double dx = 0, dy = 0, dz = 0;
    Prop& main = *selectedProp_;
    if (vertical) {
        CameraFacing f = getCameraFacing();
        Vec3 pt;
        if (f == CameraFacing::Y || f == CameraFacing::NEG_Y) {
            pt = camera.projectViewPointToPlane(mx, my, viewport, {0, 1, 0}, main.y);
            if (pt.isNaN()) pt = main.position();
            dx = pt.x - main.x;
            main.x = pt.x;
        } else {
            pt = camera.projectViewPointToPlane(mx, my, viewport, {1, 0, 0}, main.x);
            if (pt.isNaN()) pt = main.position();
            dy = pt.y - main.y;
            main.y = pt.y;
        }
        dz = pt.z - main.z;
        main.z = pt.z;
    } else {
        Vec3 pt = camera.projectViewPointToPlane(mx, my, viewport, {0, 0, 1}, main.z);
        if (pt.isNaN()) pt = main.position();  // ray parallel to the plane: keep position
        dx = pt.x - main.x;
        dy = pt.y - main.y;
        main.x = pt.x;
        main.y = pt.y;
    }
    main.setPosition(main.x, main.y, main.z);
    for (auto& p : selectedProps)
        if (p.get() != &main) p->setPosition(p->x + dx, p->y + dy, p->z + dz);

    // Snap the selection as one body. Snapping every prop on its own pulls props that sit at custom
    // (off-grid) positions or heights onto different grid lines and the group falls apart. The anchor (the
    // main prop, or else the first prop that snaps) goes to the grid and the rest follow by the same shift.
    // For props that already sit on the grid this gives the same result as snapping each one.
    auto snaps = [&](const Prop& p) { return snapMode || p.kind == PropKind::Mesh; };
    Prop* anchor = snaps(main) ? &main : nullptr;
    if (!anchor)
        for (auto& p : selectedProps)
            if (snaps(*p)) {
                anchor = p.get();
                break;
            }
    Vec3 shift{0, 0, 0};
    if (anchor) {
        const Vec3 before = anchor->position();
        anchor->snapToGrid();
        shift = anchor->position() - before;
    }
    for (auto& p : selectedProps) {
        if (p.get() != anchor) p->setPosition(p->x + shift.x, p->y + shift.y, p->z + shift.z);
        if (snaps(*p)) occupyMap.occupy(*p);
    }
}

void MainScene::moveByArrows(ArrowKey key) {
    for (auto& p : selectedProps) {
        occupyMap.free(*p);
        move(*p, key);
        if (snapMode) occupyMap.occupy(*p);
    }
}

void MainScene::verticalMove(bool down) {
    double step = grid::vStep();
    if (down) step = -step;
    for (auto& p : selectedProps) {
        occupyMap.free(*p);
        p->setZ(p->z + step);
        if (snapMode) occupyMap.occupy(*p);
    }
}

void MainScene::moveProps(const PropSet& set, const Vec3& delta) {
    for (auto& p : set) {
        occupyMap.free(*p);
        p->setPosition(p->x - delta.x, p->y - delta.y, p->z - delta.z);
        if (snapMode) {
            p->snapToGrid();
            occupyMap.occupy(*p);
        }
    }
}

void MainScene::freeProps(const PropSet& set) {
    for (auto& p : set) occupyMap.free(*p);
}

void MainScene::rotateCounterClockwise(const PropSet* set) {
    const PropSet& s = set ? *set : selectedProps;
    freeProps(s);
    rotatePropsCounterClockwise(s);
    if (snapMode)
        for (auto& p : s) p->snapToGrid();
    changed = true;
}

void MainScene::rotateClockwise(const PropSet* set) {
    const PropSet& s = set ? *set : selectedProps;
    freeProps(s);
    rotatePropsClockwise(s);
    if (snapMode)
        for (auto& p : s) p->snapToGrid();
    changed = true;
}

void MainScene::hideSelectedProps() {
    PropSet sel = selectedProps;
    deselectProps();
    for (auto& p : sel) {
        hiddenProps_.push_back(p);
        if (!p->free) {
            occupyMap.free(*p);
            p->free = true;
        }
        p->hide();
    }
}

void MainScene::showAll() {
    for (auto& p : hiddenProps_) {
        p->show();
        occupyMap.occupy(*p);
    }
    hiddenProps_.clear();
}

void MainScene::showPlaneBounds() {
    for (auto& p : children)
        if (p->kind == PropKind::Mesh && p->height == 0) p->showBound = true;
}

void MainScene::hidePlaneBounds() {
    for (auto& p : children)
        if (p->kind == PropKind::Mesh && p->height == 0) p->showBound = false;
}

void MainScene::undo(JournalItem& item) {
    switch (item.op) {
        case JournalItem::ADD:
        case JournalItem::COPY:
            deleteProps(&item.props);
            break;
        case JournalItem::DELETE:
            for (auto& p : item.props) {
                p->deselect();
                addProp(p, p->position(), p->rotationZ, false);
            }
            break;
        case JournalItem::MOVE:
            moveProps(item.props, item.delta);
            item.delta *= -1;
            break;
        case JournalItem::ROTATE:
            if (item.clockwise)
                rotateCounterClockwise(&item.props);
            else
                rotateClockwise(&item.props);
            item.clockwise = !item.clockwise;
            break;
        default:
            break;
    }
}

void MainScene::redo(JournalItem& item) {
    switch (item.op) {
        case JournalItem::ADD: {
            PropRef p = item.props.peek();
            if (p) addProp(p, p->position(), p->rotationZ, false);
            break;
        }
        case JournalItem::COPY:
            for (auto& p : item.props) addProp(p, p->position(), p->rotationZ, false);
            break;
        case JournalItem::DELETE:
            deleteProps(&item.props);
            break;
        case JournalItem::MOVE:
            moveProps(item.props, item.delta);
            item.delta *= -1;
            break;
        case JournalItem::ROTATE:
            if (item.clockwise)
                rotateCounterClockwise(&item.props);
            else
                rotateClockwise(&item.props);
            item.clockwise = !item.clockwise;
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// EditorScene helpers

CameraFacing MainScene::getCameraFacing() const {
    Matrix4 m = camera.cameraMatrix();
    if (m.a > 1 / std::sqrt(2.0)) return CameraFacing::Y;
    if (m.a < -1 / std::sqrt(2.0)) return CameraFacing::NEG_Y;
    if (m.e > 0) return CameraFacing::NEG_X;
    return CameraFacing::X;
}

void MainScene::move(Prop& p, ArrowKey key) {
    double vx = 0, vy = 0;
    switch (key) {
        case ArrowKey::Up: vy = 1; break;
        case ArrowKey::Down: vy = -1; break;
        case ArrowKey::Left: vx = -1; break;
        case ArrowKey::Right: vx = 1; break;
    }
    double s = 0, c = 0;
    switch (getCameraFacing()) {
        case CameraFacing::X: s = -1; c = 0; break;
        case CameraFacing::Y: s = 0; c = 1; break;
        case CameraFacing::NEG_X: s = 1; c = 0; break;
        case CameraFacing::NEG_Y: s = 0; c = -1; break;
    }
    double gx = c * vx - s * vy, gy = s * vx + c * vy;
    double step = grid::hStep();
    p.setPosition(p.x + step * gx, p.y + step * gy, p.z);
}

Vec2 MainScene::getPropsGroupCenter(const PropSet& set) const {
    double minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY, sumX = 0, sumY = 0;
    for (auto& p : set) {
        double a = p->distancesX.x + p->x, b = p->distancesX.y + p->x;
        if (a < minX) minX = a;
        if (b > maxX) maxX = b;
        a = p->distancesY.x + p->y;
        b = p->distancesY.y + p->y;
        if (a < minY) minY = a;
        if (b > maxY) maxY = b;
        sumX += p->x;
        sumY += p->y;
    }
    double rx = std::fmod((maxX - minX) / grid::hBase2, 2.0);
    double ry = std::fmod((maxY - minY) / grid::hBase2, 2.0);
    if (rx != ry) {
        if (rx != 0) {
            sumX /= (double)set.size();
            if (std::abs(sumX - maxX) < std::abs(sumX - minX))
                maxX += grid::hBase2;
            else
                minX -= grid::hBase2;
        } else {
            sumY /= (double)set.size();
            if (std::abs(sumY - maxY) < std::abs(sumY - minY))
                maxY += grid::hBase2;
            else
                minY -= grid::hBase2;
        }
    }
    return {(maxX + minX) / 2, (maxY + minY) / 2};
}

void MainScene::rotatePropsCounterClockwise(const PropSet& set) { rotateProps(set, grid::snapByHalf ? PI / 4 : PI / 2); }
void MainScene::rotatePropsClockwise(const PropSet& set) { rotateProps(set, grid::snapByHalf ? -PI / 4 : -PI / 2); }

void MainScene::rotateProps(const PropSet& set, double angle) {
    rotatePropsAround(set, getPropsGroupCenter(set), angle);
}

void MainScene::translateProps(const PropSet& set, const Vec3& delta) {
    for (auto& p : set) p->setPosition(p->x + delta.x, p->y + delta.y, p->z + delta.z);
    changed = true;
}

void MainScene::rotatePropsAround(const PropSet& set, const Vec2& pivot, double angle) {
    double c = std::cos(angle), s = std::sin(angle);
    // A quarter turn maps the footprint exactly. Any other angle (45 degrees in half-cell mode) has no
    // exact grid footprint, so it is recomputed from the rotated mesh instead.
    const double turns = angle / (PI / 2);
    const bool quarter = std::abs(turns - std::round(turns)) < 1e-9;
    const int quarterTurnsCcw = (int)std::lround(turns);
    if (quarter) {
        // cos/sin of a quarter turn must be exactly 0 or +-1. cos(PI/2) is 6e-17, and that noise is enough for
        // snapToGrid (ceilTo) to push a prop sitting on a grid line one whole cell further.
        static const double kCos[4] = {1, 0, -1, 0}, kSin[4] = {0, 1, 0, -1};
        const int k = ((quarterTurnsCcw % 4) + 4) % 4;
        c = kCos[k];
        s = kSin[k];
    }
    for (auto& p : set) {
        double dx = p->x - pivot.x, dy = p->y - pivot.y;
        p->setPosition(dx * c - dy * s + pivot.x, dx * s + dy * c + pivot.y, p->z);
        p->setRotationZ(p->rotationZ + angle);
        // The footprint feeds the occupancy map and the pivot of the next group rotation, so it has to
        // follow the prop; otherwise props drift apart and stop fitting each other.
        if (quarter) {
            p->rotateFootprint(quarterTurnsCcw);
        } else {
            // calculate() only ever sets `multi` (footprint wider than one cell), so a diagonal turn would
            // switch the prop to the corner lattice for good. `multi` describes the prop itself, not its pose.
            const bool multi = p->multi;
            p->calculate(true);
            p->multi = multi;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Queries

PropRef MainScene::pick(double mx, double my) const {
    Vec3 o, d;
    camera.ray(mx, my, viewport, o, d);
    d = d.normalized();
    PropRef best;
    double bestT = DBL_MAX;
    for (auto& p : children) {
        if (!isSelectableProp(*p)) continue;
        double t;
        if (picking::rayProp(*p, camera, collisionView, o, d, t) && t < bestT && t > CameraRig::NEAR_CLIP) {
            bestT = t;
            best = p;
        }
    }
    return best;
}

Vec3 MainScene::projectDown(const Vec3& from, const Prop* ignore) const {
    const double radius = 30, distance = 10000;
    Vec3 dir{0, 0, -1};
    double bestT = distance;
    bool hit = false;
    for (auto& p : children) {
        if (p.get() == ignore || !p->visible || !p->model || !p->model->mesh || p->isSprite()) continue;
        double t;
        if (picking::rayMesh(*p->model->mesh, p->objectMatrix(), from, dir, t) && t >= 0 && t <= distance &&
            (!hit || t < bestT)) {
            bestT = t;
            hit = true;
        }
    }
    if (gridVisible && from.z > 0) {
        // The grid plane is part of the scene graph and stops the collider too.
        double t = from.z;
        Vec3 h = from + dir * t;
        if (h.x >= -3500 && h.x <= 4000 && h.y >= -3500 && h.y <= 4000 && (!hit || t < bestT)) {
            bestT = t;
            hit = true;
        }
    }
    if (!hit) return from + dir * distance;
    return from + dir * std::max(0.0, bestT - radius);
}

void MainScene::updateShadows() {
    for (auto& p : children) {
        if (p->kind != PropKind::Bonus || !p->shadowDirty) continue;
        Vec3 from{p->x + p->scaleX * Prop::BOX_W / 2, p->y + p->scaleY * Prop::BOX_L / 2, p->z - 50};
        Vec3 dest = projectDown(from, p.get());
        double localZ = (dest.z - p->z) / (p->scaleZ != 0 ? p->scaleZ : 1);
        p->shadowLocalZ = localZ + 3;
        p->shadowDirty = false;
    }
}

// ---------------------------------------------------------------------------------------------
// CursorScene

void CursorScene::setObject(const PropRef& prefab) {
    if (prefab_ == prefab) return;
    bool hadPrev = (bool)ghost_;
    Vec3 prev;
    if (ghost_) {
        prev = ghost_->position();
        clearSelection();
    }
    prefab_ = prefab;
    if (!prefab) return;
    ghost_ = prefab->clone();
    ghost_->alpha = 0.5f;
    if (hadPrev) ghost_->setPosition(prev.x, prev.y, prev.z);
    ghost_->visible = visible_;
    snapToGridIfNecessary();
    checkForConflicts();
}

void CursorScene::setSnapMode(bool snap) {
    if (snapMode_ == snap) return;
    snapMode_ = snap;
    if (!ghost_) return;
    snapToGridIfNecessary();
}

void CursorScene::snapToGridIfNecessary() {
    if (snapMode_ && !ghost_->isSprite()) ghost_->snapToGrid();
}

void CursorScene::moveCursorByMouse(double mx, double my) {
    if (!ghost_) return;
    Vec3 pt = scene_.camera.projectViewPointToPlane(mx, my, scene_.viewport, {0, 0, 1}, ghost_->z);
    if (std::isnan(pt.x)) return;
    ghost_->setPosition(pt.x, pt.y, ghost_->z);
    snapToGridIfNecessary();
    checkForConflicts();
}

void CursorScene::checkForConflicts() {
    if (!ghost_) return;
    if (!snapMode_) {
        ghost_->materialOverride = MaterialOverride::None;
        return;
    }
    freeState_ = !scene_.occupyMap.isConflict(*ghost_);
    if (ghost_->isSprite())
        ghost_->materialOverride = freeState_ ? MaterialOverride::SpriteAccept : MaterialOverride::SpriteConflict;
    else
        ghost_->materialOverride = freeState_ ? MaterialOverride::FillAccept : MaterialOverride::FillConflict;
}

void CursorScene::clear() {
    visible_ = false;
    clearSelection();
}

void CursorScene::clearSelection() {
    if (!ghost_) return;
    prefab_ = nullptr;
    ghost_ = nullptr;
}

void CursorScene::setVisible(bool v) {
    if (visible_ == v) return;
    visible_ = v;
    if (ghost_) {
        ghost_->visible = v;
        if (v) checkForConflicts();
    }
}

void CursorScene::moveByArrows(ArrowKey k) {
    if (!ghost_) return;
    scene_.move(*ghost_, k);
    checkForConflicts();
}

static PropSet single(const PropRef& p) {
    PropSet s;
    s.add(p);
    return s;
}

void CursorScene::rotateCounterClockwise() {
    if (!ghost_) return;
    scene_.rotatePropsCounterClockwise(single(ghost_));
    if (snapMode_ || ghost_->kind == PropKind::Mesh) ghost_->snapToGrid();
}

void CursorScene::rotateClockwise() {
    if (!ghost_) return;
    scene_.rotatePropsClockwise(single(ghost_));
    if (snapMode_ || ghost_->kind == PropKind::Mesh) ghost_->snapToGrid();
}

}  // namespace ae
