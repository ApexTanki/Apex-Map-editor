// Native file dialogs (Windows IFileDialog). Paths are UTF-8; empty string = cancelled.
#pragma once
#include <string>

namespace ae::dialogs {

std::string pickFolder(const std::string& title);
// filterName/filterSpec e.g. "Tanks level (*.xml)", "*.xml"
std::string openFile(const std::string& title, const std::string& filterName, const std::string& filterSpec);
std::string saveFile(const std::string& title, const std::string& filterName, const std::string& filterSpec,
                     const std::string& defaultExt);

}  // namespace ae::dialogs
