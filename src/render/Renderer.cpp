#include "render/Renderer.h"

#include <algorithm>
#include <string>

#include "scene/MainScene.h"

namespace ae {

// ---------------------------------------------------------------------------------------------
// Framebuffer

Framebuffer::~Framebuffer() { release(); }

void Framebuffer::release() {
    if (!glDeleteFramebuffers) return;
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (color_) glDeleteTextures(1, &color_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
    fbo_ = color_ = depth_ = 0;
}

void Framebuffer::resize(int w, int h) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (fbo_ && w == w_ && h == h_) return;
    release();
    w_ = w;
    h_ = h;
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glGenTextures(1, &color_);
    glBindTexture(GL_TEXTURE_2D, color_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
}

void Framebuffer::unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

std::vector<uint8_t> Framebuffer::readPixels() {
    std::vector<uint8_t> px((size_t)w_ * h_ * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return px;
}

// ---------------------------------------------------------------------------------------------
// Shaders

static const char* VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* FS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform int uMode;        // 0 texture, 1 flat fill, 2 wire grid
uniform vec4 uColor;
uniform float uAlpha;
uniform float uSegments;  // grid: number of lines per axis
uniform float uHalf;      // grid: half line thickness in cell units
void main() {
    if (uMode == 0) {
        vec4 c = texture(uTex, vUV);
        c.a *= uAlpha;
        if (c.a < 0.004) discard;
        fragColor = c;
    } else if (uMode == 1) {
        fragColor = vec4(uColor.rgb, uAlpha);
    } else {
        vec2 f = vUV * uSegments;
        vec2 k = floor(f + 0.5);
        vec2 d = abs(f - k);
        // The line at the far edge (k == segments) is not part of the wire texture.
        if (k.x >= uSegments) d.x = 1e3;
        if (k.y >= uSegments) d.y = 1e3;
        vec2 aa = max(fwidth(f) * 0.75, vec2(1e-5));
        vec2 cov = 1.0 - smoothstep(vec2(uHalf) - aa, vec2(uHalf) + aa, d);
        float a = max(cov.x, cov.y) * uAlpha;
        if (a < 0.01) discard;
        fragColor = vec4(uColor.rgb, a);
    }
}
)";

static GLuint compile(GLenum type, const char* src, std::string* err) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        if (err) *err = log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Renderer::init(std::string* err) {
    GLuint vs = compile(GL_VERTEX_SHADER, VS, err);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS, err);
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        if (err) *err = log;
        return false;
    }
    uMVP_ = glGetUniformLocation(program_, "uMVP");
    uMode_ = glGetUniformLocation(program_, "uMode");
    uColor_ = glGetUniformLocation(program_, "uColor");
    uAlpha_ = glGetUniformLocation(program_, "uAlpha");
    uTex_ = glGetUniformLocation(program_, "uTex");
    uSegments_ = glGetUniformLocation(program_, "uSegments");
    uHalf_ = glGetUniformLocation(program_, "uHalf");

    glGenVertexArrays(1, &quadVao_);
    glBindVertexArray(quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 5 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    plane_ = primitives::plane(Prop::BOX_W, Prop::BOX_L);
    return true;
}

void Renderer::shutdown() {
    plane_.reset();
    if (quadVbo_) glDeleteBuffers(1, &quadVbo_);
    if (quadVao_) glDeleteVertexArrays(1, &quadVao_);
    if (program_) glDeleteProgram(program_);
    quadVbo_ = quadVao_ = program_ = 0;
}

// ---------------------------------------------------------------------------------------------

void Renderer::beginFrame(Framebuffer& fb, const float clear[4]) {
    fb.bind();
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glUseProgram(program_);
    glUniform1i(uTex_, 0);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::setCamera(const Matrix4& cameraWorld, double w, double h) {
    cameraWorld_ = cameraWorld;
    CameraRig tmp;  // only used for the projection formula
    Viewport vp{w, h};
    double f = CameraRig::focalLength(vp);
    // Near plane 10 (instead of 1) for better depth precision; it never clips editor content.
    double n = 10, fa = CameraRig::FAR_CLIP;
    Mat4f p;
    for (float& v : p.m) v = 0;
    p.m[0] = (float)(f / (w * 0.5));
    p.m[5] = (float)(f / (h * 0.5));
    p.m[10] = (float)(-(fa + n) / (fa - n));
    p.m[11] = -1;
    p.m[14] = (float)(-2 * fa * n / (fa - n));
    Matrix4 flip;
    flip.f = -1;
    flip.k = -1;
    Mat4f view = Mat4f::fromAffine(Matrix4::mul(flip, cameraWorld.inverted()));
    viewProj_ = Mat4f::mul(p, view);
    (void)tmp;
}

void Renderer::drawMesh(MeshObject& mesh, const Matrix4& world, Texture* tex, uint32_t fill, bool useFill,
                        float alpha, bool twoSided) {
    Mat4f mvp = Mat4f::mul(viewProj_, Mat4f::fromAffine(world));
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, mvp.m);
    glUniform1f(uAlpha_, alpha);
    if (useFill || !tex) {
        uint32_t c = useFill ? fill : 0x808080;
        glUniform1i(uMode_, 1);
        glUniform4f(uColor_, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, 1.f);
    } else {
        glUniform1i(uMode_, 0);
        glBindTexture(GL_TEXTURE_2D, tex->glId());
    }
    if (twoSided)
        glDisable(GL_CULL_FACE);
    else
        glEnable(GL_CULL_FACE);
    mesh.draw();
}

void Renderer::drawSprite(const Prop& p, Texture* tex, float alpha) {
    if (!tex) return;
    Vec3 q[4];
    // picking::spriteQuad uses a CameraRig; build the quad from the stored camera matrix here.
    Vec3 right = cameraWorld_.axisX().normalized(), down = cameraWorld_.axisY().normalized();
    const PropModel& m = *p.model;
    double w = m.spriteWidth * m.spriteScale * p.scaleX, h = m.spriteHeight * m.spriteScale * p.scaleY;
    double l = -m.originX * w, r = (1 - m.originX) * w, t = -m.originY * h, b = (1 - m.originY) * h;
    Vec3 c = p.position();
    q[0] = c + right * l + down * b;
    q[1] = c + right * r + down * b;
    q[2] = c + right * r + down * t;
    q[3] = c + right * l + down * t;
    const float uv[8] = {0, 1, 1, 1, 1, 0, 0, 0};
    float data[20];
    for (int i = 0; i < 4; ++i) {
        data[i * 5 + 0] = (float)q[i].x;
        data[i * 5 + 1] = (float)q[i].y;
        data[i * 5 + 2] = (float)q[i].z;
        data[i * 5 + 3] = uv[i * 2];
        data[i * 5 + 4] = uv[i * 2 + 1];
    }
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, viewProj_.m);
    glUniform1f(uAlpha_, alpha);
    glUniform1i(uMode_, 0);
    glBindTexture(GL_TEXTURE_2D, tex->glId());
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void Renderer::drawGridQuad(const Vec3 c[4], int segments, float halfCells, uint32_t color, float alpha) {
    const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    float data[20];
    for (int i = 0; i < 4; ++i) {
        data[i * 5 + 0] = (float)c[i].x;
        data[i * 5 + 1] = (float)c[i].y;
        data[i * 5 + 2] = (float)c[i].z;
        data[i * 5 + 3] = uv[i * 2];
        data[i * 5 + 4] = uv[i * 2 + 1];
    }
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, viewProj_.m);
    glUniform1i(uMode_, 2);
    glUniform1f(uAlpha_, alpha);
    glUniform1f(uSegments_, (float)segments);
    glUniform1f(uHalf_, halfCells);
    glUniform4f(uColor_, ((color >> 16) & 0xFF) / 255.f, ((color >> 8) & 0xFF) / 255.f, (color & 0xFF) / 255.f, 1.f);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

bool Renderer::propIsTransparent(const Prop& p, bool collisionView) const {
    if (p.alpha < 1.f || (p.model && p.model->objectAlpha < 1.f)) return true;
    if (p.kind == PropKind::Bonus) return true;
    if (collisionView && p.kind == PropKind::Mesh) return false;
    uint32_t dummy;
    if (p.fillColor(dummy)) return false;
    TextureRef t = p.currentTexture();
    return t && t->hasAlpha;
}

void Renderer::drawPropPass(const Prop& p, bool transparentPass, bool collisionView) {
    if (!p.visible || !p.model) return;
    const float alpha = p.alpha * p.model->objectAlpha;
    uint32_t fill = 0;
    bool useFill = p.fillColor(fill);
    TextureRef tex = useFill ? nullptr : p.currentTexture();

    if (collisionView && p.isMeshProp() && p.materialOverride == MaterialOverride::None) {
        // MeshProp.showCollisionBoxes: visual mesh hidden, collision meshes in flat #FF7F7F.
        if (transparentPass) return;
        Matrix4 pm = p.transformation();
        for (auto& c : p.model->collision) drawMesh(*c, Matrix4::mul(pm, c->localMatrix()), nullptr, 0xFF7F7F, true, p.alpha, false);
        return;
    }
    if (p.isSprite()) {
        drawSprite(p, tex.get(), p.alpha);
        return;
    }
    if (p.model->mesh) drawMesh(*p.model->mesh, p.objectMatrix(), tex.get(), fill, useFill, alpha, p.isBox());
    if (p.kind == PropKind::Bonus) {
        // Shadow plane (child at local (250, 250, shadowZ)).
        Matrix4 local = Matrix4::compose(Prop::BOX_W / 2, Prop::BOX_L / 2, p.shadowLocalZ, 0, 0, 0);
        drawMesh(*plane_, Matrix4::mul(p.transformation(), local), tex.get(), fill, useFill, 0.5f * p.alpha, true);
    }
}

void Renderer::renderScene(MainScene& scene, Framebuffer& fb, const RenderOptions& opt) {
    scene.updateShadows();
    beginFrame(fb, opt.clear);
    setCamera(scene.camera.cameraMatrix(), fb.width(), fb.height());

    std::vector<const Prop*> all;
    all.reserve(scene.children.size() + 1);
    for (auto& p : scene.children) all.push_back(p.get());
    if (auto g = scene.cursor.object()) all.push_back(g.get());

    Matrix4 inv = scene.camera.cameraMatrix().inverted();
    std::vector<DrawItem> transparent;
    // Opaque pass
    glDepthMask(GL_TRUE);
    for (const Prop* p : all) {
        if (!p->visible) continue;
        if (propIsTransparent(*p, opt.collisionView)) {
            transparent.push_back({p, inv.transform(p->position()).z});
            continue;
        }
        drawPropPass(*p, false, opt.collisionView);
    }
    // Plane bounds (key T): blue wire over the footprint, slightly above the prop.
    for (const Prop* p : all) {
        if (!p->visible || !p->showBound) continue;
        double z = p->z + 0.1;
        Vec3 c[4] = {{p->distancesX.y + p->x, p->distancesY.x + p->y, z},
                     {p->distancesX.y + p->x, p->distancesY.y + p->y, z},
                     {p->distancesX.x + p->x, p->distancesY.y + p->y, z},
                     {p->distancesX.x + p->x, p->distancesY.x + p->y, z}};
        drawGridQuad(c, 5, 2.0f / 128.0f * 5.0f, opt.boundColor, 1.0f);
    }
    // Grid (WireMaterial(10, 7500, 7500, 15, 0x909090)), centred at (250, 250).
    if (opt.grid) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.f, -4.f);
        glDepthMask(GL_FALSE);
        const double half = 7500 / 2.0;
        Vec3 c[4] = {{250 - half, 250 - half, 0}, {250 + half, 250 - half, 0}, {250 + half, 250 + half, 0},
                     {250 - half, 250 + half, 0}};
        drawGridQuad(c, 15, 5.0f / 500.0f, opt.gridColor, 1.0f);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    // Transparent pass, back to front.
    std::sort(transparent.begin(), transparent.end(), [](const DrawItem& a, const DrawItem& b) { return a.depth > b.depth; });
    for (auto& it : transparent) {
        bool cutout = !(it.prop->alpha < 1.f || it.prop->model->objectAlpha < 1.f || it.prop->kind == PropKind::Bonus);
        glDepthMask(cutout ? GL_TRUE : GL_FALSE);
        drawPropPass(*it.prop, false, opt.collisionView);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    Framebuffer::unbind();
}

void Renderer::renderProp(const Prop& prop, const Matrix4& cameraWorld, Framebuffer& fb, const float clear[4]) {
    beginFrame(fb, clear);
    setCamera(cameraWorld, fb.width(), fb.height());
    bool transparent = propIsTransparent(prop, false);
    glDepthMask(transparent ? GL_FALSE : GL_TRUE);
    drawPropPass(prop, false, false);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    Framebuffer::unbind();
}

}  // namespace ae
