// Light / dark UI theme: the ImGui style plus every colour the editor draws itself
// (workspace background, grid, overlays) so that the whole window switches together.
#pragma once
#include <cstdint>

namespace ae {

// Colours that are not part of the ImGui style. 0xRRGGBB unless noted.
// The defaults are the light theme, i.e. the original editor look.
struct Theme {
    bool dark = false;
    float windowClear[3] = {0.85f, 0.87f, 0.88f};  // behind the ImGui windows (r, g, b)
    float workspaceClear[3] = {1.f, 1.f, 1.f};     // 3D workspace, preview panel, library thumbnails
    uint32_t grid = 0x909090;                      // ground grid
    uint32_t planeBound = 0x0000FF;                // plane bounds (T key)
    uint32_t axisBlue = 0x0000FF;                  // Z axis indicator
    uint32_t selectionRect = 0x000000;             // rubber-band selection rectangle
};

const Theme& currentTheme();

// Switches the ImGui style and currentTheme() to the light or dark palette.
void applyTheme(bool dark);

}  // namespace ae
