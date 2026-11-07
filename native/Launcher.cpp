// The administrator-capable package loads only its own deployed Qt runtime.
#include <windows.h>
#include <cstdio>
#include <string>
#include <string_view>

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    std::wstring self(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, self.data(), DWORD(self.size()));
    if (!n || n >= self.size()) return 3;
    self.resize(n);
    auto dir = self.substr(0, self.find_last_of(L"\\/"));
    auto appDir = dir + L"\\runtime";
    if (GetFileAttributesW((appDir + L"\\Qt6Core.dll").c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fputs("ScreenshotTranslate: bundled runtime missing; reinstall the complete plugin package\n", stderr);
        return 2;
    }
    SetEnvironmentVariableW(L"WS_APP_DIR", appDir.c_str());
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", appDir.c_str());
    SetEnvironmentVariableW(L"QML_IMPORT_PATH", (appDir + L"\\qml").c_str());
    SetEnvironmentVariableW(L"QML2_IMPORT_PATH", nullptr);
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) ||
        !AddDllDirectory(appDir.c_str())) return 4;
    auto dll = LoadLibraryExW((dir + L"\\ScreenshotTranslateApp.dll").c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!dll) {
        std::fprintf(stderr, "ScreenshotTranslate: cannot load native module (Windows error %lu)\n", GetLastError());
        return 5;
    }
    using Entry = int(__cdecl*)(const wchar_t*);
    bool probe=false,selection=false;
    for(int i=1;i<argc;++i){probe=probe||std::wstring_view(argv[i])==L"--probe";selection=selection||std::wstring_view(argv[i])==L"--selection-worker";}
    auto entry = reinterpret_cast<Entry>(GetProcAddress(dll, selection?"screenshotTranslateSelection":probe?"screenshotTranslateProbe":"screenshotTranslateMain"));
    return entry ? entry(appDir.c_str()) : 6;
}
