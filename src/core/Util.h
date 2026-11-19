// Small string / file / number-format helpers.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ae {

std::string toLower(std::string s);
std::string toUpper(std::string s);
bool startsWith(const std::string& s, const std::string& prefix);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char sep);
std::string join(const std::vector<std::string>& v, const std::string& sep);

// UTF-8 <-> std::filesystem::path (wide on Windows).
std::filesystem::path u8path(const std::string& utf8);
std::string pathToUtf8(const std::filesystem::path& p);

bool readFileBytes(const std::string& utf8Path, std::vector<uint8_t>& out);
bool readFileText(const std::string& utf8Path, std::string& out);
bool writeFileText(const std::string& utf8Path, const std::string& text);
bool fileExists(const std::string& utf8Path);
bool isDirectory(const std::string& utf8Path);
std::string joinPath(const std::string& dir, const std::string& name);

// AS3 Number.toFixed(digits)
std::string toFixed(double v, int digits);
// AS3 String(Number) (shortest round-trip, integers without decimals, NaN -> "NaN")
std::string numberToString(double v);
// AS3 Number(String): trims whitespace, empty -> 0, invalid -> NaN.
double parseNumber(const std::string& s);

}  // namespace ae
