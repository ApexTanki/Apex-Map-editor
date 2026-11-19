// Port of alternativa.editor.SceneContainer: mouse/keyboard handling of the 3D workspace.
#pragma once
#include <functional>
#include <string>

#include "scene/MainScene.h"

namespace ae {

enum class MultiPropMode { ANY, GROUP, NONE };

struct Modifiers {
    bool shift = false, ctrl = false, alt = false;
};

// Keys the workspace reacts to (translated from the UI toolkit).
enum class Key {
    None, Num0, Up, Down, Left, Right, V, W, S, Delete, C, Z, X, Escape, NumpadAdd, NumpadSubtract,
    Equal, Minus, Q, T, K
};

class SceneContainer {
  public:
    explicit SceneContainer(MainScene& scene);

    MainScene& scene;
    EventJournal journal;
    MultiPropMode multiplePropMode = MultiPropMode::NONE;
    bool showBoundBoxes = false;

    // UI hooks
    std::function<void(const std::string& message, std::function<void(bool yes)>)> askYesNo;

    void setSnapMode(bool snap);
    bool snapMode() const { return snapMode_; }

    // Mouse (coordinates are view-local pixels)
    void onMouseDown(double x, double y, Modifiers m);
    void onMouseUp(double x, double y, Modifiers m);
    void onMouseMove(double x, double y, Modifiers m);
    void onMouseWheel(double delta, Modifiers m);
    void onMiddleMouseDown(double x, double y, Modifiers m);  // middle or right button
    void onMiddleMouseUp();

    void onKeyDown(Key key, Modifiers m);
    void onKeyUp(Key key);

    // Per-frame update (onEnterFrame). `held` = KeyMapper state.
    void update(double dt, const CameraRig::Input& held);

    void addProp(const PropRef& prefab);  // Space: paste the prefab at the cursor
    void undo();
    void redo();
    void clearEventJournal() { journal.clear(); }
    void zoom(int step);
    void resetCamera();

    // Rubber band in view coordinates (valid while selecting).
    bool selectionRectVisible = false;
    double selRectX = 0, selRectY = 0, selRectW = 0, selRectH = 0;

  private:
    void checkConflict();
    void conflictAlert();

    bool snapMode_ = false;
    bool verticalMoving_ = false;
    bool copy_ = false;
    bool mouseDown_ = false;
    double mouseDownX_ = 0, mouseDownY_ = 0;
    double mouseOX_ = 0, mouseOY_ = 0;
    double prevMoveX_ = 0, prevMoveY_ = 0;
    bool leaveClickZone_ = false;
    const double cloneThreshold_ = 20;
    Vec3 startSelectionCoords_;
    PropSet rectProps_;
    bool middleDown_ = false;
    double cameraDistance_ = 0;
    double clickZ_ = 0;
    bool propDown_ = false;
    PropRef pressTarget_;  // prop under the mouse at press time (for CLICK events)
};

}  // namespace ae
