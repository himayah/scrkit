#include "ConfigDialogWin32.h"

#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>

#include "resource.h"

#include "../../core/ConfigModel.h"
#include "../../core/Logger.h"
#include "../../core/effects/EffectCatalog.h"
#include "AppPaths.h"
#include "OpenGLContext.h"
#include "StringConvert.h"
#include "WallpaperProvider.h"
#include "WinFileIO.h"

namespace platform {

namespace {

// Defined further down, alongside the rest of the effects-dialog code;
// forward-declared so SettingsDialogProc (above it in this file) can call it.
void ShowEffectsDialog(HWND parent, HINSTANCE instance, core::fx::EngineConfig& config);

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

            if (id == IDC_EFFECTS_BUTTON && state) {
                auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hDlg, GWLP_HINSTANCE));
                ShowEffectsDialog(hDlg, instance, state->effects);
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

// ---- Effects dialog (DESIGN_EFFECTS.md §9.5, §16 Step 11) ----------------

namespace {

// The 14 fg (8 continuous + 6 terminal) and 11 bg (continuous only --
// BackgroundSuction is never user-facing, §9.5) rows the two list views show.
std::vector<core::fx::EffectId> ForegroundDialogCatalog() {
    std::vector<core::fx::EffectId> ids = core::fx::ForegroundContinuousCatalog();
    const auto& terminal = core::fx::ForegroundTerminalCatalog();
    ids.insert(ids.end(), terminal.begin(), terminal.end());
    return ids;
}

struct EffectsDialogState {
    core::fx::EngineConfig working;      // edited copy; written back to *target only on this dialog's own OK
    core::fx::EngineConfig* target = nullptr;
    bool hasSelection = false;
    core::fx::LayerKind selectedLayer = core::fx::LayerKind::Foreground;
    core::fx::EffectId selectedId = core::fx::EffectId::FlagWave;
};

core::fx::LayerEffectConfig& LayerConfigFor(EffectsDialogState& state, core::fx::LayerKind kind) {
    return kind == core::fx::LayerKind::Foreground ? state.working.foreground : state.working.background;
}

void SetEditFloat(HWND hDlg, int id, float value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    SetDlgItemTextA(hDlg, id, buf);
}

float GetEditFloat(HWND hDlg, int id, float fallback) {
    char buf[32] = {};
    GetDlgItemTextA(hDlg, id, buf, sizeof(buf));
    try {
        return std::stof(buf);
    } catch (...) {
        return fallback;
    }
}

void InitEffectsListView(HWND list) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    LVCOLUMNW col{};
    col.mask = LVCF_WIDTH;
    col.cx = 200;
    ListView_InsertColumn(list, 0, &col);
}

void PopulateEffectsListView(HWND list, const core::fx::LayerEffectConfig& layerConfig,
                              const std::vector<core::fx::EffectId>& ids) {
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < ids.size(); ++i) {
        const std::wstring name = Utf8ToWide(core::fx::EffectIdToString(ids[i]));
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(name.c_str());
        item.lParam = static_cast<LPARAM>(ids[i]);
        ListView_InsertItem(list, &item);

        const auto it = layerConfig.perEffect.find(ids[i]);
        const bool enabled = it == layerConfig.perEffect.end() || it->second.enabled;
        ListView_SetCheckState(list, static_cast<int>(i), enabled);
    }
}

void PopulateEffectsDialog(HWND hDlg, const EffectsDialogState& state) {
    CheckDlgButton(hDlg, IDC_FX_ENABLED, state.working.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetEditFloat(hDlg, IDC_FX_FG_MIN, state.working.foreground.defaultMinSeconds);
    SetEditFloat(hDlg, IDC_FX_FG_MAX, state.working.foreground.defaultMaxSeconds);
    SetEditFloat(hDlg, IDC_FX_BG_MIN, state.working.background.defaultMinSeconds);
    SetEditFloat(hDlg, IDC_FX_BG_MAX, state.working.background.defaultMaxSeconds);
    SetEditFloat(hDlg, IDC_FX_SHOWCASE, state.working.foregroundShowcaseSeconds);

    PopulateEffectsListView(GetDlgItem(hDlg, IDC_FX_FG_LIST), state.working.foreground, ForegroundDialogCatalog());
    PopulateEffectsListView(GetDlgItem(hDlg, IDC_FX_BG_LIST), state.working.background,
                             core::fx::BackgroundContinuousCatalog());

    HWND trackbar = GetDlgItem(hDlg, IDC_FX_INTENSITY);
    SendMessageW(trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    EnableWindow(trackbar, FALSE); // enabled once a row is selected
}

// Reads the current edit-box values (persisted regardless of which control
// posted the notification, so no field is ever lost on a later OK).
void CommitTimingFields(HWND hDlg, EffectsDialogState& state) {
    state.working.enabled = IsDlgButtonChecked(hDlg, IDC_FX_ENABLED) == BST_CHECKED;
    state.working.foreground.defaultMinSeconds = GetEditFloat(hDlg, IDC_FX_FG_MIN, 5.0f);
    state.working.foreground.defaultMaxSeconds = GetEditFloat(hDlg, IDC_FX_FG_MAX, 10.0f);
    state.working.background.defaultMinSeconds = GetEditFloat(hDlg, IDC_FX_BG_MIN, 8.0f);
    state.working.background.defaultMaxSeconds = GetEditFloat(hDlg, IDC_FX_BG_MAX, 15.0f);
    state.working.foregroundShowcaseSeconds = GetEditFloat(hDlg, IDC_FX_SHOWCASE, 40.0f);
}

void OnListSelectionChanged(HWND hDlg, HWND list, core::fx::LayerKind kind, EffectsDialogState& state) {
    const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (selected < 0) return;

    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = selected;
    ListView_GetItem(list, &item);

    state.hasSelection = true;
    state.selectedLayer = kind;
    state.selectedId = static_cast<core::fx::EffectId>(item.lParam);

    const auto& layerConfig = LayerConfigFor(state, kind);
    const auto it = layerConfig.perEffect.find(state.selectedId);
    const float intensity = it != layerConfig.perEffect.end() ? it->second.intensity : 0.7f;

    HWND trackbar = GetDlgItem(hDlg, IDC_FX_INTENSITY);
    EnableWindow(trackbar, TRUE);
    SendMessageW(trackbar, TBM_SETPOS, TRUE, static_cast<LPARAM>(intensity * 100.0f + 0.5f));
}

void OnListCheckToggled(HWND list, core::fx::LayerEffectConfig& layerConfig, int item) {
    LVITEMW lvItem{};
    lvItem.mask = LVIF_PARAM;
    lvItem.iItem = item;
    ListView_GetItem(list, &lvItem);
    const auto id = static_cast<core::fx::EffectId>(lvItem.lParam);
    layerConfig.perEffect[id].enabled = ListView_GetCheckState(list, item) != 0;
}

void OnIntensityChanged(HWND hDlg, EffectsDialogState& state) {
    if (!state.hasSelection) return;
    const int pos = static_cast<int>(SendMessageW(GetDlgItem(hDlg, IDC_FX_INTENSITY), TBM_GETPOS, 0, 0));
    LayerConfigFor(state, state.selectedLayer).perEffect[state.selectedId].intensity = pos / 100.0f;
}

INT_PTR CALLBACK EffectsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            auto* state = reinterpret_cast<EffectsDialogState*>(lParam);
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            InitEffectsListView(GetDlgItem(hDlg, IDC_FX_FG_LIST));
            InitEffectsListView(GetDlgItem(hDlg, IDC_FX_BG_LIST));
            PopulateEffectsDialog(hDlg, *state);
            return TRUE;
        }

        case WM_NOTIFY: {
            auto* state = reinterpret_cast<EffectsDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
            if (!state || !nmhdr) return FALSE;
            const bool isFg = nmhdr->idFrom == IDC_FX_FG_LIST;
            const bool isBg = nmhdr->idFrom == IDC_FX_BG_LIST;
            if ((isFg || isBg) && nmhdr->code == LVN_ITEMCHANGED) {
                auto* nmlv = reinterpret_cast<NMLISTVIEW*>(lParam);
                const core::fx::LayerKind kind = isFg ? core::fx::LayerKind::Foreground : core::fx::LayerKind::Background;
                auto& layerConfig = LayerConfigFor(*state, kind);
                if ((nmlv->uChanged & LVIF_STATE) && ((nmlv->uOldState ^ nmlv->uNewState) & LVIS_STATEIMAGEMASK)) {
                    OnListCheckToggled(nmhdr->hwndFrom, layerConfig, nmlv->iItem);
                }
                if ((nmlv->uNewState & LVIS_SELECTED) && !(nmlv->uOldState & LVIS_SELECTED)) {
                    OnListSelectionChanged(hDlg, nmhdr->hwndFrom, kind, *state);
                }
                return TRUE;
            }
            return FALSE;
        }

        case WM_HSCROLL: {
            auto* state = reinterpret_cast<EffectsDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            if (state && reinterpret_cast<HWND>(lParam) == GetDlgItem(hDlg, IDC_FX_INTENSITY)) {
                OnIntensityChanged(hDlg, *state);
                return TRUE;
            }
            return FALSE;
        }

        case WM_COMMAND: {
            auto* state = reinterpret_cast<EffectsDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            const int id = LOWORD(wParam);

            if (id == IDC_FX_DEFAULTS && state) {
                state->working = core::fx::MakeDefaultEngineConfig();
                state->hasSelection = false;
                PopulateEffectsDialog(hDlg, *state);
                return TRUE;
            }
            if (id == IDOK && state) {
                CommitTimingFields(hDlg, *state);
                *state->target = state->working;
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            return FALSE;
        }

        default:
            return FALSE;
    }
}

// Shows the modal effects dialog, editing `*config` in place (only if the
// dialog's own OK is pressed -- Cancel leaves it untouched). The caller
// (SettingsDialogProc) still owns the single SaveConfigToFile call on its
// own OK, same as every other field in the outer dialog.
void ShowEffectsDialog(HWND parent, HINSTANCE instance, core::fx::EngineConfig& config) {
    EffectsDialogState state;
    state.working = config;
    state.target = &config;
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_EFFECTS), parent, EffectsDialogProc,
                     reinterpret_cast<LPARAM>(&state));
}

} // namespace

void ShowConfigDialog(HWND parent, HINSTANCE instance) {
    // Registers SysListView32/msctls_trackbar32 (IDD_EFFECTS's list views and
    // trackbar, §9.5) -- harmless to call before every dialog show, and
    // simpler than threading an "already initialized" flag through.
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), parent, SettingsDialogProc, 0);
}

} // namespace platform
