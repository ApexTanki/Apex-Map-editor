// OpenGL 3.3 Core scene renderer.
#pragma once
#include <vector>

#include "core/Math.h"
#include "gl/gl.h"
#include "scene/Prop.h"

namespace ae {

class MainScene;

class Framebuffer {
  public:
    ~Framebuffer();
    void resize(int w, int h);  // (re)creates attachments when the size changes
    void bind();
    static void unbind();
    GLuint colorTexture() const { return color_; }
    int width() const { return w_; }
    int height() const { return h_; }
    std::vector<uint8_t> readPixels();  // RGBA, bottom row first

  private:
    void release();
    GLuint fbo_ = 0, color_ = 0, depth_ = 0;
    int w_ = 0, h_ = 0;
};

struct RenderOptions {
    bool grid = true;
    bool collisionView = false;
    float clear[4] = {0.f, 0.f, 0.f, 1.f};
    uint32_t gridColor = 0x909090;   // 0xRRGGBB
    uint32_t boundColor = 0x0000FF;  // plane bounds
};

class Renderer {
  public:
    bool init(std::string* err);
    void shutdown();

    // Renders the main scene (props + cursor ghost + grid) into `fb`.
    void renderScene(MainScene& scene, Framebuffer& fb, const RenderOptions& opt);
    // Renders one prop with an explicit camera (preview panel and library icons).
    void renderProp(const Prop& prop, const Matrix4& cameraWorld, Framebuffer& fb, const float clear[4]);

  private:
    struct DrawItem {
        const Prop* prop;
        double depth;
    };
    void beginFrame(Framebuffer& fb, const float clear[4]);
    void setCamera(const Matrix4& cameraWorld, double w, double h);
    void drawPropPass(const Prop& p, bool transparentPass, bool collisionView);
    void drawMesh(MeshObject& mesh, const Matrix4& world, Texture* tex, uint32_t fill, bool useFill, float alpha,
                  bool twoSided);
    void drawSprite(const Prop& p, Texture* tex, float alpha);
    void drawGridQuad(const Vec3 corners[4], int segments, float halfThicknessCells, uint32_t color, float alpha);
    bool propIsTransparent(const Prop& p, bool collisionView) const;

    GLuint program_ = 0;
    GLint uMVP_ = -1, uMode_ = -1, uColor_ = -1, uAlpha_ = -1, uTex_ = -1, uSegments_ = -1, uHalf_ = -1;
    GLuint quadVao_ = 0, quadVbo_ = 0;
    Mat4f viewProj_;
    Matrix4 cameraWorld_;
    MeshRef plane_;  // unit shadow plane for bonus regions
};

}  // namespace ae
