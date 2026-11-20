// Application shell: window layout, menus, panels (port of AlternativaEditor.mxml and the
// Flex panels), wired to MainScene / SceneContainer / LibraryManager.
#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/SceneContainer.h"
#include "lib/LibraryManager.h"
#include "map/MapIO.h"
#include "render/Renderer.h"
#include "scene/MainScene.h"

struct GLFWwindow;
struct ImDrawList;

namespace ae {

struct StartupOptions {
    std::vector<std::string> libraries;  // --lib <dir>
    std::string map;                     // --map <file.xml>
    std::string exportPath;              // --export <file.xml> (V1 full, then quit)
    std::string screenshot;              // --screenshot <file.png> (then quit)
    std::string selectProp;              // --select <library/group/name> put on cursor
    bool collisionView = false;          // --collision
};

class App {
  public:
    bool init(GLFWwindow* window, const std::string& assetsDir, const StartupOptions& opts, std::string* err);
    void shutdown();
    void frame(double dt);
    bool wantsQuit() const { return quit_; }
    void onDrop(const std::vector<std::string>& paths) { drops_.insert(drops_.end(), paths.begin(), paths.end()); }

  private:
    // ---- alerts ----
    enum Buttons { OK = 1, YES_NO = 2, YES_CANCEL = 3 };
    struct Alert {
        std::string title, text;
        Buttons buttons = OK;
        std::function<void(bool yes)> callback;
    };
    void alert(const std::string& text, const std::string& title = "", Buttons b = OK,
               std::function<void(bool)> cb = nullptr);
    void drawAlerts();

    // ---- commands (menu handlers) ----
    void cmdNewLevel();
    void cmdImport(bool clearFirst, bool special);
    void startImport(const std::string& path, bool special);
    void cmdExport(MapFormat format);
    void cmdUndo() { container_->undo(); }
    void cmdRedo() { container_->redo(); }
    void cmdClearLevel();
    void cmdSelectAll();
    void cmdLoadLibrary(bool clearFirst);
    void cmdLoadRecent(const std::string& dir);
    void cmdClearLibraries();
    void setSnapping(bool on);
    void setGeometryView(bool collision);
    void requestDarkMode(bool on);  // saved at once, applied at the start of the next frame
    void applyThemeNow();
    void askToDiscardChanges(std::function<void()> then);
    void onLibrariesLoaded();
    void selectLibrary(const std::string& name);
    void onPropListSelect(const PropRef& prop);

    // ---- per-frame pieces ----
    void processShortcuts();
    void processDrops();
    void drawMenuBar();
    void drawControlBar(float y, float h);
    void drawWorkspace(float x, float y, float w, float h);
    void drawPreview(float x, float y, float w, float h);
    void drawPropList(float x, float y, float w, float h);
    void drawPropertiesPanel(float x, float y, float w, float h);
    void drawPropParamsPanel(float x, float y, float w, float h);
    void drawLayersWindow();
    void drawErrorWindow();
    void drawHelpWindow();
    void drawProgress();
    void drawOverlays(::ImDrawList* dl, float ox, float oy, float w, float h);
    bool splitter(const char* id, bool vertical, float x, float y, float w, float h, float& value, float minV, float maxV,
                  float sign);

    // property panels
    void drawTexturePanel();
    void drawBonusPanel();
    void drawKillPanel();
    void drawControlPointPanel();
    enum class PanelKind { None, Textures, Bonus, Kill, ControlPoint };
    PanelKind currentPanel() const;
    const PropModel* noConflictBitmaps() const;
    void applyTexture(const std::string& textureName);
    bool isTexturePanel() const;

    // icons / preview
    unsigned iconTexture(const Prop& prop);
    void renderPreviewProp(const Prop& prop, Framebuffer& fb, double yaw);
    void loadUiIcons(const std::string& dir);

    GLFWwindow* window_ = nullptr;
    std::string assetsDir_;
    StartupOptions opts_;
    bool quit_ = false;

    Renderer renderer_;
    Framebuffer sceneFb_, previewFb_;
    MainScene scene_;
    std::unique_ptr<SceneContainer> container_;
    LibraryManager libs_;
    MapImporter importer_;
    MapExporter exporter_;

    // toolbar / view state
    bool showCollision_ = false;
    bool snapping_ = false;
    bool showProperties_ = false;
    bool showLayers_ = false;
    bool showHelp_ = false;
    bool themeDirty_ = false;
    std::string helpText_;
    std::map<std::string, bool> layerVisible_;

    // layout
    float rightWidth_ = 300, previewHeight_ = 300, propsHeight_ = 200, paramsWidth_ = 200;

    // library panel
    std::string currentLibrary_;
    int openGroup_ = 0;
    PropRef listSelected_;
    PropRef previewProp_;
    double previewYaw_ = 0;
    std::unordered_map<const Prop*, std::unique_ptr<Framebuffer>> icons_;
    std::unordered_map<std::string, TextureRef> uiIcons_;
    int iconsThisFrame_ = 0;

    // texture panel
    std::string textureSelected_;
    const PropModel* textureBitmaps_ = nullptr;

    // workspace input state
    bool leftDownInView_ = false, midDownInView_ = false;
    double lastMouseX_ = -1, lastMouseY_ = -1;
    float viewX_ = 0, viewY_ = 0, viewW_ = 1, viewH_ = 1;

    std::deque<Alert> alerts_;
    std::vector<std::string> drops_;
    std::string progressLabel_;
    bool importStarted_ = false;

    double loadStart_ = 0;   // time the current library load started (for the timing log)
    bool wasLoading_ = false;

    // startup automation
    int startupStage_ = 0;
    int framesAfterLoad_ = 0;
};

}  // namespace ae
