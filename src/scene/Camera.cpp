#include "scene/Camera.h"

#include <algorithm>
#include <cmath>

namespace ae {

void CameraRig::reset() {
    containerPos = {};
    containerRotX = containerRotZ = 0;
    cameraLocal = {250, -7800, 4670};
    cameraRotX = DEFAULT_ROT_X;
    velocity_ = {};
    mouseLook_ = false;
}

double CameraRig::focalLength(const Viewport& vp) {
    double hx = vp.width * 0.5, hy = vp.height * 0.5;
    return std::sqrt(hx * hx + hy * hy) / std::tan(FOV * 0.5);
}

Mat4f CameraRig::viewMatrix() const {
    // Alternativa camera space -> GL eye space: flip Y (down->up) and Z (forward->backward).
    Matrix4 inv = cameraMatrix().inverted();
    Matrix4 flip;
    flip.f = -1;
    flip.k = -1;
    return Mat4f::fromAffine(Matrix4::mul(flip, inv));
}

Mat4f CameraRig::projectionMatrix(const Viewport& vp) const {
    double f = focalLength(vp);
    double n = NEAR_CLIP, fa = FAR_CLIP;
    Mat4f p;
    for (float& v : p.m) v = 0;
    p.m[0] = (float)(f / (vp.width * 0.5));
    p.m[5] = (float)(f / (vp.height * 0.5));
    p.m[10] = (float)(-(fa + n) / (fa - n));
    p.m[11] = -1;
    p.m[14] = (float)(-2 * fa * n / (fa - n));
    return p;
}

void CameraRig::ray(double mx, double my, const Viewport& vp, Vec3& origin, Vec3& dir) const {
    Matrix4 m = cameraMatrix();
    double f = focalLength(vp);
    double px = mx - (double)((int)vp.width >> 1);
    double py = my - (double)((int)vp.height >> 1);
    origin = m.position();
    dir = m.transformDir({px, py, f});
}

Vec3 CameraRig::projectViewPointToPlane(double mx, double my, const Viewport& vp, const Vec3& n, double offset) const {
    Vec3 o, d;
    ray(mx, my, vp, o, d);
    double dot = n.dot(d);
    if (dot < 1e-8 && dot > -1e-8) return {std::nan(""), std::nan(""), std::nan("")};
    double t = (offset - o.dot(n)) / dot;
    return o + d * t;
}

Vec3 CameraRig::projectGlobal(const Vec3& p, const Viewport& vp) const {
    Vec3 c = cameraMatrix().inverted().transform(p);
    double f = focalLength(vp);
    double hx = vp.width * 0.5, hy = vp.height * 0.5;
    return {c.x * f / c.z + hx, c.y * f / c.z + hy, c.z};
}

void CameraRig::update(double frameTime, const Input& in) {
    // ObjectController.processInput: clamp to 100 ms
    frameTime = std::min(frameTime, 0.1);
    if (frameTime <= 0) return;
    const double yawSpeed = 1, pitchSpeed = 1;

    // WalkController.rotateObject (container is not a camera: pitch clamp +-90)
    if (in.yawLeft)
        containerRotZ += yawSpeed * frameTime;
    else if (in.yawRight)
        containerRotZ -= yawSpeed * frameTime;
    double rotX = std::nan("");
    if (in.pitchUp)
        rotX = containerRotX + pitchSpeed * frameTime;
    else if (in.pitchDown)
        rotX = containerRotX - pitchSpeed * frameTime;
    if (!std::isnan(rotX)) containerRotX = std::clamp(rotX, -DEG90, DEG90);

    // WalkController.getDisplacement (fly mode off, gravity 0)
    bool active = in.forward || in.back || in.left || in.right;
    double spd = containerSpeed;
    if (active) {
        double hc = std::cos(containerRotZ), hs = std::sin(containerRotZ);
        if (in.forward) {
            velocity_.x -= spd * hs;
            velocity_.y += spd * hc;
        } else if (in.back) {
            velocity_.x += spd * hs;
            velocity_.y -= spd * hc;
        }
        if (in.right) {
            velocity_.x += spd * hc;
            velocity_.y += spd * hs;
        } else if (in.left) {
            velocity_.x -= spd * hc;
            velocity_.y -= spd * hs;
        }
    } else {
        double k = 1 / std::pow(3.0, frameTime * 10);
        velocity_ *= k;
    }
    double len = velocity_.length();
    if (len > spd) velocity_ = velocity_ * (spd / len);
    Vec3 disp = velocity_ * frameTime;

    // applyDisplacement (no collisions)
    containerPos += disp;
    velocity_ = disp * (1 / frameTime);
    if (velocity_.length() < 1) velocity_ = {};  // speedThreshold
}

void CameraRig::startMouseLook(double sx, double sy) {
    mouseLook_ = true;
    startMouseX_ = sx;
    startMouseY_ = sy;
    startRotX_ = containerRotX;
    startRotZ_ = containerRotZ;
    lastMouseX_ = sx;
    lastMouseY_ = sy;
}

void CameraRig::mouseLookMove(double sx, double sy) {
    if (!mouseLook_) return;
    if (sx == lastMouseX_ && sy == lastMouseY_) return;
    lastMouseX_ = sx;
    lastMouseY_ = sy;
    const double coef = PI / 360;  // mouseSensitivity 1 * PI/360
    containerRotZ = startRotZ_ + (startMouseX_ - sx) * coef;
    double rx = startRotX_ + (startMouseY_ - sy) * coef;
    containerRotX = std::clamp(rx, -DEG90, DEG90);
}

}  // namespace ae
