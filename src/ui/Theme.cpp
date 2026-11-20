#include "ui/Theme.h"

#include <imgui.h>

namespace ae {

static Theme g_theme;  // light until applyTheme() says otherwise

const Theme& currentTheme() { return g_theme; }

static ImVec4 rgb(uint32_t c, float a = 1.f) {
    return ImVec4(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, a);
}

static void setRgb(float out[3], uint32_t c) {
    out[0] = ((c >> 16) & 0xFF) / 255.f;
    out[1] = ((c >> 8) & 0xFF) / 255.f;
    out[2] = (c & 0xFF) / 255.f;
}

static void applyLight(ImGuiStyle& s) {
    // Light theme loosely matching the Flex "halo" look of the original.
    ImGui::StyleColorsLight(&s);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.91f, 0.92f, 0.93f, 1.f);
    c[ImGuiCol_TitleBg] = ImVec4(0.80f, 0.84f, 0.86f, 1.f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.80f, 0.84f, 0.86f, 1.f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.f);
    c[ImGuiCol_Button] = ImVec4(0.93f, 0.94f, 0.95f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.73f, 0.87f, 0.96f, 1.f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.76f, 0.91f, 1.f);
    c[ImGuiCol_Header] = ImVec4(0.84f, 0.88f, 0.90f, 1.f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.73f, 0.87f, 0.96f, 1.f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.55f, 0.76f, 0.91f, 1.f);
    g_theme = Theme();
}

static void applyDark(ImGuiStyle& s) {
    // Neutral charcoal panels with the same blue accent the light theme uses for hover/active.
    ImGui::StyleColorsDark(&s);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = rgb(0xE3E6EA);
    c[ImGuiCol_WindowBg] = rgb(0x212326);
    c[ImGuiCol_TitleBg] = rgb(0x2B2E33);
    c[ImGuiCol_TitleBgActive] = rgb(0x2B2E33);
    c[ImGuiCol_MenuBarBg] = rgb(0x2A2C30);
    c[ImGuiCol_FrameBg] = rgb(0x2B2E33);
    c[ImGuiCol_FrameBgHovered] = rgb(0x353A41);
    c[ImGuiCol_FrameBgActive] = rgb(0x3A4553);
    c[ImGuiCol_Button] = rgb(0x33373D);
    c[ImGuiCol_ButtonHovered] = rgb(0x3F5F80);
    c[ImGuiCol_ButtonActive] = rgb(0x2F6BA0);
    c[ImGuiCol_Header] = rgb(0x373C43);
    c[ImGuiCol_HeaderHovered] = rgb(0x3F5F80);
    c[ImGuiCol_HeaderActive] = rgb(0x2F6BA0);

    Theme t;
    t.dark = true;
    setRgb(t.windowClear, 0x141517);
    setRgb(t.workspaceClear, 0x1A1C20);
    t.grid = 0x626976;          // about the same contrast on the dark floor as 0x909090 has on white
    t.planeBound = 0x5A8CFF;    // pure blue is unreadable on a dark background
    t.axisBlue = 0x5A8CFF;
    t.selectionRect = 0xE6E6E6;
    g_theme = t;
}

void applyTheme(bool dark) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 3;
    s.FrameRounding = 3;
    s.GrabRounding = 3;
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 1;
    if (dark)
        applyDark(s);
    else
        applyLight(s);
}

}  // namespace ae
