#include "map/Collision.h"

#include <cfloat>
#include <cmath>

#include "core/Util.h"

namespace ae {

static constexpr int PRECISION = 3;
static constexpr int ROT_PRECISION = 6;

static void addVec(pugi::xml_node parent, const char* name, double x, double y, double z, int precision) {
    pugi::xml_node n = parent.append_child(name);
    n.append_child("x").text().set(toFixed(x, precision).c_str());
    n.append_child("y").text().set(toFixed(y, precision).c_str());
    n.append_child("z").text().set(toFixed(z, precision).c_str());
}

static CollisionPrimitive parseRect(const MeshObject& mesh) {
    CollisionPrimitive p;
    p.kind = CollisionPrimitive::Rect;
    Vec3 v[3] = {mesh.vertex(mesh.indices[0]), mesh.vertex(mesh.indices[1]), mesh.vertex(mesh.indices[2])};
    Vec3 edges[3];
    double lens[3];
    double maxLen = -1;
    int maxIdx = 0;
    for (int q = 0; q < 3; ++q) {
        edges[q] = v[(q + 1) % 3] - v[q];
        lens[q] = edges[q].length();
        if (lens[q] > maxLen) {
            maxLen = lens[q];
            maxIdx = q;
        }
    }
    int i8 = (maxIdx + 2) % 3, i9 = (maxIdx + 1) % 3;
    Vec3 a = edges[i8];
    Vec3 b = -edges[i9];
    p.width = lens[i8];
    p.length = lens[i9];
    Vec3 center = v[(maxIdx + 2) % 3];
    center.x += 0.5 * (a.x + b.x);
    center.y += 0.5 * (a.y + b.y);
    center.z += 0.5 * (a.z + b.z);
    a = a.normalized();
    b = b.normalized();
    Vec3 n = a.cross(b);
    p.transform.setVectors(a, b, n, center);
    p.transform.rotate(mesh.rotX, mesh.rotY, mesh.rotZ);
    p.transform.translate(mesh.x, mesh.y, mesh.z);
    return p;
}

static CollisionPrimitive parseBox(const MeshObject& mesh) {
    CollisionPrimitive p;
    p.kind = CollisionPrimitive::Box;
    double minX = DBL_MAX, maxX = -DBL_MAX, minY = DBL_MAX, maxY = -DBL_MAX, minZ = DBL_MAX, maxZ = -DBL_MAX;
    for (size_t q = 0; q < mesh.vertexCount(); ++q) {
        Vec3 v = mesh.vertex(q);
        minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
        minZ = std::min(minZ, v.z); maxZ = std::max(maxZ, v.z);
    }
    p.size = {maxX - minX, maxY - minY, maxZ - minZ};
    Vec3 center{0.5 * (maxX + minX), 0.5 * (maxY + minY), 0.5 * (maxZ + minZ)};
    p.transform = Matrix4::identity();
    p.transform.rotate(mesh.rotX, mesh.rotY, mesh.rotZ);
    p.transform.translate(mesh.x, mesh.y, mesh.z);
    center = p.transform.transform(center);
    p.transform.d = center.x;
    p.transform.h = center.y;
    p.transform.l = center.z;
    return p;
}

static CollisionPrimitive parseTriangle(const MeshObject& mesh) {
    CollisionPrimitive p;
    p.kind = CollisionPrimitive::Triangle;
    Vec3 v0 = mesh.vertex(mesh.indices[0]), v1 = mesh.vertex(mesh.indices[1]), v2 = mesh.vertex(mesh.indices[2]);
    Vec3 center{(v0.x + v1.x + v2.x) / 3, (v0.y + v1.y + v2.y) / 3, (v0.z + v1.z + v2.z) / 3};
    Vec3 e1 = v1 - v0;
    p.v1 = {e1.length(), 0, 0};
    e1 = e1.normalized();
    Vec3 e2 = v2 - v0;
    double proj = e2.dot(e1);
    double h = std::sqrt(e2.lengthSqr() - proj * proj);
    p.v2 = {proj, h, 0};
    Vec3 n = e1.cross(e2).normalized();
    Vec3 yAxis = n.cross(e1).normalized();
    p.transform.setVectors(e1, yAxis, n, center);
    p.transform.rotate(mesh.rotX, mesh.rotY, mesh.rotZ);
    p.transform.translate(mesh.x, mesh.y, mesh.z);
    double cx = (p.v1.x + p.v2.x) / 3, cy = (p.v1.y + p.v2.y) / 3;
    p.v0 = {-cx, -cy, 0};
    p.v1.x -= cx;
    p.v1.y -= cy;
    p.v2.x -= cx;
    p.v2.y -= cy;
    return p;
}

std::vector<CollisionPrimitive> createPropCollisionPrimitives(const Prop& prop) {
    std::vector<CollisionPrimitive> out;
    if (!prop.model) return out;
    for (auto& mesh : prop.model->collision) {
        if (mesh->indices.size() < 3) continue;
        std::string n = toLower(mesh->name);
        if (startsWith(n, "plane"))
            out.push_back(parseRect(*mesh));
        else if (startsWith(n, "box"))
            out.push_back(parseBox(*mesh));
        else if (startsWith(n, "tri"))
            out.push_back(parseTriangle(*mesh));
    }
    return out;
}

void CollisionPrimitive::appendXml(pugi::xml_node parent, const Matrix4& propMatrix) const {
    Matrix4 m = worldTransform(&propMatrix);
    Vec3 rot = m.getRotations();
    pugi::xml_node node;
    switch (kind) {
        case Rect:
            node = parent.append_child("collision-plane");
            node.append_attribute("id") = id;
            node.append_child("width").text().set(toFixed(width, PRECISION).c_str());
            node.append_child("length").text().set(toFixed(length, PRECISION).c_str());
            break;
        case Box:
            node = parent.append_child("collision-box");
            node.append_attribute("id") = id;
            addVec(node, "size", size.x, size.y, size.z, PRECISION);
            break;
        case Triangle:
            node = parent.append_child("collision-triangle");
            node.append_attribute("id") = id;
            addVec(node, "v0", v0.x, v0.y, v0.z, PRECISION);
            addVec(node, "v1", v1.x, v1.y, v1.z, PRECISION);
            addVec(node, "v2", v2.x, v2.y, v2.z, PRECISION);
            break;
    }
    addVec(node, "position", m.d, m.h, m.l, PRECISION);
    addVec(node, "rotation", rot.x, rot.y, rot.z, ROT_PRECISION);
}

}  // namespace ae
