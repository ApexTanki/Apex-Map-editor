// Port of alternativa.editor.scene.MainScene / EditorScene / CursorScene.
#pragma once
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "scene/Camera.h"
#include "scene/OccupyMap.h"
#include "scene/Prop.h"

namespace ae {

enum class CameraFacing { X, Y, NEG_X, NEG_Y };
enum class ArrowKey { Up, Down, Left, Right };

class MainScene;

// Ray / geometry queries against props (implemented in Picking.cpp).
namespace picking {
bool rayMesh(const MeshObject& mesh, const Matrix4& world, const Vec3& o, const Vec3& d, double& tOut);
bool rayProp(const Prop& p, const CameraRig& cam, bool collisionView, const Vec3& o, const Vec3& d, double& tOut);
// World-space corners of a sprite billboard facing the camera (bl, br, tr, tl).
void spriteQuad(const Prop& p, const CameraRig& cam, Vec3 out[4]);
}  // namespace picking

// The cursor ghost that follows clicks (CursorScene).
class CursorScene {
  public:
    explicit CursorScene(MainScene& s) : scene_(s) {}
    void setObject(const PropRef& prefab);
    PropRef object() const { return ghost_; }
    PropRef prefab() const { return prefab_; }
    void setSnapMode(bool snap);
    void moveCursorByMouse(double mx, double my);
    bool freeState() const { return freeState_; }
    void checkForConflicts();
    void clear();
    void setVisible(bool v);
    bool visible() const { return visible_; }
    void moveByArrows(ArrowKey k);
    void rotateCounterClockwise();
    void rotateClockwise();

  private:
    void clearSelection();
    void snapToGridIfNecessary();
    MainScene& scene_;
    PropRef prefab_, ghost_;
    bool freeState_ = true;
    bool snapMode_ = true;
    bool visible_ = false;
};

class MainScene {
  public:
    MainScene();

    CameraRig camera;
    Viewport viewport;
    OccupyMap occupyMap;
    CursorScene cursor{*this};

    std::vector<PropRef> children;   // root.children (props only, in insertion order)
    PropSet selectedProps;
    bool propMouseDown = false;
    bool snapMode = true;
    bool changed = false;
    bool gridVisible = true;
    bool collisionView = false;

    // Selectable type filter (toolbar combo index 0..3).
    void setSelectableFilter(int index);
    int selectableFilter() const { return filterIndex_; }
    bool isSelectableProp(const Prop& p) const;

    PropRef selectedProp() const { return selectedProp_; }
    void setSelectedProp(const PropRef& p) { selectedProp_ = p; }

    // ---- scene content ----
    PropRef addProp(const PropRef& src, const Vec3& pos, double rotation, bool cloneIt = true, bool occupy = true);
    PropSet deleteProps(const PropSet* set = nullptr);
    void clear();
    bool contains(const Prop* p) const;

    // ---- selection ----
    void onPropMouseDown(const PropRef& target, bool shift, bool alt, bool ctrl);
    void deselectProps();
    void deselectProp(const PropRef& p, bool updatePanels);
    void selectProps(const PropSet& newSelection, bool updatePanels);
    void selectProp(const PropRef& p, bool updatePanels);
    PropSet getPropsUnderRect(double rx, double ry, double rw, double rh, bool select, bool changeState);
    void selectAll();
    void selectConflictingProps();

    // ---- editing ----
    void moveSelectedPropsByMouse(bool vertical, double mx, double my);
    void moveByArrows(ArrowKey key);
    void verticalMove(bool down);
    void moveProps(const PropSet& set, const Vec3& delta);
    void rotateCounterClockwise(const PropSet* set = nullptr);
    void rotateClockwise(const PropSet* set = nullptr);
    void hideSelectedProps();
    void showAll();
    void showPlaneBounds();
    void hidePlaneBounds();
    void undo(JournalItem& item);
    void redo(JournalItem& item);

    // ---- EditorScene helpers ----
    CameraFacing getCameraFacing() const;
    void move(Prop& p, ArrowKey key);
    Vec2 getPropsGroupCenter(const PropSet& set) const;
    void rotatePropsCounterClockwise(const PropSet& set);
    void rotatePropsClockwise(const PropSet& set);

    // ---- layers ----
    Layers layers;
    void setLayerVisible(const std::string& layer, bool visible);
    void onLayerContentChange(const PropRef& p, const std::string& layer, bool contains);
    std::vector<PropRef> dominationPoints() const;

    // ---- domination links (Ctrl+click) ----
    void dominationLinkStart(const PropRef& spawn) { domSpawnPoint_ = spawn; }
    void dominationLinkEnd(const PropRef& controlPoint);

    // ---- queries ----
    PropRef pick(double mx, double my) const;  // nearest selectable prop under the mouse
    // MainScene.getProjectedPoint: drop a 30-unit ellipsoid straight down from `from`.
    Vec3 projectDown(const Vec3& from, const Prop* ignore) const;
    void updateShadows();

    // Panel refresh requests for the UI (MainScene.showPropertyPanel / hideAllPropertyPanelItems).
    int panelVersion = 0;

  private:
    void addPropToLayer(const PropRef& p, const std::string& layer = "");
    void checkPropVisibility(const PropRef& p);
    void hidePropInternal(const PropRef& p);
    void showPropInternal(const PropRef& p);
    void freeProps(const PropSet& set);
    void rotateProps(const PropSet& set, double angle);
    void requestPanels() { ++panelVersion; }

    PropRef selectedProp_;
    std::vector<PropRef> hiddenProps_;
    PropRef domSpawnPoint_;
    int filterIndex_ = 0;
};

}  // namespace ae
