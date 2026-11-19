// Camera rig of CursorScene: root -> container (pivot, WalkController) -> camera (WalkController).
// Projection follows Alternativa Camera3D: diagonal FOV of 90 degrees, camera space X right,
// Y down, Z forward.
#pragma once
#include "core/Math.h"

namespace ae {

struct Viewport {
    double width = 100, height = 100;
};

class CameraRig {
  public:
    static constexpr double FOV = PI / 2;
    static constexpr double NEAR_CLIP = 1;
    static constexpr double FAR_CLIP = 1000000;
    static constexpr double DEFAULT_ROT_X = -DEG90 - DEG30;  // -120 degrees

    CameraRig() { reset(); }
    void reset();

    // Container (pivot) state
    Vec3 containerPos;
    double containerRotX = 0, containerRotZ = 0;
    // Camera local state (relative to the container)
    Vec3 cameraLocal{250, -7800, 4670};
    double cameraRotX = DEFAULT_ROT_X;

    Matrix4 containerMatrix() const { return Matrix4::compose(containerPos.x, containerPos.y, containerPos.z, containerRotX, 0, containerRotZ); }
    Matrix4 cameraLocalMatrix() const { return Matrix4::compose(cameraLocal.x, cameraLocal.y, cameraLocal.z, cameraRotX, 0, 0); }
    Matrix4 cameraMatrix() const { return Matrix4::mul(containerMatrix(), cameraLocalMatrix()); }

    static double focalLength(const Viewport& vp);
    // OpenGL matrices (view uses Alternativa camera axes converted to GL conventions).
    Mat4f viewMatrix() const;
    Mat4f projectionMatrix(const Viewport& vp) const;

    // Camera3D.projectViewPointToPlane. Returns NaNs if the ray is parallel to the plane.
    Vec3 projectViewPointToPlane(double mx, double my, const Viewport& vp, const Vec3& normal, double offset) const;
    // Camera3D.projectGlobal: returns screen x, y and camera-space depth z.
    Vec3 projectGlobal(const Vec3& p, const Viewport& vp) const;
    // Ray through a view pixel (origin = camera position, normalized direction).
    void ray(double mx, double my, const Viewport& vp, Vec3& origin, Vec3& dir) const;

    // Container-space helpers (container.globalToLocal / localToGlobal)
    Vec3 containerGlobalToLocal(const Vec3& p) const { return containerMatrix().inverted().transform(p); }
    Vec3 containerLocalToGlobal(const Vec3& p) const { return containerMatrix().transform(p); }

    // ---- container WalkController (keyboard) ----
    struct Input {
        bool yawLeft = false, yawRight = false, pitchUp = false, pitchDown = false;
        bool left = false, right = false, forward = false, back = false;
    };
    void update(double frameTime, const Input& in);

    // ---- mouse look on the container ----
    void startMouseLook(double sx, double sy);
    void stopMouseLook() { mouseLook_ = false; }
    bool mouseLookActive() const { return mouseLook_; }
    void mouseLookMove(double sx, double sy);

    double containerSpeed = 2000;

  private:
    Vec3 velocity_;
    bool mouseLook_ = false;
    double startMouseX_ = 0, startMouseY_ = 0, startRotX_ = 0, startRotZ_ = 0;
    double lastMouseX_ = -1e9, lastMouseY_ = -1e9;
};

}  // namespace ae
