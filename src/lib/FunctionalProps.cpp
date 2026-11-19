#include "lib/FunctionalProps.h"

#include "io/Parser3DS.h"

namespace ae::functional {

std::string getPropLayer(const Prop& p) {
    if (p.libraryName != LIBRARY_NAME) return "";
    if (p.groupName == GRP_SPAWN_POINTS) return p.name == DM_SPAWN ? "dm" : "tdm";
    if (p.groupName == GRP_FLAGS) return "tdm";
    if (p.groupName == GRP_DOMINATION) return "dom";
    if (p.groupName == GRP_BONUS_REGIONS) return "bonuses";
    if (p.groupName == KILL_GEOMETRY) return "special";
    return "";
}

static PropModelRef parseModel(const std::string& path, std::string* err) {
    Parser3DS parser;
    if (!parser.parseFile(path, err) || parser.objects.empty()) {
        if (err && err->empty()) *err = "cannot parse " + path;
        return nullptr;
    }
    auto model = std::make_shared<PropModel>();
    model->mesh = parser.objects[0];
    model->mesh->x = model->mesh->y = model->mesh->z = 0;
    return model;
}

bool build(const std::string& dir, std::vector<GroupProps>& out, std::string* err) {
    out.clear();
    PropModelRef flagModel = parseModel(dir + "/functional/flag.3ds", err);
    PropModelRef spawnModel = parseModel(dir + "/functional/spawn.3ds", err);
    TextureRef flagBlue = image::load(dir + "/functional/flag_blue.jpg", err);
    TextureRef flagRed = image::load(dir + "/functional/flag_red.jpg", err);
    if (!flagModel || !spawnModel || !flagBlue || !flagRed) return false;

    const uint32_t RED = 14960384, BLUE = 786943, YELLOW = 14860033;
    auto mono = [](uint32_t c) { return image::solid(c); };

    GroupProps flags{GRP_FLAGS, {}};
    flags.props.push_back(Prop::createFlag(flagModel, flagBlue, BLUE_FLAG, LIBRARY_NAME, GRP_FLAGS));
    flags.props.push_back(Prop::createFlag(flagModel, flagRed, RED_FLAG, LIBRARY_NAME, GRP_FLAGS));
    out.push_back(flags);

    GroupProps kill{KILL_GEOMETRY, {}};
    kill.props.push_back(Prop::createKillBox(KILL_GEOMETRY, LIBRARY_NAME, KILL_GEOMETRY));
    out.push_back(kill);

    GroupProps spawns{GRP_SPAWN_POINTS, {}};
    spawns.props.push_back(Prop::createSpawn(spawnModel, mono(YELLOW), DM_SPAWN, LIBRARY_NAME, GRP_SPAWN_POINTS));
    spawns.props.push_back(Prop::createSpawn(spawnModel, mono(BLUE), BLUE_SPAWN, LIBRARY_NAME, GRP_SPAWN_POINTS));
    spawns.props.push_back(Prop::createSpawn(spawnModel, mono(RED), RED_SPAWN, LIBRARY_NAME, GRP_SPAWN_POINTS));
    out.push_back(spawns);

    GroupProps bonus{GRP_BONUS_REGIONS, {}};
    bonus.props.push_back(Prop::createBonusRegion(FREE_BONUS_NAME, LIBRARY_NAME, GRP_BONUS_REGIONS));
    out.push_back(bonus);

    auto cpModel = std::make_shared<PropModel>();
    cpModel->mesh = primitives::box(100, 100, 400);
    GroupProps dom{GRP_DOMINATION, {}};
    dom.props.push_back(Prop::createControlPoint(cpModel, mono(0xFF00FF), DOMINATION_POINT, LIBRARY_NAME, GRP_DOMINATION));
    dom.props.push_back(Prop::createSpawn(spawnModel, mono(YELLOW), DOMINATION_SPAWN, LIBRARY_NAME, GRP_DOMINATION));
    dom.props.push_back(Prop::createSpawn(spawnModel, mono(RED), DOMINATION_RED_SPAWN, LIBRARY_NAME, GRP_DOMINATION));
    dom.props.push_back(Prop::createSpawn(spawnModel, mono(BLUE), DOMINATION_BLUE_SPAWN, LIBRARY_NAME, GRP_DOMINATION));
    out.push_back(dom);
    return true;
}

}  // namespace ae::functional
