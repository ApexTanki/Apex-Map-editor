// CPU image + lazily-uploaded GL texture, and the registry of derived textures
// (port of mod.textures.ConvertedBitmapsRegistry / MaterialsRegistry).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gl/gl.h"

namespace ae {

struct Texture {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // RGBA8, row 0 = top of the image
    bool hasAlpha = false;        // any pixel with alpha < 255
    std::string source;           // file path or description (diagnostics)

    ~Texture();
    GLuint glId();  // uploads on first use
    void invalidate();
    void updateHasAlpha();

  private:
    GLuint id_ = 0;
};
using TextureRef = std::shared_ptr<Texture>;

namespace image {
// Load a PNG/JPG/... into RGBA8. Returns nullptr on failure (err receives the reason).
TextureRef load(const std::string& path, std::string* err = nullptr);
TextureRef loadFromMemory(const uint8_t* data, size_t size, std::string* err = nullptr);
TextureRef solid(uint32_t rgb, int w = 1, int h = 1);
TextureRef clone(const Texture& src);
// 20x20 magenta/black checker used for textures that failed to load.
TextureRef stub();
// TextureMapsLoader: copy the red channel of `alpha` into the alpha of `diffuse`
// (alpha is rescaled to the diffuse size when they differ).
TextureRef withAlphaFromRed(const Texture& diffuse, const Texture& alpha);
// BitmapData.draw(mask, scale(s,s), MULTIPLY): mask scaled uniformly by width ratio.
void drawMultiply(Texture& dst, const Texture& mask);
// BitmapData.draw(overlay, scale(w/ow, h/oh)) with normal alpha blending.
void drawOverStretched(Texture& dst, const Texture& overlay);
// BitmapData.draw(mask, scale(s,s), HARDLIGHT).
void drawHardLight(Texture& dst, const Texture& mask);
TextureRef resized(const Texture& src, int w, int h);
bool savePng(const std::string& path, int w, int h, const uint8_t* rgba, bool flipY);
}  // namespace image

// Derived-texture caches. Keys are the base texture objects.
class TextureRegistry {
  public:
    static TextureRegistry& instance();
    bool init(const std::string& assetsDir, std::string* err);

    TextureRef selected(const TextureRef& base);
    TextureRef noCollision(const TextureRef& base);
    TextureRef cursor(const TextureRef& base, bool accepted);

    TextureRef invisible() const { return invisible_; }
    void clear();

  private:
    TextureRef selectionMask_, conflictMask_, acceptMask_, noCollisionOverlay_, invisible_;
    std::map<const Texture*, std::pair<std::weak_ptr<Texture>, TextureRef>> selected_, noCollision_,
        cursorAccept_, cursorConflict_;
};

}  // namespace ae
