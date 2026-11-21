// ==========================================================================================
// PUSH-UP COUNTER — LVGL v8 + TFT_eSPI + ESP32 CYD (ESP32-2432S028R)
// (เลขเวอร์ชันดูได้ที่ FW_VERSION ด้านล่าง — แก้ที่เดียว อัปเดตทุกที่อัตโนมัติ)
//
// *** ใช้ NVS (Non-Volatile Storage) แทน AT24C256 EEPROM ***
//   - ไม่ต้องใช้ฮาร์ดแวร์ EEPROM เพิ่มเติม
//   - เก็บข้อมูลใน Flash ของ ESP32 โดยตรง
//   - อ่าน/เขียนเร็วกว่า และทนทานกว่า
// ==========================================================================================
// Libraries ที่ต้องติดตั้ง (Library Manager):
//   - lvgl                  v8.x  (ไม่ใช่ v9)
//   - TFT_eSPI              latest
//   - XPT2046_Touchscreen   latest
//   - Adafruit_VL53L0X      latest
//
// Hardware เพิ่มเติม:
//   - ไม่ต้องใช้ AT24C256 EEPROM แล้ว! ใช้ NVS ใน Flash ESP32 แทน
//
// *** IMPORTANT — Arduino IDE Settings ***
//   Board      : ESP32 Dev Module
//   PSRAM      : Disabled
//
// ไฟล์เพิ่มเติม:
//   - lv_conf.h  → วางใน Arduino/libraries/lv_conf.h
//                  (ข้างๆ โฟลเดอร์ lvgl/ ไม่ใช่ข้างใน)
//
// User_Setup.h ของ TFT_eSPI:
//   #define ILI9341_DRIVER
//   #define TFT_MISO 12
//   #define TFT_MOSI 13
//   #define TFT_SCLK 14
//   #define TFT_CS   15
//   #define TFT_DC    2
//   #define TFT_RST  -1
//   #define SPI_FREQUENCY  40000000
// ==========================================================================================
#define FW_VERSION "4.3.0-NVS"
#define FW_BUILD_DATE "17/07/2026"
#define FW_AUTHOR "Tewan Isaravsut"
// ==========================================================================================
// แก้เลขเวอร์ชันที่นี่ที่เดียว จะอัปเดตทั้ง Serial log และหน้า splash screen อัตโนมัติ
// ==========================================================================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include "fonts_thai.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Adafruit_VL53L0X.h>

// ==========================================================================================
// NVS (Non-Volatile Storage) — ใช้เก็บ Settings และ History แทน EEPROM
// ==========================================================================================
#include <Preferences.h>
Preferences preferences;

// ==========================================================================================
// Pin Definitions (CYD — ไม่เปลี่ยน)
// ==========================================================================================
#define I2C_SDA_PIN   27
#define I2C_SCL_PIN   22
#define PIN_BUZZER    26
#define PIN_TFT_BL    21
#define PIN_MODE_SWITCH 35  // ต่อกับ GND = Online mode, Float/HIGH = Offline mode

// ==========================================================================================
// Online Mode (WiFi + Web Dashboard)
// ==========================================================================================
#define WIFI_AP_NAME "PushupCounter-Setup"
AsyncWebServer webServer(80);
DNSServer dns;
bool isOnlineMode = false;
static lv_obj_t *mainScr = NULL;

// Touch SPI (HSPI)
#define TOUCH_CS      33
#define TOUCH_MOSI    32
#define TOUCH_MISO    39
#define TOUCH_SCK     25

// Touch calibration — ค่าไม่เหมือนกันระหว่างแนวตั้ง/แนวนอน (ตั้งค่าจริงใน setup() ตาม orientation)
#define TOUCH_X_MIN_LAND  200
#define TOUCH_X_MAX_LAND   3500 //เดิม 3600
#define TOUCH_Y_MIN_LAND   250   //เดิม 280
#define TOUCH_Y_MAX_LAND   3600

#define TOUCH_X_MIN_PORT   481
#define TOUCH_X_MAX_PORT   3910
#define TOUCH_Y_MIN_PORT   157
#define TOUCH_Y_MAX_PORT   3533

static int TOUCH_X_MIN = TOUCH_X_MIN_LAND;
static int TOUCH_X_MAX = TOUCH_X_MAX_LAND;
static int TOUCH_Y_MIN = TOUCH_Y_MIN_LAND;
static int TOUCH_Y_MAX = TOUCH_Y_MAX_LAND;

// Screen — ขนาดจริงกำหนดตอน runtime ตาม orientation (ดู SCR_W/SCR_H ด้านล่าง)
#define SCR_W_LANDSCAPE 320
#define SCR_H_LANDSCAPE 240
#define SCR_W_PORTRAIT  240
#define SCR_H_PORTRAIT  320
#define SCR_DIM_MAX     320   // ค่าที่ใหญ่สุดของสองแนว ใช้กำหนดขนาด buffer แบบ static

static int SCR_W = SCR_W_LANDSCAPE;
static int SCR_H = SCR_H_LANDSCAPE;

// ==========================================================================================
// Orientation (แนวจอ)
// ==========================================================================================
enum { ORIENT_PORTRAIT = 0, ORIENT_LANDSCAPE = 1 };
static int bootOrientation = ORIENT_LANDSCAPE; // แนวจอที่ใช้ตอน build UI จริง (เปลี่ยนได้เมื่อรีสตาร์ทเท่านั้น)

// ==========================================================================================
// Forward Declarations
// ==========================================================================================
void screenWake();
void screenSleep();
void buzzUpdate();
void setupWiFiDashboard();
void wifiResetAndReboot();
String getSystemUptimeString();
String getWorkoutTimeStr();
void showQROverlay();
void hideQROverlay();
void showHistoryOverlay();
void hideHistoryOverlay();

static lv_obj_t *qrOverlay = NULL;
static lv_obj_t *historyOverlay  = NULL;
static lv_obj_t *hist_lbl_pushups  = NULL;
static lv_obj_t *hist_lbl_calories = NULL;
static lv_obj_t *hist_lbl_sessions = NULL;
void workoutTimerStart();
void workoutTimerPause();
void workoutTimerReset();
unsigned long workoutTimerGetMs();
void updateCalories();
bool nvsCheck();
void saveSettings();
void loadSettings();
void saveHistory();
void loadHistory();
void clearHistory();
void ui_update_status();
void ui_update_workout_time();
void ui_update_count();
void ui_update_info();
void ui_update_all();
void ui_build();
void ui_build_landscape();
void ui_build_portrait();
void showQROverlay_landscape();
void showQROverlay_portrait();
void showHistoryOverlay_landscape();
void showHistoryOverlay_portrait();
void showSettingsScreen();
void hideSettingsScreen();
void applySaveSettings();

// ==========================================================================================
// LVGL Display Buffer
// ==========================================================================================
static const uint16_t BUF_LINES = 10;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCR_DIM_MAX * BUF_LINES];

// ==========================================================================================
// Objects
// ==========================================================================================
TFT_eSPI        tft;
SPIClass        mySPI(HSPI);
XPT2046_Touchscreen ts(TOUCH_CS);
Adafruit_VL53L0X lox;

bool nvsAvailable = false;
bool sensorOK = false;

// ==========================================================================================
// Application Data
// ==========================================================================================
struct AppData {
    // ---- ข้อมูลการนับ push-up ----
    int   count          = 0;      // จำนวนในเซ็ตปัจจุบัน
    int   totalCount     = 0;      // จำนวนรวมทั้งหมด
    int   targetPerSet   = 10;     // เป้าหมายต่อเซ็ต
    int   currentSet     = 1;      // เซ็ตปัจจุบัน
    int   totalSets      = 1;      // จำนวนเซ็ตทั้งหมด
    
    // ---- ค่า Threshold ของ Sensor ----
    int   threshDown     = 15;     // ระยะทางที่ตรวจจับว่าก้มลง (cm)
    int   threshUp       = 25;     // ระยะทางที่ตรวจจับว่าดันขึ้น (cm)
    
    // ---- ข้อมูลร่างกายและ Calories ----
    float bodyWeight     = 70.0f;  // น้ำหนักตัว (kg)
    float caloriesBurned = 0.0f;   // แคลอรี่ที่เผาผลาญ
    
    // ---- การตั้งค่าต่างๆ ----
    int   restDurationSec = 30;    // ระยะเวลาพักระหว่างเซ็ต (วินาที)
    int   screenTimeout  = 5;      // ระยะเวลาก่อนจอหลับ (นาที, 0 = ไม่ดับ)
    int   calMode        = 0;      // 0 = MET-based, 1 = Simple (per-rep)
    int   orientation    = ORIENT_LANDSCAPE; // 0 = VERTICAL, 1 = HORIZONTAL (ใช้จริงหลังรีสตาร์ท)
    
    // ---- Workout Timer ----
    unsigned long workoutStartTime  = 0;  // millis() ตอนเริ่มเซ็ตแรก
    unsigned long workoutElapsedMs  = 0;  // เวลารวมที่นับไปแล้ว (สะสมข้าม pause)
    bool  workoutTimerActive = false;     // กำลังนับเวลาอยู่หรือไม่
    
    // ---- สถานะการออกกำลังกาย ----
    bool  isDown         = false;  // กำลังก้มลงอยู่หรือไม่
    bool  isRunning      = false;  // กำลังออกกำลังกายอยู่หรือไม่
    bool  isResting      = false;  // กำลังพักอยู่หรือไม่
    bool  isComplete     = false;  // ทำครบทุกเซ็ตแล้วหรือยัง
    
    // ---- เวลา ----
    unsigned long restStartTime  = 0;     // เวลาเริ่มพัก
    unsigned long lastCountTime  = 0;     // เวลานับครั้งล่าสุด
    int   settingMode    = 0;             // โหมดการตั้งค่า 0-11
    int   lastDistance   = 0;             // ระยะทางล่าสุดจาก Sensor
    
    // =============================================================
    // ตัวแปรเพิ่มเติมสำหรับแก้ไขปัญหา
    // =============================================================
    unsigned long downStartTime = 0;      // เวลาที่เริ่มก้มลง (ใช้ Auto Reset isDown)
    unsigned long lastMovementTime = 0;   // เวลาเคลื่อนไหวล่าสุด (นับ push-up สำเร็จ)
    bool  isTimerPaused = false;          // สถานะหยุด Timer MET (เมื่อไม่เคลื่อนไหว)
    
    // =============================================================
    // ตัวแปรสำหรับกรองสัญญาณรบกวนของ Sensor (NEW!)
    // =============================================================
    int   downCounter = 0;          // นับจำนวนครั้งที่อ่านได้ < T-Down ติดต่อกัน
    int   upCounter = 0;            // นับจำนวนครั้งที่อ่านได้ > T-Up ติดต่อกัน
    int   minDownSamples = 3;       // ต้องอ่านค่า < T-Down กี่ครั้งติดต่อกัน (default 3)
    int   minUpSamples = 2;         // ต้องอ่านค่า > T-Up กี่ครั้งติดต่อกัน (default 2)
    int   minDownTimeMs = 150;      // ระยะเวลาขั้นต่ำที่ต้องอยู่ในท่าก้ม (ms)
};
AppData app;

// ==========================================================================================
// NVS (Preferences) Keys
// ==========================================================================================
#define NVS_NAMESPACE "pushup"

// Settings keys
#define KEY_MAGIC       "magic"
#define KEY_VERSION     "version"
#define KEY_TARGET      "target"
#define KEY_TOTAL_SETS  "totalsets"
#define KEY_REST        "rest"
#define KEY_WEIGHT      "weight"
#define KEY_THRESH_DOWN "tdown"
#define KEY_THRESH_UP   "tup"
#define KEY_SCREEN_TO   "screento"
#define KEY_CAL_MODE    "calmode"
#define KEY_ORIENT      "orient"
#define KEY_MIN_DOWN    "mindown"

// History keys
#define KEY_HIST_MAGIC   "histmagic"
#define KEY_HIST_PUSH    "histpush"
#define KEY_HIST_CAL     "histcal"
#define KEY_HIST_SESS    "histsess"
#define KEY_HIST_CHECK   "histchk"

#define NVS_MAGIC       0xA5
#define NVS_VERSION     2
#define NVS_HIST_MAGIC  0xB6

// ==========================================================================================
// Lifetime History — บันทึกสถิติสะสมตลอดชีวิต
// ==========================================================================================
static uint32_t lifetimePushups  = 0;
static float    lifetimeCalories = 0.0f;
static uint32_t lifetimeSessions = 0;

static bool screenOn = true;
static unsigned long lastTouchTime = 0;
static unsigned long blockTouchUntil = 0; // block touch ช่วง transition ระหว่าง screen

// ---- สำคัญ: ใช้ flag นี้แทนการเรียก ui_update_all()/lv_* โดยตรงจาก AsyncWebServer route
// handler เพราะ ESPAsyncWebServer ทำงานอยู่คนละ FreeRTOS task กับ loop() ที่รัน
// lv_task_handler() อยู่ — LVGL ไม่ thread-safe ถ้าเรียกจากคนละ task พร้อมกันจะทำให้
// เครื่อง crash/reboot แบบสุ่ม (เป็นบางครั้งไม่ใช่ทุกครั้ง ขึ้นกับจังหวะ) — route handler
// จึงแค่ตั้ง flag นี้ไว้ แล้วให้ loop() เป็นคนเรียก ui_update_all() แทนอย่างปลอดภัย
static volatile bool webUIUpdatePending = false;

// ==========================================================================================
// LVGL Widgets
// ==========================================================================================
static lv_obj_t *lbl_status;
static lv_obj_t *lbl_set_indicator;
static lv_obj_t *lbl_dist;
static lv_obj_t *lbl_workout_time;
static lv_obj_t *lbl_total_count;
static lv_obj_t *bar_progress;
static lv_obj_t *lbl_rep;
static lv_obj_t *lbl_v_target;
static lv_obj_t *lbl_v_set;
static lv_obj_t *lbl_v_rest;
static lv_obj_t *lbl_v_weight;
static lv_obj_t *lbl_v_cal;
static lv_obj_t *lbl_v_dist_info;
static lv_obj_t *lbl_v_tdown;
static lv_obj_t *lbl_v_tup;
static lv_obj_t *lbl_v_screen;
static lv_obj_t *btn_start;
static lv_obj_t *lbl_btn_start;
static lv_obj_t *btn_reset;
static lv_obj_t *btn_settings;

// ---- Settings screen (หน้าตั้งค่าใหม่ — แทนที่ MODE/ENTER/+/- เดิม) ----
static lv_obj_t *settingsScr = NULL;
static lv_obj_t *lbl_set_target, *lbl_set_sets, *lbl_set_rest, *lbl_set_weight, *lbl_set_tdown, *lbl_set_tup, *lbl_set_screen;
static lv_obj_t *lbl_sw_calmode;
static lv_obj_t *lbl_sw_rotate;
static lv_obj_t *lbl_set_mindown;  // NEW!
static lv_obj_t *btn_save_set;
static unsigned long savePressStart = 0;

// ==========================================================================================
// LVGL HAL — Display flush callback
// ==========================================================================================
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)color_p, w * h, false);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

// ==========================================================================================
// LVGL HAL — Touch read callback
// ==========================================================================================
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    // Block touch ระหว่าง screen transition เพื่อป้องกัน phantom touch
    if (millis() < blockTouchUntil) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        int x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCR_W);
        int y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCR_H);
        x = constrain(x, 0, SCR_W - 1);
        y = constrain(y, 0, SCR_H - 1);
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
        screenWake();
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ==========================================================================================
// Buzzer — Non-blocking state machine
// ==========================================================================================
#define BUZZ_QUEUE_MAX 8

struct BuzzStep {
    int freq;
    int durationMs;
};

static BuzzStep buzzQueue[BUZZ_QUEUE_MAX];
static int buzzQueueLen = 0;
static int buzzQueueIndex = 0;
static unsigned long buzzStepStartTime = 0;
static bool buzzActive = false;

void buzzQueuePush(int freq, int durationMs) {
    if (buzzQueueLen < BUZZ_QUEUE_MAX) {
        buzzQueue[buzzQueueLen].freq = freq;
        buzzQueue[buzzQueueLen].durationMs = durationMs;
        buzzQueueLen++;
    }
}

void buzzStart() {
    buzzQueueIndex = 0;
    buzzActive = true;
    buzzStepStartTime = millis();
    if (buzzQueueLen > 0) {
        ledcWriteTone(0, buzzQueue[0].freq);
    }
}

void buzzUpdate() {
    if (!buzzActive) return;
    unsigned long elapsed = millis() - buzzStepStartTime;
    if (elapsed >= (unsigned long)buzzQueue[buzzQueueIndex].durationMs) {
        buzzQueueIndex++;
        if (buzzQueueIndex >= buzzQueueLen) {
            ledcWriteTone(0, 0);
            buzzActive = false;
            buzzQueueLen = 0;
            return;
        }
        buzzStepStartTime = millis();
        ledcWriteTone(0, buzzQueue[buzzQueueIndex].freq);
    }
}

// ---- เสียงต่างๆ ----
void beepClick()   { buzzQueueLen = 0; buzzQueuePush(2200, 20);  buzzStart(); }
void beepConfirm() { buzzQueueLen = 0; buzzQueuePush(2000, 100); buzzStart(); }
void beepIncrease(){ buzzQueueLen = 0; buzzQueuePush(1800, 35);  buzzStart(); }
void beepDecrease(){ buzzQueueLen = 0; buzzQueuePush(1300, 30);  buzzStart(); }
void beepComplete() {
    buzzQueueLen = 0;
    buzzQueuePush(2000, 200);
    buzzQueuePush(0,    60);
    buzzQueuePush(2500, 200);
    buzzQueuePush(0,    60);
    buzzQueuePush(3000, 350);
    buzzStart();
}
void beepClearHistory() {
    buzzQueueLen = 0;
    buzzQueuePush(500, 200);
    buzzQueuePush(0, 60);
    buzzQueuePush(500, 200);
    buzzQueuePush(0, 60);
    buzzQueuePush(800, 300);
    buzzStart();
}

// ==========================================================================================
// Backlight control
// ==========================================================================================
void screenWake() {
    if (!screenOn) {
        digitalWrite(PIN_TFT_BL, HIGH);
        screenOn = true;
    }
    lastTouchTime = millis();
}

void screenSleep() {
    if (screenOn) {
        digitalWrite(PIN_TFT_BL, LOW);
        screenOn = false;
    }
}

// ==========================================================================================
// Workout Timer
// ==========================================================================================
void workoutTimerStart() {
    app.workoutStartTime = millis();
    app.workoutTimerActive = true;
}

void workoutTimerPause() {
    if (app.workoutTimerActive) {
        app.workoutElapsedMs += millis() - app.workoutStartTime;
        app.workoutTimerActive = false;
    }
}

void workoutTimerReset() {
    app.workoutElapsedMs = 0;
    app.workoutStartTime = 0;
    app.workoutTimerActive = false;
    app.isTimerPaused = false;
}

unsigned long workoutTimerGetMs() {
    if (app.workoutTimerActive) {
        return app.workoutElapsedMs + (millis() - app.workoutStartTime);
    }
    return app.workoutElapsedMs;
}

// ==========================================================================================
// Calorie Calculation
// ==========================================================================================
#define PUSHUP_MET 8.0f
#define CAL_PER_REP_AT_70KG 0.45f

void updateCalories() {
    if (app.calMode == 1) {
        // Simple mode — อิงจำนวนครั้งรวม
        app.caloriesBurned = app.totalCount * CAL_PER_REP_AT_70KG * (app.bodyWeight / 70.0f);
    } else {
        // MET-based mode — นับเฉพาะเวลาที่ออกกำลังกายจริง (ไม่นับเวลาหยุด)
        float hours;
        if (app.workoutTimerActive && !app.isTimerPaused) {
            hours = workoutTimerGetMs() / 1000.0f / 3600.0f;
        } else {
            hours = app.workoutElapsedMs / 1000.0f / 3600.0f;
        }
        app.caloriesBurned = PUSHUP_MET * app.bodyWeight * hours;
    }
}

// ==========================================================================================
// NVS (Preferences) Driver
// ==========================================================================================
bool nvsCheck() {
    // Preferences ทำงานได้ตลอด ไม่ต้องตรวจสอบฮาร์ดแวร์
    return true;
}

// ==========================================================================================
// Settings persistence (ใช้ NVS)
// ==========================================================================================
void saveSettings() {
    if (!nvsAvailable) {
        Serial.println("NVS not available — settings not saved!");
        return;
    }
    
    preferences.begin(NVS_NAMESPACE, false);
    
    preferences.putUChar(KEY_MAGIC, NVS_MAGIC);
    preferences.putUChar(KEY_VERSION, NVS_VERSION);
    preferences.putInt(KEY_TARGET, app.targetPerSet);
    preferences.putInt(KEY_TOTAL_SETS, app.totalSets);
    preferences.putInt(KEY_REST, app.restDurationSec);
    preferences.putFloat(KEY_WEIGHT, app.bodyWeight);
    preferences.putInt(KEY_THRESH_DOWN, app.threshDown);
    preferences.putInt(KEY_THRESH_UP, app.threshUp);
    preferences.putInt(KEY_SCREEN_TO, app.screenTimeout);
    preferences.putUChar(KEY_CAL_MODE, app.calMode);
    preferences.putUChar(KEY_ORIENT, app.orientation);
    preferences.putInt(KEY_MIN_DOWN, app.minDownTimeMs);
    
    preferences.end();
    Serial.println("Settings saved to NVS.");
}

void loadSettings() {
    if (!nvsAvailable) {
        Serial.println("NVS not available — using defaults.");
        return;
    }
    
    preferences.begin(NVS_NAMESPACE, true);
    
    uint8_t magic = preferences.getUChar(KEY_MAGIC, 0);
    if (magic != NVS_MAGIC) {
        preferences.end();
        Serial.println("NVS empty/uninitialized — using defaults.");
        return;
    }
    
    uint8_t version = preferences.getUChar(KEY_VERSION, 0);
    
    app.targetPerSet    = preferences.getInt(KEY_TARGET, 10);
    app.totalSets       = preferences.getInt(KEY_TOTAL_SETS, 1);
    app.restDurationSec = preferences.getInt(KEY_REST, 30);
    app.bodyWeight      = preferences.getFloat(KEY_WEIGHT, 70.0f);
    app.threshDown      = preferences.getInt(KEY_THRESH_DOWN, 15);
    app.threshUp        = preferences.getInt(KEY_THRESH_UP, 25);
    app.screenTimeout   = preferences.getInt(KEY_SCREEN_TO, 5);
    app.calMode         = preferences.getUChar(KEY_CAL_MODE, 0);
    app.orientation     = preferences.getUChar(KEY_ORIENT, ORIENT_LANDSCAPE);
    
    // NEW! โหลดค่า minDownTimeMs (ถ้าเวอร์ชันเก่าไม่มี ให้ใช้ default)
    if (version >= 2) {
        app.minDownTimeMs = preferences.getInt(KEY_MIN_DOWN, 150);
        if (app.minDownTimeMs < 50) app.minDownTimeMs = 150;
        if (app.minDownTimeMs > 500) app.minDownTimeMs = 150;
    } else {
        app.minDownTimeMs = 150;  // default
    }
    
    preferences.end();
    Serial.println("Settings loaded from NVS.");
}

// ==========================================================================================
// Lifetime History persistence (ใช้ NVS)
// ==========================================================================================
void saveHistory() {
    if (!nvsAvailable) return;
    
    preferences.begin(NVS_NAMESPACE, false);
    
    preferences.putUChar(KEY_HIST_MAGIC, NVS_HIST_MAGIC);
    preferences.putUInt(KEY_HIST_PUSH, lifetimePushups);
    preferences.putFloat(KEY_HIST_CAL, lifetimeCalories);
    preferences.putUInt(KEY_HIST_SESS, lifetimeSessions);
    
    // Simple checksum (XOR of all history values)
    uint8_t chk = (uint8_t)((lifetimePushups ^ (uint32_t)(lifetimeCalories * 10) ^ lifetimeSessions) & 0xFF);
    preferences.putUChar(KEY_HIST_CHECK, chk);
    
    preferences.end();
    Serial.println("History saved to NVS.");
}

void loadHistory() {
    if (!nvsAvailable) return;
    
    preferences.begin(NVS_NAMESPACE, true);
    
    uint8_t magic = preferences.getUChar(KEY_HIST_MAGIC, 0);
    if (magic != NVS_HIST_MAGIC) {
        preferences.end();
        Serial.println("History empty — starting fresh.");
        return;
    }
    
    uint32_t savedPushups = preferences.getUInt(KEY_HIST_PUSH, 0);
    float savedCalories = preferences.getFloat(KEY_HIST_CAL, 0.0f);
    uint32_t savedSessions = preferences.getUInt(KEY_HIST_SESS, 0);
    uint8_t savedChecksum = preferences.getUChar(KEY_HIST_CHECK, 0);
    
    // Verify checksum
    uint8_t calcChk = (uint8_t)((savedPushups ^ (uint32_t)(savedCalories * 10) ^ savedSessions) & 0xFF);
    
    if (calcChk != savedChecksum) {
        preferences.end();
        Serial.println("History checksum mismatch — starting fresh.");
        return;
    }
    
    lifetimePushups  = savedPushups;
    lifetimeCalories = savedCalories;
    lifetimeSessions = savedSessions;
    
    preferences.end();
    Serial.printf("History loaded: %u pushups, %.1f kcal, %u sessions\n",
                  lifetimePushups, lifetimeCalories, lifetimeSessions);
}

void clearHistory() {
    lifetimePushups  = 0;
    lifetimeCalories = 0.0f;
    lifetimeSessions = 0;
    saveHistory();
    Serial.println("History cleared.");
}

// ==========================================================================================
// UI Color helpers
// ==========================================================================================
static lv_color_t COL_HEADER_BG  = LV_COLOR_MAKE(0x08, 0x10, 0x40);
static lv_color_t COL_COUNT_BG   = LV_COLOR_MAKE(0x0D, 0x11, 0x17);
static lv_color_t COL_GREEN      = LV_COLOR_MAKE(0x22, 0xC5, 0x5E);
static lv_color_t COL_RED        = LV_COLOR_MAKE(0xEF, 0x44, 0x44);
static lv_color_t COL_YELLOW     = LV_COLOR_MAKE(0xFB, 0xBF, 0x24);
static lv_color_t COL_CYAN       = LV_COLOR_MAKE(0x22, 0xD3, 0xEE);
static lv_color_t COL_ORANGE     = LV_COLOR_MAKE(0xFB, 0x92, 0x3C);
static lv_color_t COL_GREY_DIM   = LV_COLOR_MAKE(0x40, 0x40, 0x40);
static lv_color_t COL_GREY_LABEL = LV_COLOR_MAKE(0x70, 0x70, 0x70);
static lv_color_t COL_VAL_BG     = LV_COLOR_MAKE(0x10, 0x18, 0x40);
static lv_color_t COL_BTN_RESET  = LV_COLOR_MAKE(0x45, 0x0A, 0x0A);
static lv_color_t COL_BTN_MINUS  = LV_COLOR_MAKE(0x7F, 0x1D, 0x1D);
static lv_color_t COL_BTN_PLUS   = LV_COLOR_MAKE(0x14, 0x53, 0x2D);

// =============================================================
// สีของแต่ละโหมดการตั้งค่า (12 โหมด: 0-11)
// =============================================================
#define SETTING_MODE_COUNT 13

static lv_color_t modeColors[SETTING_MODE_COUNT] = {
    LV_COLOR_MAKE(0x00,0x00,0x80),  // 0: TARGET
    LV_COLOR_MAKE(0x50,0x00,0x80),  // 1: SETS
    LV_COLOR_MAKE(0x00,0x55,0x55),  // 2: REST
    LV_COLOR_MAKE(0x80,0x20,0x00),  // 3: WEIGHT
    LV_COLOR_MAKE(0x7F,0x00,0x00),  // 4: T-DOWN
    LV_COLOR_MAKE(0x00,0x60,0x00),  // 5: T-UP
    LV_COLOR_MAKE(0x20,0x20,0x20),  // 6: SCREEN
    LV_COLOR_MAKE(0x60,0x50,0x00),  // 7: CAL-MODE
    LV_COLOR_MAKE(0x00,0x30,0x50),  // 8: HISTORY (ชื่อ)
    LV_COLOR_MAKE(0x00,0x40,0x60),  // 9: HISTORY (แสดงทันที)
    LV_COLOR_MAKE(0x30,0x20,0x70),  // 10: QR-IP (ชื่อ)
    LV_COLOR_MAKE(0x40,0x20,0x80),  // 11: QR-IP (แสดงทันที)
    LV_COLOR_MAKE(0x00,0x70,0x70),  // 12: ROTATE (เลือกแนวจอ)
};

static const char* modeNames[SETTING_MODE_COUNT] = {
    "TARGET",     // 0
    "SETS",       // 1
    "REST",       // 2
    "WEIGHT",     // 3
    "T-DOWN",     // 4
    "T-UP",       // 5
    "SCREEN",     // 6
    "CAL-MODE",   // 7
    "HISTORY",    // 8
    "HISTORY",    // 9 (แสดงทันที)
    "QR-IP",      // 10
    "QR-IP",      // 11 (แสดงทันที)
    "ROTATE",     // 12: เลือกแนวจอ (HORIZONTAL/VERTICAL)
};

// ==========================================================================================
// Button event callbacks
// ==========================================================================================
static void cb_btn_start(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    screenWake();
    if (!screenOn) return;
    hideQROverlay();

    if (!app.isRunning && !app.isComplete && !app.isResting) {
        // เริ่มออกกำลังกายครั้งใหม่
        app.isRunning = true;
        app.isDown = false;
        app.downStartTime = 0;
        app.lastMovementTime = millis();
        app.isTimerPaused = false;
        
        // NEW! รีเซ็ตตัวกรองสัญญาณ
        app.downCounter = 0;
        app.upCounter = 0;
        
        workoutTimerStart();
        beepClick();
    } else if (app.isRunning) {
        // หยุดชั่วคราว (PAUSE)
        app.isRunning = false;
        workoutTimerPause();
        app.isTimerPaused = true;
        updateCalories();
        beepConfirm();
    } else if (app.isComplete) {
        // เริ่มเซ็ตใหม่หลังจากจบครบแล้ว
        app.count = 0; app.totalCount = 0;
        app.currentSet = 1; app.caloriesBurned = 0;
        app.isComplete = false; app.isRunning = true;
        app.isDown = false;
        app.downStartTime = 0;
        app.lastMovementTime = millis();
        app.isTimerPaused = false;
        
        // NEW! รีเซ็ตตัวกรองสัญญาณ
        app.downCounter = 0;
        app.upCounter = 0;
        
        workoutTimerReset();
        workoutTimerStart();
        beepClick();
    }
    ui_update_all();
}

static unsigned long resetPressStart = 0;

static void cb_btn_reset(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    screenWake();
    if (!screenOn) return;

    if (code == LV_EVENT_PRESSED) {
        resetPressStart = millis();
        // เปลี่ยนสีเตือน
        lv_obj_set_style_bg_color(btn_reset,
            LV_COLOR_MAKE(0xFF, 0x60, 0x00), LV_PART_MAIN);

    } else if (code == LV_EVENT_LONG_PRESSED) {
        // long-press 2 วินาที → RESET session เท่านั้น (ไม่สะสม history)
        hideQROverlay();
        hideHistoryOverlay();
        app.count = 0; 
        app.totalCount = 0;
        app.currentSet = 1; 
        app.caloriesBurned = 0;
        app.isRunning = false; 
        app.isResting = false;
        app.isComplete = false; 
        app.isDown = false;
        app.downStartTime = 0;
        app.lastMovementTime = 0;
        app.isTimerPaused = false;
        
        // NEW! รีเซ็ตตัวกรองสัญญาณ
        app.downCounter = 0;
        app.upCounter = 0;
        
        workoutTimerReset();
        lv_obj_set_style_bg_color(btn_reset, COL_BTN_RESET, LV_PART_MAIN);
        beepConfirm();
        ui_update_all();

    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(btn_reset, COL_BTN_RESET, LV_PART_MAIN);
        resetPressStart = 0;
    }
}

// =============================================================
// applySaveSettings() — บันทึกค่าตั้งทั้งหมด (เรียกจากปุ่ม SAVE ในหน้า Settings)
// ถ้าเปลี่ยนแนวจอ (ROTATE) จะรีสตาร์ทเครื่องอัตโนมัติเพื่อ apply
// =============================================================
void applySaveSettings() {
    screenWake();
    if (!screenOn) return;
    saveSettings();  // บันทึกการตั้งค่าลง NVS

    // กระพริบไฟ feedback
    if (btn_save_set) {
        lv_obj_set_style_bg_color(btn_save_set, LV_COLOR_MAKE(0xFF,0xFF,0x00), LV_PART_MAIN);
        lv_task_handler();
        delay(120);
        lv_obj_set_style_bg_color(btn_save_set, LV_COLOR_MAKE(0x00,0x6A,0x4E), LV_PART_MAIN);
    }

    // ---- ถ้าเปลี่ยนแนวจอ (ROTATE) ต้องรีสตาร์ทเพื่อ init จอ/ทัชใหม่ทั้งหมด ----
    if (app.orientation != bootOrientation) {
        // สร้างหน้าจอแจ้งเตือน
        lv_obj_t *msg = lv_label_create(lv_scr_act());
        lv_label_set_text(msg, "Restarting...\nApplying screen orientation");
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(msg, lv_color_white(), 0);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(msg, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(msg, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(msg, 12, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
        lv_task_handler();
        
        // เริ่มเสียง confirmation (แบบ non-blocking)
        beepConfirm();
        
        // รอด้วยลูป non-blocking เพื่อให้เสียงเล่นจบ
        unsigned long startTime = millis();
        while (millis() - startTime < 1200) {
            buzzUpdate();        // จัดการเสียงให้จบตามเวลา
            lv_task_handler();   // อัปเดต UI
            delay(5);
        }
        
        // ปิดเสียงให้ชัวร์ก่อนรีสตาร์ท (กันเสียงค้าง)
        ledcWriteTone(0, 0);
        buzzActive = false;
        buzzQueueLen = 0;
        
        delay(100);
        ESP.restart();
    } else {
        // ไม่เปลี่ยนแนวจอ → แค่เสียง confirm ปกติ (ไม่ต้องรอ blocking)
        beepConfirm();
    }
}

static unsigned long enterPressStart = 0;

static void cb_btn_enter_longpress(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (!isOnlineMode) return;
    screenWake();

    if (code == LV_EVENT_PRESSED) {
        enterPressStart = millis();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (enterPressStart > 0 && (millis() - enterPressStart >= 5000)) {
            // ค้าง ENTER 5 วินาที → รีเซ็ต WiFi
            buzzQueueLen = 0;
            buzzQueuePush(1000, 100); buzzQueuePush(0, 50);
            buzzQueuePush(1000, 100); buzzQueuePush(0, 50);
            buzzQueuePush(500,  300);
            buzzStart();
            unsigned long t = millis();
            while (millis() - t < 800) { buzzUpdate(); lv_task_handler(); }
            wifiResetAndReboot();
        }
        enterPressStart = 0;
    }
}

// =============================================================
// ปุ่ม SETTINGS — เปิดหน้าจอตั้งค่า
// =============================================================
static void cb_btn_settings(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    screenWake();
    if (!screenOn) return;
    if (app.isRunning) return;  // ห้ามเข้าตั้งค่าขณะออกกำลังกาย
    beepClick();
    Serial.printf("[SETTINGS] before build: ESP free heap=%u\n", ESP.getFreeHeap());
    showSettingsScreen();
    Serial.printf("[SETTINGS] after build:  ESP free heap=%u\n", ESP.getFreeHeap());
}

// ==========================================================================================
// UI Update helpers
// ==========================================================================================
void ui_update_status() {
    // อัปเดตข้อความสถานะและสี
    const char *txt;
    lv_color_t col;
    if (app.isRunning)       { txt = "RUNNING";   col = COL_GREEN; }
    else if (app.isResting)  { txt = "REST";      col = COL_YELLOW; }
    else if (app.isComplete) { txt = "COMPLETE!"; col = COL_CYAN; }
    else                     { txt = "READY";     col = COL_GREY_LABEL; }
    lv_label_set_text(lbl_status, txt);
    lv_obj_set_style_text_color(lbl_status, col, 0);

    // แสดงเวลานับถอยหลังระหว่างพัก
    if (app.isResting) {
        unsigned long el = millis() - app.restStartTime;
        int rem = app.restDurationSec - (int)(el / 1000);
        if (rem < 0) rem = 0;
        char buf[20];
        snprintf(buf, sizeof(buf), "REST %ds", rem);
        lv_label_set_text(lbl_status, buf);
    }

    // เปลี่ยนปุ่ม START เป็น PAUSE เมื่อกำลังออกกำลังกาย
    lv_label_set_text(lbl_btn_start, app.isRunning ? "PAUSE" : "START");
    lv_obj_set_style_bg_color(btn_start,
        app.isRunning ? COL_RED : COL_GREEN, LV_PART_MAIN);
}

void ui_update_workout_time() {
    if (isOnlineMode) {
        if (WiFi.isConnected()) {
            char ipbuf[32];
            snprintf(ipbuf, sizeof(ipbuf), LV_SYMBOL_WIFI " %s",
                     WiFi.localIP().toString().c_str());
            lv_label_set_text(lbl_workout_time, ipbuf);
            lv_obj_set_style_text_color(lbl_workout_time, COL_GREEN, 0);
        } else {
            lv_label_set_text(lbl_workout_time, "OFFLINE");
            lv_obj_set_style_text_color(lbl_workout_time, COL_RED, 0);
        }
        return;
    }
    unsigned long ms = workoutTimerGetMs();
    unsigned long totalSec = ms / 1000;
    int mm = (int)(totalSec / 60);
    int ss = (int)(totalSec % 60);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);
    lv_label_set_text(lbl_workout_time, buf);
}

void ui_update_count() {
    // อัปเดตตัวเลขจำนวนนับ
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", app.totalCount);
    lv_label_set_text(lbl_total_count, buf);

    // =============================================================
    // เปลี่ยนสีตัวนับตามสถานะ
    // =============================================================
    if (app.isComplete) {
        lv_obj_set_style_text_color(lbl_total_count, COL_GREEN, 0);   // เขียว = สำเร็จ
    } else if (app.isRunning) {
        lv_obj_set_style_text_color(lbl_total_count, COL_CYAN, 0);    // ฟ้า = กำลังทำ
    } else {
        lv_obj_set_style_text_color(lbl_total_count, lv_color_white(), 0);  // ขาว = พร้อม
    }

    // แสดงเซ็ตปัจจุบัน
    char si[20];
    snprintf(si, sizeof(si), "SET %d/%d", app.currentSet, app.totalSets);
    lv_label_set_text(lbl_set_indicator, si);

    // แถบความคืบหน้า
    int val = (app.targetPerSet > 0)
        ? (int)((long)app.count * 100 / app.targetPerSet)
        : 0;
    val = constrain(val, 0, 100);
    lv_bar_set_value(bar_progress, val, LV_ANIM_OFF);

    // ข้อความ REP
    char rep[20];
    if (app.isRunning || app.isResting) {
        snprintf(rep, sizeof(rep), "REP %d/%d", app.count, app.targetPerSet);
    } else {
        snprintf(rep, sizeof(rep), "TOTAL REPS");
    }
    lv_label_set_text(lbl_rep, rep);
}

void ui_update_info() {
    // อัปเดตข้อมูลทุกช่องในตาราง
    char buf[20];
    snprintf(buf, sizeof(buf), "%d", app.targetPerSet);
    lv_label_set_text(lbl_v_target, buf);

    snprintf(buf, sizeof(buf), "%d/%d", app.currentSet, app.totalSets);
    lv_label_set_text(lbl_v_set, buf);

    snprintf(buf, sizeof(buf), "%ds", app.restDurationSec);
    lv_label_set_text(lbl_v_rest, buf);

    snprintf(buf, sizeof(buf), "%.1fkg", app.bodyWeight);
    lv_label_set_text(lbl_v_weight, buf);

    snprintf(buf, sizeof(buf), "%.1fkcal", app.caloriesBurned);
    lv_label_set_text(lbl_v_cal, buf);

    snprintf(buf, sizeof(buf), "%dcm", app.lastDistance);
    lv_label_set_text(lbl_v_dist_info, buf);

    snprintf(buf, sizeof(buf), "%dcm", app.threshDown);
    lv_label_set_text(lbl_v_tdown, buf);

    snprintf(buf, sizeof(buf), "%dcm", app.threshUp);
    lv_label_set_text(lbl_v_tup, buf);

    if (app.screenTimeout == 0)
        snprintf(buf, sizeof(buf), "OFF");
    else
        snprintf(buf, sizeof(buf), "%dmin", app.screenTimeout);
    lv_label_set_text(lbl_v_screen, buf);

    // แสดงระยะทางและสถานะ DOWN/UP
    char dh[16];
    snprintf(dh, sizeof(dh), "%dcm %s",
             app.lastDistance, app.isDown ? "DOWN" : "UP");
    lv_label_set_text(lbl_dist, dh);
    lv_obj_set_style_text_color(lbl_dist,
        app.isDown ? COL_RED : COL_GREEN, 0);
}

void ui_update_all() {
    ui_update_status();
    ui_update_count();
    ui_update_info();
    ui_update_workout_time();
}

// ==========================================================================================
// UI Build — สร้างหน้าจอ
// ==========================================================================================
#define HDR_H     34
#define COUNT_H   55
#define BAR_H      6
#define INFO_Y    91
#define INFO_H    76
#define BTN1_Y   166
#define BTN1_H    32 
#define BTN2_Y   200
#define BTN2_H    32

void ui_build_landscape() {
    lv_obj_t *scr = lv_scr_act();
    mainScr = scr;
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Helper: สร้าง Label
    auto make_label = [](lv_obj_t *parent,
                         const char *txt,
                         const lv_font_t *font,
                         lv_color_t col,
                         int x, int y) -> lv_obj_t* {
        lv_obj_t *l = lv_label_create(parent);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, col, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(l, 0, 0);
        lv_obj_set_pos(l, x, y);
        return l;
    };

    // Helper: สร้างปุ่ม
    auto make_btn = [](lv_obj_t *parent,
                       int x, int y, int w, int h,
                       lv_color_t bg,
                       lv_event_cb_t cb,
                       const char *label_txt,
                       const lv_font_t *font) -> lv_obj_t* {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, w, h);
        lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label_txt);
        lv_obj_set_style_text_font(lbl, font, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(lbl, 0, 0);
        lv_obj_center(lbl);
        return btn;
    };

    // =============================================================
    // HEADER BAR (แถบด้านบน)
    // =============================================================
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, SCR_W, HDR_H);
    lv_obj_set_style_bg_color(hdr, COL_HEADER_BG, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(hdr, false, 0);

    lbl_status = make_label(hdr, "READY",
                            &lv_font_montserrat_14,
                            COL_GREY_DIM, 6, 4);

    lbl_workout_time = make_label(hdr, "00:00",
                                  &lv_font_montserrat_12,
                                  COL_YELLOW, 6, 20);

    lbl_set_indicator = make_label(hdr, "SET 1/1",
                                   &lv_font_montserrat_12,
                                   lv_color_white(), 228, 5);

    lbl_dist = make_label(hdr, "0cm UP",
                          &lv_font_montserrat_12,
                          COL_GREEN, 228, 19);

    // =============================================================
    // BIG COUNT ZONE (ตัวเลขจำนวนนับ)
    // =============================================================
    lv_obj_t *count_bg = lv_obj_create(scr);
    lv_obj_set_pos(count_bg, 0, HDR_H + 1);
    lv_obj_set_size(count_bg, SCR_W, COUNT_H);
    lv_obj_set_style_bg_color(count_bg, COL_COUNT_BG, 0);
    lv_obj_set_style_border_width(count_bg, 0, 0);
    lv_obj_set_style_radius(count_bg, 0, 0);
    lv_obj_set_style_pad_all(count_bg, 0, 0);
    lv_obj_clear_flag(count_bg, LV_OBJ_FLAG_SCROLLABLE);

    lbl_total_count = lv_label_create(count_bg);
    lv_label_set_text(lbl_total_count, "0");
    lv_obj_set_style_text_font(lbl_total_count, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_total_count, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lbl_total_count, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lbl_total_count, 0, 0);
    lv_obj_align(lbl_total_count, LV_ALIGN_CENTER, 0, -4);

    lbl_rep = make_label(count_bg, "TOTAL REPS",
                         &lv_font_montserrat_12,
                         COL_GREY_LABEL, 4, COUNT_H - 20);

    // แถบความคืบหน้า
    bar_progress = lv_bar_create(scr);
    lv_obj_set_pos(bar_progress, 0, HDR_H + COUNT_H - 1);
    lv_obj_set_size(bar_progress, SCR_W, BAR_H);
    lv_obj_set_style_bg_color(bar_progress, COL_GREY_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_progress, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_progress, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_progress, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(bar_progress, 0, LV_PART_MAIN);
    lv_bar_set_range(bar_progress, 0, 100);
    lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);

    // =============================================================
    // INFO GRID (ตารางข้อมูล)
    // =============================================================
    lv_obj_t *info_box = lv_obj_create(scr);
    lv_obj_set_pos(info_box, 0, INFO_Y);
    lv_obj_set_size(info_box, SCR_W, INFO_H);
    lv_obj_set_style_bg_color(info_box, lv_color_black(), 0);
    lv_obj_set_style_border_width(info_box, 0, 0);
    lv_obj_set_style_radius(info_box, 0, 0);
    lv_obj_set_style_pad_all(info_box, 0, 0);
    lv_obj_clear_flag(info_box, LV_OBJ_FLAG_SCROLLABLE);

    // เส้นแบ่งแนวตั้ง
    lv_obj_t *vline = lv_obj_create(info_box);
    lv_obj_set_pos(vline, 160, 0);
    lv_obj_set_size(vline, 1, INFO_H);
    lv_obj_set_style_bg_color(vline, COL_GREY_DIM, 0);
    lv_obj_set_style_border_width(vline, 0, 0);
    lv_obj_set_style_radius(vline, 0, 0);

    // Row 1: Target | Set
    make_label(info_box,"Target", &lv_font_montserrat_12, COL_GREY_LABEL,  8,  4);
    lbl_v_target = make_label(info_box,"10", &lv_font_montserrat_12, lv_color_white(), 60, 4);
    make_label(info_box,"Set",    &lv_font_montserrat_12, COL_GREY_LABEL, 168, 4);
    lbl_v_set = make_label(info_box,"1/1",&lv_font_montserrat_12, lv_color_white(),200, 4);

    // Row 2: Rest | Weight
    make_label(info_box,"Rest",   &lv_font_montserrat_12, COL_GREY_LABEL,  8, 18);
    lbl_v_rest = make_label(info_box,"30s",&lv_font_montserrat_12, COL_CYAN, 60, 18);
    make_label(info_box,"Weight", &lv_font_montserrat_12, COL_GREY_LABEL, 168, 18);
    lbl_v_weight = make_label(info_box,"70.0",&lv_font_montserrat_12, COL_CYAN, 220, 18);

    // Row 3: Cal | Dist
    make_label(info_box,"Cal",    &lv_font_montserrat_12, COL_GREY_LABEL,  8, 32);
    lbl_v_cal = make_label(info_box,"0.0k",&lv_font_montserrat_12, COL_ORANGE, 60, 32);
    make_label(info_box,"Dist",   &lv_font_montserrat_12, COL_GREY_LABEL, 168, 32);
    lbl_v_dist_info = make_label(info_box,"0cm",&lv_font_montserrat_12, COL_GREY_LABEL,204, 32);

    // Row 4: T-Down | T-Up
    make_label(info_box,"T-Down", &lv_font_montserrat_12, COL_GREY_LABEL,  8, 46);
    lbl_v_tdown = make_label(info_box,"15cm",&lv_font_montserrat_12, COL_RED, 60, 46);
    make_label(info_box,"T-Up",   &lv_font_montserrat_12, COL_GREY_LABEL, 168, 46);
    lbl_v_tup = make_label(info_box,"25cm",&lv_font_montserrat_12, COL_GREEN, 204, 46);

    // Row 5: Screen timeout
    make_label(info_box,"Screen", &lv_font_montserrat_12, COL_GREY_LABEL,  8, 60);
    lbl_v_screen = make_label(info_box,"5min",&lv_font_montserrat_12, COL_GREY_DIM, 60, 60);

    // เส้นแบ่งก่อนแถวปุ่ม
    lv_obj_t *hline1 = lv_obj_create(scr);
    lv_obj_set_pos(hline1, 0, BTN1_Y - 1);
    lv_obj_set_size(hline1, SCR_W, 1);
    lv_obj_set_style_bg_color(hline1, COL_GREY_DIM, 0);
    lv_obj_set_style_border_width(hline1, 0, 0);
    lv_obj_set_style_radius(hline1, 0, 0);

    // =============================================================
    // แถวปุ่มเดียว: START | RESET | SETTINGS (ใช้พื้นที่ 2 แถวเดิมรวมกัน)
    // =============================================================
    {
        int rowY = BTN1_Y;
        int rowH = (BTN2_Y + BTN2_H) - BTN1_Y;
        int gap  = 4;
        int btnW = (SCR_W - 4 - 2 * gap) / 3;

        btn_start = make_btn(scr, 2, rowY, btnW, rowH,
                             COL_GREEN, cb_btn_start, "START",
                             &lv_font_montserrat_16);
        lbl_btn_start = lv_obj_get_child(btn_start, 0);

        btn_reset = make_btn(scr, 2 + btnW + gap, rowY, btnW, rowH,
                             COL_BTN_RESET, cb_btn_reset, "RESET",
                             &lv_font_montserrat_16);
        lv_obj_remove_event_cb(btn_reset, cb_btn_reset);
        lv_obj_add_event_cb(btn_reset, cb_btn_reset, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(btn_reset, cb_btn_reset, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(btn_reset, cb_btn_reset, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_add_event_cb(btn_reset, cb_btn_reset, LV_EVENT_LONG_PRESSED, NULL);

        btn_settings = make_btn(scr, 2 + 2 * (btnW + gap), rowY, btnW, rowH,
                             LV_COLOR_MAKE(0x30,0x30,0x60), cb_btn_settings, LV_SYMBOL_SETTINGS " SET",
                             &lv_font_montserrat_14);
    }
}

// ==========================================================================================
// UI Build — สร้างหน้าจอ (PORTRAIT MODE — ปุ่มชิดขอบล่าง)
// ==========================================================================================
// ค่า Layout ของโหมด PORTRAIT ไม่เหมือนกับ LANDSCAPE — undef ของเดิมก่อนกำหนดใหม่
#undef COUNT_H
#undef INFO_Y
#undef INFO_H
#undef BTN1_Y
#undef BTN1_H
#undef BTN2_Y
#undef BTN2_H
#define COUNT_H   80
#define INFO_Y    120
#define INFO_H    100
#define BTN1_Y    238   // ปุ่มแถวที่ 1 ชิดขอบล่าง
#define BTN1_H    39
#define BTN2_Y    280   // ปุ่มแถวที่ 2 ชิดขอบล่าง
#define BTN2_H    39

void ui_build_portrait() {
    lv_obj_t *scr = lv_scr_act();
    mainScr = scr;
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    auto make_label = [](lv_obj_t *parent, const char *txt, const lv_font_t *font,
                         lv_color_t col, int x, int y) -> lv_obj_t* {
        lv_obj_t *l = lv_label_create(parent);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, col, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(l, 0, 0);
        lv_obj_set_pos(l, x, y);
        return l;
    };

    auto make_btn = [](lv_obj_t *parent, int x, int y, int w, int h,
                       lv_color_t bg, lv_event_cb_t cb, const char *label_txt,
                       const lv_font_t *font) -> lv_obj_t* {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, w, h);
        lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label_txt);
        lv_obj_set_style_text_font(lbl, font, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(lbl, 0, 0);
        lv_obj_center(lbl);
        return btn;
    };

    // =============================================================
    // HEADER BAR (แถบด้านบน)
    // =============================================================
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, SCR_W, HDR_H);
    lv_obj_set_style_bg_color(hdr, COL_HEADER_BG, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(hdr, false, 0);

    lbl_status = make_label(hdr, "READY", &lv_font_montserrat_14, COL_GREY_DIM, 6, 4);
    lbl_workout_time = make_label(hdr, "00:00", &lv_font_montserrat_12, COL_YELLOW, 6, 20);
    lbl_set_indicator = make_label(hdr, "SET 1/1", &lv_font_montserrat_12, lv_color_white(), 160, 5);
    lbl_dist = make_label(hdr, "0cm UP", &lv_font_montserrat_12, COL_GREEN, 160, 19);

    // =============================================================
    // BIG COUNT ZONE (ตัวเลขจำนวนนับ)
    // =============================================================
    lv_obj_t *count_bg = lv_obj_create(scr);
    lv_obj_set_pos(count_bg, 0, HDR_H + 1);
    lv_obj_set_size(count_bg, SCR_W, COUNT_H);
    lv_obj_set_style_bg_color(count_bg, COL_COUNT_BG, 0);
    lv_obj_set_style_border_width(count_bg, 0, 0);
    lv_obj_set_style_radius(count_bg, 0, 0);
    lv_obj_set_style_pad_all(count_bg, 0, 0);
    lv_obj_clear_flag(count_bg, LV_OBJ_FLAG_SCROLLABLE);

    lbl_total_count = lv_label_create(count_bg);
    lv_label_set_text(lbl_total_count, "0");
    lv_obj_set_style_text_font(lbl_total_count, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_total_count, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lbl_total_count, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lbl_total_count, 0, 0);
    lv_obj_align(lbl_total_count, LV_ALIGN_CENTER, 0, -4);

    lbl_rep = make_label(count_bg, "TOTAL REPS", &lv_font_montserrat_12, COL_GREY_LABEL, 4, COUNT_H - 20);

    // แถบความคืบหน้า
    bar_progress = lv_bar_create(scr);
    lv_obj_set_pos(bar_progress, 0, HDR_H + COUNT_H - 1);
    lv_obj_set_size(bar_progress, SCR_W, BAR_H);
    lv_obj_set_style_bg_color(bar_progress, COL_GREY_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_progress, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_progress, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_progress, 0, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(bar_progress, 0, LV_PART_MAIN);
    lv_bar_set_range(bar_progress, 0, 100);
    lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);

    // =============================================================
    // INFO GRID (ตารางข้อมูล) — PORTRAIT
    // =============================================================
    lv_obj_t *info_box = lv_obj_create(scr);
    lv_obj_set_pos(info_box, 0, INFO_Y);
    lv_obj_set_size(info_box, SCR_W, INFO_H);
    lv_obj_set_style_bg_color(info_box, lv_color_black(), 0);
    lv_obj_set_style_border_width(info_box, 0, 0);
    lv_obj_set_style_radius(info_box, 0, 0);
    lv_obj_set_style_pad_all(info_box, 0, 0);
    lv_obj_clear_flag(info_box, LV_OBJ_FLAG_SCROLLABLE);

// เส้นแบ่งแนวตั้ง
lv_obj_t *vline = lv_obj_create(scr);  // <--- เปลี่ยนเป็นลูกของ scr (หน้าจอหลัก)
lv_obj_set_pos(vline, 116, INFO_Y);    // <--- เปลี่ยน Y เป็น INFO_Y
lv_obj_set_size(vline, 1, BTN1_Y - INFO_Y); // <--- ใส่ความสูงเท่าเดิม
lv_obj_set_style_bg_color(vline, COL_GREY_DIM, 0);
lv_obj_set_style_border_width(vline, 0, 0);
lv_obj_set_style_radius(vline, 0, 0);

    // Row 1: Target | Set
    make_label(info_box,"Target", &lv_font_montserrat_12, COL_GREY_LABEL,  8,  4);
    lbl_v_target = make_label(info_box,"10", &lv_font_montserrat_12, lv_color_white(), 60, 4);
    make_label(info_box,"Set",    &lv_font_montserrat_12, COL_GREY_LABEL, 128, 4);
    lbl_v_set = make_label(info_box,"1/1",&lv_font_montserrat_12, lv_color_white(), 168, 4);

    // Row 2: Rest | Weight
    make_label(info_box,"Rest",   &lv_font_montserrat_12, COL_GREY_LABEL,  8, 18);
    lbl_v_rest = make_label(info_box,"30s",&lv_font_montserrat_12, COL_CYAN, 60, 18);
    make_label(info_box,"Weight", &lv_font_montserrat_12, COL_GREY_LABEL, 128, 18);
    lbl_v_weight = make_label(info_box,"70.0",&lv_font_montserrat_12, COL_CYAN, 180, 18);

    // Row 3: Cal | Dist
    make_label(info_box,"Cal",    &lv_font_montserrat_12, COL_GREY_LABEL,  8, 32);
    lbl_v_cal = make_label(info_box,"0.0k",&lv_font_montserrat_12, COL_ORANGE, 60, 32);
    make_label(info_box,"Dist",   &lv_font_montserrat_12, COL_GREY_LABEL, 128, 32);
    lbl_v_dist_info = make_label(info_box,"0cm",&lv_font_montserrat_12, COL_GREY_LABEL, 168, 32);

    // Row 4: T-Down | T-Up
    make_label(info_box,"T-Down", &lv_font_montserrat_12, COL_GREY_LABEL,  8, 46);
    lbl_v_tdown = make_label(info_box,"15cm",&lv_font_montserrat_12, COL_RED, 60, 46);
    make_label(info_box,"T-Up",   &lv_font_montserrat_12, COL_GREY_LABEL, 128, 46);
    lbl_v_tup = make_label(info_box,"25cm",&lv_font_montserrat_12, COL_GREEN, 168, 46);

    // Row 5: Screen timeout
    make_label(info_box,"Screen", &lv_font_montserrat_12, COL_GREY_LABEL,  8, 60);
    lbl_v_screen = make_label(info_box,"5min",&lv_font_montserrat_12, COL_GREY_DIM, 60, 60);

    // เส้นแบ่งก่อนปุ่มแถว 1
    lv_obj_t *hline1 = lv_obj_create(scr);
    lv_obj_set_pos(hline1, 0, BTN1_Y - 2);
    lv_obj_set_size(hline1, SCR_W, 1);
    lv_obj_set_style_bg_color(hline1, COL_GREY_DIM, 0);
    lv_obj_set_style_border_width(hline1, 0, 0);
    lv_obj_set_style_radius(hline1, 0, 0);

    // =============================================================
    // แถวปุ่มเดียว: START | RESET | SETTINGS (ใช้พื้นที่ 2 แถวเดิมรวมกัน, ชิดขอบล่าง)
    // =============================================================
    {
        int rowY = BTN1_Y;
        int rowH = (BTN2_Y + BTN2_H) - BTN1_Y;
        int gap  = 4;
        int btnW = (SCR_W - 4 - 2 * gap) / 3;

        btn_start = make_btn(scr, 2, rowY, btnW, rowH,
                             COL_GREEN, cb_btn_start, "START", &lv_font_montserrat_14);
        lbl_btn_start = lv_obj_get_child(btn_start, 0);

        btn_reset = make_btn(scr, 2 + btnW + gap, rowY, btnW, rowH,
                             COL_BTN_RESET, cb_btn_reset, "RESET", &lv_font_montserrat_14);
        lv_obj_add_event_cb(btn_reset, cb_btn_reset, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(btn_reset, cb_btn_reset, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(btn_reset, cb_btn_reset, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_add_event_cb(btn_reset, cb_btn_reset, LV_EVENT_LONG_PRESSED, NULL);

        btn_settings = make_btn(scr, 2 + 2 * (btnW + gap), rowY, btnW, rowH,
                             LV_COLOR_MAKE(0x30,0x30,0x60), cb_btn_settings, LV_SYMBOL_SETTINGS,
                             &lv_font_montserrat_14);
    }
}

// ==========================================================================================
// ui_build() — ตัวเลือก dispatch ไปตามแนวจอที่ตั้งไว้ตอนบูต (bootOrientation)
// ==========================================================================================
void ui_build() {
    if (bootOrientation == ORIENT_LANDSCAPE) {
        ui_build_landscape();
    } else {
        ui_build_portrait();
    }
}

// ==========================================================================================
// SETTINGS SCREEN — หน้าตั้งค่าใหม่ (แถบเลื่อน + สวิตช์ + ปุ่ม)
//   - เปิดจากปุ่ม SETTINGS บนหน้าหลัก
//   - แตะพื้นหลัง/แถบหัวข้อ ที่ไม่ใช่ปุ่ม/แถบเลื่อน → กลับหน้าหลักทันที
//   - HISTORY / QR-CODE เปิดเป็นหน้าเต็มจอทับ แล้ว "back" กลับมาที่หน้านี้
//   - SAVE บันทึกค่าทั้งหมดลง NVS (ถ้าเปลี่ยนแนวจอ จะรีสตาร์ทอัตโนมัติ)
// ==========================================================================================
#define SET_HDR_H 34

void showSettingsScreen() {
    if (settingsScr) { lv_scr_load(settingsScr); return; }

    settingsScr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(settingsScr, lv_color_black(), 0);
    lv_obj_set_style_border_width(settingsScr, 0, 0);
    lv_obj_clear_flag(settingsScr, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Header: ปุ่ม BACK + หัวข้อ ----
    lv_obj_t *hdr = lv_obj_create(settingsScr);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, SCR_W, SET_HDR_H);
    lv_obj_set_style_bg_color(hdr, COL_HEADER_BG, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdrLbl = lv_label_create(hdr);
    lv_label_set_text(hdrLbl, "SETTINGS");
    lv_obj_set_style_text_font(hdrLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hdrLbl, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(hdrLbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdrLbl, 0, 0);
    lv_obj_align(hdrLbl, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t *btn_back = lv_label_create(hdr);
    lv_obj_set_pos(btn_back, 4, -10);
    lv_obj_set_size(btn_back, 78, SET_HDR_H - 6);
    lv_label_set_text(btn_back, LV_SYMBOL_LEFT " BACK");
    lv_obj_set_style_text_align(btn_back, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(btn_back, 5, 0);
    lv_obj_set_style_text_font(btn_back, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(btn_back, lv_color_white(), 0);
    lv_obj_set_style_bg_color(btn_back, LV_COLOR_MAKE(0x60,0x10,0x10), 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_set_style_border_color(btn_back, lv_color_white(), 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_add_flag(btn_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        beepClick();
        hideSettingsScreen();
    }, LV_EVENT_CLICKED, NULL);

    // ---- Content panel — เลื่อนได้ในแนวตั้ง ----
    lv_obj_t *panel = lv_obj_create(settingsScr);
    lv_obj_set_pos(panel, 0, SET_HDR_H);
    lv_obj_set_size(panel, SCR_W, SCR_H - SET_HDR_H);
    lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    int rowW  = SCR_W - 12;
    int btnW  = 40, btnH = 30;
    int nameW = rowW - 2 * btnW - 10;
    static int yCur;
    yCur = 2;
    char buf[32];

    // ---- Helper: กล่องข้อความที่แตะได้ ----
    auto make_tap_btn = [](lv_obj_t *parent, int x, int y, int w, int h,
                            const char *txt, lv_color_t col, const lv_font_t *font) -> lv_obj_t* {
        lv_obj_t *b = lv_label_create(parent);
        lv_obj_set_pos(b, x, y);
        lv_obj_set_size(b, w, h);
        lv_label_set_text(b, txt);
        lv_label_set_long_mode(b, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
        int padTop = (h - 16) / 2;
        if (padTop < 0) padTop = 0;
        lv_obj_set_style_pad_top(b, padTop, 0);
        lv_obj_set_style_text_font(b, font, 0);
        lv_obj_set_style_text_color(b, lv_color_white(), 0);
        lv_obj_set_style_bg_color(b, col, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_border_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        return b;
    };

    // ---- Helper: สร้าง label ชื่อ+ค่า ----
    auto make_num_label = [&](lv_color_t col) -> lv_obj_t* {
        lv_obj_t *lbl = lv_label_create(panel);
        lv_obj_set_pos(lbl, 2, yCur + 7);
        lv_obj_set_width(lbl, nameW);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, col, 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        return lbl;
    };

    int mbX = nameW + 4;
    int pbX = nameW + 4 + btnW + 6;

    // ---- 2.1 TARGET (1-99) ----
    lbl_set_target = make_num_label(lv_color_white());
    snprintf(buf, sizeof(buf), "TARGET/SET: %d", app.targetPerSet);
    lv_label_set_text(lbl_set_target, buf);
    {
        lv_obj_t *mb = make_tap_btn(panel, mbX, yCur, btnW, btnH, "-", COL_BTN_MINUS, &lv_font_montserrat_16);
        lv_obj_t *pb = make_tap_btn(panel, pbX, yCur, btnW, btnH, "+", COL_BTN_PLUS, &lv_font_montserrat_16);
        lv_obj_add_event_cb(mb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.targetPerSet--;
            if (app.targetPerSet < 1) app.targetPerSet = 99;
            char b[32]; snprintf(b, sizeof(b), "TARGET/SET: %d", app.targetPerSet);
            lv_label_set_text(lbl_set_target, b);
            beepDecrease();
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.targetPerSet++;
            if (app.targetPerSet > 99) app.targetPerSet = 1;
            char b[32]; snprintf(b, sizeof(b), "TARGET/SET: %d", app.targetPerSet);
            lv_label_set_text(lbl_set_target, b);
            beepIncrease();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 5;

    // ---- 2.2 SETS (1-20) ----
    lbl_set_sets = make_num_label(COL_CYAN);
    snprintf(buf, sizeof(buf), "SETS: %d", app.totalSets);
    lv_label_set_text(lbl_set_sets, buf);
    {
        lv_obj_t *mb = make_tap_btn(panel, mbX, yCur, btnW, btnH, "-", COL_BTN_MINUS, &lv_font_montserrat_16);
        lv_obj_t *pb = make_tap_btn(panel, pbX, yCur, btnW, btnH, "+", COL_BTN_PLUS, &lv_font_montserrat_16);
        lv_obj_add_event_cb(mb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.totalSets--;
            if (app.totalSets < 1) app.totalSets = 20;
            char b[32]; snprintf(b, sizeof(b), "SETS: %d", app.totalSets);
            lv_label_set_text(lbl_set_sets, b);
            beepDecrease();
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.totalSets++;
            if (app.totalSets > 20) app.totalSets = 1;
            char b[32]; snprintf(b, sizeof(b), "SETS: %d", app.totalSets);
            lv_label_set_text(lbl_set_sets, b);
            beepIncrease();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 5;

    // ---- 2.3 REST (0-60s) ----
    lbl_set_rest = make_num_label(COL_YELLOW);
    snprintf(buf, sizeof(buf), "REST: %ds", app.restDurationSec);
    lv_label_set_text(lbl_set_rest, buf);
    {
        lv_obj_t *mb = make_tap_btn(panel, mbX, yCur, btnW, btnH, "-", COL_BTN_MINUS, &lv_font_montserrat_16);
        lv_obj_t *pb = make_tap_btn(panel, pbX, yCur, btnW, btnH, "+", COL_BTN_PLUS, &lv_font_montserrat_16);
        lv_obj_add_event_cb(mb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.restDurationSec--;
            if (app.restDurationSec < 0) app.restDurationSec = 60;
            char b[32]; snprintf(b, sizeof(b), "REST: %ds", app.restDurationSec);
            lv_label_set_text(lbl_set_rest, b);
            beepDecrease();
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.restDurationSec++;
            if (app.restDurationSec > 60) app.restDurationSec = 0;
            char b[32]; snprintf(b, sizeof(b), "REST: %ds", app.restDurationSec);
            lv_label_set_text(lbl_set_rest, b);
            beepIncrease();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 5;

    // ---- 2.4 WEIGHT (20.0-150.0 kg, step 0.5) ----
    lbl_set_weight = make_num_label(COL_ORANGE);
    snprintf(buf, sizeof(buf), "WEIGHT: %.1fkg", app.bodyWeight);
    lv_label_set_text(lbl_set_weight, buf);
    {
        lv_obj_t *mb = make_tap_btn(panel, mbX, yCur, btnW, btnH, "-", COL_BTN_MINUS, &lv_font_montserrat_16);
        lv_obj_t *pb = make_tap_btn(panel, pbX, yCur, btnW, btnH, "+", COL_BTN_PLUS, &lv_font_montserrat_16);
        lv_obj_add_event_cb(mb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.bodyWeight -= 0.5f;
            if (app.bodyWeight < 20.0f) app.bodyWeight = 150.0f;
            updateCalories();
            char b[32]; snprintf(b, sizeof(b), "WEIGHT: %.1fkg", app.bodyWeight);
            lv_label_set_text(lbl_set_weight, b);
            beepDecrease();
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.bodyWeight += 0.5f;
            if (app.bodyWeight > 150.0f) app.bodyWeight = 20.0f;
            updateCalories();
            char b[32]; snprintf(b, sizeof(b), "WEIGHT: %.1fkg", app.bodyWeight);
            lv_label_set_text(lbl_set_weight, b);
            beepIncrease();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 5;

    // ---- 2.5 T-DOWN (3-70cm, ต้องน้อยกว่า T-UP อย่างน้อย 3) ----
    lbl_set_tdown = make_num_label(COL_RED);
    snprintf(buf, sizeof(buf), "T-DOWN: %dcm", app.threshDown);
    lv_label_set_text(lbl_set_tdown, buf);
    {
        lv_obj_t *mb = make_tap_btn(panel, mbX, yCur, btnW, btnH, "-", COL_BTN_MINUS, &lv_font_montserrat_16);
        lv_obj_t *pb = make_tap_btn(panel, pbX, yCur, btnW, btnH, "+", COL_BTN_PLUS, &lv_font_montserrat_16);
        lv_obj_add_event_cb(mb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.threshDown--;
            if (app.threshDown < 3) app.threshDown = 3;
            if (app.threshDown >= app.threshUp - 2) app.threshDown = app.threshUp - 3;
            char b[32]; snprintf(b, sizeof(b), "T-DOWN: %dcm", app.threshDown);
            lv_label_set_text(lbl_set_tdown, b);
            beepDecrease();
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.threshDown++;
            if (app.threshDown >= app.threshUp - 2) app.threshDown = app.threshUp - 3;
            char b[32]; snprintf(b, sizeof(b), "T-DOWN: %dcm", app.threshDown);
            lv_label_set_text(lbl_set_tdown, b);
            beepIncrease();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 5;

    // ---- 2.6 T-UP (3-70cm, ต้องมากกว่า T-DOWN อย่างน้อย 3) ----
    lbl_set_tup = make_num_label(COL_GREEN);
    snprintf(buf, sizeof(buf), "T-UP: %dcm", app.threshUp);
    lv_label_set_text(lbl_set_tup, buf);
    {
        lv_obj_t *mb = make_tap_btn(panel, mbX, yCur, btnW, btnH, "-", COL_BTN_MINUS, &lv_font_montserrat_16);
        lv_obj_t *pb = make_tap_btn(panel, pbX, yCur, btnW, btnH, "+", COL_BTN_PLUS, &lv_font_montserrat_16);
        lv_obj_add_event_cb(mb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.threshUp--;
            if (app.threshUp <= app.threshDown + 2) app.threshUp = app.threshDown + 3;
            char b[32]; snprintf(b, sizeof(b), "T-UP: %dcm", app.threshUp);
            lv_label_set_text(lbl_set_tup, b);
            beepDecrease();
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.threshUp++;
            if (app.threshUp > 70) app.threshUp = app.threshDown + 3;
            char b[32]; snprintf(b, sizeof(b), "T-UP: %dcm", app.threshUp);
            lv_label_set_text(lbl_set_tup, b);
            beepIncrease();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 5;

    // ---- 2.7 SCREEN TIMEOUT (0-60 นาที, 0 = ไม่ดับจอ) ----
    lbl_set_screen = make_num_label(COL_GREY_DIM);
    if (app.screenTimeout == 0) snprintf(buf, sizeof(buf), "TIMEOUT: OFF");
    else snprintf(buf, sizeof(buf), "TIMEOUT: %dmin", app.screenTimeout);
    lv_label_set_text(lbl_set_screen, buf);
    {
        lv_obj_t *mb = make_tap_btn(panel, mbX, yCur, btnW, btnH, "-", COL_BTN_MINUS, &lv_font_montserrat_16);
        lv_obj_t *pb = make_tap_btn(panel, pbX, yCur, btnW, btnH, "+", COL_BTN_PLUS, &lv_font_montserrat_16);
        lv_obj_add_event_cb(mb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.screenTimeout--;
            if (app.screenTimeout < 0) app.screenTimeout = 60;
            char b[32];
            if (app.screenTimeout == 0) snprintf(b, sizeof(b), "TIMEOUT: OFF");
            else snprintf(b, sizeof(b), "TIMEOUT: %dmin", app.screenTimeout);
            lv_label_set_text(lbl_set_screen, b);
            beepDecrease();
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.screenTimeout++;
            if (app.screenTimeout > 60) app.screenTimeout = 0;
            char b[32];
            if (app.screenTimeout == 0) snprintf(b, sizeof(b), "TIMEOUT: OFF");
            else snprintf(b, sizeof(b), "TIMEOUT: %dmin", app.screenTimeout);
            lv_label_set_text(lbl_set_screen, b);
            beepIncrease();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 5;

    // =============================================================
    // ✅ MIN-DOWN อยู่ตรงนี้ (ใต้ TIMEOUT, ก่อน CAL-MODE)
    // =============================================================
    // ---- 2.8 MIN-DOWN TIME (50-500ms) ----
    lbl_set_mindown = make_num_label(COL_GREY_LABEL);
    snprintf(buf, sizeof(buf), "MIN-DOWN: %dms", app.minDownTimeMs);
    lv_label_set_text(lbl_set_mindown, buf);
    {
        lv_obj_t *mb = make_tap_btn(panel, mbX, yCur, btnW, btnH, "-", COL_BTN_MINUS, &lv_font_montserrat_16);
        lv_obj_t *pb = make_tap_btn(panel, pbX, yCur, btnW, btnH, "+", COL_BTN_PLUS, &lv_font_montserrat_16);
        lv_obj_add_event_cb(mb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.minDownTimeMs -= 10;
            if (app.minDownTimeMs < 50) app.minDownTimeMs = 500;
            char b[32]; snprintf(b, sizeof(b), "MIN-DOWN: %dms", app.minDownTimeMs);
            lv_label_set_text(lbl_set_mindown, b);
            beepDecrease();
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(pb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.minDownTimeMs += 10;
            if (app.minDownTimeMs > 500) app.minDownTimeMs = 50;
            char b[32]; snprintf(b, sizeof(b), "MIN-DOWN: %dms", app.minDownTimeMs);
            lv_label_set_text(lbl_set_mindown, b);
            beepIncrease();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 5;

    // ---- 2.9 CAL-MODE (MET / SIMPLE) — ปุ่มเดียวแตะสลับ ----
    lbl_sw_calmode = make_num_label(COL_GREY_LABEL);
    lv_obj_set_width(lbl_sw_calmode, nameW);
    snprintf(buf, sizeof(buf), "CAL-MODE: %s", app.calMode == 0 ? "MET" : "SIMPLE");
    lv_label_set_text(lbl_sw_calmode, buf);
    {
        lv_obj_t *tb = make_tap_btn(panel, mbX, yCur, btnW * 2 + 6, btnH,
                                     "TOGGLE", LV_COLOR_MAKE(0x30,0x30,0x60), &lv_font_montserrat_14);
        lv_obj_add_event_cb(tb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.calMode = 1 - app.calMode;
            updateCalories();
            char b[32]; snprintf(b, sizeof(b), "CAL-MODE: %s", app.calMode == 0 ? "MET" : "SIMPLE");
            lv_label_set_text(lbl_sw_calmode, b);
            beepClick();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 5;

    // ---- 2.10 ROTATE (HORIZONTAL / VERTICAL) — ใช้จริงหลังกด SAVE (รีสตาร์ท) ----
    lbl_sw_rotate = make_num_label(COL_GREY_LABEL);
    lv_obj_set_width(lbl_sw_rotate, nameW);
    snprintf(buf, sizeof(buf), "ROTATE: %s", app.orientation == ORIENT_LANDSCAPE ? "HORI" : "VERT");
    lv_label_set_text(lbl_sw_rotate, buf);
    {
        lv_obj_t *tb = make_tap_btn(panel, mbX, yCur, btnW * 2 + 6, btnH,
                                     "TOGGLE", LV_COLOR_MAKE(0x30,0x30,0x60), &lv_font_montserrat_14);
        lv_obj_add_event_cb(tb, [](lv_event_t *e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            app.orientation = (app.orientation == ORIENT_LANDSCAPE) ? ORIENT_PORTRAIT : ORIENT_LANDSCAPE;
            char b[32]; snprintf(b, sizeof(b), "ROTATE: %s", app.orientation == ORIENT_LANDSCAPE ? "HORI" : "VERT");
            lv_label_set_text(lbl_sw_rotate, b);
            beepClick();
        }, LV_EVENT_CLICKED, NULL);
    }
    yCur += btnH + 8;

    // ---- 2.11 / 2.12 ปุ่ม HISTORY และ QR CODE (แถวเดียวกัน) ----
    int halfW = (rowW - 6) / 2;
    lv_obj_t *btn_history_set = make_tap_btn(panel, 2, yCur, halfW, 38,
                                  "HISTORY", LV_COLOR_MAKE(0x00,0x30,0x50), &lv_font_montserrat_14);
    lv_obj_add_event_cb(btn_history_set, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        beepClick();
        lv_obj_t *oldSettings = settingsScr;
        settingsScr = NULL;
        showHistoryOverlay();
        if (historyOverlay) {
            if (oldSettings) lv_obj_del_async(oldSettings);
        } else {
            settingsScr = oldSettings;
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_qr_set = make_tap_btn(panel, 2 + halfW + 6, yCur, halfW, 38,
                              "QR CODE", LV_COLOR_MAKE(0x30,0x20,0x70), &lv_font_montserrat_14);
    lv_obj_add_event_cb(btn_qr_set, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        beepClick();
        lv_obj_t *oldSettings = settingsScr;
        settingsScr = NULL;
        showQROverlay();
        if (qrOverlay) {
            if (oldSettings) lv_obj_del_async(oldSettings);
        } else {
            settingsScr = oldSettings;
        }
    }, LV_EVENT_CLICKED, NULL);
    yCur += 38 + 6;

    // ---- 2.13 ปุ่ม SAVE ----
    btn_save_set = make_tap_btn(panel, 2, yCur, rowW, 42,
                                 "SAVE", LV_COLOR_MAKE(0x00,0x6A,0x4E), &lv_font_montserrat_16);
    lv_obj_add_event_cb(btn_save_set, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        applySaveSettings();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_save_set, cb_btn_enter_longpress, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(btn_save_set, cb_btn_enter_longpress, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(btn_save_set, cb_btn_enter_longpress, LV_EVENT_PRESS_LOST, NULL);
    yCur += 42 + 4;

    lv_scr_load(settingsScr);
    blockTouchUntil = millis() + 400;
    lv_task_handler();
}
// =============================================================
// hideSettingsScreen() — กดปุ่ม BACK ในหน้า Settings → กลับหน้าหลัก
// =============================================================
void hideSettingsScreen() {
    if (!settingsScr) return;
    if (qrOverlay || historyOverlay) return;
    lv_obj_t *old = settingsScr;
    settingsScr = NULL;
    lv_scr_load(mainScr);
    lv_obj_del_async(old);
    blockTouchUntil = millis() + 400;
    lv_task_handler();
    ui_update_all();
}

// ==========================================================================================
// QR Code Overlay — แสดง QR code ของ IP address (LANDSCAPE)
// ==========================================================================================
void showQROverlay_landscape() {
    if (qrOverlay) return;
    if (!isOnlineMode || !WiFi.isConnected()) return;

    String url = "http://" + WiFi.localIP().toString();

    qrOverlay = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(qrOverlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qrOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qrOverlay, 0, 0);
    lv_obj_set_style_radius(qrOverlay, 0, 0);
    lv_obj_clear_flag(qrOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *qr = lv_qrcode_create(qrOverlay, 120,
                                     lv_color_black(), lv_color_white());
    lv_img_dsc_t *qrImg = qr ? lv_canvas_get_img(qr) : NULL;
    if (!qr || !qrImg || !qrImg->data) {
        Serial.println("[QR] สร้าง QR canvas ไม่สำเร็จ (หน่วยความจำ LVGL/LV_MEM_SIZE อาจไม่พอ) - ยกเลิกและกลับหน้า Settings");
        lv_obj_del(qrOverlay);
        qrOverlay = NULL;
        showSettingsScreen();
        return;
    }
    lv_qrcode_update(qr, url.c_str(), url.length());
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 4, 0);

    lv_obj_t *title = lv_label_create(qrOverlay);
    lv_label_set_text(title, "Scan to open Dashboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title, 0, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *urlLbl = lv_label_create(qrOverlay);
    lv_label_set_text(urlLbl, url.c_str());
    lv_obj_set_style_text_font(urlLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(urlLbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(urlLbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(urlLbl, 0, 0);
    lv_obj_align(urlLbl, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_obj_t *hint = lv_label_create(qrOverlay);
    lv_label_set_text(hint, "[ tap to close ]");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, COL_GREY_LABEL, 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hint, 0, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -22);

    lv_obj_add_flag(qrOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(qrOverlay, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            hideQROverlay();
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(qrOverlay);
    blockTouchUntil = millis() + 600;
    lv_task_handler();
}

// ==========================================================================================
// QR Code Overlay — PORTRAIT
// ==========================================================================================
void showQROverlay_portrait() {
    if (qrOverlay) return;
    if (!isOnlineMode || !WiFi.isConnected()) return;

    String url = "http://" + WiFi.localIP().toString();

    qrOverlay = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(qrOverlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qrOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qrOverlay, 0, 0);
    lv_obj_set_style_radius(qrOverlay, 0, 0);
    lv_obj_clear_flag(qrOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *qr = lv_qrcode_create(qrOverlay, 120,
                                     lv_color_black(), lv_color_white());
    lv_img_dsc_t *qrImg = qr ? lv_canvas_get_img(qr) : NULL;
    if (!qr || !qrImg || !qrImg->data) {
        Serial.println("[QR] สร้าง QR canvas ไม่สำเร็จ (หน่วยความจำ LVGL/LV_MEM_SIZE อาจไม่พอ) - ยกเลิกและกลับหน้า Settings");
        lv_obj_del(qrOverlay);
        qrOverlay = NULL;
        showSettingsScreen();
        return;
    }
    lv_qrcode_update(qr, url.c_str(), url.length());
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 4, 0);

    lv_obj_t *title = lv_label_create(qrOverlay);
    lv_label_set_text(title, "Scan to open Dashboard");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title, 0, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *urlLbl = lv_label_create(qrOverlay);
    lv_label_set_text(urlLbl, url.c_str());
    lv_obj_set_style_text_font(urlLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(urlLbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(urlLbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(urlLbl, 0, 0);
    lv_obj_align(urlLbl, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *hint = lv_label_create(qrOverlay);
    lv_label_set_text(hint, "[ tap to close ]");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, COL_GREY_LABEL, 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hint, 0, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_add_flag(qrOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(qrOverlay, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            hideQROverlay();
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(qrOverlay);
    blockTouchUntil = millis() + 600;
    lv_task_handler();
}

// =============================================================
// showQROverlay() — dispatch ตามแนวจอ
// =============================================================
void showQROverlay() {
    if (bootOrientation == ORIENT_LANDSCAPE) {
        showQROverlay_landscape();
    } else {
        showQROverlay_portrait();
    }
}

// =============================================================
// hideQROverlay() — ปิด QR แล้วกลับไปหน้า Settings
// =============================================================
void hideQROverlay() {
    if (!qrOverlay) return;
    lv_obj_t *old = qrOverlay;
    qrOverlay = NULL;
    lv_scr_load(mainScr);
    lv_obj_del_async(old);
    blockTouchUntil = millis() + 400;
    lv_task_handler();
    showSettingsScreen();
}

// ==========================================================================================
// Online Mode — Web Dashboard
// ==========================================================================================
String getSystemUptimeString() {
    unsigned long s = millis() / 1000;
    int h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, sec);
    return String(buf);
}

String getWorkoutTimeStr() {
    unsigned long ms = workoutTimerGetMs();
    unsigned long s = ms / 1000;
    int h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, sec);
    return String(buf);
}

// =============================================================
// HTML Dashboard - เก็บใน PROGMEM (Flash) เพื่อลดการใช้ RAM/Stack
// =============================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang='th'>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#0f172a;color:#f8fafc;margin:0;display:flex;justify-content:center;padding:20px 0 40px}
.container{width:95%;max-width:450px;text-align:center}
h1{color:#22d3ee;font-size:20px;letter-spacing:1px;margin-bottom:20px;opacity:.8}
.main-box{background:linear-gradient(135deg,#064e3b,#065f46);padding:25px;border-radius:24px;border:1px solid #10b981;margin-bottom:15px;box-shadow:0 10px 15px rgba(0,0,0,.3)}
.total-label{font-size:16px;color:#a7f3d0;text-transform:uppercase;margin-bottom:5px;font-weight:700}
.total-value{font-size:90px;font-weight:900;color:#fff;line-height:1}
.control-group{display:flex;gap:12px;margin-bottom:15px}
.btn{flex:1;border:none;padding:18px;font-size:18px;font-weight:700;border-radius:18px;cursor:pointer;transition:all .1s;color:#fff;text-transform:uppercase;box-shadow:0 4px 6px rgba(0,0,0,.15)}
.btn-start{background:#0284c7;border:1px solid #38bdf8}
.btn-start.running{background:#d97706;border:1px solid #fbbf24}
.btn-reset{background:#dc2626;border:1px solid #f87171}
.stat-card{background:#1e293b;padding:20px;border-radius:20px;border:1px solid #334155;margin-bottom:15px}
.stat-label{font-size:16px;color:#94a3b8;text-transform:uppercase;margin-bottom:8px;font-weight:700}
.stat-value{font-size:54px;font-weight:800;color:#f1f5f9;line-height:1.1}
.accent-blue{color:#38bdf8}
.accent-yellow{color:#fbbf24}
.accent-rose{color:#f43f5e}
.accent-emerald{color:#34d399}
.accent-purple{color:#a855f7}
.badge{display:inline-block;padding:4px 12px;border-radius:99px;font-size:12px;font-weight:700;margin-bottom:12px}
.badge-online{background:#064e3b;color:#34d399;border:1px solid #10b981}
.error-banner{background:#7f1d1d;border:2px solid #ef4444;color:#fecaca;padding:15px;border-radius:18px;margin-bottom:15px;font-weight:700;display:none}
.settings-card{background:#1e293b;padding:20px;border-radius:20px;border:1px solid #334155;margin-bottom:15px;text-align:left}
.settings-title{font-size:16px;color:#94a3b8;text-transform:uppercase;font-weight:700;margin-bottom:16px;text-align:center}
.setting-row{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;gap:10px}
.setting-label{font-size:14px;color:#cbd5e1;min-width:90px;font-weight:600}
.setting-input{background:#0f172a;border:1px solid #475569;border-radius:10px;color:#f8fafc;font-size:15px;padding:8px 12px;width:90px;text-align:center}
.setting-input:focus{outline:none;border-color:#38bdf8}
.select-input{background:#0f172a;border:1px solid #475569;border-radius:10px;color:#f8fafc;font-size:14px;padding:8px 10px;flex:1}
.btn-save{width:100%;background:#0369a1;border:none;padding:14px;font-size:16px;font-weight:700;border-radius:14px;cursor:pointer;color:#fff;margin-top:8px;border:1px solid #38bdf8}
.btn-save:active{background:#075985}
.btn-clear{width:100%;background:#7f1d1d;border:none;padding:14px;font-size:16px;font-weight:700;border-radius:14px;cursor:pointer;color:#fff;margin-top:8px;border:1px solid #f87171}
.btn-clear:active{background:#991b1b}
.save-ok{color:#34d399;font-size:13px;margin-top:8px;display:none;text-align:center}
</style>
<script>
function sendCmd(p,needConfirm){if(needConfirm&&!confirm('ต้องการรีเซ็ตสถิติทั้งหมดใช่หรือไม่?'))return;fetch(p)}
function updateStats(){fetch('/data').then(r=>r.json()).then(d=>{
var e=document.getElementById('errBanner');
if(d.eeErr||d.lsErr){e.style.display='block';e.innerText=d.eeErr?'⚠️ HARDWARE ERROR: NVS/EEPROM':'⚠️ HARDWARE ERROR: VL53L0X NOT FOUND'}
else e.style.display='none';
document.getElementById('totalCount').innerText=d.total;
document.getElementById('currentSet').innerHTML=d.cSet+' <span style="font-size:24px;color:#64748b">/</span> '+d.tSet;
document.getElementById('repsInSet').innerHTML=d.count+' <span style="font-size:24px;color:#64748b">/</span> '+d.target;
document.getElementById('calories').innerText=d.cal.toFixed(1);
document.getElementById('wTime').innerText=d.wTime;
document.getElementById('uTime').innerText=d.uTime;
document.getElementById('calMode').innerText=d.calMode==0?'MET':'Simple';
var b=document.getElementById('btnStart');
if(d.state==1){b.innerText='PAUSE ⏸';b.className='btn btn-start running'}
else{b.innerText='START ▶';b.className='btn btn-start'}
if(document.getElementById('histPushups')){
document.getElementById('histPushups').innerText=d.histPushups;
document.getElementById('histCal').innerHTML=d.histCal.toFixed(1)+' <span style="font-size:14px">kcal</span>';
document.getElementById('histSess').innerText=d.histSess;
}})}
function loadSettings(){fetch('/data').then(r=>r.json()).then(d=>{
document.getElementById('sTarget').value=d.target;
document.getElementById('sSets').value=d.tSet;
document.getElementById('sRest').value=d.rest;
document.getElementById('sWeight').value=d.weight;
document.getElementById('sTDown').value=d.threshDn;
document.getElementById('sTUp').value=d.threshUp;
document.getElementById('sScreen').value=d.screenTo;
document.getElementById('sCalMode').value=d.calMode;
})}
function saveSettings(){
var p=new URLSearchParams();
p.append('target', document.getElementById('sTarget').value);
p.append('sets',   document.getElementById('sSets').value);
p.append('rest',   document.getElementById('sRest').value);
p.append('weight', document.getElementById('sWeight').value);
p.append('tdown',  document.getElementById('sTDown').value);
p.append('tup',    document.getElementById('sTUp').value);
p.append('screen', document.getElementById('sScreen').value);
p.append('calmode',document.getElementById('sCalMode').value);
fetch('/settings?'+p.toString()).then(r=>r.text()).then(t=>{
var ok=document.getElementById('saveOk');
ok.style.display='block';ok.innerText=t;setTimeout(()=>ok.style.display='none',2500)
})}
setInterval(updateStats,1000);updateStats();loadSettings();
function clearHistory(){
if(!confirm('Clear ALL lifetime history? This cannot be undone!'))return;
fetch('/clearhistory').then(r=>r.text()).then(t=>{
document.getElementById('clearOk').style.display='block';
document.getElementById('clearOk').innerText=t;
document.getElementById('histPushups').innerText='0';
document.getElementById('histCal').innerHTML='0 <span style="font-size:14px">kcal</span>';
document.getElementById('histSess').innerText='0';
setTimeout(()=>document.getElementById('clearOk').style.display='none',2500)
})}
</script>
</head>
<body>
<div class='container'>
<h1>🏋️ PUSH-UP MONITOR</h1>
<span class='badge badge-online'>● ONLINE</span>
<div id='errBanner' class='error-banner'>⚠️ HARDWARE ERROR</div>
<div class='main-box'>
<div class='total-label'>Total Reps</div>
<div class='total-value' id='totalCount'>0</div>
</div>
<div class='control-group'>
<button id='btnStart' class='btn btn-start' onclick='sendCmd("/start")'>START ▶</button>
<button class='btn btn-reset' onclick='sendCmd("/reset",true)'>RESET ↺</button>
</div>
<div class='stat-card' style='border-color:#34d399'>
<div class='stat-label' style='color:#a7f3d0'>Workout Time</div>
<div class='stat-value accent-emerald' id='wTime'>00:00:00</div>
</div>
<div class='stat-card' style='border-color:#a855f7'>
<div class='stat-label' style='color:#d8b4fe'>System Uptime</div>
<div class='stat-value accent-purple' id='uTime'>00:00:00</div>
</div>
<div class='stat-card' style='border-color:#f43f5e'>
<div class='stat-label' style='color:#fda4af'>Calories Burned</div>
<div class='stat-value accent-rose'>
<span id='calories'>0.0</span>
<span style='font-size:18px;color:#94a3b8'>kcal</span>
</div>
<div style='font-size:12px;color:#64748b;margin-top:6px'>
Mode: <span id='calMode'>MET</span>
</div>
</div>
<div class='stat-card'>
<div class='stat-label'>Current Set</div>
<div class='stat-value accent-blue' id='currentSet'>1 / 1</div>
</div>
<div class='stat-card'>
<div class='stat-label'>Reps in Set</div>
<div class='stat-value accent-yellow' id='repsInSet'>0 / 10</div>
</div>
<div class='settings-card'>
<div class='settings-title'>⚙️ Settings</div>
<div class='setting-row'>
<span class='setting-label'>Target</span>
<input id='sTarget' class='setting-input' type='number' min='1' max='99' value='10'>
</div>
<div class='setting-row'>
<span class='setting-label'>Sets</span>
<input id='sSets' class='setting-input' type='number' min='1' max='20' value='1'>
</div>
<div class='setting-row'>
<span class='setting-label'>Rest (s)</span>
<input id='sRest' class='setting-input' type='number' min='0' max='60' value='30'>
</div>
<div class='setting-row'>
<span class='setting-label'>Weight (kg)</span>
<input id='sWeight' class='setting-input' type='number' min='20' max='150' step='0.5' value='70.0'>
</div>
<div class='setting-row'>
<span class='setting-label'>T-Down (cm)</span>
<input id='sTDown' class='setting-input' type='number' min='3' max='50' value='15'>
</div>
<div class='setting-row'>
<span class='setting-label'>T-Up (cm)</span>
<input id='sTUp' class='setting-input' type='number' min='6' max='70' value='25'>
</div>
<div class='setting-row'>
<span class='setting-label'>Screen (min)</span>
<input id='sScreen' class='setting-input' type='number' min='0' max='60' value='5'>
</div>
<div class='setting-row'>
<span class='setting-label'>Cal Mode</span>
<select id='sCalMode' class='select-input'>
<option value='0'>MET (time-based)</option>
<option value='1'>Simple (per-rep)</option>
</select>
</div>
<button class='btn-save' onclick='saveSettings()'>💾 Save &amp; Store to NVS</button>
<div id='saveOk' class='save-ok'></div>
</div>
<div class='settings-card' style='border-color:#a855f7'>
<div class='settings-title'>📊 Lifetime History</div>
<div class='setting-row'>
<span class='setting-label' style='color:#a7f3d0'>Total Pushups</span>
<span style='font-size:28px;font-weight:800;color:#22c55e' id='histPushups'>0</span>
</div>
<div class='setting-row'>
<span class='setting-label' style='color:#fda4af'>Total Calories</span>
<span style='font-size:28px;font-weight:800;color:#f43f5e' id='histCal'>0.0 <span style='font-size:14px'>kcal</span></span>
</div>
<div class='setting-row'>
<span class='setting-label' style='color:#d8b4fe'>Sessions</span>
<span style='font-size:28px;font-weight:800;color:#a855f7' id='histSess'>0</span>
</div>
<button class='btn-clear' onclick='clearHistory()'>🗑️ Clear History</button>
<div id='clearOk' class='save-ok'></div>
</div>
</div>
</body>
</html>
)rawliteral";

String getDataJSON() {
    char buf[512];
    int state = app.isRunning ? 1 : (app.isResting ? 2 : (app.isComplete ? 3 : 0));
    snprintf(buf, sizeof(buf),
        "{\"total\":%d,\"count\":%d,\"target\":%d,"
        "\"cSet\":%d,\"tSet\":%d,"
        "\"cal\":%.1f,\"calMode\":%d,"
        "\"wTime\":\"%s\",\"uTime\":\"%s\","
        "\"state\":%d,"
        "\"rest\":%d,\"weight\":%.1f,"
        "\"threshDn\":%d,\"threshUp\":%d,"
        "\"screenTo\":%d,"
        "\"histPushups\":%lu,\"histCal\":%.1f,\"histSess\":%lu,"
        "\"eeErr\":%d,\"lsErr\":%d}",
        app.totalCount, app.count, app.targetPerSet,
        app.currentSet, app.totalSets,
        app.caloriesBurned, app.calMode,
        getWorkoutTimeStr().c_str(),
        getSystemUptimeString().c_str(),
        state,
        app.restDurationSec, app.bodyWeight,
        app.threshDown, app.threshUp,
        app.screenTimeout,
        (unsigned long)lifetimePushups,
        lifetimeCalories,
        (unsigned long)lifetimeSessions,
        nvsAvailable ? 0 : 1,
        sensorOK ? 0 : 1
    );
    return String(buf);
}

void wifiResetAndReboot() {
    lv_obj_t *resetScr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(resetScr, lv_color_black(), 0);
    lv_scr_load(resetScr);

    lv_obj_t *msg = lv_label_create(resetScr);
    lv_label_set_text(msg,
        LV_SYMBOL_WIFI " WiFi Reset!\n\n"
        "Clearing saved WiFi...\n"
        "Rebooting in 2s\n\n"
        "Connect to:\n\"" WIFI_AP_NAME "\"");
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(msg, COL_YELLOW, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(msg, 0, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
    lv_task_handler();
    delay(100);

    AsyncWiFiManager wm(&webServer, &dns);
    wm.resetSettings();
    Serial.println("WiFi settings cleared — rebooting...");

    delay(2000);
    ESP.restart();
}

// =============================================================
// showHistoryOverlay() — แสดงหน้า HISTORY พร้อมปุ่ม CLEAR
// =============================================================
void showHistoryOverlay_landscape() {
    if (historyOverlay) return;

    historyOverlay = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(historyOverlay, LV_COLOR_MAKE(0x0F,0x17,0x2A), 0);
    lv_obj_set_style_bg_opa(historyOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(historyOverlay, 0, 0);
    lv_obj_set_style_radius(historyOverlay, 0, 0);
    lv_obj_clear_flag(historyOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(historyOverlay);
    lv_label_set_text(title, "LIFETIME HISTORY");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, LV_COLOR_MAKE(0x22,0xD3,0xEE), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title, 0, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    auto makeStatBlock = [](lv_obj_t *parent, const char *label, lv_color_t col,
                            int y) -> lv_obj_t* {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, LV_COLOR_MAKE(0x94,0xA3,0xB8), 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(lbl, 0, 0);
        lv_obj_set_pos(lbl, 16, y);
        lv_obj_t *val = lv_label_create(parent);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(val, col, 0);
        lv_obj_set_style_bg_opa(val, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(val, 0, 0);
        lv_obj_set_pos(val, 16, y + 16);
        return val;
    };

    char buf[32];

    hist_lbl_pushups = makeStatBlock(historyOverlay, "Total Pushups",
                                     LV_COLOR_MAKE(0x22,0xC5,0x5E), 38);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)lifetimePushups);
    lv_label_set_text(hist_lbl_pushups, buf);

    hist_lbl_calories = makeStatBlock(historyOverlay, "Total Calories (kcal)",
                                      LV_COLOR_MAKE(0xFB,0xBF,0x24), 98);
    snprintf(buf, sizeof(buf), "%.1f", lifetimeCalories);
    lv_label_set_text(hist_lbl_calories, buf);

    hist_lbl_sessions = makeStatBlock(historyOverlay, "Sessions",
                                      LV_COLOR_MAKE(0xA8,0x55,0xF7), 158);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)lifetimeSessions);
    lv_label_set_text(hist_lbl_sessions, buf);

    static lv_obj_t *btn_clear_hist = NULL;
    static unsigned long clearPressStartTime = 0;
    
    btn_clear_hist = lv_btn_create(historyOverlay);
    lv_obj_set_pos(btn_clear_hist, 230, 200);
    lv_obj_set_size(btn_clear_hist, 80, 34);
    lv_obj_set_style_bg_color(btn_clear_hist, LV_COLOR_MAKE(0x7F, 0x00, 0x00), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_clear_hist, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_clear_hist, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_clear_hist, 1, LV_PART_MAIN);
    lv_obj_clear_flag(btn_clear_hist, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_clear = lv_label_create(btn_clear_hist);
    lv_label_set_text(lbl_clear, "CLEAR");
    lv_obj_set_style_text_font(lbl_clear, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_clear, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lbl_clear, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lbl_clear, 0, 0);
    lv_obj_center(lbl_clear);

    lv_obj_add_event_cb(btn_clear_hist, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED) {
            clearPressStartTime = millis();
            lv_obj_set_style_bg_color(lv_event_get_target(e),
                LV_COLOR_MAKE(0xFF, 0x00, 0x00), LV_PART_MAIN);
        }
    }, LV_EVENT_PRESSED, NULL);

    lv_obj_add_event_cb(btn_clear_hist, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            lv_obj_set_style_bg_color(lv_event_get_target(e),
                LV_COLOR_MAKE(0x7F, 0x00, 0x00), LV_PART_MAIN);
            if (clearPressStartTime > 0 && (millis() - clearPressStartTime >= 2000)) {
                clearHistory();
                beepClearHistory();
                if (historyOverlay) ui_update_history_overlay();
                beepConfirm();
            }
            clearPressStartTime = 0;
        }
    }, LV_EVENT_RELEASED, NULL);
    
    lv_obj_add_event_cb(btn_clear_hist, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_PRESS_LOST) {
            lv_obj_set_style_bg_color(lv_event_get_target(e),
                LV_COLOR_MAKE(0x7F, 0x00, 0x00), LV_PART_MAIN);
            clearPressStartTime = 0;
        }
    }, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t *hint = lv_label_create(historyOverlay);
    lv_label_set_text(hint, "[ tap to close ]");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, LV_COLOR_MAKE(0x47,0x55,0x69), 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hint, 0, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);

    lv_obj_add_flag(historyOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(historyOverlay, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            hideHistoryOverlay();
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(historyOverlay);
    blockTouchUntil = millis() + 600;
    lv_task_handler();
}

void showHistoryOverlay_portrait() {
    if (historyOverlay) return;

    historyOverlay = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(historyOverlay, LV_COLOR_MAKE(0x0F,0x17,0x2A), 0);
    lv_obj_set_style_bg_opa(historyOverlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(historyOverlay, 0, 0);
    lv_obj_set_style_radius(historyOverlay, 0, 0);
    lv_obj_clear_flag(historyOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(historyOverlay);
    lv_label_set_text(title, "LIFETIME HISTORY");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, LV_COLOR_MAKE(0x22,0xD3,0xEE), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title, 0, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    auto makeStatBlock = [](lv_obj_t *parent, const char *label, lv_color_t col,
                            int y) -> lv_obj_t* {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, LV_COLOR_MAKE(0x94,0xA3,0xB8), 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(lbl, 0, 0);
        lv_obj_set_pos(lbl, 16, y);
        lv_obj_t *val = lv_label_create(parent);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(val, col, 0);
        lv_obj_set_style_bg_opa(val, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(val, 0, 0);
        lv_obj_set_pos(val, 16, y + 16);
        return val;
    };

    char buf[32];

    hist_lbl_pushups = makeStatBlock(historyOverlay, "Total Pushups",
                                     LV_COLOR_MAKE(0x22,0xC5,0x5E), 38);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)lifetimePushups);
    lv_label_set_text(hist_lbl_pushups, buf);

    hist_lbl_calories = makeStatBlock(historyOverlay, "Total Calories (kcal)",
                                      LV_COLOR_MAKE(0xFB,0xBF,0x24), 98);
    snprintf(buf, sizeof(buf), "%.1f", lifetimeCalories);
    lv_label_set_text(hist_lbl_calories, buf);

    hist_lbl_sessions = makeStatBlock(historyOverlay, "Sessions",
                                      LV_COLOR_MAKE(0xA8,0x55,0xF7), 158);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)lifetimeSessions);
    lv_label_set_text(hist_lbl_sessions, buf);

    static lv_obj_t *btn_clear_hist = NULL;
    static unsigned long clearPressStartTime = 0;
    
    btn_clear_hist = lv_btn_create(historyOverlay);
    lv_obj_set_pos(btn_clear_hist, 152, 280);
    lv_obj_set_size(btn_clear_hist, 80, 34);
    lv_obj_set_style_bg_color(btn_clear_hist, LV_COLOR_MAKE(0x7F, 0x00, 0x00), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_clear_hist, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_clear_hist, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_clear_hist, 1, LV_PART_MAIN);
    lv_obj_clear_flag(btn_clear_hist, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_clear = lv_label_create(btn_clear_hist);
    lv_label_set_text(lbl_clear, "CLEAR");
    lv_obj_set_style_text_font(lbl_clear, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_clear, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lbl_clear, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lbl_clear, 0, 0);
    lv_obj_center(lbl_clear);

    lv_obj_add_event_cb(btn_clear_hist, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED) {
            clearPressStartTime = millis();
            lv_obj_set_style_bg_color(lv_event_get_target(e),
                LV_COLOR_MAKE(0xFF, 0x00, 0x00), LV_PART_MAIN);
        }
    }, LV_EVENT_PRESSED, NULL);

    lv_obj_add_event_cb(btn_clear_hist, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            lv_obj_set_style_bg_color(lv_event_get_target(e),
                LV_COLOR_MAKE(0x7F, 0x00, 0x00), LV_PART_MAIN);
            if (clearPressStartTime > 0 && (millis() - clearPressStartTime >= 2000)) {
                clearHistory();
                beepClearHistory();
                if (historyOverlay) ui_update_history_overlay();
                beepConfirm();
            }
            clearPressStartTime = 0;
        }
    }, LV_EVENT_RELEASED, NULL);
    
    lv_obj_add_event_cb(btn_clear_hist, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_PRESS_LOST) {
            lv_obj_set_style_bg_color(lv_event_get_target(e),
                LV_COLOR_MAKE(0x7F, 0x00, 0x00), LV_PART_MAIN);
            clearPressStartTime = 0;
        }
    }, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t *hint = lv_label_create(historyOverlay);
    lv_label_set_text(hint, "[ tap to close ]");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, LV_COLOR_MAKE(0x47,0x55,0x69), 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hint, 0, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 12, -5);

    lv_obj_add_flag(historyOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(historyOverlay, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            hideHistoryOverlay();
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_scr_load(historyOverlay);
    blockTouchUntil = millis() + 600;
    lv_task_handler();
}

void showHistoryOverlay() {
    if (bootOrientation == ORIENT_LANDSCAPE) {
        showHistoryOverlay_landscape();
    } else {
        showHistoryOverlay_portrait();
    }
}

void hideHistoryOverlay() {
    if (!historyOverlay) return;
    lv_obj_t *old = historyOverlay;
    historyOverlay = NULL;
    hist_lbl_pushups  = NULL;
    hist_lbl_calories = NULL;
    hist_lbl_sessions = NULL;
    lv_scr_load(mainScr);
    lv_obj_del_async(old);
    blockTouchUntil = millis() + 400;
    lv_task_handler();
    showSettingsScreen();
}

void ui_update_history_overlay() {
    if (!historyOverlay) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)lifetimePushups);
    lv_label_set_text(hist_lbl_pushups, buf);
    snprintf(buf, sizeof(buf), "%.1f", lifetimeCalories);
    lv_label_set_text(hist_lbl_calories, buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)lifetimeSessions);
    lv_label_set_text(hist_lbl_sessions, buf);
}

void setupWiFiDashboard() {
    lv_obj_t *wifiScr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(wifiScr, lv_color_black(), 0);
    lv_scr_load(wifiScr);

    lv_obj_t *wifiLbl = lv_label_create(wifiScr);
    lv_label_set_text(wifiLbl,
        LV_SYMBOL_WIFI " ONLINE MODE\n\nConnecting WiFi...\n\n"
        "First time?\nConnect to:\n\"" WIFI_AP_NAME "\"");
    lv_obj_set_style_text_font(wifiLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wifiLbl, COL_YELLOW, 0);
    lv_obj_set_style_text_align(wifiLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(wifiLbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifiLbl, 0, 0);
    lv_obj_align(wifiLbl, LV_ALIGN_CENTER, 0, 0);
    lv_task_handler();

    delay(800);
    AsyncWiFiManager wm(&webServer, &dns);
    wm.setConnectTimeout(30);
    wm.setConfigPortalTimeout(120);
    bool connected = wm.autoConnect(WIFI_AP_NAME);

    lv_scr_load(mainScr);
    lv_obj_del(wifiScr);

    if (!connected) {
        isOnlineMode = false;
        Serial.println("WiFi failed — offline mode");
        return;
    }

    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());

    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    webServer.on("/data", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", getDataJSON());
    });

    webServer.on("/start", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!app.isRunning && !app.isComplete && !app.isResting) {
            app.isRunning = true;
            app.isDown = false;
            app.downStartTime = 0;
            app.lastMovementTime = millis();
            app.isTimerPaused = false;
            app.downCounter = 0;
            app.upCounter = 0;
            workoutTimerStart();
            beepClick();
        } else if (app.isRunning) {
            app.isRunning = false;
            workoutTimerPause();
            app.isTimerPaused = true;
            updateCalories();
            beepConfirm();
        } else if (app.isComplete) {
            app.count = 0; app.totalCount = 0;
            app.currentSet = 1; app.caloriesBurned = 0;
            app.isComplete = false;
            app.isDown = false;
            app.downStartTime = 0;
            app.lastMovementTime = millis();
            app.isTimerPaused = false;
            app.downCounter = 0;
            app.upCounter = 0;
            workoutTimerReset();
            workoutTimerStart();
            app.isRunning = true;
            beepClick();
        }
        webUIUpdatePending = true;  // ให้ loop() เป็นคนอัปเดตหน้าจอแทน (thread-safe)
        req->send(200, "text/plain", "OK");
    });

    webServer.on("/reset", HTTP_GET, [](AsyncWebServerRequest *req) {
        app.count = 0; app.totalCount = 0;
        app.currentSet = 1; app.caloriesBurned = 0;
        app.isRunning = false; app.isResting = false;
        app.isComplete = false; app.isDown = false;
        app.downStartTime = 0;
        app.lastMovementTime = 0;
        app.isTimerPaused = false;
        app.downCounter = 0;
        app.upCounter = 0;
        workoutTimerReset();
        beepConfirm();
        webUIUpdatePending = true;
        req->send(200, "text/plain", "OK");
    });

    webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (app.isRunning) {
            req->send(400, "text/plain", "Cannot change settings while running!");
            return;
        }

        bool changed = false;

        if (req->hasParam("target")) {
            int v = req->getParam("target")->value().toInt();
            if (v >= 1 && v <= 99) { app.targetPerSet = v; changed = true; }
        }
        if (req->hasParam("sets")) {
            int v = req->getParam("sets")->value().toInt();
            if (v >= 1 && v <= 20) { app.totalSets = v; changed = true; }
        }
        if (req->hasParam("rest")) {
            int v = req->getParam("rest")->value().toInt();
            if (v >= 0 && v <= 60) { app.restDurationSec = v; changed = true; }
        }
        if (req->hasParam("weight")) {
            float v = req->getParam("weight")->value().toFloat();
            if (v >= 20.0f && v <= 150.0f) { app.bodyWeight = v; changed = true; }
        }
        if (req->hasParam("tdown")) {
            int v = req->getParam("tdown")->value().toInt();
            if (v >= 3 && v < app.threshUp - 2) { app.threshDown = v; changed = true; }
        }
        if (req->hasParam("tup")) {
            int v = req->getParam("tup")->value().toInt();
            if (v > app.threshDown + 2 && v <= 70) { app.threshUp = v; changed = true; }
        }
        if (req->hasParam("screen")) {
            int v = req->getParam("screen")->value().toInt();
            if (v >= 0 && v <= 60) { app.screenTimeout = v; changed = true; }
        }
        if (req->hasParam("calmode")) {
            int v = req->getParam("calmode")->value().toInt();
            if (v == 0 || v == 1) { app.calMode = v; changed = true; }
        }

        if (changed) {
            saveSettings();
            updateCalories();
            webUIUpdatePending = true;
            req->send(200, "text/plain", "✅ Saved to NVS");
        } else {
            req->send(400, "text/plain", "❌ Invalid values");
        }
    });

    webServer.on("/clearhistory", HTTP_GET, [](AsyncWebServerRequest *req) {
        clearHistory();
        webUIUpdatePending = true;
        req->send(200, "text/plain", "✅ History cleared");
    });

    webServer.onNotFound([](AsyncWebServerRequest *req) {
        req->redirect("/");
    });

    webServer.begin();
    Serial.println("Web server started");
}

// ==========================================================================================
// เพิ่มขนาด stack ของ loop() task
// ==========================================================================================
uint32_t getArduinoLoopTaskStackSize(void) {
    return 24576;
}

// ==========================================================================================
// Setup
// ==========================================================================================
void setup() {
    delay(300);
    Serial.begin(115200);
    Serial.print("Push-up Counter v");
    Serial.print(FW_VERSION);
    Serial.println(" (LVGL) starting...");
    pinMode(4, OUTPUT);
    pinMode(16, OUTPUT);
    pinMode(17, OUTPUT);
    digitalWrite(4, HIGH);   // ปิดไฟสีแดง
    digitalWrite(16, HIGH);  // ปิดไฟสีเขียว
    digitalWrite(17, HIGH);  // ปิดไฟสีน้ำเงิน
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    pinMode(PIN_MODE_SWITCH, INPUT_PULLUP);
    delay(10);
    isOnlineMode = (digitalRead(PIN_MODE_SWITCH) == LOW);

    // NVS (Preferences) — ไม่ต้องตรวจสอบฮาร์ดแวร์เพิ่ม
    nvsAvailable = true;
    Serial.println("NVS (Preferences) ready");

    loadSettings();
    loadHistory();
    bootOrientation = app.orientation;

    if (bootOrientation == ORIENT_LANDSCAPE) {
        SCR_W = SCR_W_LANDSCAPE;  SCR_H = SCR_H_LANDSCAPE;
        TOUCH_X_MIN = TOUCH_X_MIN_LAND; TOUCH_X_MAX = TOUCH_X_MAX_LAND;
        TOUCH_Y_MIN = TOUCH_Y_MIN_LAND; TOUCH_Y_MAX = TOUCH_Y_MAX_LAND;
    } else {
        SCR_W = SCR_W_PORTRAIT;  SCR_H = SCR_H_PORTRAIT;
        TOUCH_X_MIN = TOUCH_X_MIN_PORT; TOUCH_X_MAX = TOUCH_X_MAX_PORT;
        TOUCH_Y_MIN = TOUCH_Y_MIN_PORT; TOUCH_Y_MAX = TOUCH_Y_MAX_PORT;
    }

    ledcSetup(0, 2000, 8);
    ledcAttachPin(PIN_BUZZER, 0);

    mySPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(mySPI);
    ts.setRotation(bootOrientation == ORIENT_LANDSCAPE ? 1 : 0);

    if (!lox.begin()) {
        Serial.println("VL53L0X NOT FOUND");
        sensorOK = false;
    } else {
        Serial.println("VL53L0X OK");
        sensorOK = true;
    }

    tft.begin();
    tft.setRotation(bootOrientation == ORIENT_LANDSCAPE ? 1 : 0);
    tft.fillScreen(TFT_BLACK);

    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);

    lv_init();

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCR_W * BUF_LINES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = SCR_W;
    disp_drv.ver_res  = SCR_H;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type     = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb  = my_touchpad_read;
    indev_drv.long_press_time = 2000;
    lv_indev_drv_register(&indev_drv);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *splash = lv_label_create(scr);
    char splashBuf[96];
    snprintf(splashBuf, sizeof(splashBuf),
             "PUSH-UP\nCOUNTER\nV%s LVGL\n%s\n%s",
             FW_VERSION, FW_AUTHOR, FW_BUILD_DATE);
    lv_label_set_text(splash, splashBuf);
    lv_obj_set_style_text_font(splash, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(splash, COL_CYAN, 0);
    lv_obj_set_style_text_align(splash, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(splash, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(splash, 0, 0);
    lv_obj_align(splash, LV_ALIGN_CENTER, 0, 0);
    lv_task_handler();
    delay(3000);
    lv_obj_del(splash);

    ui_build();
    ui_update_all();
    lastTouchTime = millis();

    if (isOnlineMode) {
        setupWiFiDashboard();
        if (WiFi.isConnected()) {
            char ipbuf[32];
            snprintf(ipbuf, sizeof(ipbuf), LV_SYMBOL_WIFI " %s",
                     WiFi.localIP().toString().c_str());
            lv_label_set_text(lbl_workout_time, ipbuf);
            lv_obj_set_style_text_color(lbl_workout_time, COL_GREEN, 0);
        }
    }

    if (!sensorOK) {
        lv_obj_t *err = lv_label_create(lv_scr_act());
        lv_label_set_text(err, "! VL53L0X\nNOT FOUND");
        lv_obj_set_style_text_font(err, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(err, COL_RED, 0);
        lv_obj_set_style_text_align(err, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_opa(err, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(err, 0, 0);
        lv_obj_align(err, LV_ALIGN_TOP_RIGHT, -4, 25);
    }

    // ไม่ต้องแสดงข้อความ EEPROM NOT FOUND อีกต่อไป (NVS ทำงานได้ตลอด)

    Serial.println("Ready!");
}

// ==========================================================================================
// Loop
// ==========================================================================================
void loop() {
    lv_task_handler();
    buzzUpdate();

    // ---- ประมวลผลคำขออัปเดตหน้าจอที่มาจาก Web Dashboard ปลอดภัยเพราะรันบน task
    // เดียวกับ lv_task_handler() ตรงนี้ ต่างจากการเรียก ui_update_all() ตรงๆ
    // จาก route handler ของ AsyncWebServer ซึ่งอยู่คนละ task และไม่ thread-safe ----
    if (webUIUpdatePending) {
        webUIUpdatePending = false;
        ui_update_all();
    }

    static unsigned long lastDistUpdate  = 0;
    static unsigned long lastRestUpdate  = 0;
    static unsigned long lastStatusUpdate = 0;
    static unsigned long lastTimeUpdate  = 0;

    // =============================================================
    // Screen timeout
    // =============================================================
    if (app.isRunning || app.isResting) {
        lastTouchTime = millis();
        if (!screenOn) screenWake();
    }
    if (app.screenTimeout > 0 && screenOn
        && !app.isRunning && !app.isResting
        && (millis() - lastTouchTime > (unsigned long)app.screenTimeout * 60000UL)) {
        screenSleep();
    }

    // =============================================================
    // Sensor read — พร้อมกรองสัญญาณรบกวน (NEW!)
    // =============================================================
    if (app.isRunning && !app.isResting && sensorOK) {
        VL53L0X_RangingMeasurementData_t measure;
        lox.rangingTest(&measure, false);

        if (measure.RangeStatus != 4) {
            long distance = measure.RangeMilliMeter / 10;

            if (millis() - lastDistUpdate > 100) {
                lastDistUpdate = millis();
                if (abs(distance - app.lastDistance) > 1) {
                    app.lastDistance = distance;
                    ui_update_info();
                }
            }

            if (distance > 2 && distance < 80) {
                
                // ---- ตรวจจับการก้มลง (DOWN) พร้อมกรองสัญญาณ ----
                if (distance < app.threshDown) {
                    app.downCounter++;
                    app.upCounter = 0;
                    
                    if (app.downCounter >= app.minDownSamples && !app.isDown) {
                        app.isDown = true;
                        app.downStartTime = millis();
                        lastTouchTime = millis();
                        ui_update_info();
                    }
                } 
                // ---- ตรวจจับการดันขึ้น (UP) พร้อมกรองสัญญาณ ----
                else if (distance > app.threshUp) {
                    app.upCounter++;
                    app.downCounter = 0;
                    
                    if (app.upCounter >= app.minUpSamples && app.isDown) {
                        // ✅ ตรวจสอบระยะเวลาขั้นต่ำที่ต้องอยู่ในท่าก้ม
                        unsigned long downDuration = millis() - app.downStartTime;
                        
                        if (downDuration >= (unsigned long)app.minDownTimeMs) {
                            // =============================================================
                            // นับ 1 ครั้ง !!
                            // =============================================================
                            app.isDown = false;
                            app.downStartTime = 0;
                            app.downCounter = 0;
                            app.upCounter = 0;
                            
                            app.count++;
                            app.totalCount++;
                            app.lastMovementTime = millis();
                            lastTouchTime = millis();
                            if (!screenOn) screenWake();
                            updateCalories();
                            beepClick();
                            ui_update_count();
                            ui_update_info();

                            if (app.count >= app.targetPerSet) {
                                if (app.currentSet < app.totalSets) {
                                    app.isRunning   = false;
                                    app.isResting   = true;
                                    app.restStartTime = millis();
                                    lastTouchTime = millis();
                                    beepConfirm();
                                    ui_update_all();
                                } else {
                                    app.isRunning  = false;
                                    app.isComplete = true;
                                    workoutTimerPause();
                                    app.isTimerPaused = true;
                                    updateCalories();
                                    lifetimePushups  += app.totalCount;
                                    lifetimeCalories += app.caloriesBurned;
                                    lifetimeSessions++;
                                    saveHistory();
                                    lastTouchTime = millis();
                                    beepComplete();
                                    ui_update_all();
                                }
                            }
                        } else {
                            // ❌ ก้มลงไม่นานพอ — รีเซ็ตสถานะ
                            app.isDown = false;
                            app.downStartTime = 0;
                            app.downCounter = 0;
                            app.upCounter = 0;
                        }
                    }
                } else {
                    // ระยะทางอยู่ระหว่าง T-Down และ T-Up
                    app.downCounter = 0;
                    app.upCounter = 0;
                }
            }
        }
    }

    // =============================================================
    // Rest countdown
    // =============================================================
    if (app.isResting) {
        unsigned long elapsed = millis() - app.restStartTime;
        if (elapsed >= (unsigned long)app.restDurationSec * 1000) {
            app.isResting = false;
            app.currentSet++;
            app.count = 0;
            app.isRunning = true;
            app.lastMovementTime = millis();
            app.isTimerPaused = false;
            app.downCounter = 0;
            app.upCounter = 0;
            lastTouchTime = millis();
            beepConfirm();
            ui_update_all();
        } else if (millis() - lastRestUpdate > 1000) {
            lastRestUpdate = millis();
            ui_update_status();
        }
    }

    // =============================================================
    // Auto reset isDown — ถ้าค้าง DOWN นานเกิน 3 วินาที
    // =============================================================
    if (app.isDown && (millis() - app.downStartTime > 3000)) {
        app.isDown = false;
        app.downStartTime = 0;
        app.downCounter = 0;
        app.upCounter = 0;
    }

    // =============================================================
    // หยุด Timer MET — ถ้าไม่มีการเคลื่อนไหวเกิน 5 วินาที
    // =============================================================
    if (app.isRunning && !app.isResting && sensorOK) {
        if (!app.isTimerPaused && (millis() - app.lastMovementTime > 5000)) {
            workoutTimerPause();
            app.isTimerPaused = true;
        }
        else if (app.isTimerPaused && (millis() - app.lastMovementTime < 5000)) {
            workoutTimerStart();
            app.isTimerPaused = false;
        }
    }

    // =============================================================
    // Periodic updates
    // =============================================================
    if (millis() - lastStatusUpdate > 500) {
        lastStatusUpdate = millis();
        ui_update_status();
    }

    if (app.workoutTimerActive && millis() - lastTimeUpdate > 1000) {
        lastTimeUpdate = millis();
        updateCalories();
        ui_update_workout_time();
        ui_update_info();
    }

    delay(5);
}
