// Ray queries used for mouse picking (MouseEvent3D) and for the bonus-region shadow collider.
#include <cfloat>
#include <cmath>

#include "scene/MainScene.h"

namespace ae::picking {

static bool rayTriangle(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c, double& t) {
    const double EPS = 1e-12;
    Vec3 e1 = b - a, e2 = c - a;
    Vec3 p = d.cross(e2);
    double det = e1.dot(p);
    if (std::abs(det) < EPS) return false;
    double inv = 1.0 / det;
    Vec3 s = o - a;
    double u = s.dot(p) * inv;
    if (u < 0 || u > 1) return false;
    Vec3 q = s.cross(e1);
    double v = d.dot(q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = e2.dot(q) * inv;
    return t >= 0;
}

static bool rayAabb(const Vec3& o, const Vec3& d, const Vec3& mn, const Vec3& mx) {
    double t0 = -DBL_MAX, t1 = DBL_MAX;
    const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    const double lo[3] = {mn.x, mn.y, mn.z}, hi[3] = {mx.x, mx.y, mx.z};
    for (int a = 0; a < 3; ++a) {
        if (std::abs(dd[a]) < 1e-15) {
            if (oo[a] < lo[a] - 1e-6 || oo[a] > hi[a] + 1e-6) return false;
            continue;
        }
        double ta = (lo[a] - oo[a]) / dd[a], tb = (hi[a] - oo[a]) / dd[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1 + 1e-9) return false;
    }
    return t1 >= 0;
}

bool rayMesh(const MeshObject& mesh, const Matrix4& world, const Vec3& o, const Vec3& d, double& tOut) {
    if (mesh.indices.empty()) return false;
    // Intersect in object space; t is preserved because the direction is not renormalized.
    Matrix4 inv = world.inverted();
    Vec3 lo = inv.transform(o), ld = inv.transformDir(d);
    Vec3 pad{1e-3, 1e-3, 1e-3};
    if (!rayAabb(lo, ld, mesh.boundMin - pad, mesh.boundMax + pad)) return false;
    bool hit = false;
    double best = DBL_MAX;
    for (size_t q = 0; q + 2 < mesh.indices.size(); q += 3) {
        double t;
        if (rayTriangle(lo, ld, mesh.vertex(mesh.indices[q]), mesh.vertex(mesh.indices[q + 1]),
                        mesh.vertex(mesh.indices[q + 2]), t) &&
            t < best) {
            best = t;
            hit = true;
        }
    }
    if (hit) tOut = best;
    return hit;
}

void spriteQuad(const Prop& p, const CameraRig& cam, Vec3 out[4]) {
    Matrix4 cm = cam.cameraMatrix();
    Vec3 right = cm.axisX().normalized(), down = cm.axisY().normalized();
    const PropModel& m = *p.model;
    double w = m.spriteWidth * m.spriteScale * p.scaleX;
    double h = m.spriteHeight * m.spriteScale * p.scaleY;
    double left = -m.originX * w, rightE = (1 - m.originX) * w;
    double top = -m.originY * h, bottom = (1 - m.originY) * h;
    Vec3 c = p.position();
    out[0] = c + right * left + down * bottom;   // bottom-left  (u0, v1)
    out[1] = c + right * rightE + down * bottom; // bottom-right (u1, v1)
    out[2] = c + right * rightE + down * top;    // top-right    (u1, v0)
    out[3] = c + right * left + down * top;      // top-left     (u0, v0)
}

bool rayProp(const Prop& p, const CameraRig& cam, bool collisionView, const Vec3& o, const Vec3& d, double& tOut) {
    if (!p.model) return false;
    bool hit = false;
    double best = DBL_MAX, t;
    if (p.isSprite()) {
        Vec3 q[4];
        spriteQuad(p, cam, q);
        if (rayTriangle(o, d, q[0], q[1], q[2], t) && t < best) { best = t; hit = true; }
        if (rayTriangle(o, d, q[0], q[2], q[3], t) && t < best) { best = t; hit = true; }
    } else if (p.model->mesh) {
        if (rayMesh(*p.model->mesh, p.objectMatrix(), o, d, t) && t < best) { best = t; hit = true; }
        if (collisionView && p.kind == PropKind::Mesh) {
            Matrix4 pm = p.transformation();
            for (auto& cmesh : p.model->collision)
                if (rayMesh(*cmesh, Matrix4::mul(pm, cmesh->localMatrix()), o, d, t) && t < best) {
                    best = t;
                    hit = true;
                }
        }
    }
    if (hit) tOut = best;
    return hit;
}

}  // namespace ae::picking
