#include "ConfigDialogWin32.h"

#include <commdlg.h>
#include <string>

#include "resource.h"

#include "../../core/ConfigModel.h"
#include "../../core/Logger.h"
#include "AppPaths.h"
#include "OpenGLContext.h"
#include "StringConvert.h"
#include "WallpaperProvider.h"
#include "WinFileIO.h"

namespace platform {

namespace {

// Combo box item order must match core::ParticlePreset's declaration order
// exactly, since we use the combo selection index as the enum value.
const wchar_t* kPresetLabels[] = {
    L"Low (1,000 particles)",  L"Mid (3,000 particles)",  L"High (6,000 particles)",
    L"Max (12,000 particles)", L"Auto (GPU detect)",      L"Custom",
};
constexpr int kPresetCount = 6;

int QueryAutoParticleCountViaTempContext() {
    const wchar_t* className = L"SpiralSuctionSaverTempGL";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    RegisterClassW(&wc); // ignore failure: harmless if already registered

    HWND hwnd = CreateWindowExW(0, className, L"", WS_POPUP, 0, 0, 4, 4, nullptr, nullptr,
                                 wc.hInstance, nullptr);
    int count = core::ConfigModel::ParticleCountForPreset(core::ParticlePreset::Mid);
    if (hwnd) {
        OpenGLContext ctx;
        if (ctx.Create(hwnd)) {
            count = ResolveAutoParticleCountFromCurrentContext();
            ctx.Destroy();
        }
        DestroyWindow(hwnd);
    }
    return count;
}

void UpdateGpuInfoLabel(HWND hDlg, int presetIndex) {
    if (presetIndex == static_cast<int>(core::ParticlePreset::Auto)) {
        const int count = QueryAutoParticleCountViaTempContext();
        const std::wstring text = L"Auto-detected particle count: " + std::to_wstring(count);
        SetDlgItemTextW(hDlg, IDC_GPU_INFO_STATIC, text.c_str());
    } else {
        SetDlgItemTextW(hDlg, IDC_GPU_INFO_STATIC, L"");
    }
}

void UpdateCustomEditEnabled(HWND hDlg, int presetIndex) {
    const BOOL enabled = presetIndex == static_cast<int>(core::ParticlePreset::Custom);
    EnableWindow(GetDlgItem(hDlg, IDC_CUSTOM_COUNT_EDIT), enabled);
}

core::ConfigModel LoadCurrentConfig() {
    core::ConfigModel config;
    const std::wstring path = GetConfigFilePath();
    std::string text;
    if (!path.empty() && ReadTextFileW(path, text)) {
        config = core::ParseConfigIni(text);
    }
    return config;
}

void SaveConfig(const core::ConfigModel& config) {
    const std::wstring path = GetConfigFilePath();
    if (path.empty()) {
        core::Logger::Error("ConfigDialog: could not resolve config file path (APPDATA missing?)");
        return;
    }
    if (!WriteTextFileW(path, core::SerializeConfigIni(config))) {
        core::Logger::Error("ConfigDialog: failed to write config.ini");
    }
}

void PopulateDialog(HWND hDlg, const core::ConfigModel& config) {
    HWND combo = GetDlgItem(hDlg, IDC_PRESET_COMBO);
    for (int i = 0; i < kPresetCount; ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kPresetLabels[i]));
    }
    const int presetIndex = static_cast<int>(config.preset);
    SendMessageW(combo, CB_SETCURSEL, presetIndex, 0);

    SetDlgItemTextA(hDlg, IDC_CUSTOM_COUNT_EDIT, std::to_string(config.customParticleCount).c_str());
    UpdateCustomEditEnabled(hDlg, presetIndex);
    UpdateGpuInfoLabel(hDlg, presetIndex);

    std::wstring bgDisplay = config.backgroundImageOverridePath.empty()
                                  ? GetSystemWallpaperPath()
                                  : Utf8ToWide(config.backgroundImageOverridePath);
    SetDlgItemTextW(hDlg, IDC_BG_PATH_EDIT, bgDisplay.c_str());
}

void BrowseForImage(HWND hDlg, core::ConfigModel& state) {
    wchar_t fileBuffer[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hDlg;
    ofn.lpstrFilter = L"Image Files\0*.bmp;*.jpg;*.jpeg;*.png;*.gif\0All Files\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        state.backgroundImageOverridePath = WideToUtf8(fileBuffer);
        SetDlgItemTextW(hDlg, IDC_BG_PATH_EDIT, fileBuffer);
    }
}

int ReadCustomCountOrDefault(HWND hDlg) {
    char buf[32] = {};
    GetDlgItemTextA(hDlg, IDC_CUSTOM_COUNT_EDIT, buf, sizeof(buf));
    try {
        const int value = std::stoi(buf);
        if (value > 0) return value;
    } catch (...) {
        // fall through to default below
    }
    MessageBoxW(hDlg, L"Custom particle count must be a positive number. Using 3000.",
                L"Spiral Suction Saver", MB_OK | MB_ICONWARNING);
    return core::ConfigModel::ParticleCountForPreset(core::ParticlePreset::Mid);
}

INT_PTR CALLBACK SettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            auto* state = new core::ConfigModel(LoadCurrentConfig());
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            PopulateDialog(hDlg, *state);
            return TRUE;
        }

        case WM_COMMAND: {
            auto* state = reinterpret_cast<core::ConfigModel*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            const int id = LOWORD(wParam);

            if (id == IDC_PRESET_COMBO && HIWORD(wParam) == CBN_SELCHANGE) {
                const int sel = static_cast<int>(SendMessageW(GetDlgItem(hDlg, IDC_PRESET_COMBO),
                                                                CB_GETCURSEL, 0, 0));
                UpdateCustomEditEnabled(hDlg, sel);
                UpdateGpuInfoLabel(hDlg, sel);
                return TRUE;
            }

            if (id == IDC_BG_BROWSE_BUTTON && state) {
                BrowseForImage(hDlg, *state);
                return TRUE;
            }

            if (id == IDC_BG_USE_SYSTEM_BUTTON && state) {
                state->backgroundImageOverridePath.clear();
                SetDlgItemTextW(hDlg, IDC_BG_PATH_EDIT, GetSystemWallpaperPath().c_str());
                return TRUE;
            }

            if (id == IDOK && state) {
                const int sel = static_cast<int>(SendMessageW(GetDlgItem(hDlg, IDC_PRESET_COMBO),
                                                                CB_GETCURSEL, 0, 0));
                state->preset = static_cast<core::ParticlePreset>(sel);
                if (state->preset == core::ParticlePreset::Custom) {
                    state->customParticleCount = ReadCustomCountOrDefault(hDlg);
                }
                SaveConfig(*state);
                EndDialog(hDlg, IDOK);
                return TRUE;
            }

            if (id == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            return FALSE;
        }

        case WM_DESTROY: {
            auto* state = reinterpret_cast<core::ConfigModel*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            delete state;
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
            return TRUE;
        }

        default:
            return FALSE;
    }
}

} // namespace

void ShowConfigDialog(HWND parent, HINSTANCE instance) {
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), parent, SettingsDialogProc, 0);
}

} // namespace platform
