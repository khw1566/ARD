// ARD HMI MCU v2.2 | Portrait 240x320 | ESP32-DualCore
#include <Arduino.h>
#include "logo_jpg.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WebServer.h>
#include <WiFi.h>
#include "CYD28_TouchscreenR.h"
#include <vector>
#include <BluetoothSerial.h>

// --- Custom Font ---
#include "font8x8.h"

// --- Forward Declarations ---
void redraw(bool full = false);

// --- Pin ---
#define BL_PIN    21
#define SCR_W  240
#define SCR_H  320
#define HDR_H      28
#define TAB_H      30
#define SUB_H      26
#define CONT_Y     (HDR_H+TAB_H+SUB_H)
#define RBTN_W     62
#define INFO_W     (SCR_W-RBTN_W)
#define CONT_H     160
#define BOT_Y      (CONT_Y+CONT_H)

// --- WiFi ---
const char* WIFI_SSID_1 = "1-309";
const char* WIFI_PASS_1 = "0522300751";
const char* WIFI_SERVER_1 = "192.168.0.41";
const char* WIFI_SSID_2 = "A35_m";
const char* WIFI_PASS_2 = "00000000";
const char* WIFI_SERVER_2 = "10.115.5.12";
const char* WIFI_SSID_3 = "YOUR_WIFI_SSID";
const char* WIFI_PASS_3 = "YOUR_WIFI_PASSWORD";
const char* WIFI_SERVER_3 = "192.168.0.29";

String ssid_active = "";
String pass_active = "";
String server_ip_pref = "";
String server_ip = "192.168.0.35";

// --- Colors ---
#define CB   TFT_WHITE
#define CHD  0xEF7D    
#define CTBA 0xD67A    
#define CACC 0x0210    
#define CAC2 0x0410    
#define CBTN 0xE71C    
#define CINF 0xF7BE    
#define CPNL 0xEF7D    
#define CSEP 0xD6BA    
#define CON  0x0500    
#define COFF 0xA000    
#define CDM  0x4208    
#define CWT  0x2104    

enum AppState { ST_SPLASH, ST_CONN, ST_MAIN };
AppState st = ST_SPLASH;

#define TMAX 4
const char* TNAMES[TMAX] = {"Drive","Robot","Cam","Sound"};
int curTab = 0;
int subPage[TMAX] = {0, 0, 0, 0};
int maxPage[TMAX] = {4, 4, 4, 1};

struct ABt { String lbl; String cmd; uint16_t col; };
struct PR { String lbl,val; };
PR pdata[TMAX][15];
int pcnt[TMAX]={0,0,0,0};
struct DS { bool on; int rssi; String ip; float t; String last_hb="--:--:--"; };
DS ds[TMAX];

String control_mode = "---"; // ControlMCU 토글 스위치 상태

// --- Approval Popup States ---
bool showApprovalPopup = false;
String pending_action_id = "";
String pending_action_desc = "";

void sendApproval(String id, bool approve) {
    if(WiFi.status()==WL_CONNECTED) {
        HTTPClient h;
        h.begin("http://"+server_ip+":5000/api/hmi/approve");
        h.addHeader("Content-Type", "application/json");
        JsonDocument d;
        d["id"] = id;
        d["approve"] = approve;
        String pl; serializeJson(d, pl);
        h.setTimeout(1000);
        h.POST(pl);
        h.end();
    }
}

// --- Persistent States ---
int hb_interval_idx = 2; 
const char* hb_lbls[4] = {"HB 200", "HB 500", "HB 1K", "HB 2K"};
const char* hb_cmds[4] = {"mzh200", "mzh500", "mzh1000", "mzh2000"};
int i2c_speed_idx = 1; 
const char* i2c_lbls[3] = {"I2C LO", "I2C MD", "I2C HI"};
const char* i2c_cmds[3] = {"mzc50000", "mzc100000", "mzc400000"};

// --- PID Mode States ---
bool is_pid_mode = false;
int target_tof = 145;
int z_step = 50;
int r3_speeds[3] = {100, 700, 1000};
int r3_step_x = 50;
int r3_step_y = 50;
int r3_step_z = 50;
bool showCamImage = false;
unsigned long lastImageTime = 0;

ABt* getButtons(int tab, int page, int &cnt) {
    static ABt b[6];
    cnt = 0;
    if (tab == 0) {
        if (page == 0) {
            b[0] = {hb_lbls[hb_interval_idx], hb_cmds[hb_interval_idx], CACC}; 
            b[1] = {i2c_lbls[i2c_speed_idx], i2c_cmds[i2c_speed_idx], CACC}; 
            cnt=2;
        } else if (page == 1) {
            b[0] = {"R-FW", "b45,-45", CACC}; b[1] = {"R-BW", "b-45,45", CACC}; 
            b[2] = {"Y-CW", "b45,45", CAC2}; b[3] = {"Y-CCW", "b-45,-45", CAC2}; cnt=4;
        } else if (page == 2) {
            b[0] = {"X+50", "b50,0", 0x07FF}; b[1] = {"X-50", "b-50,0", 0x07FF}; 
            b[2] = {"Y+50", "b0,50", 0x07FF}; b[3] = {"Y-50", "b0,-50", 0x07FF}; cnt=4;
        } else if (page == 3) {
            b[0] = {is_pid_mode ? "PID Mode" : "OpenLoop", "mzm", (uint16_t)(is_pid_mode ? CON : CAC2)};
            if(is_pid_mode) {
                b[1] = {"TGT +10", "mzt+10", CACC};
                b[2] = {"TGT -10", "mzt-10", CACC};
            } else {
                b[1] = {"STP +50", "mzs+50", CACC};
                b[2] = {"STP -50", "mzs-50", CACC};
            }
            b[3] = {"HOME", "home", COFF}; cnt=4;
        }
    } else if (tab == 1) {
        if (page == 0) { // Page 1: Network
            b[0] = {hb_lbls[hb_interval_idx], hb_cmds[hb_interval_idx], CACC}; 
            b[1] = {"REFRESH", "refresh", CBTN}; cnt=2;
        } else if (page == 1) { // Page 2: Motor Control
            b[0] = {"X+"+String(r3_step_x), "rx"+String(r3_step_x), 0x07FF}; 
            b[1] = {"X-"+String(r3_step_x), "rx-"+String(r3_step_x), 0x07FF}; 
            b[2] = {"Y+"+String(r3_step_y), "ry"+String(r3_step_y), 0x07FF}; 
            b[3] = {"Y-"+String(r3_step_y), "ry-"+String(r3_step_y), 0x07FF}; 
            b[4] = {"Z+"+String(r3_step_z), "rz"+String(r3_step_z), 0x07FF}; 
            b[5] = {"Z-"+String(r3_step_z), "rz-"+String(r3_step_z), 0x07FF}; cnt=6;
        } else if (page == 2) { // Page 3: Gripper
            b[0] = {"GRAB OP", "grab open", CON}; b[1] = {"GRAB CL", "grab close", COFF}; 
            b[2] = {"HOME", "home", CACC}; cnt=3;
        } else if (page == 3) { // Page 4: Max Speed
            b[0] = {"X SPD+", "rxs10", CACC}; b[1] = {"X SPD-", "rxs-10", CACC}; 
            b[2] = {"Y SPD+", "rys50", CACC}; b[3] = {"Y SPD-", "rys-50", CACC}; 
            b[4] = {"Z SPD+", "rzs50", CACC}; b[5] = {"Z SPD-", "rzs-50", CACC}; cnt=6;
        }
    } else if (tab == 2) { // Cam Tab
        if (page == 0) { // Page 1: Network
            b[0] = {hb_lbls[hb_interval_idx], hb_cmds[hb_interval_idx], CACC}; 
            b[1] = {"SPI 10M", "cspi10", CAC2}; b[2] = {"SPI 20M", "cspi20", CAC2}; cnt=3;
        } else if (page == 1) { // Page 2: Control
            b[0] = {"SHOT", "capture", CACC}; 
            b[1] = {showCamImage ? "HIDE" : "VIEW", "toggle_view", CON}; 
            b[2] = {"AI ANALYZE", "analyze", CAC2}; 
            b[3] = {"STREAM", "stream", (uint16_t)(showCamImage ? COFF : CBTN)}; cnt=4;
        } else if (page == 2) { // Page 3: Sensor & Quality
            String sen="---", res="---", qlt="---";
            for(int i=0; i<pcnt[2]; i++) {
                if(pdata[2][i].lbl == "SENS") sen = pdata[2][i].val;
                if(pdata[2][i].lbl == "RES") res = pdata[2][i].val;
                if(pdata[2][i].lbl == "QUAL") qlt = pdata[2][i].val;
            }
            b[0] = {"SENS\n"+sen, "c_sens", CACC}; 
            String px = (res=="UXGA")?"1600x":(res=="SXGA")?"1280x":(res=="XGA")?"1024x":(res=="SVGA")?"800x600":"640x480";
            if(res=="UXGA"||res=="SXGA"||res=="XGA") px += (res=="UXGA")?"1200":(res=="SXGA")?"1024":"768";
            b[1] = {res+"\n"+px, "c_res", CACC}; 
            b[2] = {"QUAL\n"+qlt, "c_qlt", CACC}; cnt=3;
        } else if (page == 3) { // Page 4: AI Model
            String mdl = "---";
            for(int i=0; i<pcnt[2]; i++) if(pdata[2][i].lbl == "AI Model") mdl = pdata[2][i].val;
            if(mdl.indexOf("flash")!=-1) mdl = "FLASH";
            b[0] = {"AI MDL\n"+mdl, "c_mdl", CACC}; cnt=1;
        }
    } else if (tab == 3) {
        b[0] = {"VAD", "vad", CON}; b[1] = {"PTT", "ptt", CACC}; 
        b[2] = {"MUTE", "mute", COFF}; b[3] = {"PLAY", "play", CON}; cnt=4;
    }
    return b;
}

String clk="--:--";
bool wok=false;
unsigned long lclk=0;
TFT_eSPI tft=TFT_eSPI();
CYD28_TouchR touch = CYD28_TouchR(320, 240);
WebServer wsrv(80);
Preferences prefs;
SemaphoreHandle_t xMutex;

void drText(int x, int y, const String& s, uint16_t color, uint16_t bg, int scale, int align) {
    int start = 0;
    std::vector<String> lines;
    while (true) {
        int next = s.indexOf('\n', start);
        if (next == -1) { lines.push_back(s.substring(start)); break; }
        lines.push_back(s.substring(start, next));
        start = next + 1;
    }
    int totalH = lines.size() * 8 * scale;
    int curY = y;
    if (align == 3 || align == 4 || align == 5) curY -= totalH / 2;
    for (const String& line : lines) {
        int tw = line.length() * 8 * scale;
        int dx = x;
        if (align == 1 || align == 3) dx -= tw / 2;
        if (align == 2 || align == 4) dx -= tw;
        int w = 8 * scale; int h = 8 * scale;
        uint16_t buf[w * h];
        bool transp = (color == bg);
        for (int i = 0; i < (int)line.length(); i++) {
            char c = line[i] & 0x7F;
            char* bmp = font8x8_basic[(int)c];
            if (transp) {
                for (int row = 0; row < 8; row++) {
                    for (int col = 0; col < 8; col++) {
                        if (bmp[row] & (1 << col)) tft.fillRect(dx + i*w + col*scale, curY + row*scale, scale, scale, color);
                    }
                }
            } else {
                int idx = 0;
                for (int row = 0; row < 8; row++) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int col = 0; col < 8; col++) {
                            uint16_t pCol = (bmp[row] & (1 << col)) ? color : bg;
                            for (int sx = 0; sx < scale; sx++) buf[idx++] = pCol;
                        }
                    }
                }
                tft.pushImage(dx + i * w, curY, w, h, buf);
            }
        }
        curY += 8 * scale;
    }
}

bool tft_cb(int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t*b){
    if(y>=tft.height())return false;
    tft.pushImage(x,y,w,h,b);return true;
}

void drSig(int x,int y,int r){
    int b=r>-70?3:r>-80?2:r>-90?1:0;
    for(int i=0;i<3;i++){int bh=5+i*3;tft.fillRect(x+i*6,y+(11-bh),4,bh,i<b?CON:CSEP);}
}
void drDot(int x,int y,bool on){tft.fillCircle(x,y,4,on?CON:COFF);}
void updClk(){struct tm ti;if(getLocalTime(&ti)){char b[9];strftime(b,9,"%H:%M",&ti);clk=b;}}

void drawSplash(){
    tft.fillScreen(CB);
    tft.setSwapBytes(true);
    TJpgDec.drawJpg((SCR_W-180)/2,45,LOGO_JPG,LOGO_JPG_LEN);
    tft.setSwapBytes(false); 
    drText(SCR_W/2, 185, "ARD HMI System v2.2", CACC, CB, 2, 3);
    drText(SCR_W/2, 210, "Portrait | DualCore", CDM, CB, 1, 3);
    tft.drawFastHLine(20,225,SCR_W-40,CSEP);
    drText(SCR_W/2, 242, "Initializing...", CWT, CB, 1, 3);
    delay(1000);
}

void TaskNetwork(void *pvParameters) {
    unsigned long lpoll=0, lhb=0;
    while(1) {
        wsrv.handleClient();
        unsigned long now = millis();
        if (now - lpoll >= 3000) {
            lpoll = now;
            if (wok && WiFi.status() == WL_CONNECTED) {
                HTTPClient h; h.begin("http://"+server_ip+":5000/api/status");
                h.setTimeout(1000);
                if(h.GET()==200){
                    JsonDocument d;
                    if(!deserializeJson(d, h.getString())){
                        if(xSemaphoreTake(xMutex, portMAX_DELAY)) {
                            const char* keys[]={"drivemcu","robot3axismcu","imagesendermcu","soundmcu"};
                            const char* names[]={"DriveMCU","Robot3Axis","CamMCU","SoundMCU"};
                            for(int i=0;i<TMAX;i++){
                                String k = keys[i];
                                ds[i].on = (String(d[k]["state"]|"")=="Online");
                                ds[i].rssi = d[k]["rssi"]|0;
                                ds[i].ip = String(d[k]["ip"]|"---");
                                ds[i].t = d[k]["temp"]|0.0f;
                                if(ds[i].on) {
                                   struct tm ti; if(getLocalTime(&ti)){
                                       char buf[10]; strftime(buf,10,"%H:%M:%S",&ti);
                                       ds[i].last_hb = buf;
                                   }
                                }
                                pdata[i][0] = {"Board", names[i]};
                                pdata[i][1] = {"Status", ds[i].on ? "Online" : "Offline"};
                                pdata[i][2] = {"Signal", String(ds[i].rssi) + " dBm"};
                                pdata[i][3] = {"IP", ds[i].ip};
                                pdata[i][4] = {"CPU Temp", String(ds[i].t, 1) + " C"};
                                pdata[i][5] = {"Last HB", ds[i].last_hb};
                                pcnt[i] = 6;
                            }
                            pdata[0][pcnt[0]++] = {"ToF Dist", String(d["drivemcu"]["tof_dist"].as<int>()) + "mm"};
                            pdata[0][pcnt[0]++] = {"Z-Est", String(d["drivemcu"]["z_est"].as<int>()) + "mm"};
                            pdata[1][pcnt[1]++] = {"X Deg", String(d["robot3axismcu"]["state_data"]["x_deg"].as<float>(), 1)};
                            pdata[1][pcnt[1]++] = {"Y mm", String(d["robot3axismcu"]["state_data"]["y_mm"].as<float>(), 1)};
                            pdata[1][pcnt[1]++] = {"Z mm", String(d["robot3axismcu"]["state_data"]["z_mm"].as<float>(), 1)};
                            pdata[1][pcnt[1]++] = {"Grab", String(d["robot3axismcu"]["state_data"]["grab_state"]|"---")};
                            pdata[1][pcnt[1]++] = {"X-Spd", String(d["robot3axismcu"]["state_data"]["x_speed"]|100)};
                            pdata[1][pcnt[1]++] = {"Y-Spd", String(d["robot3axismcu"]["state_data"]["y_speed"]|700)};
                            pdata[1][pcnt[1]++] = {"Z-Spd", String(d["robot3axismcu"]["state_data"]["z_speed"]|1000)};
                            
                            // Cam Tab Data
                            if (d["imagesendermcu"]["config"].is<JsonObject>()) {
                                JsonObject cfg = d["imagesendermcu"]["config"];
                                pdata[2][pcnt[2]++] = {"SENS", cfg["camera_model"].as<String>()};
                                pdata[2][pcnt[2]++] = {"RES", cfg["resolution"].as<String>()};
                                pdata[2][pcnt[2]++] = {"QUAL", String(cfg["jpeg_quality"].as<int>())};
                            }
                            if (d["imagesendermcu"]["last_analysis"].is<JsonObject>()) {
                                JsonObject la = d["imagesendermcu"]["last_analysis"];
                                pdata[2][pcnt[2]++] = {"OBJ", la["object"].as<String>()};
                                pdata[2][pcnt[2]++] = {"CAT", la["category"].as<String>()};
                            }
                            pdata[2][pcnt[2]++] = {"AI Model", String(d["imagesendermcu"]["model_name"]|"---")};
                            pdata[3][pcnt[3]++] = {"Mode", String(d["soundmcu"]["mode"]|"---")};
                            pdata[3][pcnt[3]++] = {"Status", String(d["soundmcu"]["status"]|"---")};
                            
                            // ControlMCU 상태 파싱
                            if (d["controlmcu"].is<JsonObject>()) {
                                control_mode = String(d["controlmcu"]["mode"] | "---");
                            }
                            
                            if (d["hmi_mcu"]["pending_action"].is<JsonObject>()) {
                                String id = d["hmi_mcu"]["pending_action"]["id"].as<String>();
                                String desc = d["hmi_mcu"]["pending_action"]["desc"].as<String>();
                                if (id != pending_action_id || !showApprovalPopup) {
                                    pending_action_id = id;
                                    pending_action_desc = desc;
                                    showApprovalPopup = true;
                                    redraw(true);
                                }
                            } else {
                                if (showApprovalPopup) {
                                    showApprovalPopup = false;
                                    redraw(true);
                                }
                            }
                            
                            // PID 설정 동기화
                            if (d["drive_settings"].is<JsonObject>()) {
                                is_pid_mode = d["drive_settings"]["pid_mode"] | is_pid_mode;
                                target_tof = d["drive_settings"]["pid_target_tof"] | target_tof;
                                z_step = d["drive_settings"]["z_step"] | z_step;
                            }
                            xSemaphoreGive(xMutex);
                        }
                    }
                }
                h.end();
            }
        }
        if (now - lhb >= 10000) {
            lhb = now;
            if (wok && WiFi.status() == WL_CONNECTED) {
                HTTPClient h; h.begin("http://"+server_ip+":5000/api/heartbeat");
                h.addHeader("Content-Type","application/json");
                JsonDocument d; d["source"]="HMI_MCU"; d["rssi"]=WiFi.RSSI();
                d["ip"]=WiFi.localIP().toString(); d["temp"]=temperatureRead();
                String pl; serializeJson(d,pl);
                h.setTimeout(500); h.POST(pl); h.end();
            }
        }
        delay(10);
    }
}

void drHdr(){
    tft.fillRect(0,0,SCR_W,HDR_H,CHD);
    tft.drawFastHLine(0,HDR_H,SCR_W,CSEP);
    drText(6, 6, clk, CWT, CHD, 2, 0);
    drText(SCR_W/2, HDR_H/2, "ARD", CACC, CHD, 2, 3);
    drSig(SCR_W-32,6,wok?WiFi.RSSI():-100);
    drDot(SCR_W-8,14,wok);
}
void drTabs(){
    int tw=SCR_W/TMAX;
    for(int i=0;i<TMAX;i++){
        int tx=i*tw;bool act=(i==curTab);
        uint16_t bgCol = act?CTBA:CHD;
        tft.fillRect(tx,HDR_H,tw,TAB_H,bgCol);
        if(act)tft.fillRect(tx,HDR_H+TAB_H-3,tw,3,CACC);
        drText(tx+tw/2, HDR_H+TAB_H/2, TNAMES[i], act?CWT:CDM, bgCol, 1, 3);
        if(i<TMAX-1)tft.drawFastVLine(tx+tw,HDR_H,TAB_H,CSEP);
    }
    tft.drawFastHLine(0,HDR_H+TAB_H,SCR_W,CSEP);
}
void drSub(){
    int y=HDR_H+TAB_H;
    tft.fillRect(0,y,SCR_W,SUB_H,CBTN);
    tft.drawFastHLine(0,y+SUB_H-1,SCR_W,CSEP);
    drText(SCR_W/2, y+SUB_H/2, "[ Device Info ]", CDM, CBTN, 1, 3);
}
void drInfo(){
    int y=CONT_Y;
    tft.fillRect(0,y,INFO_W,CONT_H,CINF);
    tft.drawFastVLine(INFO_W,y,CONT_H,CSEP);
    const char* dn[TMAX]={"DriveMCU","Robot3Axis","CamMCU","SoundMCU"};
    drText(8, y+10, dn[curTab], CWT, CINF, 1, 0); 
    drDot(INFO_W-12, y+12, ds[curTab].on);      
    
    if (curTab == 0) {
        int p = subPage[0];
        if (p == 0) {
            drText(8, y+36, "IP:", CDM, CINF, 1, 0); drText(42, y+36, ds[0].ip.length()?ds[0].ip:"---", CWT, CINF, 1, 0);
            drText(8, y+56, "RSSI:", CDM, CINF, 1, 0); drText(56, y+56, String(ds[0].rssi)+"dBm", CWT, CINF, 1, 0);
            drText(8, y+76, "Temp:", CDM, CINF, 1, 0); drText(56, y+76, String(ds[0].t,1)+"C", CWT, CINF, 1, 0);
            drText(8, y+96, "HB:", CDM, CINF, 1, 0); drText(40, y+96, ds[0].last_hb, CACC, CINF, 1, 0);
            drText(8, y+116, "I2C:", CDM, CINF, 1, 0); drText(40, y+116, "Ready", CWT, CINF, 1, 0);
            drSig(8,y+145,wok?WiFi.RSSI():-100);
        } else if (p == 1) {
            drText(8, y+36, "ToF Dist:", CDM, CINF, 1, 0);
            if(pcnt[0]>8) drText(70, y+36, pdata[0][8].val, CWT, CINF, 1, 0); else drText(70, y+36, "---", CWT, CINF, 1, 0);
            drText(8, y+56, "Roll(Est):", CDM, CINF, 1, 0); drText(70, y+56, "N/A", CWT, CINF, 1, 0);
            drText(8, y+76, "Yaw(Est):", CDM, CINF, 1, 0); drText(70, y+76, "N/A", CWT, CINF, 1, 0);
        } else if (p == 2) {
            drText(8, y+36, "X Speed:", CDM, CINF, 1, 0); drText(70, y+36, "100%", CWT, CINF, 1, 0);
            drText(8, y+56, "Y Speed:", CDM, CINF, 1, 0); drText(70, y+56, "100%", CWT, CINF, 1, 0);
            drText(8, y+76, "Ctrl Step:", CDM, CINF, 1, 0); drText(70, y+76, "50", CACC, CINF, 1, 0);
        } else if (p == 3) {
            drText(8, y+10, "Z-Control | ", CWT, CINF, 1, 0); 
            drText(80, y+10, is_pid_mode?"PID":"OPEN", is_pid_mode?CON:COFF, CINF, 1, 0);
            int rx = 140, ry = y+30, rh = 110;
            tft.fillRect(rx, ry, 6, rh, CSEP); 
            int tof = 145, est = 145;
            for(int i=0; i<pcnt[0]; i++) {
                if(pdata[0][i].lbl == "ToF Dist") tof = pdata[0][i].val.toInt();
                if(pdata[0][i].lbl == "Z-Est") est = pdata[0][i].val.toInt();
            }
            // Map 145~175 range to rail height
            int ty = ry + rh - ((tof - 145) * rh / 30); ty = constrain(ty, ry, ry+rh);
            int ey = ry + rh - ((est - 145) * rh / 30); ey = constrain(ey, ry, ry+rh);
            tft.fillRect(rx-10, ty-1, 10, 3, CON);  
            tft.fillRect(rx+6, ey-1, 10, 3, CAC2); 
            drText(8, y+40, "ToF:", CDM, CINF, 1, 0); drText(45, y+40, String(tof)+"mm", CON, CINF, 1, 0);
            drText(8, y+65, "Est:", CDM, CINF, 1, 0); drText(45, y+65, String(est)+"mm", CAC2, CINF, 1, 0);
            drText(8, y+100, is_pid_mode?"TGT:":"STP:", CDM, CINF, 1, 0);
            drText(45, y+100, is_pid_mode?String(target_tof)+"mm":String(z_step)+"mm", CWT, CINF, 1, 0);
        }
    } else if (curTab == 1) { // Robot3Axis Pages
        int p = subPage[1];
        if (p == 0) { // Network
            drText(8, y+36, "IP:", CDM, CINF, 1, 0); drText(42, y+36, ds[1].ip.length()?ds[1].ip:"---", CWT, CINF, 1, 0);
            drText(8, y+56, "RSSI:", CDM, CINF, 1, 0); drText(56, y+56, String(ds[1].rssi)+"dBm", CWT, CINF, 1, 0);
            drText(8, y+76, "HB:", CDM, CINF, 1, 0); drText(40, y+76, ds[1].last_hb, CACC, CINF, 1, 0);
            drSig(8,y+145,wok?WiFi.RSSI():-100);
        } else if (p == 1) { // Motor
            String xd="0.0", ym="0.0", zm="0.0";
            for(int i=0; i<pcnt[1]; i++) {
                if(pdata[1][i].lbl == "X Deg") xd = pdata[1][i].val;
                if(pdata[1][i].lbl == "Y mm") ym = pdata[1][i].val;
                if(pdata[1][i].lbl == "Z mm") zm = pdata[1][i].val;
            }
            drText(8, y+36, "X:", CDM, CINF, 1, 0); drText(30, y+36, xd+" deg", CWT, CINF, 1, 0);
            drText(8, y+56, "Y:", CDM, CINF, 1, 0); drText(30, y+56, ym+" mm", CWT, CINF, 1, 0);
            drText(8, y+76, "Z:", CDM, CINF, 1, 0); drText(30, y+76, zm+" mm", CWT, CINF, 1, 0);
            drText(8, y+110, "Control: Axis Pos", CACC, CINF, 1, 0);
        } else if (p == 2) { // Gripper
            String gst="---";
            for(int i=0; i<pcnt[1]; i++) if(pdata[1][i].lbl == "Grab") gst = pdata[1][i].val;
            drText(8, y+36, "Gripper:", CDM, CINF, 1, 0); drText(76, y+36, gst, gst=="open"?CON:COFF, CINF, 1, 0);
            drText(8, y+60, "Valve:", CDM, CINF, 1, 0); drText(56, y+60, "Dual-Solenoid", CWT, CINF, 1, 0);
            drText(8, y+110, "Control: Pneumatic", CACC, CINF, 1, 0);
        } else if (p == 3) { // Speed
            String xs="100", ys="700", zs="1000";
            for(int i=0; i<pcnt[1]; i++) {
                if(pdata[1][i].lbl == "X-Spd") xs = pdata[1][i].val;
                if(pdata[1][i].lbl == "Y-Spd") ys = pdata[1][i].val;
                if(pdata[1][i].lbl == "Z-Spd") zs = pdata[1][i].val;
            }
            drText(8, y+36, "X Spd:", CDM, CINF, 1, 0); drText(60, y+36, xs+" s/s", CWT, CINF, 1, 0);
            drText(8, y+56, "Y Spd:", CDM, CINF, 1, 0); drText(60, y+56, ys+" s/s", CWT, CINF, 1, 0);
            drText(8, y+76, "Z Spd:", CDM, CINF, 1, 0); drText(60, y+76, zs+" s/s", CWT, CINF, 1, 0);
            drText(8, y+76, "Z Spd:", CDM, CINF, 1, 0); drText(60, y+76, zs+" s/s", CWT, CINF, 1, 0);
            drText(8, y+110, "Control: Max Speed", CACC, CINF, 1, 0);
        }
    } else if (curTab == 2) { // Cam Pages
        int p = subPage[2];
        if (p == 1) { // Control & Image
            if (showCamImage) {
                // Loading animation
                int cx = INFO_W/2, cy = y + CONT_H/2;
                tft.drawSmoothArc(cx, cy, 20, 15, (millis()/5)%360, (millis()/5+90)%360, CACC, CINF, true);
                drText(cx, cy+30, "Fetching Image...", CDM, CINF, 1, 3);
                
                // Fetch and draw JPG
                HTTPClient h; h.begin("http://"+server_ip+":5000/api/hmi/last_image");
                h.setTimeout(1500); // 1.5s timeout for HMI safety
                int httpCode = h.GET();
                if(httpCode == 200) {
                    int len = h.getSize();
                    if(len > 0 && len < 40000) { // Safety limit: 40KB
                        uint8_t* buf = (uint8_t*)malloc(len);
                        if(buf) {
                            int rlen = h.getStream().readBytes(buf, len);
                            if(rlen == len) {
                                tft.setSwapBytes(true);
                                JRESULT jres = TJpgDec.drawJpg(0, y, buf, len);
                                tft.setSwapBytes(false);
                                if(jres != JDR_OK) {
                                    drText(cx, cy, "Decode Err: " + String((int)jres), COFF, CINF, 1, 3);
                                }
                            } else {
                                drText(cx, cy, "Stream Error", COFF, CINF, 1, 3);
                            }
                            free(buf);
                        } else {
                            drText(cx, cy, "Memory Full", COFF, CINF, 1, 3);
                        }
                    } else {
                        drText(cx, cy, "Invalid Size", COFF, CINF, 1, 3);
                    }
                } else {
                    drText(cx, cy, "HTTP Error: " + String(httpCode), COFF, CINF, 1, 3);
                }
                h.end();
            } else {
                String obj="None", cat="None", ts="--:--:--";
                for(int i=0; i<pcnt[2]; i++) {
                    if(pdata[2][i].lbl == "OBJ") obj = pdata[2][i].val;
                    if(pdata[2][i].lbl == "CAT") cat = pdata[2][i].val;
                    ts = ds[2].last_hb;
                }
                drText(8, y+36, "AI Result:", CDM, CINF, 1, 0);
                drText(8, y+60, "OBJ: " + obj, CACC, CINF, 1, 0);
                drText(8, y+80, "CAT: " + cat, CACC, CINF, 1, 0);
                drText(8, y+145, "Last Update: " + ts, CDM, CINF, 1, 0);
            }
        } else if (p == 2) { // Settings Info
            String sen="---", res="---", qlt="---";
            for(int i=0; i<pcnt[2]; i++) {
                if(pdata[2][i].lbl == "SENS") sen = pdata[2][i].val;
                if(pdata[2][i].lbl == "RES") res = pdata[2][i].val;
                if(pdata[2][i].lbl == "QUAL") qlt = pdata[2][i].val;
            }
            drText(8, y+36, "Sensor:", CDM, CINF, 1, 0); drText(64, y+36, sen, CWT, CINF, 1, 0);
            drText(8, y+56, "Res:", CDM, CINF, 1, 0); drText(64, y+56, res, CWT, CINF, 1, 0);
            drText(8, y+76, "Quality:", CDM, CINF, 1, 0); drText(64, y+76, qlt, CWT, CINF, 1, 0);
            drText(8, y+110, "Control: Cam Config", CACC, CINF, 1, 0);
        } else {
            drText(8, y+36, "IP:", CDM, CINF, 1, 0); drText(42, y+36, ds[2].ip.length()?ds[2].ip:"---", CWT, CINF, 1, 0);
            drText(8, y+56, "RSSI:", CDM, CINF, 1, 0); drText(56, y+56, String(ds[2].rssi)+"dBm", CWT, CINF, 1, 0);
            drText(8, y+110, "CAM System Ready", CON, CINF, 1, 0);
        }
    } else {
        drText(8, y+36, "IP:", CDM, CINF, 1, 0); drText(42, y+36, ds[curTab].ip.length()?ds[curTab].ip:"---", CWT, CINF, 1, 0);
        drText(8, y+56, "RSSI:", CDM, CINF, 1, 0); drText(56, y+56, String(ds[curTab].rssi)+"dBm", CWT, CINF, 1, 0);
        drText(8, y+76, "Temp:", CDM, CINF, 1, 0); drText(56, y+76, String(ds[curTab].t,1)+"C", CWT, CINF, 1, 0);
        tft.drawFastHLine(6,y+100,INFO_W-12,CSEP);
        drText(8, y+114, "Server:", CDM, CINF, 1, 0); drText(8, y+132, server_ip.length()?server_ip:"---", CACC, CINF, 1, 0);
        drSig(8,y+150,wok?WiFi.RSSI():-100);
    }
}
void drRBtns(){
    int x=INFO_W,y=CONT_Y;
    tft.fillRect(x,y,RBTN_W,CONT_H,CB);
    int cnt=0; ABt* bArr = NULL;
    if(xSemaphoreTake(xMutex, portMAX_DELAY)) {
        bArr = getButtons(curTab, subPage[curTab], cnt);
        if(cnt==0 || bArr==NULL) { xSemaphoreGive(xMutex); return; }
        int bh=CONT_H/cnt;
        for(int i=0;i<cnt;i++){
            int by=y+i*bh;
            tft.fillRoundRect(x+3,by+3,RBTN_W-6,bh-6,5,bArr[i].col);
            tft.drawRoundRect(x+2,by+2,RBTN_W-4,bh-4,6,CSEP);
            drText(x+RBTN_W/2, by+bh/2, bArr[i].lbl, CWT, bArr[i].col, 1, 3);
        }
        xSemaphoreGive(xMutex);
    }
}
void drBot(){
    int y=BOT_Y, bw=42; 
    tft.fillRect(0,y,SCR_W,76,CPNL);
    tft.drawFastHLine(0,y,SCR_W,CSEP);
    char pb[16]; snprintf(pb,16,"PAGE %d/%d", subPage[curTab]+1, maxPage[curTab]);
    drText(8, y+25, pb, CWT, CPNL, 1, 0);
    drText(8, y+45, TNAMES[curTab], CACC, CPNL, 1, 0);
    
    // 제어 보드 모드 표시
    String modeStr = "Offline";
    if (control_mode == "A") modeStr = "A(Robot X-Z)";
    else if (control_mode == "N") modeStr = "N(Robot Y)";
    else if (control_mode == "B") modeStr = "B(Drive)";
    drText(8, y+65, "CTRL: " + modeStr, 0xFFE0, CPNL, 1, 0); // Yellowish color for visibility

    int btnX = SCR_W - bw*2 - 4;
    uint16_t bColL = (subPage[curTab] > 0) ? CACC : CBTN;
    tft.fillRoundRect(btnX, y+16, bw-4, 44, 4, bColL);
    tft.drawRoundRect(btnX, y+16, bw-4, 44, 4, CSEP);
    drText(btnX+(bw-4)/2, y+38, "<", CWT, bColL, 2, 3);
    uint16_t bColR = (subPage[curTab] < maxPage[curTab]-1) ? CACC : CBTN;
    tft.fillRoundRect(btnX+bw, y+16, bw-4, 44, 4, bColR);
    tft.drawRoundRect(btnX+bw, y+16, bw-4, 44, 4, CSEP);
    drText(btnX+bw+(bw-4)/2, y+38, ">", CWT, bColR, 2, 3);
}
void drApprovalPopup() {
    tft.fillRect(10, 60, 220, 200, CHD);
    tft.drawRect(10, 60, 220, 200, CACC);
    drText(120, 80, "AI Command", CWT, CHD, 2, 3);
    drText(120, 120, pending_action_desc.substring(0, 20), CACC, CHD, 1, 3);
    drText(120, 140, "Approve?", CWT, CHD, 2, 3);
    
    // Accept button
    tft.fillRoundRect(20, 180, 80, 60, 5, CON);
    drText(60, 210, "OK", CWT, CON, 2, 3);
    
    // Reject button
    tft.fillRoundRect(140, 180, 80, 60, 5, COFF);
    drText(180, 210, "NO", CWT, COFF, 2, 3);
}

void redraw(bool full){
    if(full)tft.fillScreen(CB);
    if(showApprovalPopup) {
        drApprovalPopup();
    } else {
        drHdr();drTabs();drSub();drInfo();drRBtns();drBot();
    }
}

void onTab(int sx){
    int tw=SCR_W/TMAX,nt=sx/tw;
    if(nt>=0&&nt<TMAX&&nt!=curTab){curTab=nt;subPage[nt]=0;redraw(true);}
}
void onBot(int sx, int sy){
    int y = BOT_Y;
    if(sy < y + 10 || sy > y + 66) return;
    int bw = 42;
    int btnX = SCR_W - bw*2 - 4;
    if (sx >= btnX && sx < btnX + bw - 4) {
        tft.fillRoundRect(btnX, y+16, bw-4, 44, 4, CON);
        drText(btnX+(bw-4)/2, y+38, "<", CWT, CON, 2, 3);
        delay(50);
        if(subPage[curTab] > 0) { subPage[curTab]--; redraw(false); }
        else { drBot(); }
    } else if (sx >= btnX + bw && sx <= SCR_W - 4) {
        tft.fillRoundRect(btnX+bw, y+16, bw-4, 44, 4, CON);
        drText(btnX+bw+(bw-4)/2, y+38, ">", CWT, CON, 2, 3);
        delay(50);
        if(subPage[curTab] < maxPage[curTab]-1) { subPage[curTab]++; redraw(false); }
        else { drBot(); }
    }
}
void onContent(int sx,int sy){
    if(sx<INFO_W)return;
    int cnt=0; ABt* bArr = getButtons(curTab, subPage[curTab], cnt);
    if(!cnt)return;
    int bh=CONT_H/cnt,ri=(sy-CONT_Y)/bh;
    if(ri<0||ri>=cnt)return;
    if (curTab == 0 && subPage[curTab] == 0) {
        if (ri == 0) hb_interval_idx = (hb_interval_idx + 1) % 4;
        else if (ri == 1) i2c_speed_idx = (i2c_speed_idx + 1) % 3;
    } else if (curTab == 0 && subPage[curTab] == 3) {
        if (ri == 0) { is_pid_mode = !is_pid_mode; redraw(false); }
        else if (ri == 1) { if(is_pid_mode) target_tof = min(175, target_tof + 5); else z_step += 10; redraw(false); }
        else if (ri == 2) { if(is_pid_mode) target_tof = max(145, target_tof - 5); else z_step -= 10; redraw(false); }
    } else if (curTab == 1 && subPage[1] == 3) {
        // Optimistic Update for Robot Speed
        int delta = (ri % 2 == 0) ? (ri < 2 ? 10 : 50) : (ri < 2 ? -10 : -50);
        String lbl = (ri < 2) ? "X-Spd" : (ri < 4 ? "Y-Spd" : "Z-Spd");
        for(int i=0; i<pcnt[1]; i++) {
            if(pdata[1][i].lbl == lbl) {
                int curVal = pdata[1][i].val.toInt();
                pdata[1][i].val = String(max(100, curVal + delta));
                break;
            }
        }
        redraw(false);
    } else if (curTab == 2 && subPage[2] == 1) {
        if (ri == 1) {
            showCamImage = !showCamImage;
            redraw(true);
        }
    } else if (curTab == 2 && subPage[2] == 2) {
        // Optimistic UI for Cam Settings
        if (ri == 0) { // Sensor cycle
            const char* sens[] = {"OV2640", "OV3660", "OV5640"};
            String cur = "---";
            for(int i=0; i<pcnt[2]; i++) if(pdata[2][i].lbl=="SENS") cur = pdata[2][i].val;
            for(int j=0; j<3; j++) {
                if(cur == sens[j]) { 
                    for(int i=0; i<pcnt[2]; i++) if(pdata[2][i].lbl=="SENS") pdata[2][i].val = sens[(j+1)%3];
                    break;
                }
            }
        } else if (ri == 1) { // Resolution cycle
            const char* ress[] = {"VGA", "SVGA", "XGA", "SXGA", "UXGA"};
            String cur = "---";
            for(int i=0; i<pcnt[2]; i++) if(pdata[2][i].lbl=="RES") cur = pdata[2][i].val;
            for(int j=0; j<5; j++) {
                if(cur == ress[j]) { 
                    for(int i=0; i<pcnt[2]; i++) if(pdata[2][i].lbl=="RES") pdata[2][i].val = ress[(j+1)%5];
                    break;
                }
            }
        } else if (ri == 2) { // Quality cycle
            int qlts[] = {5, 10, 20, 40};
            int cur = 0;
            for(int i=0; i<pcnt[2]; i++) if(pdata[2][i].lbl=="QUAL") cur = pdata[2][i].val.toInt();
            for(int j=0; j<4; j++) {
                if(cur == qlts[j]) { 
                    for(int i=0; i<pcnt[2]; i++) if(pdata[2][i].lbl=="QUAL") pdata[2][i].val = String(qlts[(j+1)%4]);
                    break;
                }
            }
        }
        redraw(false);
    }
    bArr = getButtons(curTab, subPage[curTab], cnt);
    int bx=INFO_W+3,by=CONT_Y+ri*bh+3,bw=RBTN_W-6,bh2=bh-6;
    tft.fillRoundRect(bx,by,bw,bh2,5,CON);
    drText(bx+bw/2, by+bh2/2, bArr[ri].lbl, CWT, CON, 1, 3);
    delay(50);
    if(wok&&WiFi.status()==WL_CONNECTED){
        HTTPClient h;
        String urls[]={"http://"+server_ip+":5000/api/drivemcu/command",
                        "http://"+server_ip+":5000/api/robot3axis/command",
                        "http://"+server_ip+":5000/api/imagesendermcu/command",
                        "http://"+server_ip+":5000/api/soundmcu/command"};
        h.begin(urls[curTab]);h.addHeader("Content-Type","application/json");
        JsonDocument d;
        if(curTab == 0 && subPage[curTab] == 3) {
            if(ri==0) d["command"]=is_pid_mode?"mzm1":"mzm0";
            else if(ri==1) d["command"]=is_pid_mode?("mzt"+String(target_tof)):("mzs+"+String(z_step));
            else if(ri==2) d["command"]=is_pid_mode?("mzt"+String(target_tof)):("mzs-"+String(z_step));
            else d["command"]=bArr[ri].cmd;
        } else { d["command"]=bArr[ri].cmd; }
        String pl; serializeJson(d,pl);
        h.setTimeout(300); h.POST(pl); h.end();
    }
    redraw(false);
}
void procTouch(){
    static unsigned long lt=0;
    if(touch.touched()) {
        CYD28_TS_Point p=touch.getPointScaled();
        if(millis()-lt<250)return;
        lt=millis();
        int sx=p.x; int sy=p.y;
        tft.fillCircle(sx, sy, 8, CACC); 
        delay(30);
        
        if (showApprovalPopup) {
            if (sy >= 180 && sy <= 240) {
                if (sx >= 20 && sx <= 100) {
                    sendApproval(pending_action_id, true);
                    showApprovalPopup = false;
                    redraw(true);
                } else if (sx >= 140 && sx <= 220) {
                    sendApproval(pending_action_id, false);
                    showApprovalPopup = false;
                    redraw(true);
                }
            }
            return;
        }
        
        if(sy<HDR_H)return;
        if(sy<HDR_H+TAB_H){onTab(sx);return;}
        if(sy<CONT_Y){redraw(true);return;}
        if(sy<BOT_Y){onContent(sx,sy);return;}
        onBot(sx,sy);
    }
}

void doWifi(){
    int cx=SCR_W/2,cy=140;
    tft.fillScreen(CB);
    tft.fillRect(0,0,SCR_W,32,CHD);
    drText(SCR_W/2, 16, "Network Setup", CWT, CHD, 2, 3);
    prefs.begin("hmi-cfg",true);
    ssid_active=prefs.getString("ssid","");
    pass_active=prefs.getString("password","");
    server_ip_pref=prefs.getString("server_ip","");
    prefs.end();
    const char* ssids[]={ssid_active.c_str(),WIFI_SSID_1,WIFI_SSID_2,WIFI_SSID_3};
    const char* srvs[]={server_ip_pref.c_str(),WIFI_SERVER_1,WIFI_SERVER_2,WIFI_SERVER_3};
    WiFi.mode(WIFI_STA);
    bool ok=false;
    for(int i=0;i<4;i++){
        if(strlen(ssids[i])==0)continue;
        tft.fillRoundRect(10,48,SCR_W-20,66,8,CHD);
        tft.drawRoundRect(10,48,SCR_W-20,66,8,CACC);
        drText(20, 58, "SSID:", CDM, CHD, 1, 0);
        drText(76, 58, String(ssids[i]).substring(0,18), CACC, CHD, 1, 0);
        drText(20, 82, "Server:", CDM, CHD, 1, 0);
        drText(76, 82, strlen(srvs[i])?srvs[i]:"Searching...", CON, CHD, 1, 0);
        if(strlen(srvs[i]))server_ip=srvs[i];
        const char* passes[]={pass_active.c_str(),WIFI_PASS_1,WIFI_PASS_2,WIFI_PASS_3};
        WiFi.begin(ssids[i],passes[i]);
        unsigned long t0=millis(); int sp2=0;
        while(WiFi.status()!=WL_CONNECTED&&millis()-t0<5000){
            int ang=(sp2*25)%360;
            tft.drawSmoothArc(cx,cy,28,20,ang,(ang+110)%360,CACC,CB,true);
            sp2++;
            char tb[12];snprintf(tb,12,"%.1fs",(millis()-t0)/1000.0f);
            drText(cx, cy, "          ", CB, CB, 1, 3);
            drText(cx, cy, tb, CWT, CB, 1, 3);
            int dx=20+(int)((millis()-t0)*(SCR_W-40)/5000);
            tft.fillRect(20,200,SCR_W-40,4,CSEP);tft.fillRect(20,200,dx-20,4,CACC);
            delay(50);yield();
        }
        if(WiFi.status()==WL_CONNECTED){ok=true;wok=true;break;}
        WiFi.disconnect();
    }
    tft.fillScreen(CB);
    if(ok){
        tft.fillCircle(cx,110,30,CON);
        drText(cx, 110, "OK", CWT, CON, 3, 3);
        drText(cx, 165, "Connected!", CON, CB, 2, 3);
        configTime(9*3600,0,"pool.ntp.org");
        delay(1200);
    }else{
        tft.fillCircle(cx,110,30,COFF);
        drText(cx, 110, "!", CWT, COFF, 4, 3);
        drText(cx, 165, "Failed", COFF, CB, 2, 3);
        delay(2000);
        BluetoothSerial SerialBT;
        SerialBT.begin("ARD_HMI_Config");
        while(true){
            if(touch.touched()) break;
            if(SerialBT.available()){
                String line = SerialBT.readStringUntil('\n');
                line.trim();
                if(line.startsWith("WIFI,")){
                    int p1 = line.indexOf(',', 5);
                    if(p1 > 0){
                        String newSsid = line.substring(5, p1);
                        int p2 = line.indexOf(',', p1+1);
                        if(p2 > 0){
                            String newPass = line.substring(p1+1, p2);
                            String newIp = line.substring(p2+1);
                            prefs.begin("hmi-cfg", false);
                            prefs.putString("ssid", newSsid);
                            prefs.putString("password", newPass);
                            prefs.putString("server_ip", newIp);
                            prefs.end();
                            ESP.restart();
                        }
                    }
                }
            }
            delay(10);
        }
    }
}

void setup(){
    Serial.begin(115200);delay(300);
    xMutex = xSemaphoreCreateMutex();
    pinMode(BL_PIN,OUTPUT);digitalWrite(BL_PIN,HIGH);
    tft.init();tft.setRotation(0);tft.invertDisplay(true);
    tft.fillScreen(CB);
    TJpgDec.setJpgScale(1);TJpgDec.setCallback(tft_cb);
    touch.begin();touch.setRotation(0);
    drawSplash();
    st=ST_CONN;doWifi();
    st=ST_MAIN;redraw(true);
    xTaskCreatePinnedToCore(TaskNetwork, "TaskNetwork", 8192, NULL, 1, NULL, 0);
}
void loop(){
    procTouch();
    unsigned long now=millis();
    if(now-lclk>=1000){lclk=now;updClk();if(st==ST_MAIN && !showApprovalPopup)drHdr();}
    static unsigned long ldraw=0;
    if(now-ldraw>=3000){
        ldraw=now;
        if(st==ST_MAIN){
            if(xSemaphoreTake(xMutex, 0)) {
                if(!showApprovalPopup) { drInfo(); drBot(); drHdr(); }
                xSemaphoreGive(xMutex);
            }
        }
    }
    delay(2);
}
