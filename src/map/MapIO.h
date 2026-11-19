// Map import/export (ports of TanksXmlExporterV1Lite/Full and XMLImporterV1/V1Full).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include <pugixml.hpp>

#include "map/Collision.h"

namespace ae {

class MainScene;
class LibraryManager;

enum class MapFormat { XmlV1Lite, XmlV1Full, XmlV3, Binary };

class MapExporter {
  public:
    // Serializes the scene; returns false (with err) for formats that are not available.
    bool exportScene(const MainScene& scene, MapFormat format, std::string& out, std::string* err);

  private:
    CollisionPrimitivesCache cache_;
};

// Incremental importer: tiles are added in batches of 100 per step (XMLImporterBase).
class MapImporter {
  public:
    // Parses the document and prepares the import. `special` = "append special geometry".
    // Returns false if the file could not be read or parsed.
    bool begin(const std::string& path, MainScene& scene, LibraryManager& libs, bool special, std::string* err);
    bool step();  // returns true while work remains
    bool active() const { return active_; }
    double progress() const;
    // Set when the file's version was not recognised (UI shows an alert).
    std::string unsupportedVersion;

  private:
    void processTile(pugi::xml_node item);
    void complete();
    void loadFunctionalElements();
    void loadBonuses();
    void loadSpawns();
    void loadFlags();
    void loadKillZones();
    void loadDominationControlPoints();
    PropRef createSpawnPoint(pugi::xml_node node);

    pugi::xml_document doc_;
    pugi::xml_node map_;
    std::vector<pugi::xml_node> tiles_;
    size_t next_ = 0;
    bool active_ = false;
    bool full_ = false;  // version "1.0" (kill zones)
    MainScene* scene_ = nullptr;
    LibraryManager* libs_ = nullptr;
};

}  // namespace ae
