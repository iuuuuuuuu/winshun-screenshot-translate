// Shared-host Qt loading, following LingCore/WinShun's MIT capture launcher.
#include <windows.h>
#include <cstdio>
#include <string>
#include <string_view>

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    std::wstring appDir(32768, L'\0');
    DWORD n = GetEnvironmentVariableW(L"WS_APP_DIR", appDir.data(), DWORD(appDir.size()));
    if (n && n < appDir.size()) appDir.resize(n); else appDir.clear();
    for (int i = 1; i + 1 < argc; ++i)
        if (std::wstring_view(argv[i]) == L"--app-dir") appDir = argv[i + 1];
    if (appDir.empty() || GetFileAttributesW((appDir + L"\\Qt6Core.dll").c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fputs("ScreenshotTranslate: start from WinShun 0.20+ (WS_APP_DIR required)\n", stderr);
        return 2;
    }
    SetEnvironmentVariableW(L"WS_APP_DIR", appDir.c_str());
    std::wstring self(32768, L'\0');
    n = GetModuleFileNameW(nullptr, self.data(), DWORD(self.size()));
    if (!n || n >= self.size()) return 3;
    self.resize(n);
    auto dir = self.substr(0, self.find_last_of(L"\\/"));
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) ||
        !AddDllDirectory(appDir.c_str())) return 4;
    auto dll = LoadLibraryExW((dir + L"\\ScreenshotTranslateApp.dll").c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!dll) {
        std::fprintf(stderr, "ScreenshotTranslate: cannot load native module (Windows error %lu)\n", GetLastError());
        return 5;
    }
    using Entry = int(__cdecl*)(const wchar_t*);
    bool probe=false;for(int i=1;i<argc;++i)probe=probe||std::wstring_view(argv[i])==L"--probe";
    auto entry = reinterpret_cast<Entry>(GetProcAddress(dll, probe?"screenshotTranslateProbe":"screenshotTranslateMain"));
    return entry ? entry(appDir.c_str()) : 6;
}
