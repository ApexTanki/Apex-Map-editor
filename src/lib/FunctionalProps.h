// Port of alternativa.editor.FunctionalProps (built-in "Functional" library).
#pragma once
#include <string>
#include <vector>

#include "scene/Prop.h"

namespace ae::functional {

inline const std::string LIBRARY_NAME = "Functional";
inline const std::string GRP_SPAWN_POINTS = "Spawn Points";
inline const std::string GRP_BONUS_REGIONS = "Bonus Regions";
inline const std::string GRP_FLAGS = "Flags";
inline const std::string GRP_DOMINATION = "Domination";
inline const std::string KILL_GEOMETRY = "special-geometry";
inline const std::string KILL_BOX_NAME = "special-box";
inline const std::string BLUE_SPAWN = "blue";
inline const std::string RED_SPAWN = "red";
inline const std::string DM_SPAWN = "dm";
inline const std::string FREE_BONUS_NAME = "free-bonus";
inline const std::string DOMINATION_POINT = "dom-keypoint";
inline const std::string DOMINATION_BLUE_SPAWN = "dom-blue";
inline const std::string DOMINATION_RED_SPAWN = "dom-red";
inline const std::string DOMINATION_SPAWN = "dom";
inline const std::string BLUE_FLAG = "blue_flag";
inline const std::string RED_FLAG = "red_flag";

inline const std::vector<std::string> GAME_MODES = {"dm", "tdm", "ctf", "dom", "asl", "sge", "jgr", "rgb"};
inline const std::vector<std::string> CONTROL_POINT_MODES = {"dom", "sge"};
inline const std::vector<std::string> BONUS_TYPES = {"damageup", "armorup", "nitro", "medkit",
                                                     "crystal", "crystal_100", "overdrive"};
inline const std::vector<std::string> KILL_ACTIONS = {"kill", "kick", "block"};
inline const std::string LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

// Layer a functional prop belongs to ("" for regular props).
std::string getPropLayer(const Prop& p);

struct GroupProps {
    std::string group;
    std::vector<PropRef> props;
};
// Builds the functional library (group order: Flags, special-geometry, Spawn Points,
// Bonus Regions, Domination). Returns false if an asset is missing.
bool build(const std::string& assetsDir, std::vector<GroupProps>& out, std::string* err);

}  // namespace ae::functional
