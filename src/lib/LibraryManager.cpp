#include "lib/LibraryManager.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <unordered_map>

#include <pugixml.hpp>

#include "core/ErrorLog.h"
#include "core/Util.h"
#include "io/Parser3DS.h"
#include "lib/FunctionalProps.h"
#include "ui/Locale.h"

namespace ae {

namespace {

struct ImageRedirect {
    std::string diffuse;
    std::optional<std::string> alpha;
};

struct TextureInfo {
    std::string name;
    std::string diffusePath;
    std::optional<std::string> opacityPath;
};

struct PendingProp {
    std::string group, name;
    bool isSprite = false;
    // mesh
    std::string meshFile;
    std::optional<std::string> objectName;
    std::vector<TextureInfo> textures;
    // sprite
    std::string spriteFile;
    std::optional<std::string> spriteAlpha;
    double originX = 0.5, originY = 1, scale = 1;
    // results (written by a worker)
    PropRef result;
    std::string defaultDiffuseName;  // DEFAULT texture taken from the 3DS material
};

struct PendingLibrary {
    std::string url;  // directory with trailing slash
    std::string name;
    std::map<std::string, ImageRedirect> redirects;
    std::vector<std::string> groupOrder;
    std::vector<PendingProp> props;
    bool failed = false;
};

std::optional<std::string> attr(const pugi::xml_node& n, const char* name) {
    pugi::xml_attribute a = n.attribute(name);
    if (!a) return std::nullopt;
    return std::string(a.value());
}

double attrNumber(const pugi::xml_node& n, const char* name, double def) {
    pugi::xml_attribute a = n.attribute(name);
    if (!a) return def;
    double v = parseNumber(a.value());
    return std::isnan(v) ? def : v;
}

std::string normalizeRel(std::string s) {
    s = replaceAll(trim(s), "\\", "/");
    while (startsWith(s, "./")) s.erase(0, 2);
    return s;
}

// Resolves a texture/model path. If the exact file is missing, tries the same name with other
// common image extensions (libraries often reference .jpg while shipping .png and vice versa).
std::optional<std::string> resolveFile(const std::string& path, bool image) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(u8path(path), ec)) return path;
    if (!image) return std::nullopt;
    std::filesystem::path p = u8path(path);
    for (const char* ext : {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif", ".psd"}) {
        std::filesystem::path alt = p;
        alt.replace_extension(ext);
        if (std::filesystem::is_regular_file(alt, ec)) return pathToUtf8(alt);
    }
    return std::nullopt;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Batch: shared state between the main thread and the workers.

struct LibraryManager::Batch {
    std::vector<std::unique_ptr<PendingLibrary>> libs;            // in processing order
    std::vector<std::pair<PendingLibrary*, PendingProp*>> jobs;   // one per prop
    std::atomic<size_t> next{0}, done{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    std::mutex logMutex;
    std::vector<std::string> errors;    // real failures: shown in the error window
    std::vector<std::string> warnings;  // recovered problems: logged only
    std::set<std::string> reported;

    // Texture cache: each (diffuse, opacity) pair is decoded once per batch.
    std::mutex texMutex;
    std::unordered_map<std::string, std::shared_future<TextureRef>> textures;

    void error(const std::string& s) {
        std::lock_guard<std::mutex> l(logMutex);
        if (reported.insert(s).second) errors.push_back(s);
    }
    void warning(const std::string& s) {
        std::lock_guard<std::mutex> l(logMutex);
        if (reported.insert(s).second) warnings.push_back(s);
    }

    TextureRef texture(const std::string& diffuse, const std::optional<std::string>& alpha) {
        std::string key = diffuse + '\x1f' + (alpha ? *alpha : std::string());
        std::promise<TextureRef> promise;
        std::shared_future<TextureRef> future;
        bool owner = false;
        {
            std::lock_guard<std::mutex> l(texMutex);
            auto it = textures.find(key);
            if (it != textures.end()) {
                future = it->second;
            } else {
                future = promise.get_future().share();
                textures.emplace(key, future);
                owner = true;
            }
        }
        if (!owner) return future.get();
        TextureRef result = decode(diffuse, alpha);
        promise.set_value(result);
        return result;
    }

    TextureRef decode(const std::string& diffusePath, const std::optional<std::string>& alphaPath) {
        auto diffuse = resolveFile(diffusePath, true);
        if (!diffuse) {
            error("Texture not found: " + diffusePath);
            return nullptr;
        }
        std::string err;
        TextureRef d = image::load(*diffuse, &err);
        if (!d) {
            error("Cannot decode " + *diffuse + " (" + err + ")");
            return nullptr;
        }
        if (!alphaPath) return d;
        auto alpha = resolveFile(*alphaPath, true);
        if (!alpha) {
            warning("Opacity map not found (texture used without alpha): " + *alphaPath);
            return d;
        }
        TextureRef a = image::load(*alpha, &err);
        if (!a) {
            warning("Cannot decode opacity map " + *alpha + " (" + err + ")");
            return d;
        }
        TextureRef merged = image::withAlphaFromRed(*d, *a);
        merged->source = *diffuse;
        return merged;
    }

    void loadProp(PendingLibrary& lib, PendingProp& pp);

    void worker() {
        for (;;) {
            if (stop.load()) return;
            size_t i = next.fetch_add(1);
            if (i >= jobs.size()) return;
            try {
                loadProp(*jobs[i].first, *jobs[i].second);
            } catch (const std::exception& e) {
                error(jobs[i].second->name + ": " + e.what());
            } catch (...) {
                error(jobs[i].second->name + ": unexpected error");
            }
            done.fetch_add(1);
        }
    }

    ~Batch() {
        stop = true;
        for (auto& t : threads)
            if (t.joinable()) t.join();
    }
};

void LibraryManager::Batch::loadProp(PendingLibrary& lib, PendingProp& pp) {
    const std::string where = lib.name + " / " + pp.group + " / " + pp.name;

    if (pp.isSprite) {
        TextureRef tex = texture(lib.url + pp.spriteFile,
                                 pp.spriteAlpha ? std::optional<std::string>(lib.url + *pp.spriteAlpha) : std::nullopt);
        if (!tex) tex = image::stub();
        auto model = std::make_shared<PropModel>();
        model->originX = pp.originX;
        model->originY = pp.originY;
        model->spriteScale = pp.scale;
        pp.result = Prop::createSprite(model, tex, pp.name, lib.name, pp.group);
        return;
    }

    std::string meshPath = lib.url + pp.meshFile;
    auto resolved = resolveFile(meshPath, false);
    Parser3DS parser;
    if (!resolved || !parser.parseFile(*resolved) || parser.objects.empty()) {
        error((resolved ? "Cannot parse model: " : "Model not found: ") + meshPath + "  (" + where + ")");
        return;
    }
    MeshRef main;
    if (pp.objectName) main = parser.getObjectByName(*pp.objectName);
    if (!main && pp.objectName) {
        // Case-insensitive match, then fall back to the first object with geometry.
        for (auto& o : parser.objects)
            if (toLower(o->name) == toLower(*pp.objectName)) main = o;
        if (!main) warning("Object '" + *pp.objectName + "' not found in " + meshPath + ", using the first mesh");
    }
    if (!main)
        for (auto& o : parser.objects)
            if (o->vertexCount() > 0) {
                main = o;
                break;
            }
    if (!main) {
        error("Model has no geometry: " + meshPath + "  (" + where + ")");
        return;
    }

    auto model = std::make_shared<PropModel>();
    model->mesh = main;
    for (auto& o : parser.objects) {
        if (o == main) continue;
        model->otherObjects.push_back(o);
        if (o->vertexCount() == 0) continue;  // helper/dummy node: not a collision mesh
        if (o->name.substr(0, 3) != "occ") model->collision.push_back(o);
    }

    std::vector<TextureInfo> textures = pp.textures;
    if (textures.empty() && !parser.textureMaterials.empty()) {
        const Material3DS& m = parser.textureMaterials[0];
        std::string diffuse = toLower(normalizeRel(m.diffuseMap));
        std::optional<std::string> opacity;
        if (!m.opacityMap.empty()) opacity = normalizeRel(m.opacityMap);
        pp.defaultDiffuseName = diffuse;
        auto red = lib.redirects.find(diffuse);
        if (red != lib.redirects.end()) {
            diffuse = red->second.diffuse;
            opacity = red->second.alpha;
        }
        TextureInfo ti;
        ti.name = "DEFAULT";
        ti.diffusePath = lib.url + diffuse;
        if (opacity) ti.opacityPath = lib.url + toLower(*opacity);
        textures.push_back(ti);
    }

    if (!textures.empty()) {
        model->hasBitmaps = true;
        for (auto& ti : textures) {
            TextureRef t = this->texture(ti.diffusePath, ti.opacityPath);
            model->bitmaps.emplace_back(ti.name, t ? t : image::stub());
        }
    }

    // createRegularProp: main object moved to the origin (rotation/scale are kept).
    main->x = main->y = main->z = 0;
    PropRef prop = Prop::createMesh(model, pp.name, lib.name, pp.group);
    if (TextureRef def = model->findBitmap("DEFAULT")) prop->bitmapData = def;
    if (!model->bitmaps.empty()) prop->setTextureName(model->bitmaps.front().first);
    if (!prop->bitmapData) prop->bitmapData = image::stub();
    pp.result = prop;
}

// ---------------------------------------------------------------------------------------------

LibraryManager::LibraryManager() = default;
LibraryManager::~LibraryManager() = default;

bool LibraryManager::init(const std::string& assetsDir, std::string* err) {
    std::vector<functional::GroupProps> groups;
    if (!functional::build(assetsDir, groups, err)) return false;
    std::vector<std::pair<std::string, std::vector<PropRef>>> g;
    for (auto& gp : groups) g.emplace_back(gp.group, gp.props);
    addLibrary(functional::LIBRARY_NAME, g);
    return true;
}

const std::vector<PropRef>* LibraryManager::getPropsByLibName(const std::string& lib) const {
    auto it = propsByLibrary_.find(lib);
    return it == propsByLibrary_.end() ? nullptr : &it->second;
}

PropRef LibraryManager::propByKey(const std::string& k) const {
    auto it = propByKey_.find(k);
    return it == propByKey_.end() ? nullptr : it->second;
}

static void searchForLibraries(const std::filesystem::path& dir, std::vector<std::string>& out) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    if (std::filesystem::is_regular_file(dir / "library.xml", ec)) out.push_back(pathToUtf8(dir));
    std::vector<std::filesystem::path> subdirs;
    for (auto it = std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        if (it->is_directory(ec)) subdirs.push_back(it->path());
    std::sort(subdirs.begin(), subdirs.end());
    for (auto& s : subdirs) searchForLibraries(s, out);
}

void LibraryManager::loadGivenLibraries(const std::vector<std::string>& paths, bool clearFirst) {
    if (clearFirst) clearLibraries();
    if (batch_) {
        pending_.push_back(paths);
        return;
    }
    startBatch(paths);
}

void LibraryManager::clearLibraries() {
    auto func = propsByLibrary_[functional::LIBRARY_NAME];
    libraryNames.assign({functional::LIBRARY_NAME});
    propsByLibrary_.clear();
    propsByLibrary_[functional::LIBRARY_NAME] = func;
    std::map<std::string, PropRef> keep;
    for (auto& [k, v] : propByKey_)
        if (v->libraryName == functional::LIBRARY_NAME) keep[k] = v;
    propByKey_ = keep;
}

bool LibraryManager::loading() const { return batch_ != nullptr || !pending_.empty(); }

double LibraryManager::progress() const {
    if (!batch_ || batch_->jobs.empty()) return 1.0;
    return (double)batch_->done.load() / batch_->jobs.size();
}

// Parses library.xml / images.xml of one directory (fast, main thread).
static std::unique_ptr<PendingLibrary> parseLibraryXml(const std::string& dir, LibraryManager::Batch& batch,
                                                       std::map<std::string, std::string>& diffuseNames) {
    auto lib = std::make_unique<PendingLibrary>();
    lib->url = dir;
    if (!lib->url.empty() && lib->url.back() != '/' && lib->url.back() != '\\') lib->url += "/";

    pugi::xml_document images;
    if (images.load_file(u8path(lib->url + "images.xml").c_str())) {
        for (auto img : images.document_element().children("image")) {
            ImageRedirect r;
            r.diffuse = normalizeRel(img.attribute("new-name").value());
            r.alpha = attr(img, "alpha");
            if (r.alpha) r.alpha = normalizeRel(*r.alpha);
            lib->redirects[toLower(normalizeRel(img.attribute("name").value()))] = r;
        }
    }

    pugi::xml_document doc;
    pugi::xml_parse_result res = doc.load_file(u8path(lib->url + "library.xml").c_str());
    if (!res) {
        batch.error(lib->url + "library.xml: " + res.description());
        lib->failed = true;
        return lib;
    }
    pugi::xml_node root = doc.document_element();
    lib->name = root.attribute("name").value();
    if (lib->name.empty()) lib->name = pathToUtf8(u8path(dir).filename());
    // Only first-level prop groups end up in the library (LibraryManager.addLibrary).
    for (auto group : root.children("prop-group")) {
        std::string groupName = group.attribute("name").value();
        lib->groupOrder.push_back(groupName);
        for (auto prop : group.children("prop")) {
            PendingProp pp;
            pp.group = groupName;
            pp.name = prop.attribute("name").value();
            if (auto mesh = prop.child("mesh")) {
                pp.meshFile = toLower(normalizeRel(mesh.attribute("file").value()));
                pp.objectName = attr(mesh, "object");
                for (auto tex : mesh.children("texture")) {
                    TextureInfo ti;
                    ti.name = tex.attribute("name").value();
                    std::string diffuse = toLower(normalizeRel(tex.attribute("diffuse-map").value()));
                    diffuseNames[LibraryManager::key(lib->name, groupName, pp.name) + "/" + ti.name] = diffuse;
                    std::optional<std::string> alpha = attr(tex, "opacity-map");
                    if (alpha) alpha = normalizeRel(*alpha);
                    auto red = lib->redirects.find(diffuse);
                    if (red != lib->redirects.end()) {
                        diffuse = red->second.diffuse;
                        alpha = red->second.alpha;
                    }
                    ti.diffusePath = lib->url + diffuse;
                    if (alpha && !alpha->empty()) ti.opacityPath = lib->url + toLower(*alpha);
                    pp.textures.push_back(ti);
                }
            } else if (auto sprite = prop.child("sprite")) {
                pp.isSprite = true;
                std::string diffuse = toLower(normalizeRel(sprite.attribute("file").value()));
                diffuseNames[LibraryManager::key(lib->name, groupName, pp.name) + "/DEFAULT"] = diffuse;
                std::optional<std::string> alpha;
                auto red = lib->redirects.find(diffuse);
                if (red != lib->redirects.end()) {
                    diffuse = red->second.diffuse;
                    alpha = red->second.alpha;
                } else {
                    alpha = attr(sprite, "alpha");
                    if (alpha) alpha = toLower(normalizeRel(*alpha));
                }
                pp.spriteFile = diffuse;
                if (alpha && !alpha->empty()) pp.spriteAlpha = alpha;
                pp.originX = attrNumber(sprite, "origin-x", 0.5);
                pp.originY = attrNumber(sprite, "origin-y", 1);
                pp.scale = attrNumber(sprite, "scale", 1);
            } else {
                batch.warning("Prop without <mesh> or <sprite> skipped: " + lib->name + " / " + groupName + " / " + pp.name);
                continue;
            }
            lib->props.push_back(std::move(pp));
        }
    }
    return lib;
}

void LibraryManager::startBatch(const std::vector<std::string>& paths) {
    std::vector<std::string> found;
    for (auto& p : paths) searchForLibraries(u8path(p), found);
    if (found.empty()) return;  // nothing to load (the original does nothing either)

    ErrorLog::instance().clearMessages();
    batch_ = std::make_unique<Batch>();
    // The original pops URLs from the end of the list, so the last found library loads first.
    for (auto it = found.rbegin(); it != found.rend(); ++it)
        batch_->libs.push_back(parseLibraryXml(*it, *batch_, diffuseNames));
    for (auto& lib : batch_->libs)
        for (auto& pp : lib->props) batch_->jobs.emplace_back(lib.get(), &pp);

    unsigned n = std::max(1u, std::thread::hardware_concurrency());
    n = (unsigned)std::min<size_t>(std::max(1u, n > 1 ? n - 1 : 1u), std::max<size_t>(1, batch_->jobs.size()));
    Batch* b = batch_.get();
    for (unsigned i = 0; i < n; ++i) batch_->threads.emplace_back([b] { b->worker(); });
}

void LibraryManager::finishBatch() {
    for (auto& t : batch_->threads)
        if (t.joinable()) t.join();
    for (auto& libPtr : batch_->libs) {
        PendingLibrary& lib = *libPtr;
        if (lib.failed) continue;
        std::vector<std::pair<std::string, std::vector<PropRef>>> groups;
        for (auto& g : lib.groupOrder)
            if (std::find_if(groups.begin(), groups.end(), [&](auto& x) { return x.first == g; }) == groups.end())
                groups.emplace_back(g, std::vector<PropRef>{});
        for (auto& pp : lib.props) {
            if (!pp.defaultDiffuseName.empty())
                diffuseNames[key(lib.name, pp.group, pp.name) + "/DEFAULT"] = pp.defaultDiffuseName;
            if (!pp.result) continue;
            for (auto& g : groups)
                if (g.first == pp.group) g.second.push_back(pp.result);
        }
        addLibrary(lib.name, groups);
        lastLoadedLibrary = lib.name;
    }
    ErrorLog& log = ErrorLog::instance();
    for (auto& w : batch_->warnings) log.addText("[warning] " + w);
    if (!batch_->errors.empty()) {
        for (auto& e : batch_->errors) log.addText(e);
        log.setMessage(Locale::get("ERROR_LIBRARY_LOADING"));
        log.showWindow();
    }
    batch_.reset();
    justCompleted = true;
    std::string last = lastLoadedLibrary;
    std::sort(libraryNames.begin(), libraryNames.end());
    lastLoadedLibrary = last;
}

void LibraryManager::addLibrary(const std::string& name,
                                const std::vector<std::pair<std::string, std::vector<PropRef>>>& groups) {
    std::vector<PropRef> all;
    for (auto& [group, props] : groups)
        for (auto& p : props) {
            all.push_back(p);
            propByKey_[key(name, group, p->name)] = p;
        }
    propsByLibrary_[name] = all;
    if (std::find(libraryNames.begin(), libraryNames.end(), name) == libraryNames.end()) libraryNames.push_back(name);
}

bool LibraryManager::step(double) {
    if (!batch_ && !pending_.empty()) {
        auto paths = pending_.front();
        pending_.pop_front();
        startBatch(paths);
    }
    if (batch_ && batch_->done.load() >= batch_->jobs.size()) finishBatch();
    return loading();
}

}  // namespace ae
