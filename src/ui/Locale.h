// Port of mod.locale.Locale (texts from localization.json) and the SharedObject settings.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace ae {

class Locale {
  public:
    static bool init(const std::string& jsonPath, const std::string& preferred, std::string* err);
    // Returns the text for `id` in the current language, or `id` itself when missing.
    static const std::string& get(const std::string& id);
    static const std::vector<std::string>& languages() { return languages_; }
    static const std::string& current() { return current_; }
    static bool setLocale(const std::string& id);

  private:
    static std::map<std::string, std::map<std::string, std::string>> texts_;
    static std::vector<std::string> languages_;
    static std::string current_;
    static std::string default_;
};

// SharedObject.getLocal("editor") replacement, stored as JSON in the user's app-data folder.
class Settings {
  public:
    static Settings& instance();
    void load();
    void save() const;
    std::string language;
    bool darkMode = false;
    std::vector<std::string> recentDirs;  // max 3, most recent first
    void appendRecentDirectory(const std::string& path);
    void clearRecent();

  private:
    std::string path() const;
};

}  // namespace ae
