// 3DS parser (behavioural port of alternativa.engine3d.loaders.Parser3DS).
#pragma once
#include <string>
#include <vector>

#include "scene/Mesh.h"

namespace ae {

struct Material3DS {
    std::string name;
    uint32_t color = 0;
    std::string diffuseMap;  // lower-cased file name, empty for fill materials
    std::string opacityMap;  // lower-cased, may be empty
};

class Parser3DS {
  public:
    bool parse(const std::vector<uint8_t>& data);
    bool parseFile(const std::string& path, std::string* err = nullptr);

    std::vector<MeshRef> objects;               // in keyframer (or file) order
    std::vector<Material3DS> materials;         // file order
    std::vector<Material3DS> textureMaterials;  // materials with a diffuse map, file order

    MeshRef getObjectByName(const std::string& name) const;
};

}  // namespace ae
