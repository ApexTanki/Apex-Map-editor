#include "render/Texture.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb/stb_image_write.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

namespace ae {

Texture::~Texture() { invalidate(); }

void Texture::invalidate() {
    if (id_ != 0 && glDeleteTextures) glDeleteTextures(1, &id_);
    id_ = 0;
}

void Texture::updateHasAlpha() {
    hasAlpha = false;
    for (size_t p = 3; p < pixels.size(); p += 4)
        if (pixels[p] < 255) {
            hasAlpha = true;
            break;
        }
}

GLuint Texture::glId() {
    if (id_ != 0 || width <= 0 || height <= 0) return id_;
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Rows are stored top-first; mesh UVs follow Flash convention (v = 0 at the top),
    // so the image is uploaded as-is and the shader samples (u, v) directly.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Alternativa TextureMaterial defaults to repeat = false.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id_;
}

namespace image {

static TextureRef fromStb(unsigned char* data, int w, int h, std::string* err) {
    if (!data) {
        if (err) *err = stbi_failure_reason() ? stbi_failure_reason() : "decode failed";
        return nullptr;
    }
    auto t = std::make_shared<Texture>();
    t->width = w;
    t->height = h;
    t->pixels.assign(data, data + (size_t)w * h * 4);
    stbi_image_free(data);
    t->updateHasAlpha();
    return t;
}

TextureRef load(const std::string& path, std::string* err) {
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    auto t = fromStb(data, w, h, err);
    if (t) t->source = path;
    return t;
}

TextureRef loadFromMemory(const uint8_t* bytes, size_t size, std::string* err) {
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load_from_memory(bytes, (int)size, &w, &h, &n, 4);
    return fromStb(data, w, h, err);
}

TextureRef solid(uint32_t rgb, int w, int h) {
    auto t = std::make_shared<Texture>();
    t->width = w;
    t->height = h;
    t->pixels.resize((size_t)w * h * 4);
    for (size_t p = 0; p < t->pixels.size(); p += 4) {
        t->pixels[p + 0] = (rgb >> 16) & 0xFF;
        t->pixels[p + 1] = (rgb >> 8) & 0xFF;
        t->pixels[p + 2] = rgb & 0xFF;
        t->pixels[p + 3] = 255;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "solid #%06X", rgb & 0xFFFFFF);
    t->source = buf;
    return t;
}

TextureRef clone(const Texture& src) {
    auto t = std::make_shared<Texture>();
    t->width = src.width;
    t->height = src.height;
    t->pixels = src.pixels;
    t->hasAlpha = src.hasAlpha;
    t->source = src.source;
    return t;
}

TextureRef stub() {
    // Thread-safe one-time initialisation (called from library loader workers).
    static const TextureRef s = [] {
        const int n = 20;
        TextureRef s = solid(0x000000, n, n);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; x += 2) {
                int px = (y % 2) ? x : x + 1;
                if (px >= n) continue;
                uint8_t* p = &s->pixels[((size_t)y * n + px) * 4];
                p[0] = 0xFF; p[1] = 0x00; p[2] = 0xFF;
            }
        s->source = "stub";
        return s;
    }();
    return s;
}

TextureRef resized(const Texture& src, int w, int h) {
    auto t = std::make_shared<Texture>();
    t->width = w;
    t->height = h;
    t->pixels.resize((size_t)w * h * 4);
    stbir_resize_uint8_linear(src.pixels.data(), src.width, src.height, 0, t->pixels.data(), w, h, 0,
                              STBIR_RGBA);
    t->updateHasAlpha();
    t->source = src.source;
    return t;
}

TextureRef withAlphaFromRed(const Texture& diffuse, const Texture& alphaIn) {
    TextureRef scaledAlpha;
    const Texture* alpha = &alphaIn;
    if (alphaIn.width != diffuse.width || alphaIn.height != diffuse.height) {
        scaledAlpha = resized(alphaIn, diffuse.width, diffuse.height);
        alpha = scaledAlpha.get();
    }
    auto t = clone(diffuse);
    for (size_t p = 0; p < t->pixels.size(); p += 4) t->pixels[p + 3] = alpha->pixels[p + 0];
    t->updateHasAlpha();
    return t;
}

// Nearest-neighbour sample of `src` at destination pixel (x, y) given scale factors.
static inline const uint8_t* sampleScaled(const Texture& src, int x, int y, double sx, double sy) {
    int u = (int)(x / sx), v = (int)(y / sy);
    if (u < 0 || v < 0 || u >= src.width || v >= src.height) return nullptr;
    return &src.pixels[((size_t)v * src.width + u) * 4];
}

void drawMultiply(Texture& dst, const Texture& mask) {
    double s = (double)dst.width / mask.width;
    for (int y = 0; y < dst.height; ++y)
        for (int x = 0; x < dst.width; ++x) {
            const uint8_t* m = sampleScaled(mask, x, y, s, s);
            if (!m) continue;
            uint8_t* p = &dst.pixels[((size_t)y * dst.width + x) * 4];
            for (int c = 0; c < 3; ++c) p[c] = (uint8_t)((p[c] * m[c] + 127) / 255);
        }
}

void drawOverStretched(Texture& dst, const Texture& ov) {
    double sx = (double)dst.width / ov.width, sy = (double)dst.height / ov.height;
    for (int y = 0; y < dst.height; ++y)
        for (int x = 0; x < dst.width; ++x) {
            const uint8_t* o = sampleScaled(ov, x, y, sx, sy);
            if (!o) continue;
            uint8_t* p = &dst.pixels[((size_t)y * dst.width + x) * 4];
            int a = o[3];
            for (int c = 0; c < 3; ++c) p[c] = (uint8_t)((o[c] * a + p[c] * (255 - a) + 127) / 255);
            p[3] = (uint8_t)std::min(255, a + p[3] * (255 - a) / 255);
        }
    dst.updateHasAlpha();
}

void drawHardLight(Texture& dst, const Texture& mask) {
    double s = (double)dst.width / mask.width;
    for (int y = 0; y < dst.height; ++y)
        for (int x = 0; x < dst.width; ++x) {
            const uint8_t* m = sampleScaled(mask, x, y, s, s);
            if (!m) continue;
            uint8_t* p = &dst.pixels[((size_t)y * dst.width + x) * 4];
            for (int c = 0; c < 3; ++c) {
                int b = p[c], t = m[c];
                int r = t < 128 ? (2 * t * b) / 255 : 255 - (2 * (255 - t) * (255 - b)) / 255;
                p[c] = (uint8_t)std::clamp(r, 0, 255);
            }
        }
}

bool savePng(const std::string& path, int w, int h, const uint8_t* rgba, bool flipY) {
    stbi_flip_vertically_on_write(flipY ? 1 : 0);
    int ok = stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4);
    stbi_flip_vertically_on_write(0);
    return ok != 0;
}

}  // namespace image

// ---------------------------------------------------------------------------------------------

TextureRegistry& TextureRegistry::instance() {
    static TextureRegistry r;
    return r;
}

bool TextureRegistry::init(const std::string& dir, std::string* err) {
    selectionMask_ = image::load(dir + "/textures/mask_red.jpg", err);
    conflictMask_ = selectionMask_;
    acceptMask_ = image::load(dir + "/textures/mask_green.jpg", err);
    noCollisionOverlay_ = image::load(dir + "/textures/no_collision.png", err);
    invisible_ = image::load(dir + "/textures/invisible.png", err);
    return selectionMask_ && acceptMask_ && noCollisionOverlay_ && invisible_;
}

template <typename F>
static TextureRef cached(std::map<const Texture*, std::pair<std::weak_ptr<Texture>, TextureRef>>& reg,
                         const TextureRef& base, F make) {
    if (!base) return nullptr;
    auto it = reg.find(base.get());
    if (it != reg.end()) {
        if (!it->second.first.expired()) return it->second.second;
        reg.erase(it);  // base was freed and its address reused
    }
    TextureRef t = make();
    reg[base.get()] = {base, t};
    return t;
}

TextureRef TextureRegistry::selected(const TextureRef& base) {
    return cached(selected_, base, [&] {
        auto t = image::clone(*base);
        image::drawMultiply(*t, *selectionMask_);
        return t;
    });
}

TextureRef TextureRegistry::noCollision(const TextureRef& base) {
    return cached(noCollision_, base, [&] {
        auto t = image::clone(*base);
        image::drawOverStretched(*t, *noCollisionOverlay_);
        return t;
    });
}

TextureRef TextureRegistry::cursor(const TextureRef& base, bool accepted) {
    return cached(accepted ? cursorAccept_ : cursorConflict_, base, [&] {
        auto t = image::clone(*base);
        image::drawHardLight(*t, accepted ? *acceptMask_ : *conflictMask_);
        return t;
    });
}

void TextureRegistry::clear() {
    selected_.clear();
    noCollision_.clear();
    cursorAccept_.clear();
    cursorConflict_.clear();
}

}  // namespace ae
