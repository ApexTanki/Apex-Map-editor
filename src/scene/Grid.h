// EditorScene grid constants.
#pragma once

namespace ae::grid {

constexpr double H_RES_1 = 500;  // HORIZONTAL_GRID_RESOLUTION_1
constexpr double H_RES_2 = 250;  // HORIZONTAL_GRID_RESOLUTION_2
constexpr double V_RES_1 = 300;  // VERTICAL_GRID_RESOLUTION_1
constexpr double V_RES_2 = 150;  // VERTICAL_GRID_RESOLUTION_2
constexpr double hBase = 250;
constexpr double hBase2 = 500;
constexpr double vBase = 300;

// EditorScene.snapByHalf (toggled with K)
inline bool snapByHalf = false;
inline void toggleResolution() { snapByHalf = !snapByHalf; }
inline double hStep() { return snapByHalf ? H_RES_2 : H_RES_1; }
inline double vStep() { return snapByHalf ? V_RES_2 : V_RES_1; }

}  // namespace ae::grid
