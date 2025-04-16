#include "stdafx.h"
#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"
#include <aubio/aubio.h>
#include <commctrl.h>
#include <algorithm>
#include <windows.h>
#include <iostream>
#include <shlobj.h>
#include <string>
#include <vector>
#include <cmath>
#include <map>
#pragma comment(lib, "comctl32.lib")

char mode[256] = "default";
const char* mode_list[] = { "default", "specdiff", "hfc", "energy", "phase", "complex" };
const int mode_count = sizeof(mode_list) / sizeof(mode_list[0]);

enum class ActionListSection {
    Main = 0,
};

static custom_action_register_t actReg_ =
{
    static_cast<int>(ActionListSection::Main),
    "BPM計測",
    "BPM計測",
    0,
};
static int actionID_ = 0;

double analyze_bpm_from_pcm(PCM_source* source, double length_sec) {
    if (!source) return -1.0;

    const int samplerate = source->GetSampleRate();
    const int channels = 1;
    const int win_size = 1024;
    const int hop_size = 512;

    aubio_tempo_t* tempo = new_aubio_tempo(mode, win_size, hop_size, samplerate);
    fvec_t* input = new_fvec(hop_size);
    fvec_t* output = new_fvec(1);
    std::vector<float> beat_times;

    PCM_source_transfer_t transfer = {};
    ReaSample buffer[hop_size * channels] = { 0 };

    transfer.samples = buffer;
    transfer.nch = channels;
    transfer.samplerate = samplerate;
    transfer.length = hop_size;

    const double total_samples = length_sec * samplerate;
    double pos = 0.0;

    while (pos < total_samples) {
        transfer.time_s = pos / samplerate;
        source->GetSamples(&transfer);
        int got = transfer.samples_out;
        if (got <= 0) break;

        for (int i = 0; i < got; ++i) {
            fvec_set_sample(input, buffer[i], i);
        }

        aubio_tempo_do(tempo, input, output);

        if (fvec_get_sample(output, 0) > 0.0f) {
            float beat_time = pos / samplerate;
            beat_times.push_back(beat_time);
        }

        pos += got;
    }

    del_aubio_tempo(tempo);
    del_fvec(input);
    del_fvec(output);

    if (beat_times.size() < 2) return -1.0;

    float time_diff = beat_times.back() - beat_times.front();
    int beat_count = static_cast<int>(beat_times.size() - 1);

    if (time_diff <= 0.0f || beat_count <= 0) return -1.0;

    float bpm = (beat_count / time_diff) * 60.0f;
    return std::round(bpm);
}

void RunBPMDetect() {
    MediaItem* item = GetSelectedMediaItem(0, 0);
    if (!item) {
        ShowConsoleMsg(u8"アイテムが選択されていません。\n");
        return;
    }

    MediaItem_Take* take = GetActiveTake(item);
    if (!take) {
        ShowConsoleMsg(u8"ActiveTake取得できません。\n");
        return;
    }

    PCM_source* source = GetMediaItemTake_Source(take);
    if (!source) {
        ShowConsoleMsg(u8"MediaItemTake_Sourceが取得できません。\n");
        return;
    }
    double oldbpm = TimeMap_GetDividedBpmAtTime(0.0);
    double len = source->GetLength();
    double newbpm = analyze_bpm_from_pcm(source, len);
    double itemlen = GetMediaItemInfo_Value(item, "D_LENGTH");

    if (newbpm <= 0) {
        ShowConsoleMsg(u8"BPM解析に失敗しました。\n");
        return;
    }
    SetCurrentBPM(nullptr, newbpm, true);
    SetMediaItemLength(GetSelectedMediaItem(0, 0), itemlen, true);
    SetMediaItemTakeInfo_Value(take, "D_PLAYRATE", 1.0);
    char msg[256];
    snprintf(msg, sizeof(msg), u8"推定%.2fbpm\n", newbpm);
    ShowConsoleMsg(msg);
}

static bool HookCommand2Callback(KbdSectionInfo* kbdSec, int actionID, int value, int valhw, int relmode, HWND hwnd)
{
    if (kbdSec->uniqueID != actReg_.uniqueSectionId) return false;
    if (actionID != actionID_) return false;

    RunBPMDetect();

    return true;
}

LRESULT CALLBACK PrefWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1001 && HIWORD(wParam) == CBN_SELCHANGE) {
            HWND hCombo = (HWND)lParam;
            int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < mode_count) {
                strncpy(mode, mode_list[sel], sizeof(mode));
            }
        }
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}


HWND CreatePrefWnd(HWND hWndParent)
{
    static bool registered = false;
    const wchar_t* className = L"BPM";
    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = PrefWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = className;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    HWND hWnd = CreateWindowExW(
        0, className, NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 400, 300,
        hWndParent, NULL, GetModuleHandle(NULL), NULL
    );
    HWND hText = CreateWindowW(
        L"STATIC", L"検出モード",
        WS_CHILD | WS_VISIBLE,
        20, 5, 200, 20,
        hWnd, NULL, GetModuleHandle(NULL), NULL
    );
    HWND hCombo = CreateWindowW(
        WC_COMBOBOXW, NULL,
        CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        20, 30, 200, 100,
        hWnd, (HMENU)1001, GetModuleHandle(NULL), NULL
    );
    HWND h2Text = CreateWindowW(
        L"STATIC",
        L"specdiff: スペクトル差分\n"
        L"energy: 音量差による検出\n"
        L"hfc: 高周波エネルギー差\n"
        L"phase: 位相の差分\n"
        L"complex: 振幅と位相を組み合わせた検出",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 60, 360, 100,
        hWnd, NULL, GetModuleHandle(NULL), NULL
    );
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"default");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"specdiff");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"hfc");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"energy");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"phase");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"complex");
    int default_index = 0;
    for (int i = 0; i < mode_count; ++i) {
        if (strcmp(mode, mode_list[i]) == 0) {
            default_index = i;
            break;
        }
    }
    SendMessageW(hCombo, CB_SETCURSEL, default_index, 0);
    return hWnd;
}

extern "C" {
    REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec) {
        static prefs_page_register_t g_myPrefs = {
            "bpm_mode",
            "bpm検出モード",
            CreatePrefWnd,
            1,
            NULL,
            0,
            NULL,
            NULL,
            ""
        };
        if (rec) {
            REAPERAPI_LoadAPI(rec->GetFunc);
            plugin_register("hookcommand2", HookCommand2Callback);
            actionID_ = plugin_register("custom_action", &actReg_);
            plugin_register("prefpage", &g_myPrefs);
            return 1;
        }
        else {
            plugin_register("-custom_action", &actReg_);
            plugin_register("-hookcommand2", HookCommand2Callback);
            plugin_register("-prefpage", &g_myPrefs);
            return 0;
        }
    }
}
