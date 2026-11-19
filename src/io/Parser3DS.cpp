#include "io/Parser3DS.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>

#include "core/Util.h"

namespace ae {

namespace {

enum : uint16_t {
    CHUNK_MAIN = 0x4D4D,
    CHUNK_VERSION = 0x0002,
    CHUNK_SCENE = 0x3D3D,
    CHUNK_ANIMATION = 0xB000,
    CHUNK_OBJECT = 0x4000,
    CHUNK_TRIMESH = 0x4100,
    CHUNK_VERTICES = 0x4110,
    CHUNK_FACES = 0x4120,
    CHUNK_FACESMATERIAL = 0x4130,
    CHUNK_FACESSMOOTH = 0x4150,
    CHUNK_MAPPINGCOORDS = 0x4140,
    CHUNK_TRANSFORMATION = 0x4160,
    CHUNK_MATERIAL = 0xAFFF,
};

struct MapData {
    std::string filename;
    double scaleU = 1, scaleV = 1, offsetU = 0, offsetV = 0, rotation = 0;
};

struct MaterialData {
    std::string name;
    uint32_t color = 0;
    std::optional<MapData> diffuseMap, opacityMap;
    // UV matrix (flash.geom.Matrix a,b,c,d,tx,ty)
    bool hasMatrix = false;
    double ma = 1, mb = 0, mc = 0, md = 1, tx = 0, ty = 0;
};

struct ObjectData {
    std::string name;
    std::vector<float> vertices, uvs;
    std::vector<int> faces;
    std::vector<uint32_t> smoothing;
    std::vector<std::pair<std::string, std::vector<int>>> surfaces;
    bool hasMatrix = false;
    double a = 1, b = 0, c = 0, d = 0, e = 0, f = 1, g = 0, h = 0, i = 0, j = 0, k = 1, l = 0;
};

struct AnimationData {
    std::string objectName;
    int parentIndex = 0xFFFF;
    std::optional<Vec3> pivot, position, rotation, scale;
    bool isInstance = false;
    MeshRef object;
};

struct Chunk {
    uint16_t id = 0;
    uint32_t size = 0;
    size_t dataSize = 0, dataPosition = 0, nextChunkPosition = 0;
};

class Reader {
  public:
    explicit Reader(const std::vector<uint8_t>& d) : data(d) {}
    const std::vector<uint8_t>& data;
    size_t pos = 0;
    bool ok() const { return pos <= data.size(); }
    uint8_t u8() { return pos < data.size() ? data[pos++] : (pos++, 0); }
    uint16_t u16() { uint16_t v = 0; read(&v, 2); return v; }
    uint32_t u32() { uint32_t v = 0; read(&v, 4); return v; }
    float f32() { float v = 0; read(&v, 4); return v; }
    void read(void* out, size_t n) {
        if (pos + n <= data.size()) std::memcpy(out, &data[pos], n);
        pos += n;
    }
    std::string str(size_t at) {
        pos = at;
        std::string s;
        while (pos < data.size()) {
            char ch = (char)data[pos++];
            if (ch == 0) break;
            s += ch;
        }
        return s;
    }
};

class Impl {
  public:
    explicit Impl(const std::vector<uint8_t>& d) : r(d) {}
    Reader r;
    std::vector<std::string> objectOrder;
    std::map<std::string, ObjectData> objectDatas;
    std::vector<AnimationData> animationDatas;
    std::vector<std::string> materialOrder;
    std::map<std::string, MaterialData> materialDatas;

    Chunk readChunk(size_t at) {
        r.pos = at;
        Chunk c;
        c.id = r.u16();
        c.size = r.u32();
        c.dataSize = c.size >= 6 ? c.size - 6 : 0;
        c.dataPosition = r.pos;
        c.nextChunkPosition = at + c.size;
        return c;
    }

    // Iterates sibling chunks in [pos, pos+size). Guards against malformed sizes.
    template <typename F>
    void forChunks(size_t pos, long long size, F fn) {
        while (size >= 6 && pos + 6 <= r.data.size()) {
            Chunk c = readChunk(pos);
            if (c.size < 6) break;
            fn(c);
            pos = c.nextChunkPosition;
            size -= c.size;
        }
    }

    void parseTop() {
        forChunks(0, (long long)r.data.size(), [&](const Chunk& c) {
            if (c.id == CHUNK_MAIN) parseMain(c.dataPosition, (long long)c.dataSize);
        });
    }

    void parseMain(size_t pos, long long size) {
        forChunks(pos, size, [&](const Chunk& c) {
            if (c.id == CHUNK_SCENE) parse3D(c.dataPosition, (long long)c.dataSize);
            else if (c.id == CHUNK_ANIMATION) parseAnimation(c.dataPosition, (long long)c.dataSize);
        });
    }

    void parse3D(size_t pos, long long size) {
        forChunks(pos, size, [&](const Chunk& c) {
            if (c.id == CHUNK_MATERIAL) {
                MaterialData m;
                parseMaterial(m, c.dataPosition, (long long)c.dataSize);
            } else if (c.id == CHUNK_OBJECT) {
                parseObject(c);
            }
        });
    }

    void parseObject(const Chunk& c) {
        ObjectData od;
        od.name = r.str(c.dataPosition);
        size_t skip = od.name.size() + 1;
        if (!objectDatas.count(od.name)) objectOrder.push_back(od.name);
        objectDatas[od.name] = od;
        ObjectData& ref = objectDatas[od.name];
        forChunks(c.dataPosition + skip, (long long)c.dataSize - (long long)skip, [&](const Chunk& sc) {
            if (sc.id == CHUNK_TRIMESH) parseMesh(ref, sc.dataPosition, (long long)sc.dataSize);
        });
    }

    void parseMesh(ObjectData& od, size_t pos, long long size) {
        forChunks(pos, size, [&](const Chunk& c) {
            r.pos = c.dataPosition;
            switch (c.id) {
                case CHUNK_VERTICES: {
                    int n = r.u16();
                    od.vertices.resize((size_t)n * 3);
                    for (int v = 0; v < n * 3; ++v) od.vertices[v] = r.f32();
                    break;
                }
                case CHUNK_MAPPINGCOORDS: {
                    int n = r.u16();
                    od.uvs.resize((size_t)n * 2);
                    for (int v = 0; v < n * 2; ++v) od.uvs[v] = r.f32();
                    break;
                }
                case CHUNK_TRANSFORMATION:
                    od.hasMatrix = true;
                    od.a = r.f32(); od.e = r.f32(); od.i = r.f32();
                    od.b = r.f32(); od.f = r.f32(); od.j = r.f32();
                    od.c = r.f32(); od.g = r.f32(); od.k = r.f32();
                    od.d = r.f32(); od.h = r.f32(); od.l = r.f32();
                    break;
                case CHUNK_FACES: parseFaces(od, c); break;
                default: break;
            }
        });
    }

    void parseFaces(ObjectData& od, const Chunk& c) {
        r.pos = c.dataPosition;
        int n = r.u16();
        od.faces.resize((size_t)n * 3);
        od.smoothing.assign((size_t)n, 0);
        for (int f = 0; f < n; ++f) {
            od.faces[f * 3] = r.u16();
            od.faces[f * 3 + 1] = r.u16();
            od.faces[f * 3 + 2] = r.u16();
            r.pos += 2;  // face flags
        }
        size_t used = 2 + 8 * (size_t)n;
        forChunks(c.dataPosition + used, (long long)c.dataSize - (long long)used, [&](const Chunk& sc) {
            r.pos = sc.dataPosition;
            if (sc.id == CHUNK_FACESMATERIAL) {
                std::string name = r.str(sc.dataPosition);
                int count = r.u16();
                std::vector<int> list((size_t)count);
                for (int q = 0; q < count; ++q) list[q] = r.u16();
                od.surfaces.emplace_back(name, std::move(list));
            } else if (sc.id == CHUNK_FACESSMOOTH) {
                for (int q = 0; q < n; ++q) od.smoothing[q] = r.u32();
            }
        });
    }

    void parseAnimation(size_t pos, long long size) {
        forChunks(pos, size, [&](const Chunk& c) {
            if (c.id >= 0xB001 && c.id <= 0xB007) {
                AnimationData ad;
                parseObjectAnimation(ad, c.dataPosition, (long long)c.dataSize);
                animationDatas.push_back(ad);
            }
        });
    }

    void parseObjectAnimation(AnimationData& ad, size_t pos, long long size) {
        forChunks(pos, size, [&](const Chunk& c) {
            r.pos = c.dataPosition;
            switch (c.id) {
                case 0xB010:
                    ad.objectName = r.str(c.dataPosition);
                    r.pos += 4;
                    ad.parentIndex = r.u16();
                    break;
                case 0xB011: ad.objectName = r.str(c.dataPosition); break;
                case 0xB013: {
                    float x = r.f32(), y = r.f32(), z = r.f32();
                    ad.pivot = Vec3{x, y, z};
                    break;
                }
                case 0xB020: {
                    r.pos += 20;
                    float x = r.f32(), y = r.f32(), z = r.f32();
                    ad.position = Vec3{x, y, z};
                    break;
                }
                case 0xB021: {
                    r.pos += 20;
                    float ang = r.f32(), x = r.f32(), y = r.f32(), z = r.f32();
                    ad.rotation = rotationFromAngleAxis(ang, x, y, z);
                    break;
                }
                case 0xB022: {
                    r.pos += 20;
                    float x = r.f32(), y = r.f32(), z = r.f32();
                    ad.scale = Vec3{x, y, z};
                    break;
                }
                default: break;
            }
        });
    }

    static Vec3 rotationFromAngleAxis(double angle, double x, double y, double z) {
        Vec3 res;
        double s = std::sin(angle), c = std::cos(angle), t = 1 - c;
        double k = x * z * t + y * s;
        if (k >= 1) {
            double half = angle / 2;
            res.z = -2 * std::atan2(x * std::sin(half), std::cos(half));
            res.y = -PI / 2;
            res.x = 0;
            return res;
        }
        if (k <= -1) {
            double half = angle / 2;
            res.z = 2 * std::atan2(x * std::sin(half), std::cos(half));
            res.y = PI / 2;
            res.x = 0;
            return res;
        }
        res.z = -std::atan2(z * s - x * y * t, 1 - (z * z + y * y) * t);
        res.y = -std::asin(x * z * t + y * s);
        res.x = -std::atan2(x * s - z * y * t, 1 - (x * x + y * y) * t);
        return res;
    }

    void parseMaterial(MaterialData& m, size_t pos, long long size) {
        forChunks(pos, size, [&](const Chunk& c) {
            r.pos = c.dataPosition;
            switch (c.id) {
                case 0xA000:
                    m.name = r.str(c.dataPosition);
                    break;
                case 0xA020: {
                    r.pos = c.dataPosition + 6;
                    uint32_t rr = r.u8(), gg = r.u8(), bb = r.u8();
                    m.color = (rr << 16) + (gg << 8) + bb;
                    break;
                }
                case 0xA200:
                    m.diffuseMap = MapData{};
                    parseMap(*m.diffuseMap, c.dataPosition, (long long)c.dataSize);
                    break;
                case 0xA210:
                    m.opacityMap = MapData{};
                    parseMap(*m.opacityMap, c.dataPosition, (long long)c.dataSize);
                    break;
                default: break;
            }
        });
        if (!m.name.empty()) {
            if (!materialDatas.count(m.name)) materialOrder.push_back(m.name);
            materialDatas[m.name] = m;
        }
    }

    void parseMap(MapData& md, size_t pos, long long size) {
        forChunks(pos, size, [&](const Chunk& c) {
            r.pos = c.dataPosition;
            switch (c.id) {
                case 0xA300: md.filename = toLower(r.str(c.dataPosition)); break;
                case 0xA354: md.scaleU = r.f32(); break;
                case 0xA356: md.scaleV = r.f32(); break;
                case 0xA358: md.offsetU = r.f32(); break;
                case 0xA35A: md.offsetV = r.f32(); break;
                case 0xA35C: md.rotation = r.f32(); break;
                default: break;
            }
        });
    }
};

// flash.geom.Matrix helpers: M' = M * op (Flash appends operations).
struct FlashMatrix {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    void translate(double x, double y) { tx += x; ty += y; }
    void rotate(double q) {
        double cs = std::cos(q), sn = std::sin(q);
        double na = a * cs - b * sn, nb = a * sn + b * cs;
        double nc = c * cs - d * sn, nd = c * sn + d * cs;
        double ntx = tx * cs - ty * sn, nty = tx * sn + ty * cs;
        a = na; b = nb; c = nc; d = nd; tx = ntx; ty = nty;
    }
    void scale(double sx, double sy) { a *= sx; b *= sy; c *= sx; d *= sy; tx *= sx; ty *= sy; }
};

void buildMesh(MeshObject& mesh, ObjectData od, const AnimationData* anim,
               std::map<std::string, MaterialData>& materials) {
    bool transform = false;
    if (anim != nullptr) {
        // Invert the 3DS mesh matrix (vertices are stored in world space).
        double A = od.a, B = od.b, C = od.c, D = od.d, E = od.e, F = od.f, G = od.g, H = od.h, I = od.i,
               J = od.j, K = od.k, L = od.l;
        double det = 1 / (-C * F * I + B * G * I + C * E * J - A * G * J - B * E * K + A * F * K);
        od.a = (-G * J + F * K) * det;
        od.b = (C * J - B * K) * det;
        od.c = (-C * F + B * G) * det;
        od.d = (D * G * J - C * H * J - D * F * K + B * H * K + C * F * L - B * G * L) * det;
        od.e = (G * I - E * K) * det;
        od.f = (-C * I + A * K) * det;
        od.g = (C * E - A * G) * det;
        od.h = (C * H * I - D * G * I + D * E * K - A * H * K - C * E * L + A * G * L) * det;
        od.i = (-F * I + E * J) * det;
        od.j = (B * I - A * J) * det;
        od.k = (-B * E + A * F) * det;
        od.l = (D * F * I - B * H * I - D * E * J + A * H * J + B * E * L - A * F * L) * det;
        if (anim->pivot) {
            od.d -= anim->pivot->x;
            od.h -= anim->pivot->y;
            od.l -= anim->pivot->z;
        }
        transform = true;
    }
    size_t nv = od.vertices.size() / 3;
    bool hasUV = !od.uvs.empty();
    mesh.positions.resize(nv * 3);
    mesh.uvs.assign(nv * 2, 0.f);
    for (size_t v = 0; v < nv; ++v) {
        double x = od.vertices[v * 3], y = od.vertices[v * 3 + 1], z = od.vertices[v * 3 + 2];
        if (transform) {
            double tx = od.a * x + od.b * y + od.c * z + od.d;
            double ty = od.e * x + od.f * y + od.g * z + od.h;
            double tz = od.i * x + od.j * y + od.k * z + od.l;
            x = tx; y = ty; z = tz;
        }
        mesh.positions[v * 3] = (float)x;
        mesh.positions[v * 3 + 1] = (float)y;
        mesh.positions[v * 3 + 2] = (float)z;
        if (hasUV && od.uvs.size() >= v * 2 + 2) {
            mesh.uvs[v * 2] = od.uvs[v * 2];
            mesh.uvs[v * 2 + 1] = 1.0f - od.uvs[v * 2 + 1];
        }
    }
    mesh.indices.clear();
    for (size_t q = 0; q + 2 < od.faces.size(); q += 3) {
        int i0 = od.faces[q], i1 = od.faces[q + 1], i2 = od.faces[q + 2];
        if ((size_t)i0 >= nv || (size_t)i1 >= nv || (size_t)i2 >= nv) continue;
        mesh.indices.insert(mesh.indices.end(), {(uint32_t)i0, (uint32_t)i1, (uint32_t)i2});
    }
    // Material UV transforms (each vertex is transformed at most once).
    std::vector<bool> transformed(nv, false);
    for (auto& [matName, faceList] : od.surfaces) {
        auto it = materials.find(matName);
        if (it == materials.end()) continue;
        for (int f : faceList) {
            if (f == 0) mesh.firstFaceMaterial = matName;
            const MaterialData& m = it->second;
            if (!m.hasMatrix) continue;
            if ((size_t)f * 3 + 2 >= od.faces.size()) continue;
            for (int corner = 0; corner < 3; ++corner) {
                int vi = od.faces[(size_t)f * 3 + corner];
                if ((size_t)vi >= nv || transformed[vi]) continue;
                // Same element usage as the original (a,b for u and c,d for v).
                double u = mesh.uvs[vi * 2], vv = mesh.uvs[vi * 2 + 1];
                mesh.uvs[vi * 2] = (float)(m.ma * u + m.mb * vv + m.tx);
                mesh.uvs[vi * 2 + 1] = (float)(m.mc * u + m.md * vv + m.ty);
                transformed[vi] = true;
            }
        }
    }
    mesh.calculateBounds();
}

}  // namespace

MeshRef Parser3DS::getObjectByName(const std::string& name) const {
    for (auto& o : objects)
        if (o->name == name) return o;
    return nullptr;
}

bool Parser3DS::parseFile(const std::string& path, std::string* err) {
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    return parse(bytes);
}

bool Parser3DS::parse(const std::vector<uint8_t>& data) {
    objects.clear();
    materials.clear();
    textureMaterials.clear();
    if (data.size() < 6) return false;
    Impl p(data);
    p.parseTop();

    for (const std::string& name : p.materialOrder) {
        MaterialData& m = p.materialDatas[name];
        Material3DS out;
        out.name = m.name;
        out.color = m.color;
        if (m.diffuseMap) {
            FlashMatrix fm;
            double rot = m.diffuseMap->rotation * PI / 180;
            fm.translate(-m.diffuseMap->offsetU, m.diffuseMap->offsetV);
            fm.translate(-0.5, -0.5);
            fm.rotate(-rot);
            fm.scale(m.diffuseMap->scaleU, m.diffuseMap->scaleV);
            fm.translate(0.5, 0.5);
            m.hasMatrix = true;
            m.ma = fm.a; m.mb = fm.b; m.mc = fm.c; m.md = fm.d; m.tx = fm.tx; m.ty = fm.ty;
            out.diffuseMap = m.diffuseMap->filename;
            if (m.opacityMap) out.opacityMap = m.opacityMap->filename;
            textureMaterials.push_back(out);
        }
        materials.push_back(out);
    }

    if (!p.animationDatas.empty()) {
        if (!p.objectDatas.empty()) {
            auto& anims = p.animationDatas;
            for (size_t q = 0; q < anims.size(); ++q) {
                AnimationData& ad = anims[q];
                std::string name = ad.objectName;
                auto it = p.objectDatas.find(name);
                if (it != p.objectDatas.end()) {
                    int suffix = 1;
                    for (size_t w = q + 1; w < anims.size(); ++w) {
                        if (!anims[w].isInstance && anims[w].objectName == name) {
                            std::string newName = name + std::to_string(suffix++);
                            ObjectData copy = it->second;
                            copy.name = newName;
                            p.objectDatas[newName] = copy;
                            anims[w].objectName = newName;
                            it = p.objectDatas.find(name);
                        }
                    }
                }
                auto mesh = std::make_shared<MeshObject>();
                if (it != p.objectDatas.end() && !it->second.vertices.empty())
                    buildMesh(*mesh, it->second, &ad, p.materialDatas);
                mesh->name = name;
                if (ad.position) { mesh->x = ad.position->x; mesh->y = ad.position->y; mesh->z = ad.position->z; }
                if (ad.rotation) { mesh->rotX = ad.rotation->x; mesh->rotY = ad.rotation->y; mesh->rotZ = ad.rotation->z; }
                if (ad.scale) { mesh->scaleX = ad.scale->x; mesh->scaleY = ad.scale->y; mesh->scaleZ = ad.scale->z; }
                ad.object = mesh;
            }
            for (auto& ad : anims) objects.push_back(ad.object);
        }
    } else {
        for (const std::string& name : p.objectOrder) {
            ObjectData& od = p.objectDatas[name];
            if (od.vertices.empty()) continue;
            auto mesh = std::make_shared<MeshObject>();
            mesh->name = name;
            buildMesh(*mesh, od, nullptr, p.materialDatas);
            objects.push_back(mesh);
        }
    }
    return !objects.empty();
}

}  // namespace ae
