#include "core/Util.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ae {

std::string toLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string toUpper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) r += sep;
        r += v[i];
    }
    return r;
}

std::filesystem::path u8path(const std::string& utf8) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return std::filesystem::u8path(utf8);
#endif
}

std::string pathToUtf8(const std::filesystem::path& p) {
#if defined(__cpp_char8_t)
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
#else
    return p.u8string();
#endif
}

bool readFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    f.seekg(0);
    out.resize((size_t)size);
    if (size > 0) f.read(reinterpret_cast<char*>(out.data()), size);
    return (bool)f || f.eof();
}

bool readFileText(const std::string& path, std::string& out) {
    std::vector<uint8_t> b;
    if (!readFileBytes(path, b)) return false;
    out.assign(b.begin(), b.end());
    if (out.size() >= 3 && (uint8_t)out[0] == 0xEF && (uint8_t)out[1] == 0xBB && (uint8_t)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

bool writeFileText(const std::string& path, const std::string& text) {
    std::ofstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    f.write(text.data(), (std::streamsize)text.size());
    return (bool)f;
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(u8path(path), ec);
}

bool isDirectory(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(u8path(path), ec);
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + name;
    return dir + "/" + name;
}

std::string toFixed(double v, int digits) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", digits, v);
    std::string s = buf;
    // AS3 never prints "-0.000"
    bool allZero = true;
    for (char c : s)
        if (c != '-' && c != '0' && c != '.') allZero = false;
    if (allZero && !s.empty() && s[0] == '-') s.erase(0, 1);
    return s;
}

std::string numberToString(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
    if (v == 0) return "0";
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    std::string s(buf, res.ptr);
    // Convert exponent notation produced for very large/small values to AS3 style ("1e+21").
    auto epos = s.find('e');
    if (epos != std::string::npos && s.size() > epos + 1 && s[epos + 1] != '-' && s[epos + 1] != '+')
        s.insert(epos + 1, "+");
    return s;
}

double parseNumber(const std::string& in) {
    std::string s = trim(in);
    if (s.empty()) return 0.0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return std::nan("");
    return v;
}

}  // namespace ae
