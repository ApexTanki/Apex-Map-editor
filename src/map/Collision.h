// Port of alternativa.editor.mapexport.Collision{Primitive,Rect,Box,Triangle} + cache.
#pragma once
#include <map>
#include <string>
#include <vector>

#include <pugixml.hpp>

#include "core/Math.h"
#include "scene/Prop.h"

namespace ae {

struct CollisionPrimitive {
    enum Kind { Rect, Box, Triangle } kind = Box;
    int id = 0;
    Matrix4 transform;
    // Rect
    double width = 0, length = 0;
    // Box
    Vec3 size;
    // Triangle (2D in its own plane)
    Vec3 v0, v1, v2;

    // Matrix used for export: transform combined with the prop transform.
    Matrix4 worldTransform(const Matrix4* propMatrix) const {
        if (!propMatrix) return transform;
        return Matrix4::mul(*propMatrix, transform);
    }
    void appendXml(pugi::xml_node parent, const Matrix4& propMatrix) const;
};

// Builds primitives from a mesh prop's collision meshes ("plane*", "box*", "tri*").
std::vector<CollisionPrimitive> createPropCollisionPrimitives(const Prop& prop);

class CollisionPrimitivesCache {
  public:
    const std::vector<CollisionPrimitive>* get(const std::string& lib, const std::string& group, const std::string& name) const {
        auto it = cache_.find(lib + '\x1f' + group + '\x1f' + name);
        return it == cache_.end() ? nullptr : &it->second;
    }
    const std::vector<CollisionPrimitive>& add(const std::string& lib, const std::string& group, const std::string& name,
                                               std::vector<CollisionPrimitive> prims) {
        return cache_[lib + '\x1f' + group + '\x1f' + name] = std::move(prims);
    }
    void clear() { cache_.clear(); }

  private:
    std::map<std::string, std::vector<CollisionPrimitive>> cache_;
};

}  // namespace ae
