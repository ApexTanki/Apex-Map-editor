#include "map/MapIO.h"

#include <algorithm>
#include <sstream>

#include "core/ErrorLog.h"
#include "core/Util.h"
#include "lib/FunctionalProps.h"
#include "lib/LibraryManager.h"
#include "scene/MainScene.h"
#include "ui/Locale.h"

namespace ae {

static constexpr int PRECISION = 3;

// ---------------------------------------------------------------------------------------------
// Export

static void positionXml(pugi::xml_node parent, const Prop& p) {
    pugi::xml_node n = parent.append_child("position");
    n.append_child("x").text().set(toFixed(p.x, PRECISION).c_str());
    n.append_child("y").text().set(toFixed(p.y, PRECISION).c_str());
    n.append_child("z").text().set(toFixed(p.z, PRECISION).c_str());
}

static void tileXml(pugi::xml_node parent, const Prop& p) {
    pugi::xml_node n = parent.append_child("prop");
    n.append_attribute("library-name") = p.libraryName.c_str();
    n.append_attribute("group-name") = p.groupName.c_str();
    n.append_attribute("name") = p.name.c_str();
    n.append_child("rotation").append_child("z").text().set(toFixed(p.rotationZ, 6).c_str());
    n.append_child("texture-name").text().set(p.textureName.c_str());
    positionXml(n, p);
    if (p.free) n.append_attribute("free") = "true";
    if (!p.collisionEnabled && !p.isSprite()) n.append_attribute("noCollision") = "true";
}

static void spawnXml(pugi::xml_node parent, const Prop& p) {
    pugi::xml_node n = parent.append_child("spawn-point");
    n.append_attribute("type") = p.name.c_str();
    n.append_child("rotation").append_child("z").text().set(toFixed(p.rotationZ, PRECISION).c_str());
    positionXml(n, p);
    if (p.free) n.append_attribute("free") = "true";
}

static void bonusXml(pugi::xml_node parent, const Prop& p) {
    pugi::xml_node n = parent.append_child("bonus-region");
    n.append_attribute("name") = p.name.c_str();
    n.append_attribute("parachute") = p.parachute ? "true" : "false";
    n.append_child("rotation").append_child("z").text().set(toFixed(p.rotationZ, PRECISION).c_str());
    pugi::xml_node mn = n.append_child("min");
    mn.append_child("x").text().set(toFixed(p.minX(), PRECISION).c_str());
    mn.append_child("y").text().set(toFixed(p.minY(), PRECISION).c_str());
    mn.append_child("z").text().set(toFixed(p.minZ(), PRECISION).c_str());
    pugi::xml_node mx = n.append_child("max");
    mx.append_child("x").text().set(toFixed(p.maxX(), PRECISION).c_str());
    mx.append_child("y").text().set(toFixed(p.maxY(), PRECISION).c_str());
    mx.append_child("z").text().set(toFixed(p.maxZ(), PRECISION).c_str());
    positionXml(n, p);
    if (p.free) n.append_attribute("free") = "true";
    for (auto& t : p.bonusTypes) n.append_child("bonus-type").text().set(t.c_str());
    for (auto& m : p.gameModes) n.append_child("game-mode").text().set(m.c_str());
}

static void killXml(pugi::xml_node parent, const Prop& p) {
    pugi::xml_node n = parent.append_child(functional::KILL_BOX_NAME.c_str());
    n.append_attribute("type") = p.name.c_str();
    n.append_child("minX").text().set(numberToString(p.minX()).c_str());
    n.append_child("minY").text().set(numberToString(p.minY()).c_str());
    n.append_child("minZ").text().set(numberToString(p.minZ()).c_str());
    n.append_child("maxX").text().set(numberToString(p.maxX()).c_str());
    n.append_child("maxY").text().set(numberToString(p.maxY()).c_str());
    n.append_child("maxZ").text().set(numberToString(p.maxZ()).c_str());
    n.append_child("action").text().set(p.killAction.c_str());
    if (p.free) n.append_attribute("free") = "true";
}

static void ctfFlag(pugi::xml_node map, const Prop& p) {
    pugi::xml_node flags = map.child("ctf-flags");
    if (!flags) flags = map.append_child("ctf-flags");
    const char* tag = nullptr;
    if (p.name == functional::RED_FLAG) tag = "flag-red";
    else if (p.name == functional::BLUE_FLAG) tag = "flag-blue";
    if (!tag) return;
    pugi::xml_node n = flags.append_child(tag);
    n.append_child("x").text().set(toFixed(p.x, PRECISION).c_str());
    n.append_child("y").text().set(toFixed(p.y, PRECISION).c_str());
    n.append_child("z").text().set(toFixed(p.z, PRECISION).c_str());
}

static void dominationPoint(pugi::xml_node map, const Prop& p) {
    pugi::xml_node points = map.child("dom-keypoints");
    if (!points) points = map.append_child("dom-keypoints");
    pugi::xml_node n = points.append_child("dom-keypoint");
    n.append_attribute("free") = p.free ? "true" : "false";
    n.append_attribute("name") = p.controlPointName.c_str();
    n.append_attribute("modes") = join(p.gameModes, ",").c_str();
    positionXml(n, p);
    for (auto& s : p.getSpawnPoints()) spawnXml(n, *s);
}

bool MapExporter::exportScene(const MainScene& scene, MapFormat format, std::string& out, std::string* err) {
    if (format != MapFormat::XmlV1Lite && format != MapFormat::XmlV1Full) {
        if (err) *err = "This export format is not implemented yet in the OpenGL port.";
        return false;
    }
    const bool full = format == MapFormat::XmlV1Full;
    pugi::xml_document doc;
    pugi::xml_node map = doc.append_child("map");
    map.append_attribute("version") = full ? "1.0" : "1.0.Light";
    pugi::xml_node staticGeom = map.append_child("static-geometry");
    pugi::xml_node collisionGeom = map.append_child("collision-geometry");
    pugi::xml_node spawns = map.append_child("spawn-points");
    pugi::xml_node bonuses = map.append_child("bonus-regions");
    pugi::xml_node kills;
    if (full) kills = map.append_child(functional::KILL_GEOMETRY.c_str());

    for (const PropRef& pr : scene.children) {
        const Prop& p = *pr;
        switch (p.type) {
            case TYPE_BONUS: bonusXml(bonuses, p); break;
            case TYPE_KILL:
                if (full) killXml(kills, p);
                break;
            case TYPE_SPAWN:
                if (p.name != functional::DOMINATION_SPAWN) spawnXml(spawns, p);
                break;
            case TYPE_FLAG: ctfFlag(map, p); break;
            case TYPE_TILE: {
                tileXml(staticGeom, p);
                if (!p.collisionEnabled) break;
                const auto* prims = cache_.get(p.libraryName, p.groupName, p.name);
                if (!prims) prims = &cache_.add(p.libraryName, p.groupName, p.name, createPropCollisionPrimitives(p));
                Matrix4 m = p.transformation();
                for (auto& prim : *prims) prim.appendXml(collisionGeom, m);
                break;
            }
            case TYPE_DOM_CP: dominationPoint(map, p); break;
            default: break;
        }
    }
    std::ostringstream ss;
    doc.save(ss, "  ", pugi::format_indent | pugi::format_no_declaration, pugi::encoding_utf8);
    out = ss.str();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Import

static double num(pugi::xml_node n) { return parseNumber(n.text().get()); }

static Vec3 pointFrom(pugi::xml_node n) { return {num(n.child("x")), num(n.child("y")), num(n.child("z"))}; }

bool MapImporter::begin(const std::string& path, MainScene& scene, LibraryManager& libs, bool special, std::string* err) {
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    pugi::xml_parse_result r = doc_.load_buffer(bytes.data(), bytes.size());
    if (!r) {
        if (err) *err = std::string("XML error: ") + r.description();
        return false;
    }
    map_ = doc_.document_element();
    scene_ = &scene;
    libs_ = &libs;
    unsupportedVersion.clear();
    std::string version = map_.attribute("version").value();
    if (version == "1.0.Light") {
        full_ = false;
    } else if (version == "1.0") {
        full_ = true;
    } else {
        // Versions 2.0 / 3.0 are handled by later phases of the port; like the original for an
        // unknown version, fall back to the 1.0 importer and tell the user.
        unsupportedVersion = version;
        full_ = true;
    }
    ErrorLog::instance().clearMessages();
    if (special) {
        // addInternalObjectsToExistingScene: only the V1 Full importer adds kill zones.
        if (full_) loadKillZones();
        active_ = false;
        return true;
    }
    tiles_.clear();
    next_ = 0;
    if (pugi::xml_node sg = map_.child("static-geometry"))
        for (auto prop : sg.children("prop")) tiles_.push_back(prop);
    active_ = true;
    return true;
}

double MapImporter::progress() const { return tiles_.empty() ? 1.0 : (double)next_ / tiles_.size(); }

bool MapImporter::step() {
    if (!active_) return false;
    const size_t BATCH = 100;
    size_t end = std::min(tiles_.size(), next_ + BATCH);
    for (; next_ < end; ++next_) processTile(tiles_[next_]);
    if (next_ >= tiles_.size()) complete();
    return active_;
}

void MapImporter::processTile(pugi::xml_node item) {
    std::string key = std::string(item.attribute("library-name").value()) + item.attribute("group-name").value() +
                      item.attribute("name").value();
    PropRef proto = libs_->propByKey(key);
    auto& log = ErrorLog::instance();
    if (!proto) {
        log.setMessage(Locale::get("ERROR_LIBRARIES_NOT_LOADED"));
        log.addText(key + " " + Locale::get("ERROR_CANT_BE_LOADED"));
        log.showWindow();
        return;
    }
    Vec3 pos = pointFrom(item.child("position"));
    double rotation = num(item.child("rotation").child("z"));
    PropRef prop = scene_->addProp(proto, pos, rotation, true, false);
    bool free = std::string(item.attribute("free").value()) == "true";
    if (!(free && prop->isSprite())) scene_->occupyMap.occupy(*prop);
    std::string textureName = item.child("texture-name").text().get();
    if (prop->isMeshProp()) {
        if (!prop->isSprite()) prop->collisionEnabled = std::string(item.attribute("noCollision").value()) != "true";
        if (!textureName.empty() && !prop->setTextureName(textureName)) {
            if (prop->model && !prop->model->bitmaps.empty()) prop->setTextureName(prop->model->bitmaps.front().first);
            log.addText(replaceAll(replaceAll(Locale::get("ERROR_TILE_TEXTURE_NOT_FOUND"), "$1", prop->name), "$2",
                                   textureName));
            log.showWindow();
        }
    }
}

void MapImporter::complete() {
    loadFunctionalElements();
    active_ = false;
    scene_->changed = false;
}

void MapImporter::loadFunctionalElements() {
    loadBonuses();
    loadSpawns();
    loadFlags();
    if (full_) loadKillZones();
    loadDominationControlPoints();
}

void MapImporter::loadBonuses() {
    pugi::xml_node regions = map_.child("bonus-regions");
    if (!regions) return;
    std::string key = LibraryManager::key(functional::LIBRARY_NAME, functional::GRP_BONUS_REGIONS, functional::FREE_BONUS_NAME);
    for (auto r : regions.children("bonus-region")) {
        PropRef proto = libs_->propByKey(key);
        if (!proto) continue;
        // The original edits the library prototype before cloning it (kept for fidelity).
        proto->bonusTypes.clear();
        for (auto t : r.children("bonus-type")) proto->bonusTypes.push_back(t.text().get());
        proto->gameModes.clear();
        for (auto m : r.children("game-mode")) proto->gameModes.push_back(m.text().get());
        PropRef p = scene_->addProp(proto, pointFrom(r.child("position")), 0, true, false);
        p->setPosition(num(r.child("min").child("x")), num(r.child("min").child("y")), num(r.child("min").child("z")));
        p->setMaxX(num(r.child("max").child("x")));
        p->setMaxY(num(r.child("max").child("y")));
        p->setMaxZ(num(r.child("max").child("z")));
        // `parachute = XMLList` coerces to true whenever the attribute is present.
        if (r.attribute("parachute")) p->parachute = true;
        if (std::string(r.attribute("free").value()) != "true") scene_->occupyMap.occupy(*p);
    }
}

PropRef MapImporter::createSpawnPoint(pugi::xml_node node) {
    std::string type = node.attribute("type").value();
    std::string group = startsWith(type, "dom") ? functional::GRP_DOMINATION : functional::GRP_SPAWN_POINTS;
    PropRef proto = libs_->propByKey(LibraryManager::key(functional::LIBRARY_NAME, group, type));
    if (!proto) return nullptr;
    pugi::xml_node pos = node.child("position"), rot = node.child("rotation");
    PropRef p = scene_->addProp(proto, pointFrom(pos), num(rot.child("z")), true, false);
    if (std::string(node.attribute("free").value()) != "true") scene_->occupyMap.occupy(*p);
    return p;
}

void MapImporter::loadSpawns() {
    pugi::xml_node spawns = map_.child("spawn-points");
    if (!spawns) return;
    for (auto s : spawns.children("spawn-point")) createSpawnPoint(s);
}

void MapImporter::loadFlags() {
    pugi::xml_node flags = map_.child("ctf-flags");
    if (!flags) return;
    auto addFlag = [&](pugi::xml_node n, const std::string& name) {
        PropRef proto = libs_->propByKey(LibraryManager::key(functional::LIBRARY_NAME, functional::GRP_FLAGS, name));
        if (proto && n) scene_->addProp(proto, pointFrom(n), 0, true, false);
    };
    addFlag(flags.child("flag-red"), functional::RED_FLAG);
    addFlag(flags.child("flag-blue"), functional::BLUE_FLAG);
}

void MapImporter::loadKillZones() {
    PropRef proto = libs_->propByKey(
        LibraryManager::key(functional::LIBRARY_NAME, functional::KILL_GEOMETRY, functional::KILL_GEOMETRY));
    if (!proto) return;
    // mapXML.*.elements("special-box"): any child of <map>
    for (auto child : map_.children())
        for (auto box : child.children(functional::KILL_BOX_NAME.c_str())) {
            PropRef p = scene_->addProp(proto, pointFrom(box.child("position")), 0, true, false);
            p->setPosition(num(box.child("minX")), num(box.child("minY")), num(box.child("minZ")));
            p->setMaxX(num(box.child("maxX")));
            p->setMaxY(num(box.child("maxY")));
            p->setMaxZ(num(box.child("maxZ")));
            p->killAction = box.child("action").text().get();
        }
}

void MapImporter::loadDominationControlPoints() {
    pugi::xml_node points = map_.child("dom-keypoints");
    if (!points) return;
    PropRef proto = libs_->propByKey(
        LibraryManager::key(functional::LIBRARY_NAME, functional::GRP_DOMINATION, functional::DOMINATION_POINT));
    if (!proto) return;
    for (auto kp : points.children("dom-keypoint")) {
        PropRef p = scene_->addProp(proto, pointFrom(kp.child("position")), 0, true, false);
        std::string name = toUpper(kp.attribute("name").value());
        if (!name.empty() && name.size() == 1 && functional::LETTERS.find(name) != std::string::npos)
            p->controlPointName = name;
        std::string modes = kp.attribute("modes").value();
        if (!modes.empty()) {
            p->gameModes.clear();
            for (auto& m : split(toLower(modes), ','))
                if (std::find(functional::CONTROL_POINT_MODES.begin(), functional::CONTROL_POINT_MODES.end(), m) !=
                    functional::CONTROL_POINT_MODES.end())
                    p->gameModes.push_back(m);
        }
        if (std::string(kp.attribute("free").value()) != "true") scene_->occupyMap.occupy(*p);
        for (auto sp : kp.children("spawn-point"))
            if (PropRef s = createSpawnPoint(sp)) p->addSpawnPoint(s);
    }
}

}  // namespace ae
