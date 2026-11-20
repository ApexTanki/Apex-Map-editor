// Apex Editor (OpenGL 3.3 Core port of AlternativaEditor) - entry point.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "gl/gl.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "core/Util.h"
#include "ui/App.h"
#include "ui/Theme.h"

#if defined(_WIN32)
#include <windows.h>
#endif

static ae::App* g_app = nullptr;

static void onDrop(GLFWwindow*, int count, const char** paths) {
    if (!g_app) return;
    std::vector<std::string> v;
    for (int i = 0; i < count; ++i) v.emplace_back(paths[i]);
    g_app->onDrop(v);
}

static std::string executableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return ae::pathToUtf8(std::filesystem::path(std::wstring(buf, n)).parent_path());
#else
    return ".";
#endif
}

static void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    for (const char* f : candidates) {
        if (std::filesystem::exists(f)) {
            static ImVector<ImWchar> ranges;
            ImFontGlyphRangesBuilder b;
            b.AddRanges(io.Fonts->GetGlyphRangesDefault());
            b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
            b.AddText("—–…");
            b.BuildRanges(&ranges);
            if (io.Fonts->AddFontFromFileTTF(f, 16.0f, nullptr, ranges.Data)) return;
        }
    }
    io.Fonts->AddFontDefault();
}

static void printUsage() {
    std::printf(
        "ApexEditor [options]\n"
        "  --lib <dir>          load prop libraries found under <dir> (repeatable)\n"
        "  --map <file.xml>     import a map after the libraries are loaded\n"
        "  --select lib/grp/prop put a library prop on the cursor\n"
        "  --collision          start in collision-geometry view\n"
        "  --export <file.xml>  export the scene as XML 1.0 and quit (unless --screenshot)\n"
        "  --screenshot <png>   save a screenshot after loading and quit\n");
}

int main(int argc, char** argv) {
    ae::StartupOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--lib") opts.libraries.push_back(next());
        else if (a == "--map") opts.map = next();
        else if (a == "--export") opts.exportPath = next();
        else if (a == "--screenshot") opts.screenshot = next();
        else if (a == "--select") opts.selectProp = next();
        else if (a == "--collision") opts.collisionView = true;
        else if (a == "--help" || a == "-h") {
            printUsage();
            return 0;
        }
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Apex Editor", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Cannot create an OpenGL 3.3 Core window\n");
        glfwTerminate();
        return 1;
    }
    glfwSetWindowSizeLimits(window, 800, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    const char* missing = nullptr;
    if (!gl::load(&missing)) {
        std::fprintf(stderr, "Missing OpenGL entry point: %s\n", missing ? missing : "?");
        return 1;
    }
    std::printf("OpenGL %s | %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    loadFonts();  // the theme is applied by App::init, once the saved settings are known
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    std::string assets = ae::joinPath(executableDir(), "assets");
    if (!ae::isDirectory(assets)) assets = "assets";
    ae::App app;
    std::string err;
    if (!app.init(window, assets, opts, &err)) {
        std::fprintf(stderr, "Initialisation failed: %s\n", err.c_str());
#if defined(_WIN32)
        MessageBoxA(nullptr, err.c_str(), "Apex Editor", MB_ICONERROR);
#endif
        return 1;
    }
    g_app = &app;
    glfwSetDropCallback(window, onDrop);

    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window) && !app.wantsQuit()) {
        glfwPollEvents();
        double now = glfwGetTime();
        double dt = now - last;
        last = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.frame(dt);
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        const ae::Theme& th = ae::currentTheme();
        glClearColor(th.windowClear[0], th.windowClear[1], th.windowClear[2], 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    g_app = nullptr;
    app.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
