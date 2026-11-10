// WinShun 0.23+ supplies a validated Qt directory through its runner.
// Elevated launches use only that runner's own host directory; no PATH fallback.
#include <windows.h>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::wstring environment(const wchar_t* name)
{
    DWORD count = GetEnvironmentVariableW(name, nullptr, 0);
    if (!count) return {};
    std::wstring value(count, L'\0');
    DWORD length = GetEnvironmentVariableW(name, value.data(), count);
    if (!length || length >= count) return {};
    value.resize(length);
    return value;
}
bool absoluteDirectory(const std::wstring& path)
{
    return path.size() > 3 && ((path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
        (path[0] == L'\\' && path[1] == L'\\')) && path.find(L';') == std::wstring::npos;
}
bool compatibleQt(const std::wstring& file)
{
    DWORD ignored = 0, size = GetFileVersionInfoSizeW(file.c_str(), &ignored);
    if (!size) return false;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) return false;
    VS_FIXEDFILEINFO* info = nullptr; UINT length = 0;
    return VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) &&
        length >= sizeof(*info) && info->dwSignature == 0xfeef04bd &&
        HIWORD(info->dwFileVersionMS) == 6 && LOWORD(info->dwFileVersionMS) == 12;
}
}
int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    std::wstring self(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, self.data(), DWORD(self.size()));
    if (!n || n >= self.size()) return 3;
    self.resize(n);
    const auto dir = self.substr(0, self.find_last_of(L"\\/"));
    const auto qtDir = environment(L"WS_QT_DIR");
    if (!absoluteDirectory(qtDir)) {
        std::fputs("ScreenshotTranslate: WS_QT_DIR missing or invalid; start through WinShun 0.23.0 or newer\n", stderr);
        return 2;
    }
    if (!compatibleQt(qtDir + L"\\Qt6Core.dll")) {
        std::fputs("ScreenshotTranslate: host Qt 6.12.x is required; update WinShun or install a compatible plugin\n", stderr);
        return 2;
    }
    for (auto file : {L"\\Qt6Gui.dll", L"\\Qt6Qml.dll", L"\\Qt6Quick.dll", L"\\platforms\\qwindows.dll",
                      L"\\qml\\QtQuick\\qmldir", L"\\qml\\QtQuick\\Controls\\Basic\\qmldir", L"\\qml\\QtQuick\\Layouts\\qmldir"}) {
        if (GetFileAttributesW((qtDir + file).c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::fputs("ScreenshotTranslate: host Qt runtime incomplete; repair the WinShun installation\n", stderr);
            return 2;
        }
    }
    // Keep all Qt paths on the host, including selection-reader child processes.
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", qtDir.c_str());
    SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH", (qtDir + L"\\platforms").c_str());
    SetEnvironmentVariableW(L"QML_IMPORT_PATH", (qtDir + L"\\qml").c_str());
    SetEnvironmentVariableW(L"QML2_IMPORT_PATH", (qtDir + L"\\qml").c_str());
    SetEnvironmentVariableW(L"QT_QUICK_CONTROLS_STYLE", L"Basic");
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32) ||
        !AddDllDirectory(qtDir.c_str())) return 4;
    auto dll = LoadLibraryExW((dir + L"\\ScreenshotTranslateApp.dll").c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!dll) {
        std::fprintf(stderr, "ScreenshotTranslate: cannot load native module (Windows error %lu)\n", GetLastError());
        return 5;
    }
    using Entry = int(__cdecl*)(const wchar_t*);
    bool probe=false,selection=false;
    for(int i=1;i<argc;++i){probe=probe||std::wstring_view(argv[i])==L"--probe";selection=selection||std::wstring_view(argv[i])==L"--selection-worker";}
    auto entry = reinterpret_cast<Entry>(GetProcAddress(dll, selection?"screenshotTranslateSelection":probe?"screenshotTranslateProbe":"screenshotTranslateMain"));
    return entry ? entry(qtDir.c_str()) : 6;
}
