#include "editor/SceneContainer.h"

#include <algorithm>
#include <cmath>

#include "lib/FunctionalProps.h"
#include "scene/Grid.h"
#include "ui/Locale.h"

namespace ae {

SceneContainer::SceneContainer(MainScene& s) : scene(s) {
    cameraDistance_ = scene.camera.cameraLocal.length();
}

void SceneContainer::setSnapMode(bool snap) {
    snapMode_ = snap;
    scene.snapMode = snap;
    scene.cursor.setSnapMode(snap);
}

// ---------------------------------------------------------------------------------------------
// Mouse

void SceneContainer::onMouseDown(double x, double y, Modifiers m) {
    // MouseEvent3D.MOUSE_DOWN is dispatched by the view before the container's listener.
    PropRef target = scene.pick(x, y);
    pressTarget_ = target;
    if (target) {
        scene.onPropMouseDown(target, m.shift, m.alt, m.ctrl);
        if (target->clickZListener) {
            clickZ_ = target->z;
            propDown_ = true;
        }
    }

    leaveClickZone_ = false;
    mouseOX_ = x;
    mouseOY_ = y;
    if (scene.propMouseDown && scene.selectedProp()) {
        startSelectionCoords_ = scene.selectedProp()->position();
        scene.cursor.setVisible(false);
    }
    mouseDown_ = true;
    mouseDownX_ = x;
    mouseDownY_ = y;
}

void SceneContainer::onMouseUp(double x, double y, Modifiers m) {
    PropRef sel = scene.selectedProp();
    PropSet& selected = scene.selectedProps;
    if (scene.propMouseDown) {
        if (sel) {
            bool moved = false;
            if (copy_) {
                journal.addEvent(JournalItem::COPY, selected);
            } else if (startSelectionCoords_ != sel->position()) {
                Vec3 delta = sel->position() - startSelectionCoords_;
                journal.addEvent(JournalItem::MOVE, selected, delta);
                moved = true;
            }
            if (snapMode_ && (copy_ || moved)) checkConflict();
            copy_ = false;
        }
        scene.propMouseDown = false;
    } else {
        double dx = std::abs(mouseDownX_ - x), dy = std::abs(mouseDownY_ - y);
        if (dx < 3 && dy < 3) {
            if (propDown_) {
                if (auto g = scene.cursor.object()) g->setZ(clickZ_);
                propDown_ = false;
            }
            scene.cursor.moveCursorByMouse(x, y);
            if (!scene.cursor.visible()) {
                scene.deselectProps();
                scene.cursor.setVisible(true);
            }
        } else if (m.shift) {
            for (auto& p : rectProps_) {
                if (m.alt) {
                    if (selected.has(p)) scene.deselectProp(p, false);
                } else if (!selected.has(p)) {
                    scene.selectProp(p, false);
                }
            }
        }
    }

    // MouseEvent3D.CLICK (press and release on the same prop) - domination linking.
    if (m.ctrl && pressTarget_ && scene.pick(x, y) == pressTarget_) {
        const PropRef& t = pressTarget_;
        if (t->kind == PropKind::Spawn && t->groupName == functional::GRP_DOMINATION &&
            t->name == functional::DOMINATION_SPAWN)
            scene.dominationLinkStart(t);
        else if (t->kind == PropKind::ControlPoint)
            scene.dominationLinkEnd(t);
    }
    pressTarget_.reset();

    mouseDown_ = false;
    selectionRectVisible = false;
    scene.panelVersion++;  // showPropertyPanelIsNeccessary
}

void SceneContainer::checkConflict() {
    if (multiplePropMode == MultiPropMode::ANY) return;
    for (auto& p : scene.selectedProps) {
        bool conflict = (multiplePropMode == MultiPropMode::NONE && scene.occupyMap.isConflict(*p)) ||
                        (multiplePropMode == MultiPropMode::GROUP && scene.occupyMap.isConflictGroup(*p));
        if (conflict) {
            conflictAlert();
            break;
        }
    }
}

void SceneContainer::conflictAlert() {
    if (!askYesNo) return;
    askYesNo(Locale::get("ERROR_LOCATION_OCCUPIED"), [this](bool yes) {
        if (!yes)
            if (JournalItem* item = journal.undo(true)) scene.undo(*item);
    });
}

void SceneContainer::onMouseMove(double x, double y, Modifiers m) {
    PropRef sel = scene.selectedProp();
    PropSet& selected = scene.selectedProps;
    if (std::abs(mouseOX_ - x) + std::abs(mouseOY_ - y) > cloneThreshold_ || leaveClickZone_) {
        leaveClickZone_ = true;
        if (scene.propMouseDown && sel) {
            if (m.shift && !copy_) {
                if (startSelectionCoords_ != sel->position()) {
                    journal.addEvent(JournalItem::MOVE, selected, sel->position() - startSelectionCoords_);
                }
                PropSet copies;
                PropRef newMain = sel;
                for (auto& p : std::vector<PropRef>(selected.items())) {
                    PropRef c = scene.addProp(p, p->position(), p->rotationZ);
                    if (p == sel) newMain = c;
                    copies.add(c);
                }
                scene.selectProps(copies, false);
                scene.setSelectedProp(newMain);
                sel = newMain;
                startSelectionCoords_ = newMain->position();
                copy_ = true;
            }
            scene.moveSelectedPropsByMouse(verticalMoving_, x, y);
        } else if (middleDown_) {
            Matrix4 cm = scene.camera.cameraMatrix();
            Vec3 ax = cm.axisX() * (10 * (prevMoveX_ - x));
            Vec3 ay = cm.axisY() * (10 * (prevMoveY_ - y));
            Vec3 pos = cm.position() + ax + ay;
            scene.camera.cameraLocal = scene.camera.containerGlobalToLocal(pos);
        } else if (mouseDown_) {
            double w = std::abs(mouseDownX_ - x), h = std::abs(mouseDownY_ - y);
            if (w > 3 && h > 3) {
                double rx = std::min(x, mouseDownX_), ry = std::min(y, mouseDownY_);
                selectionRectVisible = true;
                selRectX = rx;
                selRectY = ry;
                selRectW = w;
                selRectH = h;
                if (m.shift) {
                    PropSet old = rectProps_;
                    if (m.alt) {
                        rectProps_ = scene.getPropsUnderRect(rx, ry, w, h, false, true);
                        for (auto& p : old)
                            if (!rectProps_.has(p) && selected.has(p)) p->select();
                    } else {
                        rectProps_ = scene.getPropsUnderRect(rx, ry, w, h, true, true);
                        for (auto& p : old)
                            if (!rectProps_.has(p) && !selected.has(p)) p->deselect();
                    }
                } else {
                    scene.selectProps(scene.getPropsUnderRect(rx, ry, w, h, true, false), false);
                }
                scene.cursor.setVisible(false);
            }
        }
    }
    if (scene.camera.mouseLookActive()) scene.camera.mouseLookMove(x, y);
    prevMoveX_ = x;
    prevMoveY_ = y;
}

void SceneContainer::onMouseWheel(double delta, Modifiers m) {
    int step = m.shift ? 20 : 3;
    if (delta < 0) step = -step;
    zoom(step);
}

void SceneContainer::zoom(int step) {
    CameraRig& cam = scene.camera;
    Vec3 camPos = cam.cameraLocal;
    Vec3 offset;
    if (PropRef sel = scene.selectedProp()) {
        Vec3 target = cam.containerGlobalToLocal(sel->position());
        offset = target - camPos;
    } else {
        // camera.localToGlobal(Z_AXIS, true) as a direction, then into container space
        Vec3 dirWorld = cam.cameraMatrix().transformDir({0, 0, 1});
        offset = cam.containerMatrix().inverted().transformDir(dirWorld) * 1000;
    }
    double s = step * 100.0;
    s = std::min(offset.length() - 10, s);
    offset = offset.normalized() * s;
    cam.cameraLocal = camPos + offset;
}

void SceneContainer::onMiddleMouseDown(double x, double y, Modifiers m) {
    CameraRig& cam = scene.camera;
    if (m.alt) {
        Vec3 pivot;
        if (PropRef sel = scene.selectedProp()) {
            Vec2 c = scene.getPropsGroupCenter(scene.selectedProps);
            pivot = {c.x, c.y, sel->z};
        } else {
            pivot = cam.cameraMatrix().transform({0, 0, cameraDistance_});
        }
        Vec3 diff = cam.containerPos - pivot;
        Vec3 camWorld = cam.containerLocalToGlobal(cam.cameraLocal) + diff;
        cam.cameraLocal = cam.containerGlobalToLocal(camWorld);
        cam.containerPos = pivot;
        cam.startMouseLook(x, y);
    } else {
        middleDown_ = true;
    }
    prevMoveX_ = x;
    prevMoveY_ = y;
}

void SceneContainer::onMiddleMouseUp() {
    middleDown_ = false;
    scene.camera.stopMouseLook();
}

void SceneContainer::update(double dt, const CameraRig::Input& held) { scene.camera.update(dt, held); }

void SceneContainer::resetCamera() {
    scene.camera.cameraRotX = CameraRig::DEFAULT_ROT_X;
    scene.camera.cameraLocal = {250, -7800, 4670};
    cameraDistance_ = scene.camera.cameraLocal.length();
}

// ---------------------------------------------------------------------------------------------
// Keyboard

static bool toArrow(Key k, ArrowKey& out) {
    switch (k) {
        case Key::Up: out = ArrowKey::Up; return true;
        case Key::Down: out = ArrowKey::Down; return true;
        case Key::Left: out = ArrowKey::Left; return true;
        case Key::Right: out = ArrowKey::Right; return true;
        default: return false;
    }
}

void SceneContainer::onKeyDown(Key key, Modifiers m) {
    CursorScene& cursor = scene.cursor;
    ArrowKey arrow;
    switch (key) {
        case Key::Num0:
            if (m.ctrl) resetCamera();
            break;
        case Key::Up:
        case Key::Down:
        case Key::Left:
        case Key::Right:
            toArrow(key, arrow);
            if (cursor.visible()) {
                cursor.moveByArrows(arrow);
                break;
            }
            if (PropRef sel = scene.selectedProp()) {
                Vec3 before = sel->position();
                scene.moveByArrows(arrow);
                journal.addEvent(JournalItem::MOVE, scene.selectedProps, sel->position() - before);
            }
            break;
        case Key::V:
            verticalMoving_ = true;
            break;
        case Key::W:
        case Key::S: {
            if (key == Key::S && m.ctrl) break;
            bool down = key == Key::S;
            if (cursor.visible()) {
                if (auto g = cursor.object()) {
                    g->setZ(g->z + (down ? -grid::vStep() : grid::vStep()));
                    cursor.checkForConflicts();
                }
                break;
            }
            if (PropRef sel = scene.selectedProp()) {
                Vec3 before = sel->position();
                scene.verticalMove(down);
                journal.addEvent(JournalItem::MOVE, scene.selectedProps, sel->position() - before);
            }
            break;
        }
        case Key::Delete:
        case Key::C:
            if (PropRef sel = scene.selectedProp()) {
                if (auto g = cursor.object()) {
                    g->setPosition(sel->x, sel->y, sel->z);
                    if (snapMode_) g->snapToGrid();
                }
                PropSet deleted = scene.deleteProps();
                journal.addEvent(JournalItem::DELETE, deleted);
                cursor.setVisible(true);
            }
            break;
        case Key::Z:
            if (cursor.visible()) {
                cursor.rotateCounterClockwise();
                cursor.checkForConflicts();
                break;
            }
            journal.addEvent(JournalItem::ROTATE, scene.selectedProps, {}, false);
            scene.rotateCounterClockwise();
            break;
        case Key::X:
            if (cursor.visible()) {
                cursor.rotateClockwise();
                cursor.checkForConflicts();
                break;
            }
            journal.addEvent(JournalItem::ROTATE, scene.selectedProps, {}, true);
            scene.rotateClockwise();
            break;
        case Key::Escape:
            if (PropRef sel = scene.selectedProp()) {
                if (auto g = cursor.object()) {
                    g->setPosition(sel->x, sel->y, sel->z);
                    g->snapToGrid();
                }
                scene.deselectProps();
                cursor.setVisible(true);
            }
            break;
        case Key::NumpadAdd: zoom(3); break;
        case Key::NumpadSubtract: zoom(-3); break;
        case Key::Equal:
            if (m.ctrl) zoom(3);
            break;
        case Key::Minus:
            if (m.ctrl) zoom(-3);
            break;
        case Key::Q: scene.selectConflictingProps(); break;
        case Key::T:
            if (m.shift)
                scene.hidePlaneBounds();
            else
                scene.showPlaneBounds();
            break;
        case Key::K: grid::toggleResolution(); break;
        default: break;
    }
}

void SceneContainer::onKeyUp(Key key) {
    if (key == Key::V) verticalMoving_ = false;
}

// ---------------------------------------------------------------------------------------------

void SceneContainer::addProp(const PropRef& prefab) {
    PropRef ghost = scene.cursor.object();
    if (!ghost) return;
    PropRef p = scene.addProp(prefab, ghost->position(), ghost->rotationZ);
    PropSet set;
    set.add(p);
    journal.addEvent(JournalItem::ADD, set);
    scene.cursor.checkForConflicts();  // setTimeout(checkForConflicts, 200)
    p->clickZListener = true;
    if (snapMode_ && !scene.cursor.freeState() &&
        ((multiplePropMode == MultiPropMode::NONE && scene.occupyMap.isConflict(*p)) ||
         (multiplePropMode == MultiPropMode::GROUP && scene.occupyMap.isConflictGroup(*p))))
        conflictAlert();
}

void SceneContainer::undo() {
    if (JournalItem* item = journal.undo()) {
        scene.undo(*item);
        if (scene.cursor.visible()) scene.cursor.checkForConflicts();
    }
}

void SceneContainer::redo() {
    if (JournalItem* item = journal.redo()) {
        scene.redo(*item);
        if (scene.cursor.visible()) scene.cursor.checkForConflicts();
    }
}

}  // namespace ae
