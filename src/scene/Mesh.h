// Geometry shared between library prototypes and their scene instances.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/Math.h"
#include "gl/gl.h"

namespace ae {

struct MeshObject {
    std::string name;
    std::vector<float> positions;  // xyz per vertex (object-local)
    std::vector<float> uvs;        // uv per vertex (Flash convention: v = 0 at top)
    std::vector<uint32_t> indices; // 3 per triangle
    std::string firstFaceMaterial; // name of the 3DS material on the first face (if any)

    // Local transform relative to the owning prop (3DS keyframer data).
    double x = 0, y = 0, z = 0, rotX = 0, rotY = 0, rotZ = 0, scaleX = 1, scaleY = 1, scaleZ = 1;

    Matrix4 localMatrix() const { return Matrix4::compose(x, y, z, rotX, rotY, rotZ, scaleX, scaleY, scaleZ); }
    size_t vertexCount() const { return positions.size() / 3; }
    Vec3 vertex(size_t i) const { return {positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]}; }

    // Axis-aligned bounds in object-local space.
    Vec3 boundMin, boundMax;
    void calculateBounds();

    // GPU buffers (created on first draw).
    void draw();
    void releaseGpu();
    ~MeshObject();

  private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
};
using MeshRef = std::shared_ptr<MeshObject>;

namespace primitives {
// Alternativa primitives.Box: centred on the origin, outward-facing quads split into triangles.
MeshRef box(double width, double length, double height);
// Alternativa primitives.Plane: XY plane centred on the origin, facing +Z.
MeshRef plane(double width, double length);
}  // namespace primitives

}  // namespace ae
