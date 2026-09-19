// Entry point: parses the standard Windows screensaver command-line
// switches and dispatches to the matching mode (要件.txt §1 Step 1).
//
//   /s          run the screensaver fullscreen
//   /c[:hwnd]   show the settings dialog (optionally owned by hwnd)
//   /p <hwnd>   render a live preview into the given child window
//   (anything else, or no argument at all, shows the settings dialog --
//    the conventional Windows fallback for a .scr invoked without a switch)

#include <cwctype>
#include <cwchar>
#include <objbase.h>
#include <shellapi.h>
#include <string>
#include <windows.h>

#include "../../core/Logger.h"
#include "ConfigDialogWin32.h"
#include "FileLogSink.h"
#include "SaverWindow.h"

namespace {

enum class Mode { RunFullScreen, Configure, Preview };

struct ParsedArgs {
    Mode mode = Mode::Configure;
    HWND parentOrPreviewHwnd = nullptr;
    // /scrapi:<pipeName> -- the SCRAPI viewer's control pipe (docs/SCRAPI_SPEC.md §3).
    // Honored only together with /p; ignored for /s and /c.
    std::wstring scrapiPipeName;
};

bool StartsWithSwitch(const std::wstring& token, wchar_t letter) {
    if (token.size() < 2) return false;
    if (token[0] != L'/' && token[0] != L'-') return false;
    return std::towlower(token[1]) == letter;
}

HWND ExtractHwndFromColonSuffix(const std::wstring& token) {
    const auto colon = token.find(L':');
    if (colon == std::wstring::npos) return nullptr;
    try {
        const long long value = std::stoll(token.substr(colon + 1));
        return reinterpret_cast<HWND>(static_cast<intptr_t>(value));
    } catch (...) {
        return nullptr;
    }
}

ParsedArgs ParseCommandLine() {
    ParsedArgs result;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return result;
    }

    for (int i = 1; i < argc; ++i) {
        const std::wstring token = argv[i];

        // Must be tested before the single-letter switches: "/scrapi:..." also starts with "/s".
        constexpr wchar_t kScrapiPrefix[] = L"/scrapi:";
        if (token.size() > 8 && _wcsnicmp(token.c_str(), kScrapiPrefix, 8) == 0) {
            result.scrapiPipeName = token.substr(8);
            continue;
        }

        if (StartsWithSwitch(token, L's')) {
            result.mode = Mode::RunFullScreen;
        } else if (StartsWithSwitch(token, L'c')) {
            result.mode = Mode::Configure;
            if (HWND h = ExtractHwndFromColonSuffix(token)) {
                result.parentOrPreviewHwnd = h;
            }
        } else if (StartsWithSwitch(token, L'p')) {
            result.mode = Mode::Preview;
            if (HWND h = ExtractHwndFromColonSuffix(token)) {
                result.parentOrPreviewHwnd = h;
            } else if (i + 1 < argc) {
                try {
                    const long long value = std::stoll(argv[i + 1]);
                    result.parentOrPreviewHwnd = reinterpret_cast<HWND>(static_cast<intptr_t>(value));
                    ++i;
                } catch (...) {
                    // leave parentOrPreviewHwnd null; RunPreview no-ops safely
                }
            }
        }
        // Unrecognized tokens are ignored rather than treated as an error,
        // matching how Windows itself invokes .scr files.
    }

    LocalFree(argv);
    return result;
}

} // namespace

// A plain ANSI WinMain (rather than wWinMain) is used deliberately: it is
// the one entry point signature both MSVC and MinGW-w64 link by default
// without extra linker flags (/ENTRY / -municode), and since the command
// line is parsed via GetCommandLineW()+CommandLineToArgvW() below anyway,
// lpCmdLine is unused, so nothing is lost by not using the wide entry point.
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/,
                      int /*nCmdShow*/) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    platform::InstallFileLogSink();
    core::Logger::Info("Spiral Suction Saver starting");

    const ParsedArgs args = ParseCommandLine();
    switch (args.mode) {
        case Mode::RunFullScreen:
            platform::RunFullScreenSaver(hInstance);
            break;
        case Mode::Preview:
            platform::RunPreview(hInstance, args.parentOrPreviewHwnd, args.scrapiPipeName);
            break;
        case Mode::Configure:
        default:
            platform::ShowConfigDialog(args.parentOrPreviewHwnd, hInstance);
            break;
    }

    core::Logger::Info("Spiral Suction Saver exiting");
    CoUninitialize();
    return 0;
}
