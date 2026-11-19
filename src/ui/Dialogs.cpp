#include "ui/Dialogs.h"

#if defined(_WIN32)
#include <windows.h>
#include <shobjidl.h>

#include <string>

namespace ae::dialogs {

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static std::string narrow(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

struct ComInit {
    HRESULT hr;
    ComInit() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComInit() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

static std::string run(bool save, bool folder, const std::string& title, const std::string& filterName,
                       const std::string& filterSpec, const std::string& defaultExt) {
    ComInit com;
    IFileDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  save ? IID_IFileSaveDialog : IID_IFileOpenDialog, reinterpret_cast<void**>(&dlg));
    if (FAILED(hr) || !dlg) return {};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (folder) opts |= FOS_PICKFOLDERS;
    if (save) opts |= FOS_OVERWRITEPROMPT;
    dlg->SetOptions(opts);
    std::wstring wtitle = widen(title);
    if (!wtitle.empty()) dlg->SetTitle(wtitle.c_str());
    std::wstring fname = widen(filterName), fspec = widen(filterSpec), ext = widen(defaultExt);
    if (!folder && !fspec.empty()) {
        COMDLG_FILTERSPEC specs[2] = {{fname.c_str(), fspec.c_str()}, {L"All files (*.*)", L"*.*"}};
        dlg->SetFileTypes(2, specs);
        dlg->SetFileTypeIndex(1);
    }
    if (!ext.empty()) dlg->SetDefaultExtension(ext.c_str());
    std::string result;
    if (SUCCEEDED(dlg->Show(GetActiveWindow()))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = narrow(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

std::string pickFolder(const std::string& title) { return run(false, true, title, "", "", ""); }

std::string openFile(const std::string& title, const std::string& filterName, const std::string& filterSpec) {
    return run(false, false, title, filterName, filterSpec, "");
}

std::string saveFile(const std::string& title, const std::string& filterName, const std::string& filterSpec,
                     const std::string& defaultExt) {
    return run(true, false, title, filterName, filterSpec, defaultExt);
}

}  // namespace ae::dialogs

#else

namespace ae::dialogs {
std::string pickFolder(const std::string&) { return {}; }
std::string openFile(const std::string&, const std::string&, const std::string&) { return {}; }
std::string saveFile(const std::string&, const std::string&, const std::string&, const std::string&) { return {}; }
}  // namespace ae::dialogs

#endif
