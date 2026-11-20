#include "ui/Locale.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "core/Util.h"

namespace ae {

std::map<std::string, std::map<std::string, std::string>> Locale::texts_;
std::vector<std::string> Locale::languages_;
std::string Locale::current_;
std::string Locale::default_;

bool Locale::init(const std::string& jsonPath, const std::string& preferred, std::string* err) {
    std::string text;
    if (!readFileText(jsonPath, text)) {
        if (err) *err = "cannot read " + jsonPath;
        return false;
    }
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (err) *err = "invalid JSON in " + jsonPath;
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key() == "_default") {
            default_ = it.value().get<std::string>();
            continue;
        }
        if (!it.value().is_object()) continue;
        languages_.push_back(it.key());
        auto& dst = texts_[it.key()];
        for (auto t = it.value().begin(); t != it.value().end(); ++t)
            if (t.value().is_string()) dst[t.key()] = t.value().get<std::string>();
    }
    if (!setLocale(preferred) && !setLocale(default_) && !languages_.empty()) setLocale(languages_.front());
    return true;
}

const std::string& Locale::get(const std::string& id) {
    auto lang = texts_.find(current_);
    if (lang != texts_.end()) {
        auto t = lang->second.find(id);
        if (t != lang->second.end()) return t->second;
    }
    static thread_local std::string fallback;
    fallback = id;
    return fallback;
}

bool Locale::setLocale(const std::string& id) {
    if (texts_.find(id) == texts_.end()) return false;
    current_ = id;
    return true;
}

// ---------------------------------------------------------------------------------------------

Settings& Settings::instance() {
    static Settings s;
    return s;
}

std::string Settings::path() const {
    std::string base;
#if defined(_WIN32)
    if (const wchar_t* appdata = _wgetenv(L"APPDATA")) base = pathToUtf8(std::filesystem::path(appdata));
#else
    if (const char* home = std::getenv("HOME")) base = std::string(home) + "/.config";
#endif
    if (base.empty()) base = ".";
    std::string dir = joinPath(base, "ApexEditor");
    std::error_code ec;
    std::filesystem::create_directories(u8path(dir), ec);
    return joinPath(dir, "settings.json");
}

void Settings::load() {
    std::string text;
    if (!readFileText(path(), text)) return;
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return;
    if (j.contains("lang") && j["lang"].is_string()) language = j["lang"].get<std::string>();
    if (j.contains("darkMode") && j["darkMode"].is_boolean()) darkMode = j["darkMode"].get<bool>();
    if (j.contains("recentDirs") && j["recentDirs"].is_object() && j["recentDirs"].contains("list")) {
        for (auto& d : j["recentDirs"]["list"])
            if (d.is_string()) recentDirs.push_back(d.get<std::string>());
    }
}

void Settings::save() const {
    nlohmann::json j;
    j["lang"] = language;
    j["darkMode"] = darkMode;
    j["recentDirs"]["list"] = recentDirs;
    writeFileText(path(), j.dump(2));
}

void Settings::appendRecentDirectory(const std::string& p) {
    if (std::find(recentDirs.begin(), recentDirs.end(), p) != recentDirs.end()) return;
    recentDirs.insert(recentDirs.begin(), p);
    while (recentDirs.size() > 3) recentDirs.pop_back();
    save();
}

void Settings::clearRecent() {
    recentDirs.clear();
    save();
}

}  // namespace ae
