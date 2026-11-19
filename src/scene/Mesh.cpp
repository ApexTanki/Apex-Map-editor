#include "scene/Mesh.h"

#include <algorithm>
#include <cfloat>

namespace ae {

void MeshObject::calculateBounds() {
    boundMin = {DBL_MAX, DBL_MAX, DBL_MAX};
    boundMax = {-DBL_MAX, -DBL_MAX, -DBL_MAX};
    for (size_t v = 0; v < vertexCount(); ++v) {
        Vec3 p = vertex(v);
        boundMin = {std::min(boundMin.x, p.x), std::min(boundMin.y, p.y), std::min(boundMin.z, p.z)};
        boundMax = {std::max(boundMax.x, p.x), std::max(boundMax.y, p.y), std::max(boundMax.z, p.z)};
    }
    if (vertexCount() == 0) boundMin = boundMax = {};
}

MeshObject::~MeshObject() { releaseGpu(); }

void MeshObject::releaseGpu() {
    if (!glDeleteBuffers) return;
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
}

void MeshObject::draw() {
    if (indices.empty()) return;
    if (!vao_) {
        std::vector<float> interleaved;
        interleaved.reserve(vertexCount() * 5);
        for (size_t v = 0; v < vertexCount(); ++v) {
            interleaved.push_back(positions[v * 3]);
            interleaved.push_back(positions[v * 3 + 1]);
            interleaved.push_back(positions[v * 3 + 2]);
            interleaved.push_back(uvs.size() > v * 2 ? uvs[v * 2] : 0.f);
            interleaved.push_back(uvs.size() > v * 2 + 1 ? uvs[v * 2 + 1] : 0.f);
        }
        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, interleaved.size() * sizeof(float), interleaved.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &ebo_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

namespace primitives {

static void addQuad(MeshObject& m, const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3) {
    // p0..p3 counter-clockwise when viewed from outside.
    uint32_t base = (uint32_t)m.vertexCount();
    const Vec3* ps[4] = {&p0, &p1, &p2, &p3};
    const float uv[8] = {0, 1, 1, 1, 1, 0, 0, 0};
    for (int q = 0; q < 4; ++q) {
        m.positions.insert(m.positions.end(), {(float)ps[q]->x, (float)ps[q]->y, (float)ps[q]->z});
        m.uvs.insert(m.uvs.end(), {uv[q * 2], uv[q * 2 + 1]});
    }
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

MeshRef box(double w, double l, double h) {
    auto m = std::make_shared<MeshObject>();
    m->name = "Box";
    double x = w / 2, y = l / 2, z = h / 2;
    Vec3 v000{-x, -y, -z}, v100{x, -y, -z}, v110{x, y, -z}, v010{-x, y, -z};
    Vec3 v001{-x, -y, z}, v101{x, -y, z}, v111{x, y, z}, v011{-x, y, z};
    addQuad(*m, v010, v110, v100, v000);  // bottom (-Z)
    addQuad(*m, v001, v101, v111, v011);  // top (+Z)
    addQuad(*m, v000, v100, v101, v001);  // front (-Y)
    addQuad(*m, v100, v110, v111, v101);  // right (+X)
    addQuad(*m, v110, v010, v011, v111);  // back (+Y)
    addQuad(*m, v010, v000, v001, v011);  // left (-X)
    m->calculateBounds();
    return m;
}

MeshRef plane(double w, double l) {
    auto m = std::make_shared<MeshObject>();
    m->name = "Plane";
    double x = w / 2, y = l / 2;
    addQuad(*m, {-x, -y, 0}, {x, -y, 0}, {x, y, 0}, {-x, y, 0});
    m->calculateBounds();
    return m;
}

}  // namespace primitives
}  // namespace ae
