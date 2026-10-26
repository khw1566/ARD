#include "ui_text.h"
#include <cstdarg>
#include <cstring>
#include "nvs.h"
#include "sdkconfig.h"

// Hangul glyphs used by kText below; regenerate with tools/gen_hangul_font.py.
extern "C" const lv_font_t ard_hangul_16, ard_hangul_20, ard_hangul_28;

lv_font_t ard_font_16, ard_font_20, ard_font_28;

namespace {

UiLanguage language = UI_LANG_EN;

struct Text { const char *en, *ko; };

// English keys must match the UI source strings exactly (format specifiers included).
const Text kText[] = {
    // Tabs and status bar
    {"Main", "메인"}, {"Drive", "구동"}, {"Cam", "카메라"}, {"Voice", "음성"}, {"System", "시스템"},
    {"Settings", "설정"}, {"RE-ARM", "재가동"}, {"E-STOP", "비상정지"}, {"OK", "정상"},
    {"Sync...", "동기화"},
    {"Wi-Fi offline (setup AP on)", "Wi-Fi 끊김 (설정 AP 켜짐)"}, {"Wi-Fi offline", "Wi-Fi 끊김"},
    {"Settings could not start. Restart the board.", "설정을 시작하지 못했습니다. 보드를 다시 시작하세요."},
    {"AI Command - approve?", "AI 명령 - 승인할까요?"}, {"APPROVE", "승인"}, {"REJECT", "거부"},
    {"Updating Wi-Fi co-processor (ESP32-C6)...\nDo not power off. The board restarts when done.",
     "Wi-Fi 보조 칩(ESP32-C6) 업데이트 중...\n전원을 끄지 마세요. 완료되면 다시 시작합니다."},
    // Drive tab
    {"PID target: %d mm", "PID 목표: %d mm"}, {"Z step: %d steps", "Z 이동량: %d 스텝"},
    {"PID: ON", "PID: 켜짐"}, {"PID: OFF", "PID: 꺼짐"}, {"DriveMCU (via PC)", "DriveMCU (PC 경유)"},
    {"Env: ---", "환경: ---"}, {"GPS: off", "GPS: 꺼짐"},
    {"Mode: single step", "조작: 스텝"}, {"Mode: continuous", "조작: 연속"},
    {"One move per tap", "한 번 누르면 한 번 이동"},
    {"Ready: hold to move", "준비됨: 누른 동안 이동"}, {"Press RE-ARM to move", "재가동을 누른 후 조작하세요"},
    {"Turn PID off for continuous motion", "연속 조작은 PID를 꺼주세요"},
    {"Sorting is running", "선별 운전 중"},
    {"Disk / rotation", "디스크 / 회전"}, {"Rail: hold to move", "레일: 누른 동안 이동"},
    {"Command interval: %d ms", "명령 간격: %d ms"},
    {"Fixed 20-step pulses", "고정 20스텝 연속 이동"},
    {"Unit step: %d steps", "단위 이동량: %d 스텝"}, {"Quantitative move", "정량 이동"},
    {"Dead zone: %d %%", "데드존 반경: %d %%"}, {"Dead zone / stopped", "데드존 / 이동 입력 없음"},
    {"Stick strength: %d %%", "조작 세기: %d %%"},
    {"X +", "X +"}, {"X -", "X -"}, {"Y +", "Y +"}, {"Y -", "Y -"},
    {"Disk FW", "디스크 앞"}, {"Disk BW", "디스크 뒤"}, {"Rot CW", "시계 회전"}, {"Rot CCW", "반시계 회전"},
    {"Rail +", "레일 +"}, {"Rail -", "레일 -"},
    {"HOME", "원점"}, {"Enable", "활성화"}, {"Disable", "비활성화"},
    {"PC estimate: X %ld   Y %ld   Z %ld", "PC 추정: X %ld   Y %ld   Z %ld"},
    {"DriveMCU offline / PC unavailable", "DriveMCU 오프라인 / PC 연결 안 됨"},
    {"ONLINE", "온라인"}, {"OFFLINE", "오프라인"},
    {"Drive env: %.1f C  %.0f %%  %.1f hPa", "구동부 환경: %.1f C  %.0f %%  %.1f hPa"},
    {"Drive ToF: %.0f mm", "구동부 ToF: %.0f mm"}, {"Drive GPS: %.5f, %.5f", "구동부 GPS: %.5f, %.5f"},
    // Camera tab
    {"Offline", "오프라인"}, {"Online", "온라인"}, {"PC offline", "PC 오프라인"},
    {"Fetching image...", "이미지를 가져오는 중..."}, {"Camera", "카메라"}, {"SHOT", "촬영"}, {"VIEW", "보기"},
    {"AI ANALYZE", "AI 분석"}, {"STREAM", "스트림"},
    {"New captures appear here automatically", "새로 촬영한 이미지가 자동으로 표시됩니다"},
    {"STOP STREAM", "스트림 정지"},
    {"Sensor %s\nRes %s  Q %d\nAI model %s\n\nObject: %s\nCategory: %s",
     "센서 %s\n해상도 %s  품질 %d\nAI 모델 %s\n\n물체: %s\n분류: %s"},
    // Voice tab
    {"Onboard voice", "내장 음성"}, {"VAD", "자동 감지"}, {"PTT", "누르고 말하기"}, {"MIC ON", "마이크 켜짐"},
    {"MIC OFF", "마이크 꺼짐"}, {"HOLD TO TALK (PTT)", "누르고 있는 동안 말하기 (PTT)"},
    {"Turn mic off", "마이크 끄기"}, {"Turn mic on", "마이크 켜기"},
    {"VOL -", "음량 -"}, {"VOL +", "음량 +"}, {"MUTE", "음소거"}, {"SPEAKER TEST", "스피커 시험"},
    {"PLAY", "재생"}, {"STOP AUDIO", "소리 정지"}, {"ES8311 unavailable", "ES8311 사용 불가"},
    {"Select JC1060 voice on PC", "PC에서 JC1060 음성을 선택하세요"},
    {"Onboard microphone / speaker", "내장 마이크 / 스피커"}, {"RECORDING", "녹음 중"},
    {"UPLOADING", "전송 중"}, {"PROCESSING", "처리 중"}, {"SPEAKING", "말하는 중"}, {"LISTENING", "듣는 중"},
    {" (muted)", " (음소거)"},
    {"%s  %s\nMode %s\nMic level %d / 32767\nVolume %d / 21%s\nClips %lu  errors %lu\n\n"
     "PTT: hold TALK\nVAD: automatic speech input\n\n%s",
     "%s  %s\n방식 %s\n마이크 레벨 %d / 32767\n음량 %d / 21%s\n녹음 %lu  오류 %lu\n\n"
     "PTT: 말하기 버튼을 누른 채 말하기\nVAD: 말소리를 자동으로 감지\n\n%s"},
    // System tab and setup QR
    {"Network", "네트워크"}, {"Setup QR", "설정 QR"}, {"Next QR", "다음 QR"}, {"Close", "닫기"},
    {"QR unavailable; use 192.168.4.1/network", "QR을 만들 수 없습니다. 192.168.4.1/network에 접속하세요"},
    {"1. Join setup Wi-Fi", "1. 설정용 Wi-Fi 연결"}, {"2. Open setup page", "2. 설정 페이지 열기"},
    {"AP: %s\nOpen Wi-Fi / No login required\n%s", "AP: %s\n암호 없는 Wi-Fi / 로그인 불필요\n%s"},
    {"Configured SSID: %s\nIP: %s\nPC server: %s (%s)\n\nSetup AP: %s\nOpen Wi-Fi / No login required\n"
     "http://192.168.4.1/network",
     "설정된 SSID: %s\nIP: %s\nPC 서버: %s (%s)\n\n설정 AP: %s\n암호 없는 Wi-Fi / 로그인 불필요\n"
     "http://192.168.4.1/network"},
    {"(not set)", "(설정 안 됨)"}, {"not found", "없음"},
    {"Heap %lu KB internal, %lu KB PSRAM\nTouch %s", "여유 메모리: 내부 %lu KB, PSRAM %lu KB\n터치 %s"},
    // Main tab (sorting sequence)
    {"1  CAPTURE", "1  촬영"}, {"New camera image", "새 카메라 이미지"}, {"2  AI ANALYSIS", "2  AI 분석"},
    {"Material class", "재질 분류"}, {"3  RAIL + TURN", "3  레일·선회"}, {"Move to the bin", "배출함으로 이동"},
    {"4  DUMP", "4  배출"}, {"Tilt tray to drop", "트레이를 기울여 낙하"}, {"5  RETURN", "5  복귀"},
    {"Back to start", "시작 위치로"}, {"Metal / can", "금속/캔"}, {"Glass / other", "유리/기타"},
    {"Paper / pack", "종이/팩"}, {"Plastic", "플라스틱"}, {"Metal", "금속"}, {"Glass", "유리"},
    {"Others", "기타"}, {"Paper", "종이"},
    {"Disk turn (top view)", "디스크 선회 (위에서 본 모습)"},
    {"Bins: top row rail +15 mm, bottom row -15 mm", "배출함: 위 칸 레일 +15 mm, 아래 칸 -15 mm"},
    {"Tray tilt (side)", "트레이 기울기 (옆모습)"}, {"Rail position", "레일 위치"},
    {"Sorting sequence", "분류 시퀀스"},
    {"Waiting for the PC sorting status.", "PC의 분류 상태를 기다리는 중입니다."},
    {"Sequence stopped. Motors were halted; see the PC for details.",
     "시퀀스가 중단되어 모터를 멈췄습니다. 자세한 내용은 PC에서 확인하세요."},
    {"Sorted into %s and returned to the start position.", "%s에 분류하고 시작 위치로 돌아왔습니다."},
    {"the bin", "배출함"}, {"Ready. Start sorting from the PC dashboard.", "대기 중. PC 대시보드에서 분류를 시작하세요."},
    {"Capturing a new camera image...", "새 카메라 이미지를 촬영하는 중..."},
    {"AI is classifying the material...", "AI가 재질을 분류하는 중..."},
    {"Moving rail to %+.0f mm...", "레일을 %+.0f mm로 이동하는 중..."},
    {"Turning disk to %.0f\xC2\xB0...", "디스크를 %.0f°로 선회하는 중..."},
    {"Tilting tray %.0f\xC2\xB0 to dump...", "배출을 위해 트레이를 %.0f° 기울이는 중..."},
    {"Tray tilted. Waiting for the item to drop...", "트레이를 기울였습니다. 물체가 떨어지기를 기다리는 중..."},
    {"Leveling the tray...", "트레이를 수평으로 되돌리는 중..."},
    {"Turning disk back to 0\xC2\xB0...", "디스크를 0°로 되돌리는 중..."},
    {"Returning rail to the start position...", "레일을 시작 위치로 되돌리는 중..."},
    {"Checking DriveMCU and camera...", "DriveMCU와 카메라를 확인하는 중..."},
    {"PC OFFLINE", "PC 오프라인"}, {"RUNNING  %d%%", "진행 중  %d%%"}, {"STOPPED", "중단됨"},
    {"COMPLETE", "완료"}, {"READY", "대기"}, {"Object: %s", "물체: %s"},
    {"Bin: %s  (%.0f\xC2\xB0, %+.0f mm)", "배출함: %s  (%.0f°, %+.0f mm)"}, {"Bin: -", "배출함: -"},
    {"PC update %s", "PC 갱신 %s"}, {"DriveMCU %s   Camera %s", "DriveMCU %s   카메라 %s"},
    {"online", "온라인"}, {"offline", "오프라인"},
    {"%.1f\xC2\xB0  (dump %.0f\xC2\xB0)", "%.1f°  (배출 %.0f°)"},
    {"START SORTING", "선별 시작"}, {"STOP", "정지"},
    {"Asking the PC to start sorting...", "PC에 선별 시작을 요청하는 중..."},
    {"Asking the PC to stop sorting...", "PC에 선별 정지를 요청하는 중..."},
    {"PC link busy. Try again.", "PC 통신이 바쁩니다. 다시 시도하세요."},
    {"PC link busy. Use E-STOP.", "PC 통신이 바쁩니다. 비상정지를 사용하세요."},
    {"Sorting started.", "선별을 시작했습니다."}, {"Sorting is already running.", "선별이 이미 진행 중입니다."},
    {"DriveMCU or camera is offline.", "DriveMCU 또는 카메라가 오프라인입니다."},
    {"Check the gear ratios in the PC settings.", "PC 설정의 감속비를 확인하세요."},
    {"Stop requested. Motors are halted.", "정지를 요청했습니다. 모터를 멈췄습니다."},
    {"No sorting is running.", "진행 중인 선별이 없습니다."},
    {"The PC did not accept the request (%d).", "PC가 요청을 받지 않았습니다 (%d)."},
    // Settings: tabs, display and language
    {"Display", "화면"}, {"Sound", "소리"}, {"Server", "서버"},
    {"Screen brightness", "화면 밝기"},
    {"Adjust with the slider. Your setting is saved when released.", "슬라이더로 조절하세요. 손을 떼면 저장됩니다."},
    {"Brightness is restored after restarting the board.", "보드를 다시 시작해도 밝기가 유지됩니다."},
    {"Minimum brightness is 5% so the controls remain visible.", "조작 화면이 보이도록 최소 밝기는 5%입니다."},
    {"Brightness changed, but settings storage is unavailable.", "밝기를 바꿨지만 설정 저장소를 사용할 수 없습니다."},
    {"Saving brightness...", "밝기 저장 중..."}, {"Brightness saved.", "밝기를 저장했습니다."},
    {"Could not save brightness. Try again.", "밝기를 저장하지 못했습니다. 다시 시도하세요."},
    {"Language", "언어"}, {"The board restarts to apply the language.", "언어를 바꾸면 보드가 다시 시작됩니다."},
    {"Restarting to change the language...", "언어를 바꾸기 위해 다시 시작합니다..."},
    {"Could not save the language. Try again.", "언어 설정을 저장하지 못했습니다. 다시 시도하세요."},
    {"Settings worker could not start. Restart the board.", "설정 작업을 시작하지 못했습니다. 보드를 다시 시작하세요."},
    {"Settings storage is unavailable. Restart the board.", "설정 저장소를 사용할 수 없습니다. 보드를 다시 시작하세요."},
    // Settings: sound
    {"Speaker volume", "스피커 음량"},
    {"Applies immediately. Saved on the board and in the PC voice settings.",
     "바로 적용되며 보드와 PC 음성 설정에 저장됩니다."},
    {"Mute", "음소거"}, {"Unmute", "음소거 해제"}, {"Test sound", "시험음"},
    {"Same setting as Voice tab VOL - / + and the PC voice volume. 0 is silent.",
     "음성 탭의 음량 -/+, PC 음성 음량과 같은 설정입니다. 0은 무음입니다."},
    {"Saved on this board. The PC uses another voice board.", "이 보드에 저장했습니다. PC는 다른 음성 보드를 사용 중입니다."},
    {"Saved on this board and sent to the PC.", "이 보드에 저장하고 PC에도 보냈습니다."},
    {"Saved on this board. PC busy; try again.", "이 보드에 저장했습니다. PC가 바빠 전달하지 못했으니 다시 시도하세요."},
    {"Playing test sound...", "시험음 재생 중..."},
    {"Speaker unavailable (ES8311 not started).", "스피커를 사용할 수 없습니다 (ES8311 시작 안 됨)."},
    {"Muted. Press Unmute to hear sound again.", "음소거 중입니다. [음소거 해제]를 누르면 소리가 다시 납니다."},
    {"Sound on.", "소리 켜짐."},
    // Settings: network
    {"Nearby Wi-Fi", "주변 Wi-Fi"}, {"Search Wi-Fi", "Wi-Fi 검색"}, {"Press Search Wi-Fi", "[Wi-Fi 검색]을 누르세요"},
    {"Wi-Fi starting...", "Wi-Fi 시작 중..."}, {"Wi-Fi name (SSID)", "Wi-Fi 이름 (SSID)"},
    {"Choose a network or enter its name", "네트워크를 선택하거나 이름을 입력하세요"}, {"Password", "비밀번호"},
    {"Wi-Fi password", "Wi-Fi 비밀번호"}, {"Password is shown as entered.", "비밀번호는 입력한 그대로 표시됩니다."},
    {"Connect", "연결"}, {"Open", "암호 없음"},
    {"Select a 2.4 GHz network. Connection details are saved after success.",
     "2.4 GHz 네트워크를 선택하세요. 연결에 성공하면 정보가 저장됩니다."},
    {"The setup access point turns off while connecting.", "연결하는 동안 설정용 AP는 꺼집니다."},
    {"Open network selected. Press Connect.", "암호 없는 네트워크를 선택했습니다. [연결]을 누르세요."},
    {"Saved password loaded. Check it, then Connect.", "저장된 비밀번호를 불러왔습니다. 확인 후 [연결]을 누르세요."},
    {"Enter the Wi-Fi password, then Connect.", "Wi-Fi 비밀번호를 입력한 뒤 [연결]을 누르세요."},
    {"Enter a Wi-Fi name of 1 to 32 bytes.", "Wi-Fi 이름을 1~32바이트로 입력하세요."},
    {"Wi-Fi is still starting. Wait a moment and try again.", "Wi-Fi를 시작하는 중입니다. 잠시 후 다시 시도하세요."},
    {"Password is too long (maximum 64 bytes).", "비밀번호가 너무 깁니다 (최대 64바이트)."},
    {"Password: 8-63 characters, 64 hex digits, or empty for open Wi-Fi.",
     "비밀번호: 8~63자, 16진수 64자리, 암호 없는 Wi-Fi는 비워 두세요."},
    {"Searching for nearby Wi-Fi...", "주변 Wi-Fi 검색 중..."},
    {"Connecting... Waiting for an IP address.", "연결 중... IP 주소를 기다리는 중입니다."},
    {"Connected. Wi-Fi settings saved for the next start.", "연결했습니다. 다음 시작을 위해 Wi-Fi 설정을 저장했습니다."},
    {"Select a network from the list.", "목록에서 네트워크를 선택하세요."},
    {"No networks found. Search again or enter a hidden SSID.", "네트워크가 없습니다. 다시 검색하거나 숨은 SSID를 입력하세요."},
    {"Wi-Fi is not ready. Wait a moment and try again.", "Wi-Fi가 준비되지 않았습니다. 잠시 후 다시 시도하세요."},
    {"Wi-Fi search failed. Wait a moment and search again.", "Wi-Fi 검색에 실패했습니다. 잠시 후 다시 검색하세요."},
    {"Connection or save failed. Check the password and retry. Previous settings were kept.",
     "연결 또는 저장에 실패했습니다. 비밀번호를 확인하고 다시 시도하세요. 이전 설정은 유지됩니다."},
    {"Connecting to the selected Wi-Fi...", "선택한 Wi-Fi에 연결하는 중..."},
    {"Connected: %s  |  IP: %s", "연결됨: %s  |  IP: %s"},
    {"Not connected  |  2.4 GHz Wi-Fi", "연결 안 됨  |  2.4 GHz Wi-Fi"},
    // Settings: PC server
    {"PC server address", "PC 서버 주소"}, {"Save", "저장"}, {"Default", "기본값"}, {"e.g. 192.168.0.35", "예: 192.168.0.35"},
    {"Enter the IP address of the PC running ARD, then Save.", "ARD를 실행하는 PC의 IP 주소를 입력하고 [저장]을 누르세요."},
    {"Applied immediately and kept after restart. Default: " CONFIG_ARD_DEFAULT_SERVER ". Serial console: server=<IP>",
     "바로 적용되며 재시작 후에도 유지됩니다. 기본값: " CONFIG_ARD_DEFAULT_SERVER ". 시리얼 콘솔: server=<IP>"},
    {"Enter an IP address such as 192.168.0.35 or a host name, without http://, port or path.",
     "192.168.0.35 같은 IP 주소나 호스트 이름을 입력하세요. http://, 포트, 경로는 빼고 입력합니다."},
    {"Saving PC server...", "PC 서버 저장 중..."},
    {"Saved. The board now uses this PC server.", "저장했습니다. 이제 이 PC 서버를 사용합니다."},
    {"Connect to Wi-Fi first. The server is saved with the Wi-Fi settings.",
     "먼저 Wi-Fi에 연결하세요. 서버 주소는 Wi-Fi 설정과 함께 저장됩니다."},
    {"Invalid address. Use an IPv4 address such as 192.168.0.35 or a host name.",
     "주소가 올바르지 않습니다. 192.168.0.35 같은 IPv4 주소나 호스트 이름을 쓰세요."},
    {"Wi-Fi is busy. Try again in a moment.", "Wi-Fi가 사용 중입니다. 잠시 후 다시 시도하세요."},
    {"Could not save the PC server. Try again.", "PC 서버를 저장하지 못했습니다. 다시 시도하세요."},
    {"Saved: %s   |   In use: %s   |   PC: %s", "저장: %s   |   사용 중: %s   |   PC: %s"},
    {"Wi-Fi not connected", "Wi-Fi 연결 안 됨"}, {"responding", "응답함"}, {"no response", "응답 없음"},
};

} // namespace

void ui_text_init() {
    nvs_handle_t nvs;
    uint8_t saved = UI_LANG_EN;
    if (nvs_open("ard-ui", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "lang", &saved);
        nvs_close(nvs);
    }
    language = saved == UI_LANG_KO ? UI_LANG_KO : UI_LANG_EN;
    // Montserrat keeps the existing layout; missing glyphs fall back to Hangul.
    ard_font_16 = lv_font_montserrat_16;
    ard_font_16.fallback = &ard_hangul_16;
    ard_font_20 = lv_font_montserrat_20;
    ard_font_20.fallback = &ard_hangul_20;
    ard_font_28 = lv_font_montserrat_28;
    ard_font_28.fallback = &ard_hangul_28;
}

UiLanguage ui_language() { return language; }

esp_err_t ui_save_language(UiLanguage next) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ard-ui", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(nvs, "lang", next);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

const char *ui_tr(const char *english) {
    if (language == UI_LANG_EN || !english) return english;
    for (const Text &text : kText)
        if (!std::strcmp(text.en, english)) return text.ko;
    return english;
}

// Link-time wrappers (see main/CMakeLists.txt). LVGL's own formatter keeps the
// output identical to the unwrapped lv_label_set_text_fmt.
extern "C" {
void __real_lv_label_set_text(lv_obj_t *obj, const char *text);
void __real_lv_textarea_set_placeholder_text(lv_obj_t *obj, const char *text);

void __wrap_lv_label_set_text(lv_obj_t *obj, const char *text) { __real_lv_label_set_text(obj, ui_tr(text)); }

void __wrap_lv_label_set_text_fmt(lv_obj_t *obj, const char *fmt, ...) {
    static char buffer[1024]; // LVGL objects are only touched from the LVGL task
    va_list args;
    va_start(args, fmt);
    lv_vsnprintf(buffer, sizeof(buffer), ui_tr(fmt), args);
    va_end(args);
    __real_lv_label_set_text(obj, buffer);
}

void __wrap_lv_textarea_set_placeholder_text(lv_obj_t *obj, const char *text) {
    __real_lv_textarea_set_placeholder_text(obj, ui_tr(text));
}
}
