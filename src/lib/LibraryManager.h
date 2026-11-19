// Port of LibraryManager + PropsLibrary + MeshLoader/SpriteLoader.
//
// Loading is parallel: library.xml files are parsed up front, then a worker pool parses the
// 3DS files and decodes textures (each image file is decoded once and shared). The main thread
// only polls step() and assembles the finished libraries, so the UI never stalls.
#pragma once
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "scene/Prop.h"

namespace ae {

class LibraryManager {
  public:
    LibraryManager();
    ~LibraryManager();
    bool init(const std::string& assetsDir, std::string* err);  // adds the functional library

    std::vector<std::string> libraryNames;                      // combo box data provider
    const std::vector<PropRef>* getPropsByLibName(const std::string& lib) const;
    PropRef propByKey(const std::string& key) const;
    static std::string key(const std::string& lib, const std::string& group, const std::string& name) {
        return lib + group + name;
    }

    // Recursively finds folders with library.xml under each path and loads them.
    void loadGivenLibraries(const std::vector<std::string>& paths, bool clearFirst = false);
    void clearLibraries();

    bool loading() const;
    // Polls the workers and finishes completed batches. Returns true while work remains.
    bool step(double budgetMs);
    double progress() const;  // 0..1
    // Name of the last library added (to select it in the combo box after loading).
    std::string lastLoadedLibrary;
    bool justCompleted = false;  // set when a loading batch finishes (UI clears it)

    // TextureDiffuseMapsRegistry (original diffuse file names, used by the binary exporter).
    std::map<std::string, std::string> diffuseNames;  // key(lib,group,prop) + "/" + texture -> file

    struct Batch;  // implementation detail (public so helpers in the .cpp can use it)

  private:
    void startBatch(const std::vector<std::string>& paths);
    void finishBatch();
    void addLibrary(const std::string& name, const std::vector<std::pair<std::string, std::vector<PropRef>>>& groups);

    std::map<std::string, std::vector<PropRef>> propsByLibrary_;
    std::map<std::string, PropRef> propByKey_;
    std::unique_ptr<Batch> batch_;
    std::deque<std::vector<std::string>> pending_;  // batches requested while one is running
};

}  // namespace ae
