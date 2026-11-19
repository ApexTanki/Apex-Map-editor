#include "ui/App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "core/ErrorLog.h"
#include "core/Util.h"
#include "lib/FunctionalProps.h"
#include "scene/Grid.h"
#include "ui/Dialogs.h"
#include "ui/Locale.h"

namespace ae {

static const char* L(const std::string& id) { return Locale::get(id).c_str(); }
static std::string LS(const std::string& id) { return Locale::get(id); }
static ImTextureID texId(unsigned id) { return (ImTextureID)(intptr_t)id; }

// ---------------------------------------------------------------------------------------------
// Init

bool App::init(GLFWwindow* window, const std::string& assetsDir, const StartupOptions& opts, std::string* err) {
    window_ = window;
    assetsDir_ = assetsDir;
    opts_ = opts;
    Settings::instance().load();
    if (!Locale::init(assetsDir + "/localization.json", Settings::instance().language, err)) return false;
    if (!TextureRegistry::instance().init(assetsDir, err)) {
        if (err && err->empty()) *err = "missing textures in " + assetsDir;
        return false;
    }
    if (!renderer_.init(err)) return false;
    if (!libs_.init(assetsDir, err)) return false;
    readFileText(assetsDir + "/help.txt", helpText_);
    helpText_ = replaceAll(helpText_, "\r", "");
    loadUiIcons(assetsDir + "/icons");

    container_ = std::make_unique<SceneContainer>(scene_);
    container_->askYesNo = [this](const std::string& msg, std::function<void(bool)> cb) {
        alert(msg, "", YES_NO, std::move(cb));
    };
    for (auto& l : Layers::windowLayers()) layerVisible_[l] = true;

    // initFunctionalLibrary + setSnappingEnabled(true)
    selectLibrary(functional::LIBRARY_NAME);
    setSnapping(true);
    if (opts_.collisionView) setGeometryView(true);
    loadStart_ = glfwGetTime();
    wasLoading_ = true;
    if (!opts_.libraries.empty()) libs_.loadGivenLibraries(opts_.libraries);
    return true;
}

void App::shutdown() {
    icons_.clear();
    renderer_.shutdown();
}

void App::loadUiIcons(const std::string& dir) {
    for (const char* n : {"snap_icon", "snap_icon_on", "boxes_icon", "boxes_icon_on", "grid_icon", "grid_icon_on",
                          "textures_icon", "textures_icon_on", "hide_selected", "show_all"}) {
        if (TextureRef t = image::load(dir + "/" + n + ".png")) uiIcons_[n] = t;
    }
}

// ---------------------------------------------------------------------------------------------
// Alerts

void App::alert(const std::string& text, const std::string& title, Buttons b, std::function<void(bool)> cb) {
    alerts_.push_back({title, text, b, std::move(cb)});
}

void App::drawAlerts() {
    if (alerts_.empty()) return;
    Alert& a = alerts_.front();
    const char* popupId = "##alert";
    if (!ImGui::IsPopupOpen(popupId)) ImGui::OpenPopup(popupId);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(700, 600));
    if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (!a.title.empty()) {
            ImGui::TextUnformatted(a.title.c_str());
            ImGui::Separator();
        }
        ImGui::PushTextWrapPos(660);
        ImGui::TextUnformatted(a.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        int result = -1;
        if (a.buttons == OK) {
            if (ImGui::Button(L("ALERT_BUTTON_OK"), ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) result = 1;
        } else {
            if (ImGui::Button(L("ALERT_BUTTON_YES"), ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) result = 1;
            ImGui::SameLine();
            const char* no = a.buttons == YES_NO ? L("ALERT_BUTTON_NO") : L("ALERT_BUTTON_CANCEL");
            if (ImGui::Button(no, ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) result = 0;
        }
        if (result >= 0) {
            ImGui::CloseCurrentPopup();
            auto cb = std::move(a.callback);
            alerts_.pop_front();
            if (cb) cb(result == 1);
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------------------------
// Commands

void App::askToDiscardChanges(std::function<void()> then) {
    if (!scene_.changed) {
        then();
        return;
    }
    alert(LS("ALERT_TEXT_DISCARD_CHANGES_Q"), LS("ALERT_HEADER_SCENE_HAS_BEEN_CHANGED"), YES_CANCEL,
          [then](bool yes) {
              if (yes) then();
          });
}

void App::cmdNewLevel() {
    askToDiscardChanges([this] {
        container_->clearEventJournal();
        scene_.clear();
        scene_.cursor.clear();
    });
}

void App::startImport(const std::string& path, bool special) {
    std::string err;
    progressLabel_ = LS("PROGRESSBAR_IMPORTING_LEVEL");
    if (!importer_.begin(path, scene_, libs_, special, &err)) {
        alert(err);
        return;
    }
    if (!importer_.unsupportedVersion.empty())
        alert(replaceAll(replaceAll(LS("ERROR_UNSUPPORTED_IMPORTER"), "$1", importer_.unsupportedVersion), "$2", "1.0"));
}

void App::cmdImport(bool clearFirst, bool special) {
    auto run = [this, clearFirst, special] {
        if (clearFirst) {
            // ImportManager.clearScene() happens before the file is chosen.
            container_->clearEventJournal();
            scene_.clear();
        }
        std::string path = dialogs::openFile("Open", "Tanks level (*.xml)", "*.xml");
        if (!path.empty()) startImport(path, special);
    };
    if (clearFirst)
        askToDiscardChanges(run);
    else
        run();
}

void App::cmdExport(MapFormat format) {
    if (format == MapFormat::XmlV3 || format == MapFormat::Binary) {
        alert("This export format is planned for the next phase of the OpenGL port.");
        return;
    }
    std::string path = dialogs::saveFile("Save", "XML (*.xml)", "*.xml", "xml");
    if (path.empty()) return;
    scene_.cursor.clear();
    std::string out, err;
    if (!exporter_.exportScene(scene_, format, out, &err)) {
        alert(err);
        return;
    }
    if (!writeFileText(path, out)) {
        alert("Cannot write " + path);
        return;
    }
    scene_.changed = false;
}

void App::cmdClearLevel() {
    container_->clearEventJournal();
    scene_.clear();
    scene_.cursor.clear();
    scene_.changed = true;
}

void App::cmdSelectAll() {
    scene_.cursor.setVisible(false);
    scene_.selectAll();
}

void App::cmdLoadLibrary(bool clearFirst) {
    std::string dir = dialogs::pickFolder("Load library");
    if (dir.empty()) return;
    Settings::instance().appendRecentDirectory(dir);
    progressLabel_ = LS("PROGRESSBAR_LOADING_LIBRARIES");
    libs_.loadGivenLibraries({dir}, clearFirst);
    if (clearFirst) selectLibrary(functional::LIBRARY_NAME);
}

void App::cmdLoadRecent(const std::string& dir) {
    if (!isDirectory(dir)) {
        alert(LS("ERROR_FOLDER_NO_LONGER_EXISTS") + " " + dir);
        return;
    }
    progressLabel_ = LS("PROGRESSBAR_LOADING_LIBRARIES");
    libs_.loadGivenLibraries({dir});
}

void App::cmdClearLibraries() {
    libs_.clearLibraries();
    selectLibrary(functional::LIBRARY_NAME);
}

void App::setSnapping(bool on) {
    if (snapping_ == on) return;
    snapping_ = on;
    container_->setSnapMode(on);
}

void App::setGeometryView(bool collision) {
    if (showCollision_ == collision) return;
    showCollision_ = collision;
    scene_.collisionView = collision;
}

void App::selectLibrary(const std::string& name) {
    currentLibrary_ = name;
    openGroup_ = 0;
}

void App::onLibrariesLoaded() {
    size_t total = 0;
    for (auto& name : libs_.libraryNames)
        if (auto* props = libs_.getPropsByLibName(name)) total += props->size();
    std::fprintf(stdout, "libraries loaded: %zu libraries, %zu props, %.0f ms\n", libs_.libraryNames.size(), total,
                 (glfwGetTime() - loadStart_) * 1000.0);
    std::fflush(stdout);
    scene_.cursor.clear();
    if (!libs_.lastLoadedLibrary.empty()) selectLibrary(libs_.lastLoadedLibrary);
}

void App::onPropListSelect(const PropRef& prop) {
    listSelected_ = prop;
    previewProp_ = prop;
    scene_.cursor.setObject(prop);
}

// ---------------------------------------------------------------------------------------------
// Frame

void App::frame(double dt) {
    iconsThisFrame_ = 0;
    // Background work
    if (libs_.loading() && !wasLoading_) loadStart_ = glfwGetTime();
    wasLoading_ = libs_.loading();
    if (libs_.loading()) {
        if (progressLabel_.empty()) progressLabel_ = LS("PROGRESSBAR_LOADING_LIBRARIES");
        libs_.step(12.0);
    }
    if (libs_.justCompleted) {
        libs_.justCompleted = false;
        onLibrariesLoaded();
    }
    if (importer_.active()) importer_.step();
    processDrops();

    // Startup automation (command-line options)
    if (startupStage_ == 0 && !libs_.loading()) {
        startupStage_ = 1;
        if (!opts_.map.empty()) startImport(opts_.map, false);
        if (!opts_.selectProp.empty()) {
            auto parts = split(opts_.selectProp, '/');
            if (parts.size() == 3)
                if (PropRef p = libs_.propByKey(LibraryManager::key(parts[0], parts[1], parts[2]))) {
                    onPropListSelect(p);
                    selectLibrary(parts[0]);
                    scene_.cursor.setVisible(true);
                }
        }
    }
    if (startupStage_ == 1 && !importer_.active()) {
        startupStage_ = 2;
        if (!opts_.exportPath.empty()) {
            std::string out, err;
            if (exporter_.exportScene(scene_, MapFormat::XmlV1Full, out, &err)) writeFileText(opts_.exportPath, out);
            std::fprintf(stdout, "exported %zu props to %s\n", scene_.children.size(), opts_.exportPath.c_str());
            if (opts_.screenshot.empty()) quit_ = true;
        }
    }
    if (startupStage_ == 2 && !opts_.screenshot.empty() && framesAfterLoad_ == 0) {
        // Automation: report errors on stdout instead of covering the view with the window.
        ErrorLog& log = ErrorLog::instance();
        if (log.visible) std::fprintf(stdout, "error window: %s\n%s", log.message.c_str(), log.content.c_str());
        log.visible = false;
        alerts_.clear();
    }
    if (startupStage_ == 2 && !opts_.screenshot.empty() && ++framesAfterLoad_ == 30) {
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        std::vector<uint8_t> px((size_t)w * h * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
        image::savePng(opts_.screenshot, w, h, px.data(), true);
        std::fprintf(stdout, "screenshot saved to %s\n", opts_.screenshot.c_str());
        quit_ = true;
    }

    // Held keys (KeyMapper) and camera controllers
    ImGuiIO& io = ImGui::GetIO();
    CameraRig::Input held;
    if (!io.WantTextInput && alerts_.empty()) {
        held.yawLeft = ImGui::IsKeyDown(ImGuiKey_N) && !io.KeyCtrl;
        held.yawRight = ImGui::IsKeyDown(ImGuiKey_M);
        held.left = ImGui::IsKeyDown(ImGuiKey_Keypad4);
        held.right = ImGui::IsKeyDown(ImGuiKey_Keypad6);
        held.forward = ImGui::IsKeyDown(ImGuiKey_Keypad8);
        held.back = ImGui::IsKeyDown(ImGuiKey_Keypad2);
        held.pitchDown = ImGui::IsKeyDown(ImGuiKey_Keypad9);
        held.pitchUp = ImGui::IsKeyDown(ImGuiKey_Keypad3);
        // The N key is the "Snapping" menu shortcut, which consumes it (see processShortcuts).
        held.yawLeft = false;
    }
    container_->update(dt, held);

    // ---- layout ----
    ImGuiViewport* vp = ImGui::GetMainViewport();
    drawMenuBar();
    float menuH = ImGui::GetFrameHeight();
    float barH = 30;
    drawControlBar(vp->Pos.y + menuH, barH);
    float top = vp->Pos.y + menuH + barH;
    float totalW = vp->Size.x, totalH = vp->Size.y - menuH - barH;
    const float split = 5;
    rightWidth_ = std::clamp(rightWidth_, 150.f, std::max(150.f, totalW - 300.f));
    float leftW = totalW - rightWidth_ - split;
    float propsH = showProperties_ ? std::clamp(propsHeight_, 80.f, std::max(80.f, totalH - 150.f)) : 0;
    float centerH = totalH - (showProperties_ ? propsH + split : 0);
    float paramsW = showProperties_ ? std::clamp(paramsWidth_, 120.f, std::max(120.f, leftW - 200.f)) : 0;
    float workX = vp->Pos.x + (showProperties_ ? paramsW + split : 0);
    float workW = leftW - (showProperties_ ? paramsW + split : 0);

    if (showProperties_) drawPropParamsPanel(vp->Pos.x, top, paramsW, centerH);
    drawWorkspace(workX, top, workW, centerH);
    if (showProperties_) drawPropertiesPanel(vp->Pos.x, top + centerH + split, leftW, propsH);
    float rx = vp->Pos.x + leftW + split;
    previewHeight_ = std::clamp(previewHeight_, 100.f, std::max(100.f, totalH - 150.f));
    drawPreview(rx, top, rightWidth_, previewHeight_);
    drawPropList(rx, top + previewHeight_ + split, rightWidth_, totalH - previewHeight_ - split);

    // splitters
    splitter("##split_right", true, vp->Pos.x + leftW, top, split, totalH, rightWidth_, 150, totalW - 300, -1);
    splitter("##split_preview", false, rx, top + previewHeight_, rightWidth_, split, previewHeight_, 100, totalH - 150, 1);
    if (showProperties_) {
        splitter("##split_props", false, vp->Pos.x, top + centerH, leftW, split, propsHeight_, 80, totalH - 150, -1);
        splitter("##split_params", true, vp->Pos.x + paramsW, top, split, centerH, paramsWidth_, 120, leftW - 200, 1);
    }

    drawLayersWindow();
    drawErrorWindow();
    drawHelpWindow();
    drawProgress();
    drawAlerts();
    processShortcuts();
}

bool App::splitter(const char* id, bool vertical, float x, float y, float w, float h, float& value, float minV,
                   float maxV, float sign) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::InvisibleButton("##drag", ImVec2(w, h));
    bool changed = false;
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) {
        float d = vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
        if (d != 0) {
            value = std::clamp(value + d * sign, minV, std::max(minV, maxV));
            changed = true;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return changed;
}

// ---------------------------------------------------------------------------------------------
// Shortcuts (menu key equivalents)

void App::processShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || !alerts_.empty() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    const bool c = io.KeyCtrl, s = io.KeyShift, a = io.KeyAlt;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    auto pressedRepeat = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    Modifiers mods{s, c, a};
    bool consumed = false;
    auto shortcut = [&](ImGuiKey k, bool ctrl, bool shift, bool alt, auto fn) {
        if (consumed) return;
        if (pressed(k) && c == ctrl && s == shift && a == alt) {
            consumed = true;
            fn();
        }
    };
    shortcut(ImGuiKey_N, true, false, false, [&] { cmdNewLevel(); });
    shortcut(ImGuiKey_I, true, false, false, [&] { cmdImport(true, false); });
    shortcut(ImGuiKey_A, true, false, false, [&] { cmdImport(false, false); });
    shortcut(ImGuiKey_T, true, false, false, [&] { cmdExport(MapFormat::XmlV1Full); });
    shortcut(ImGuiKey_Z, true, false, false, [&] { cmdUndo(); });
    shortcut(ImGuiKey_Y, true, false, false, [&] { cmdRedo(); });
    shortcut(ImGuiKey_C, true, false, true, [&] { cmdClearLevel(); });
    shortcut(ImGuiKey_A, false, true, false, [&] { cmdSelectAll(); });
    shortcut(ImGuiKey_1, true, false, false, [&] { setGeometryView(false); });
    shortcut(ImGuiKey_2, true, false, false, [&] { setGeometryView(true); });
    shortcut(ImGuiKey_B, true, false, false, [&] { container_->showBoundBoxes = !container_->showBoundBoxes; });
    shortcut(ImGuiKey_G, false, false, false, [&] { scene_.gridVisible = !scene_.gridVisible; });
    shortcut(ImGuiKey_L, false, false, false, [&] { showLayers_ = !showLayers_; });
    shortcut(ImGuiKey_L, true, false, true, [&] { cmdLoadLibrary(true); });
    shortcut(ImGuiKey_L, true, false, false, [&] { cmdLoadLibrary(false); });
    shortcut(ImGuiKey_N, false, false, false, [&] { setSnapping(!snapping_); });
    if (consumed) return;

    // AlternativaEditor.onKeyDown: Space
    if (pressedRepeat(ImGuiKey_Space) && !showCollision_) {
        if (scene_.cursor.visible()) {
            if (PropRef prefab = scene_.cursor.prefab())
                container_->addProp(prefab);
            else
                alert(LS("ALERT_TEXT_SELECT_PROP_FOR_PASTING"));
        } else if (showProperties_ && isTexturePanel()) {
            applyTexture(textureSelected_);
        }
    }

    // SceneContainer.onKeyDown / onKeyUp
    struct Map {
        ImGuiKey k;
        Key key;
    };
    static const Map keys[] = {{ImGuiKey_0, Key::Num0},          {ImGuiKey_UpArrow, Key::Up},
                               {ImGuiKey_DownArrow, Key::Down},  {ImGuiKey_LeftArrow, Key::Left},
                               {ImGuiKey_RightArrow, Key::Right}, {ImGuiKey_V, Key::V},
                               {ImGuiKey_W, Key::W},             {ImGuiKey_S, Key::S},
                               {ImGuiKey_Delete, Key::Delete},   {ImGuiKey_C, Key::C},
                               {ImGuiKey_Z, Key::Z},             {ImGuiKey_X, Key::X},
                               {ImGuiKey_Escape, Key::Escape},   {ImGuiKey_KeypadAdd, Key::NumpadAdd},
                               {ImGuiKey_KeypadSubtract, Key::NumpadSubtract},
                               {ImGuiKey_Equal, Key::Equal},     {ImGuiKey_Minus, Key::Minus},
                               {ImGuiKey_Q, Key::Q},             {ImGuiKey_T, Key::T},
                               {ImGuiKey_K, Key::K}};
    for (auto& m : keys) {
        if (pressedRepeat(m.k)) container_->onKeyDown(m.key, mods);
        if (ImGui::IsKeyReleased(m.k)) container_->onKeyUp(m.key);
    }
}

void App::processDrops() {
    if (drops_.empty()) return;
    std::vector<std::string> paths;
    paths.swap(drops_);
    if (paths.size() == 1 && !isDirectory(paths[0])) {
        std::string name = pathToUtf8(u8path(paths[0]).filename());
        if (name.find(".xml") != std::string::npos && name.find(".xml") > 0) {
            container_->clearEventJournal();
            scene_.clear();
            startImport(paths[0], false);
            return;
        }
    }
    progressLabel_ = LS("PROGRESSBAR_LOADING_LIBRARIES");
    libs_.loadGivenLibraries(paths);
}

// ---------------------------------------------------------------------------------------------
// Menu bar

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    auto item = [](const char* id, const char* shortcut, bool checked = false, bool enabled = true) {
        return ImGui::MenuItem(L(id), shortcut, checked, enabled);
    };
    if (ImGui::BeginMenu(L("MENUITEM_FILE"))) {
        if (item("MENUITEM_FILE_NEW", "Ctrl+N")) cmdNewLevel();
        ImGui::Separator();
        if (ImGui::BeginMenu(L("MENUITEM_FILE_IMPORT"))) {
            if (item("MENUITEM_FILE_IMPORT_IMPORTTANKIMAP", "Ctrl+I")) cmdImport(true, false);
            if (item("MENUITEM_FILE_IMPORT_APPENDTANKIMAP", "Ctrl+A")) cmdImport(false, false);
            if (item("MENUITEM_FILE_IMPORT_APPENDSPECIALGEOMETRY", nullptr)) cmdImport(false, true);
            if (item("MENUITEM_FILE_IMPORT_BATCHPROCESSING", nullptr))
                alert("Batch processing is planned for the next phase of the OpenGL port.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(L("MENUITEM_FILE_EXPORT"))) {
            if (item("MENUITEM_FILE_EXPORT_FLASHMAP1", "Ctrl+T")) cmdExport(MapFormat::XmlV1Full);
            if (item("MENUITEM_FILE_EXPORT_FLASHMAP1NOFUNC", nullptr)) cmdExport(MapFormat::XmlV1Lite);
            if (item("MENUITEM_FILE_EXPORT_FLASHMAP3", nullptr)) cmdExport(MapFormat::XmlV3);
            if (item("MENUITEM_FILE_EXPORT_HTML5MAP", nullptr)) cmdExport(MapFormat::Binary);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(L("MENUITEM_EDIT"))) {
        if (item("MENUITEM_EDIT_UNDO", "Ctrl+Z")) cmdUndo();
        if (item("MENUITEM_EDIT_REDO", "Ctrl+Y")) cmdRedo();
        if (item("MENUITEM_EDIT_CLEAR", "Ctrl+Alt+C")) cmdClearLevel();
        if (item("MENUITEM_EDIT_SELECTALL", "Shift+A")) cmdSelectAll();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(L("MENUITEM_VIEW"))) {
        if (ImGui::BeginMenu(L("MENUITEM_VIEW_GEOMETRY"))) {
            if (item("MENUITEM_VIEW_GEOMETRY_VISUAL", "Ctrl+1", !showCollision_)) setGeometryView(false);
            if (item("MENUITEM_VIEW_GEOMETRY_COLLISION", "Ctrl+2", showCollision_)) setGeometryView(true);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (item("MENUITEM_VIEW_SHOWBOUNDBOXES", "Ctrl+B", container_->showBoundBoxes))
            container_->showBoundBoxes = !container_->showBoundBoxes;
        if (item("MENUITEM_VIEW_SHOWGRID", "G", scene_.gridVisible)) scene_.gridVisible = !scene_.gridVisible;
        if (item("MENUITEM_VIEW_SHOWLAYERS", "L", showLayers_)) showLayers_ = !showLayers_;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(L("MENUITEM_LIBRARY"))) {
        if (item("MENUITEM_LIBRARY_CLEAR", nullptr)) cmdClearLibraries();
        if (item("MENUITEM_LIBRARY_CLEARANDLOAD", "Ctrl+Alt+L")) cmdLoadLibrary(true);
        if (ImGui::BeginMenu(L("MENUITEM_LIBRARY_LOAD"))) {
            if (item("MENUITEM_LIBRARY_LOADFROMDIR", "Ctrl+L")) cmdLoadLibrary(false);
            if (ImGui::BeginMenu(L("MENUITEM_LIBRARY_RECENT"))) {
                auto recent = Settings::instance().recentDirs;
                for (auto& d : recent)
                    if (ImGui::MenuItem(d.c_str())) cmdLoadRecent(d);
                ImGui::Separator();
                if (item("MENUITEM_LIBRARY_RECENT_CLEAR", nullptr)) Settings::instance().clearRecent();
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(L("MENUITEM_OPTIONS"))) {
        if (ImGui::BeginMenu(L("MENUITEM_OPTIONS_PROPSINTERSECT"))) {
            MultiPropMode& m = container_->multiplePropMode;
            if (item("MENUITEM_OPTIONS_PROPSINTERSECT_ALLALLOWED", nullptr, m == MultiPropMode::ANY)) m = MultiPropMode::ANY;
            if (item("MENUITEM_OPTIONS_PROPSINTERSECT_SAMEDENIED", nullptr, m == MultiPropMode::GROUP)) m = MultiPropMode::GROUP;
            if (item("MENUITEM_OPTIONS_PROPSINTERSECT_ALLDENIED", nullptr, m == MultiPropMode::NONE)) m = MultiPropMode::NONE;
            ImGui::EndMenu();
        }
        if (item("MENUITEM_OPTIONS_SNAP", "N", snapping_)) setSnapping(!snapping_);
        if (ImGui::BeginMenu(L("MENUITEM_OPTIONS_LANGUAGES"))) {
            for (auto& lang : Locale::languages())
                if (ImGui::MenuItem(lang.c_str(), nullptr, lang == Locale::current())) {
                    Locale::setLocale(lang);
                    Settings::instance().language = lang;
                    Settings::instance().save();
                }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(L("MENUITEM_HELP"))) {
        if (item("MENUITEM_HELP_SHOW", nullptr)) showHelp_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// ---------------------------------------------------------------------------------------------
// Control bar

void App::drawControlBar(float y, float h) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, y));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 3));
    ImGui::Begin("##controlbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    auto modeButton = [&](const char* id, const char* upIcon, const char* downIcon, bool down, const char* upTip,
                          const char* downTip) {
        TextureRef t = uiIcons_[down ? downIcon : upIcon];
        bool clicked;
        ImGui::PushID(id);
        if (down) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (t)
            clicked = ImGui::ImageButton(id, texId(t->glId()), ImVec2(16, 16));
        else
            clicked = ImGui::Button(id, ImVec2(24, 24));
        if (down) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", down ? L(downTip) : L(upTip));
        ImGui::PopID();
        ImGui::SameLine();
        return clicked;
    };
    if (modeButton("snap", "snap_icon", "snap_icon_on", snapping_, "CONTROLBAR_SNAP_ENABLE_TOOLTIP",
                   "CONTROLBAR_SNAP_DISABLE_TOOLTIP"))
        setSnapping(!snapping_);
    if (modeButton("boxes", "boxes_icon", "boxes_icon_on", showCollision_, "CONTROLBAR_SHOWCOLLISION_ENABLE_TOOLTIP",
                   "CONTROLBAR_SHOWCOLLISION_DISABLE_TOOLTIP"))
        setGeometryView(!showCollision_);
    if (modeButton("grid", "grid_icon", "grid_icon_on", scene_.gridVisible, "CONTROLBAR_GRID_ENABLE_TOOLTIP",
                   "CONTROLBAR_GRID_DISABLE_TOOLTIP"))
        scene_.gridVisible = !scene_.gridVisible;
    if (modeButton("props", "textures_icon", "textures_icon_on", showProperties_,
                   "CONTROLBAR_PROPERTIES_ENABLE_TOOLTIP", "CONTROLBAR_PROPERTIES_DISABLE_TOOLTIP"))
        showProperties_ = !showProperties_;
    auto plainButton = [&](const char* id, const char* icon, const char* tip) {
        TextureRef t = uiIcons_[icon];
        bool clicked = t ? ImGui::ImageButton(id, texId(t->glId()), ImVec2(16, 16)) : ImGui::Button(id);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L(tip));
        ImGui::SameLine();
        return clicked;
    };
    if (plainButton("hide", "hide_selected", "CONTROLBAR_HIDESELECTED")) scene_.hideSelectedProps();
    if (plainButton("show", "show_all", "CONTROLBAR_SHOWALL")) scene_.showAll();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L("CONTROLBAR_SELECTFILTER_LABEL"));
    ImGui::SameLine();
    const char* options[] = {L("CONTROLBAR_SELECTFILTER_OPTION_ALL"), L("CONTROLBAR_SELECTFILTER_OPTION_TILE"),
                             L("CONTROLBAR_SELECTFILTER_OPTION_SPRITE"), L("CONTROLBAR_SELECTFILTER_OPTION_FUNCTIONAL")};
    std::string opt[4] = {options[0], options[1], options[2], options[3]};
    int idx = scene_.selectableFilter();
    ImGui::SetNextItemWidth(130);
    if (ImGui::BeginCombo("##filter", opt[idx].c_str())) {
        for (int q = 0; q < 4; ++q)
            if (ImGui::Selectable(opt[q].c_str(), q == idx)) scene_.setSelectableFilter(q);
        ImGui::EndCombo();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------------------------
// Workspace

static Modifiers mods() {
    ImGuiIO& io = ImGui::GetIO();
    return {io.KeyShift, io.KeyCtrl, io.KeyAlt};
}

void App::drawWorkspace(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
    std::string title = LS("WORKSPACE_PANEL_TITLE") + "###workspace";
    ImGui::Begin(title.c_str(), nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int vw = std::max(1, (int)avail.x), vh = std::max(1, (int)avail.y);
    viewX_ = origin.x;
    viewY_ = origin.y;
    viewW_ = (float)vw;
    viewH_ = (float)vh;
    scene_.viewport = {(double)vw, (double)vh};

    // Render the 3D view into an FBO and show it.
    sceneFb_.resize(vw, vh);
    RenderOptions ro;
    ro.grid = scene_.gridVisible;
    ro.collisionView = showCollision_;
    ro.clear[0] = 1.0f;  // white workspace background
    ro.clear[1] = 1.0f;
    ro.clear[2] = 1.0f;
    renderer_.renderScene(scene_, sceneFb_, ro);
    ImGui::Image(texId(sceneFb_.colorTexture()), ImVec2((float)vw, (float)vh), ImVec2(0, 1), ImVec2(1, 0));
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // Input
    ImGuiIO& io = ImGui::GetIO();
    double mx = io.MousePos.x - origin.x, my = io.MousePos.y - origin.y;
    bool modal = !alerts_.empty() || importer_.active() || libs_.loading();
    if (!modal) {
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            leftDownInView_ = true;
            container_->onMouseDown(mx, my, mods());
        }
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            midDownInView_ = true;
            container_->onMiddleMouseDown(mx, my, mods());
        }
        if ((mx != lastMouseX_ || my != lastMouseY_) && (hovered || leftDownInView_ || midDownInView_))
            container_->onMouseMove(mx, my, mods());
        if (leftDownInView_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            leftDownInView_ = false;
            container_->onMouseUp(mx, my, mods());
        }
        if (midDownInView_ && !ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            midDownInView_ = false;
            container_->onMiddleMouseUp();
        }
        if (hovered && io.MouseWheel != 0) container_->onMouseWheel(io.MouseWheel, mods());
    }
    lastMouseX_ = mx;
    lastMouseY_ = my;

    drawOverlays(ImGui::GetWindowDrawList(), origin.x, origin.y, (float)vw, (float)vh);
    ImGui::End();
    ImGui::PopStyleVar();
}

void App::drawOverlays(ImDrawList* dl, float ox, float oy, float w, float h) {
    dl->PushClipRect(ImVec2(ox, oy), ImVec2(ox + w, oy + h), true);
    const Viewport& vp = scene_.viewport;
    auto P = [&](const Vec3& s) { return ImVec2(ox + (float)s.x, oy + (float)s.y); };

    // Bound boxes (Ctrl+B): AABB of every visible mesh prop, green.
    if (container_->showBoundBoxes) {
        std::vector<const Prop*> props;
        for (auto& p : scene_.children) props.push_back(p.get());
        if (auto g = scene_.cursor.object()) props.push_back(g.get());
        Matrix4 inv = scene_.camera.cameraMatrix().inverted();
        double f = CameraRig::focalLength(vp), hx = vp.width / 2, hy = vp.height / 2;
        for (const Prop* p : props) {
            if (p->kind != PropKind::Mesh || !p->visible || !p->model->mesh) continue;
            const MeshObject& m = *p->model->mesh;
            Vec3 mn = p->position() + m.boundMax, mx = p->position() + m.boundMin;  // names as in the original
            Vec3 pts[8] = {mn, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
                           {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
            ImVec2 s[8];
            bool ok = true;
            for (int q = 0; q < 8 && ok; ++q) {
                Vec3 c = inv.transform(pts[q]);
                if (c.z <= 0) { ok = false; break; }
                double sx = c.x * f / c.z, sy = c.y * f / c.z;
                if (sx < -hx || sx > hx || sy < -hy || sy > hy) { ok = false; break; }
                s[q] = ImVec2(ox + (float)(sx + hx), oy + (float)(sy + hy));
            }
            if (!ok) continue;
            ImU32 col = IM_COL32(0, 255, 0, 255);
            const int loop[] = {0, 1, 2, 3, 0, 4, 5, 6, 7, 4};
            for (int q = 0; q + 1 < 10; ++q) dl->AddLine(s[loop[q]], s[loop[q + 1]], col);
            dl->AddLine(s[1], s[5], col);
            dl->AddLine(s[2], s[6], col);
            dl->AddLine(s[3], s[7], col);
        }
    }

    // Domination links (green) while the "dom" layer is visible.
    if (scene_.layers.get("dom").visible) {
        for (auto& cp : scene_.dominationPoints()) {
            Vec3 a = scene_.camera.projectGlobal(cp->position(), vp);
            for (auto& sp : cp->getSpawnPoints()) {
                Vec3 b = scene_.camera.projectGlobal(sp->position(), vp);
                dl->AddLine(P(a), P(b), IM_COL32(0, 255, 0, 255));
            }
        }
    }

    // Rubber band
    if (container_->selectionRectVisible) {
        dl->AddRect(ImVec2(ox + (float)container_->selRectX, oy + (float)container_->selRectY),
                    ImVec2(ox + (float)(container_->selRectX + container_->selRectW),
                           oy + (float)(container_->selRectY + container_->selRectH)),
                    IM_COL32(0, 0, 0, 255));
    }

    // Axis indicator (CursorScene.drawAxis), bottom-left corner.
    Matrix4 cm = scene_.camera.cameraMatrix();
    const float size = 30;
    ImVec2 o(ox + size, oy + h - size);
    dl->AddLine(o, ImVec2(o.x + (float)cm.a * size, o.y + (float)cm.b * size), IM_COL32(255, 0, 0, 255), 2);
    dl->AddLine(o, ImVec2(o.x + (float)cm.e * size, o.y + (float)cm.f * size), IM_COL32(0, 255, 0, 255), 2);
    dl->AddLine(o, ImVec2(o.x + (float)cm.i * size, o.y + (float)cm.j * size), IM_COL32(0, 0, 255, 255), 2);
    dl->PopClipRect();
}

// ---------------------------------------------------------------------------------------------
// Preview + icons

void App::renderPreviewProp(const Prop& prop, Framebuffer& fb, double yaw) {
    // Preview.calculateOptimalCameraPosition
    double radius = 0, maxZ = -1e300, minZ = 1e300;
    if (prop.isSprite()) {
        TextureRef t = prop.bitmapData;
        double w = t ? t->width : 100, h = t ? t->height : 100;
        radius = prop.model->spriteScale * 1.5 * std::max(w, h);
        maxZ = h;
        minZ = 0;
    } else if (prop.model && prop.model->mesh) {
        const MeshObject& m = *prop.model->mesh;
        double best = 0;
        for (size_t q = 0; q < m.vertexCount(); ++q) {
            Vec3 v = m.vertex(q);
            best = std::max(best, v.lengthSqr());
            minZ = std::min(minZ, v.z);
            maxZ = std::max(maxZ, v.z);
        }
        radius = 2 * std::sqrt(best);
    }
    if (minZ > maxZ) minZ = maxZ = 0;
    double dist = radius * (std::sqrt(2.0) / (2 * std::tan(CameraRig::FOV / 2)) + 0.5);
    double height = (maxZ - minZ) / 2;
    Matrix4 container = Matrix4::compose(0, 0, 0, 0, 0, yaw);
    Matrix4 camLocal = Matrix4::compose(0, -dist, height / 2 + dist / std::sqrt(3.0), CameraRig::DEFAULT_ROT_X, 0, 0);
    // The prop is shown at the origin with its own rotation reset.
    PropRef tmp = prop.clone();
    tmp->x = tmp->y = tmp->z = 0;
    tmp->selected = false;
    tmp->materialOverride = MaterialOverride::None;
    float clear[4] = {1.f, 1.f, 1.f, 1.f};
    renderer_.renderProp(*tmp, Matrix4::mul(container, camLocal), fb, clear);
}

unsigned App::iconTexture(const Prop& prop) {
    auto it = icons_.find(&prop);
    if (it != icons_.end()) return it->second->colorTexture();
    if (iconsThisFrame_ >= 48) return 0;  // spread icon rendering over a few frames
    ++iconsThisFrame_;
    auto fb = std::make_unique<Framebuffer>();
    fb->resize(64, 64);
    renderPreviewProp(prop, *fb, PI / 2 + PI / 4);
    unsigned id = fb->colorTexture();
    icons_[&prop] = std::move(fb);
    return id;
}

void App::drawPreview(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
    std::string title = LS("PREVIEW_PANEL_TITLE") + "###preview";
    ImGui::Begin(title.c_str(), nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int pw = std::max(1, (int)avail.x), ph = std::max(1, (int)avail.y);
    previewFb_.resize(pw, ph);
    previewYaw_ += ImGui::GetIO().DeltaTime * 60.0 * DEG1;  // 1 degree per frame at 60 fps
    if (previewProp_) {
        renderPreviewProp(*previewProp_, previewFb_, previewYaw_);
    } else {
        float clear[4] = {1.f, 1.f, 1.f, 1.f};
        previewFb_.bind();
        glClearColor(clear[0], clear[1], clear[2], clear[3]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        Framebuffer::unbind();
    }
    ImGui::Image(texId(previewFb_.colorTexture()), ImVec2((float)pw, (float)ph), ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------------------------
// Library combo + prop list (accordion of tile lists)

void App::drawPropList(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##libpanel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##library", currentLibrary_.c_str())) {
        for (auto& name : libs_.libraryNames)
            if (ImGui::Selectable(name.c_str(), name == currentLibrary_)) selectLibrary(name);
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted(L("PROPSLIST_PANEL_TITLE"));
    ImGui::Separator();
    ImGui::BeginChild("##proplist", ImVec2(0, 0), ImGuiChildFlags_None);
    const std::vector<PropRef>* props = libs_.getPropsByLibName(currentLibrary_);
    if (props) {
        // group order of first appearance
        std::vector<std::string> groups;
        for (auto& p : *props)
            if (std::find(groups.begin(), groups.end(), p->groupName) == groups.end()) groups.push_back(p->groupName);
        if (openGroup_ >= (int)groups.size()) openGroup_ = 0;
        for (int g = 0; g < (int)groups.size(); ++g) {
            ImGui::PushID(g);
            bool open = g == openGroup_;
            ImGui::SetNextItemOpen(open, ImGuiCond_Always);
            if (ImGui::CollapsingHeader(groups[g].c_str())) {
                if (!open) openGroup_ = g;
                const float tileW = 64, tileH = 64 + ImGui::GetTextLineHeightWithSpacing();
                float avail = ImGui::GetContentRegionAvail().x;
                int perRow = std::max(1, (int)(avail / (tileW + 6)));
                int col = 0;
                for (auto& p : *props) {
                    if (p->groupName != groups[g]) continue;
                    ImGui::PushID(p.get());
                    if (col > 0) ImGui::SameLine();
                    ImGui::BeginGroup();
                    unsigned tex = iconTexture(*p);
                    bool sel = listSelected_ == p;
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
                    bool clicked;
                    if (tex)
                        clicked = ImGui::ImageButton("##icon", texId(tex), ImVec2(50, 50), ImVec2(0, 1), ImVec2(1, 0));
                    else
                        clicked = ImGui::Button("...", ImVec2(58, 58));
                    if (sel) ImGui::PopStyleColor();
                    std::string label = p->name;
                    if (ImGui::CalcTextSize(label.c_str()).x > tileW) {
                        while (label.size() > 3 && ImGui::CalcTextSize((label + "..").c_str()).x > tileW) label.pop_back();
                        label += "..";
                    }
                    ImGui::TextUnformatted(label.c_str());
                    ImGui::EndGroup();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p->name.c_str());
                    if (clicked) onPropListSelect(p);
                    (void)tileH;
                    ImGui::PopID();
                    col = (col + 1) % perRow;
                }
            } else if (open) {
                openGroup_ = -1;  // allow closing the open pane
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------
// Properties panel (bottom)

const PropModel* App::noConflictBitmaps() const {
    const PropModel* found = nullptr;
    for (auto& p : scene_.selectedProps) {
        if (!p->isMeshProp() || !p->model || !p->model->hasBitmaps) continue;
        if (!found)
            found = p->model.get();
        else if (found != p->model.get())
            return nullptr;
    }
    return found;
}

App::PanelKind App::currentPanel() const {
    const PropSet& sel = scene_.selectedProps;
    if (sel.empty() || !scene_.selectedProp()) return PanelKind::None;
    if (sel.size() == 1 && scene_.selectedProp()->kind == PropKind::ControlPoint) return PanelKind::ControlPoint;
    auto all = [&](auto pred) {
        for (auto& p : sel)
            if (!pred(*p)) return false;
        return true;
    };
    if (all([](const Prop& p) { return p.kind == PropKind::Bonus; })) return PanelKind::Bonus;
    if (all([](const Prop& p) { return p.isMeshProp(); })) return noConflictBitmaps() ? PanelKind::Textures : PanelKind::None;
    if (all([](const Prop& p) { return p.kind == PropKind::Kill; })) return PanelKind::Kill;
    return PanelKind::None;
}

bool App::isTexturePanel() const { return currentPanel() == PanelKind::Textures && !textureSelected_.empty(); }

void App::applyTexture(const std::string& name) {
    for (auto& p : scene_.selectedProps)
        if (p->isMeshProp() && p->model && p->model->hasBitmaps) p->setTextureName(name);
}

void App::drawPropertiesPanel(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    std::string title = LS("PROPERTIES_PANEL_TITLE") + "###properties";
    ImGui::Begin(title.c_str(), nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    switch (currentPanel()) {
        case PanelKind::Textures: drawTexturePanel(); break;
        case PanelKind::Bonus: drawBonusPanel(); break;
        case PanelKind::Kill: drawKillPanel(); break;
        case PanelKind::ControlPoint: drawControlPointPanel(); break;
        default: break;
    }
    ImGui::End();
}

void App::drawTexturePanel() {
    const PropModel* bitmaps = noConflictBitmaps();
    if (bitmaps != textureBitmaps_) {
        textureBitmaps_ = bitmaps;
        textureSelected_.clear();  // TexturePanel.fill() resets the selection
    }
    if (!bitmaps) return;
    ImGui::TextUnformatted(L("TEXTURES_PANEL_TITLE"));
    struct Item {
        std::string id, label;
        TextureRef tex;
    };
    std::vector<Item> items;
    for (auto& [name, tex] : bitmaps->bitmaps) items.push_back({name, name, tex});
    items.push_back({"invisible", LS("INVISIBLE_TEXTURE_NAME"), TextureRegistry::instance().invisible()});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.label < b.label; });

    float previewSize = 100;
    float listW = ImGui::GetContentRegionAvail().x - previewSize - 12;
    ImGui::BeginChild("##texlist", ImVec2(listW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    TextureRef selectedTex;
    for (size_t q = 0; q < items.size(); ++q) {
        auto& it = items[q];
        ImGui::PushID((int)q);
        if (q > 0) ImGui::SameLine();
        ImGui::BeginGroup();
        bool sel = it.id == textureSelected_;
        if (sel) {
            selectedTex = it.tex;
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        }
        bool clicked = it.tex ? ImGui::ImageButton("##tex", texId(it.tex->glId()), ImVec2(48, 48))
                              : ImGui::Button("?", ImVec2(56, 56));
        if (sel) ImGui::PopStyleColor();
        std::string label = it.label;
        if (label.size() > 8) label = label.substr(0, 7) + "..";
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", it.label.c_str());
        if (clicked) {
            textureSelected_ = it.id;
            selectedTex = it.tex;
            applyTexture(it.id);  // PropListEvent.SELECT -> MainScene.onTexturePanelSelect
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (selectedTex) ImGui::Image(texId(selectedTex->glId()), ImVec2(previewSize, previewSize));
}

static bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

static void setMember(std::vector<std::string>& v, const std::string& s, bool on) {
    auto it = std::find(v.begin(), v.end(), s);
    if (on && it == v.end()) v.push_back(s);
    if (!on && it != v.end()) v.erase(it);
}

void App::drawBonusPanel() {
    PropRef main = scene_.selectedProp();
    const PropSet& sel = scene_.selectedProps;
    std::map<std::string, bool> typeEnabled, modeEnabled;
    for (auto& t : functional::BONUS_TYPES) typeEnabled[t] = true;
    for (auto& m : functional::GAME_MODES) modeEnabled[m] = true;
    bool showParachute = true;
    for (auto& r : sel) {
        showParachute = showParachute && r->parachute == main->parachute;
        for (auto& t : functional::BONUS_TYPES)
            if (contains(r->bonusTypes, t) != contains(main->bonusTypes, t)) {
                typeEnabled[t] = false;
                break;  // as in the original: only the first mismatch per region
            }
        for (auto& m : functional::GAME_MODES)
            if (contains(r->gameModes, m) != contains(main->gameModes, m)) {
                modeEnabled[m] = false;
                break;
            }
    }
    ImGui::BeginGroup();
    ImGui::TextUnformatted(L("BONUSREGIONS_LABEL_BONUSTYPES"));
    for (auto& t : functional::BONUS_TYPES) {
        bool v = typeEnabled[t] && contains(main->bonusTypes, t);
        ImGui::BeginDisabled(!typeEnabled[t]);
        std::string label = LS("BONUSREGIONS_BONUSTYPE_" + toUpper(t)) + "##t" + t;
        if (ImGui::Checkbox(label.c_str(), &v))
            for (auto& r : sel) setMember(r->bonusTypes, t, v);
        ImGui::EndDisabled();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 30);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(L("BONUSREGIONS_LABEL_GAMEMODES"));
    for (auto& m : functional::GAME_MODES) {
        bool v = modeEnabled[m] && contains(main->gameModes, m);
        ImGui::BeginDisabled(!modeEnabled[m]);
        std::string label = LS("BONUSREGIONS_GAMEMODE_" + toUpper(m)) + "##m" + m;
        if (ImGui::Checkbox(label.c_str(), &v))
            for (auto& r : std::vector<PropRef>(sel.items())) {
                setMember(r->gameModes, m, v);
                scene_.onLayerContentChange(r, m, v);
            }
        ImGui::EndDisabled();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 30);
    ImGui::BeginGroup();
    bool single = sel.size() == 1;
    ImGui::TextUnformatted(L("BONUSREGIONS_LABEL_SIZE"));
    ImGui::BeginDisabled(!single);
    double sx = single ? main->scaleX : 1, sy = single ? main->scaleY : 1, sz = single ? main->scaleZ : 1;
    ImGui::SetNextItemWidth(140);
    bool ch = ImGui::InputDouble("##sx", &sx, 0.1, 1.0, "%.1f");
    ImGui::SetNextItemWidth(140);
    ch |= ImGui::InputDouble("##sy", &sy, 0.1, 1.0, "%.1f");
    ImGui::SetNextItemWidth(140);
    ch |= ImGui::InputDouble("##sz", &sz, 0.1, 1.0, "%.1f");
    if (ch && single) {
        main->setScaleX(std::clamp(sx, 0.1, 50.0));
        main->setScaleY(std::clamp(sy, 0.1, 50.0));
        main->setScaleZ(std::clamp(sz, 0.1, 50.0));
    }
    ImGui::EndDisabled();
    bool para = showParachute && main->parachute;
    ImGui::BeginDisabled(!showParachute);
    if (ImGui::Checkbox(L("BONUSREGIONS_OPTION_PARACHUTE"), &para))
        for (auto& r : sel) r->parachute = para;
    ImGui::EndDisabled();
    ImGui::EndGroup();
}

void App::drawKillPanel() {
    PropRef main = scene_.selectedProp();
    const PropSet& sel = scene_.selectedProps;
    if (sel.size() == 1) {
        ImGui::BeginGroup();
        ImGui::TextUnformatted(L("KILLBOX_LABEL_SCALE"));
        double sx = main->scaleX, sy = main->scaleY, sz = main->scaleZ;
        ImGui::SetNextItemWidth(140);
        bool ch = ImGui::InputDouble("##ksx", &sx, 0.1, 1.0, "%.1f");
        ImGui::SetNextItemWidth(140);
        ch |= ImGui::InputDouble("##ksy", &sy, 0.1, 1.0, "%.1f");
        ImGui::SetNextItemWidth(140);
        ch |= ImGui::InputDouble("##ksz", &sz, 0.1, 1.0, "%.1f");
        if (ch) {
            main->setScaleX(std::clamp(sx, 0.1, 100.0));
            main->setScaleY(std::clamp(sy, 0.1, 100.0));
            main->setScaleZ(std::clamp(sz, 0.1, 100.0));
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 30);
    }
    bool same = true;
    for (auto& b : sel)
        if (b->killAction != main->killAction) same = false;
    if (same) {
        ImGui::BeginGroup();
        ImGui::TextUnformatted(L("KILLBOX_LABEL_ACTION"));
        auto& actions = functional::KILL_ACTIONS;
        int idx = (int)(std::find(actions.begin(), actions.end(), main->killAction) - actions.begin());
        std::string preview = idx < (int)actions.size() ? LS("KILLBOX_ACTION_" + toUpper(actions[idx])) : "";
        ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo("##action", preview.c_str())) {
            for (int q = 0; q < (int)actions.size(); ++q)
                if (ImGui::Selectable(L("KILLBOX_ACTION_" + toUpper(actions[q])), q == idx))
                    for (auto& b : sel) b->killAction = actions[q];
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
    }
}

void App::drawControlPointPanel() {
    PropRef cp = scene_.selectedProp();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(L("CONTROLPOINT_CHOOSELETTER_LABEL"));
    ImGui::SetNextItemWidth(80);
    if (ImGui::BeginCombo("##letter", cp->controlPointName.c_str())) {
        for (char ch : functional::LETTERS) {
            std::string s(1, ch);
            if (ImGui::Selectable(s.c_str(), s == cp->controlPointName)) cp->controlPointName = s;
        }
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 30);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(L("CONTROLPOINT_CHOOSEMODE_LABEL"));
    std::vector<std::string> modes;
    bool changed = false;
    for (auto& m : functional::CONTROL_POINT_MODES) {
        bool v = contains(cp->gameModes, m);
        std::string label = LS("BONUSREGIONS_GAMEMODE_" + toUpper(m)) + "##cp" + m;
        if (ImGui::Checkbox(label.c_str(), &v)) changed = true;
        if (v) modes.push_back(m);
    }
    if (changed) cp->gameModes = modes;
    ImGui::EndGroup();
}

// ---------------------------------------------------------------------------------------------
// Prop parameters panel (left)

void App::drawPropParamsPanel(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    std::string title = LS("PROPPARAMS_PANEL_TITLE") + "###params";
    ImGui::Begin(title.c_str(), nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    PropRef main = scene_.selectedProp();
    const PropSet& sel = scene_.selectedProps;
    if (main && !sel.empty()) {
        // Collision checkbox: shown when every collision-capable prop shares the same value.
        if (main->hasCollisionParam()) {
            bool value = main->collisionEnabled, uniform = true;
            for (auto& p : sel)
                if (p->hasCollisionParam() && p->collisionEnabled != value) uniform = false;
            if (uniform && ImGui::Checkbox(L("PROPPARAMS_PARAM_COLLISIONS"), &value))
                for (auto& p : sel)
                    if (p->hasCollisionParam()) p->collisionEnabled = value;
        }
        if (sel.size() == 1) {
            ImGui::Text("%s %s", L("PROPPARAMS_LABEL_NAME"), main->name.c_str());
            ImGui::Text("%s %s", L("PROPPARAMS_LABEL_GROUP"), main->groupName.c_str());
            ImGui::Text("%s %s", L("PROPPARAMS_LABEL_LIBRARY"), main->libraryName.c_str());
            ImGui::Spacing();
            ImGui::TextUnformatted("Position:");
            double px = main->x, py = main->y, pz = main->z;
            ImGui::SetNextItemWidth(-1);
            bool ch = ImGui::InputDouble("##px", &px, 1, 10, "%.3f");
            ImGui::SetNextItemWidth(-1);
            ch |= ImGui::InputDouble("##py", &py, 1, 10, "%.3f");
            ImGui::SetNextItemWidth(-1);
            ch |= ImGui::InputDouble("##pz", &pz, 1, 10, "%.3f");
            if (ch) main->setPosition(px, py, pz);
            ImGui::TextUnformatted("Rotation:");
            double deg = std::fmod(toDegree(main->rotationZ), 360.0);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputDouble("##rz", &deg, 0.1, 1.0, "%.1f"))
                main->setRotationZ(toRadian(std::clamp(deg, -360.0, 360.0)));
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------
// Floating windows

void App::drawLayersWindow() {
    if (!showLayers_) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    std::string title = LS("LAYERS_PANEL_TITLE") + "###layers";
    if (ImGui::Begin(title.c_str(), &showLayers_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        for (auto& layer : Layers::windowLayers()) {
            bool v = layerVisible_[layer];
            std::string label = LS("LAYER_" + toUpper(layer)) + "##" + layer;
            if (ImGui::Checkbox(label.c_str(), &v)) {
                layerVisible_[layer] = v;
                scene_.setLayerVisible(layer, v);
            }
        }
    }
    ImGui::End();
}

void App::drawErrorWindow() {
    ErrorLog& log = ErrorLog::instance();
    if (!log.visible) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 320), ImGuiCond_Appearing);
    std::string title = LS("ERROR_PANEL_TITLE") + "###errors";
    if (ImGui::Begin(title.c_str(), &log.visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextWrapped("%s", log.message.c_str());
        ImGui::Separator();
        ImGui::BeginChild("##errtext", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(log.content.c_str());
        ImGui::EndChild();
        if (ImGui::Button(L("ALERT_BUTTON_OK"), ImVec2(80, 0))) log.visible = false;
    }
    ImGui::End();
}

void App::drawHelpWindow() {
    if (!showHelp_) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    std::string title = LS("HELP_PANEL_TITLE") + "###help";
    if (ImGui::Begin(title.c_str(), &showHelp_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted(helpText_.c_str());
        if (ImGui::Button(L("ALERT_BUTTON_OK"), ImVec2(80, 0))) showHelp_ = false;
    }
    ImGui::End();
}

void App::drawProgress() {
    bool busy = libs_.loading() || importer_.active();
    if (!busy) return;
    double p = libs_.loading() ? libs_.progress() : importer_.progress();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(320, 0));
    ImGui::Begin("##progress", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
    std::string label = libs_.loading() ? LS("PROGRESSBAR_LOADING_LIBRARIES") : LS("PROGRESSBAR_IMPORTING_LEVEL");
    ImGui::TextUnformatted(label.c_str());
    ImGui::ProgressBar((float)p, ImVec2(-1, 0));
    ImGui::End();
}

}  // namespace ae
