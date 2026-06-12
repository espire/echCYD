// --- Target Device Configuration ---
// Uncomment the line below if you are using the 3.5-inch CYD with capacitive touch (ST7796 display + GT911 touch).
// Keep it commented out if you are using the standard 2.8-inch CYD (ILI9341 display + XPT2046 touch).
#define CYD_35_CAPACITIVE

#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

#ifdef CYD_35_CAPACITIVE
  #include <Wire.h>
  #include <TAMC_GT911.h>
#else
  #include <XPT2046_Touchscreen.h>
#endif

// --- Hardware Pins & Dimensions ---
#ifdef CYD_35_CAPACITIVE
  #define SCREEN_WIDTH 480
  #define SCREEN_HEIGHT 320
  #define TFT_BL 27
  #define TS_SDA 33
  #define TS_SCL 32
  #define TS_INT 21
  #define TS_RST 25
  #define BUZZER_PIN 26
#else
  #define SCREEN_WIDTH 320
  #define SCREEN_HEIGHT 240
  #define TFT_BL 21
  #define XPT2046_IRQ  36
  #define XPT2046_MOSI 32
  #define XPT2046_MISO 39
  #define XPT2046_CLK  25
  #define XPT2046_CS   33
  #define BUZZER_PIN   26
  #define TS_MINX 200
  #define TS_MAXX 3700
  #define TS_MINY 200
  #define TS_MAXY 3600
#endif

// --- UI Scaling Macros ---
#ifdef CYD_35_CAPACITIVE
  #define SCALE_X(x) (((x) * 480) / 320)
  #define SCALE_Y(y) (((y) * 320) / 240)
#else
  #define SCALE_X(x) (x)
  #define SCALE_Y(y) (y)
#endif

// --- UI Constants ---
#define COLOR_BG      TFT_BLACK
#define COLOR_PANEL   0x2104 
#define COLOR_TEXT    TFT_LIGHTGREY
#define COLOR_RES     TFT_YELLOW
#define COLOR_CAD     TFT_CYAN
#define COLOR_WATTS   TFT_ORANGE
#define COLOR_KCAL    TFT_MAGENTA
#define COLOR_DIST    0x07FF // Cyan/Blue
#define COLOR_TIME    TFT_WHITE
#define BTN_UP_COLOR   0x03E0 
#define BTN_DOWN_COLOR 0xA000 

#define SLEEP_TIMEOUT  300000 

// --- System State ---
enum SystemState { STATE_SLEEP, STATE_DISCONNECTED, STATE_CONNECTED };
SystemState currentState = STATE_DISCONNECTED;
unsigned long lastActivityTime = 0;

TFT_eSPI tft = TFT_eSPI();

#ifdef CYD_35_CAPACITIVE
  TAMC_GT911 touch = TAMC_GT911(TS_SDA, TS_SCL, TS_INT, TS_RST, SCREEN_WIDTH, SCREEN_HEIGHT);
#else
  SPIClass touchSPI = SPIClass(HSPI);
  XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);
#endif

// --- Debug Flag ---
// #define UI_DEBUG 

// --- BLE Globals ---
static BLEUUID serviceUUID("0bf669f1-45f2-11e7-9598-0800200c9a66"); 
static BLEUUID writeCharUUID("0bf669f2-45f2-11e7-9598-0800200c9a66");  
static BLEUUID notifyCharUUID("0bf669f4-45f2-11e7-9598-0800200c9a66"); 
static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLEAddress* pServerAddress = nullptr;
static BLERemoteCharacteristic* pRemoteNotifyCharacteristic;
static BLERemoteCharacteristic* pRemoteWriteCharacteristic;
BLEClient* pClient; 

// --- Live Metrics ---
int currentCadence = 0;
int currentResistance = 0;
float currentWatts = 0;
float displayWatts = 0;
float throttledWatts = 0;
float throttledKcals = 0.0;
float throttledMiles = 0.0;
float totalMiles = 0.0;
float totalKJ = 0.0; 

#define AVG_WINDOW 100
float wattsBuffer[AVG_WINDOW];
int bufferIndex = 0;
double wattsSum = 0;

unsigned long lastPowerCalcTime = 0;
unsigned long lastWattsUpdateMillis = 0;
unsigned long workoutStartTime = 0;

// Flicker filters
int lastCadence = -1;
int lastResistance = -1;
float lastThrottledWatts = -1.0;
uint16_t lastWattsColor = 0;
float lastKcals = -1.0;
float lastMiles = -1.0;
String lastStatus = "";
String lastTimeStr = "";

// --- Helpers ---
uint16_t getPowerColor(float watts) {
  if (watts < 100.0) return TFT_CYAN;   
  if (watts < 200.0) return TFT_GREEN;  
  if (watts < 300.0) return TFT_YELLOW; 
  return TFT_RED;                      
}

void beep(int freq, int duration = 20) {
  tone(BUZZER_PIN, freq, duration);
}

// --- Unified Touch Input Handler ---
bool checkTouch(uint16_t &x, uint16_t &y) {
#ifdef CYD_35_CAPACITIVE
  touch.read();
  if (touch.isTouched && touch.touches > 0) {
    // Map the 3.5" (480x320) touch coordinates back to 320x240 coordinates
    // so the existing touch boundary checks work seamlessly.
    x = (touch.points[0].x * 320) / 480;
    y = (touch.points[0].y * 240) / 320;
    return true;
  }
#else
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    x = map(p.x, TS_MINX, TS_MAXX, 0, 320);
    y = map(p.y, TS_MINY, TS_MAXY, 0, 240);
    return true;
  }
#endif
  return false;
}

void drawButton(int x, int y, int w, int h, String label, uint16_t color, bool pressed) {
  uint16_t fill = pressed ? TFT_WHITE : color;
  uint16_t text = pressed ? color : TFT_WHITE;
  tft.fillRoundRect(SCALE_X(x), SCALE_Y(y), SCALE_X(w), SCALE_Y(h), 8, fill);
  tft.drawRoundRect(SCALE_X(x), SCALE_Y(y), SCALE_X(w), SCALE_Y(h), 8, TFT_WHITE);
  tft.setTextColor(text);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, SCALE_X(x + (w/2)), SCALE_Y(y + (h/2)), 4);
}

void drawConnectScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ECHELON CONSOLE", SCALE_X(160), SCALE_Y(60), 4);
  drawButton(30, 110, 260, 70, "START WORKOUT", 0x001F, false); 
  tft.setTextColor(TFT_DARKGREY);
  tft.drawString("Tap to connect to bike", SCALE_X(160), SCALE_Y(200), 2);
}

void drawStaticUI() {
  tft.fillScreen(COLOR_BG);
  tft.drawFastVLine(SCALE_X(106), SCALE_Y(40), SCALE_Y(200), 0x4208); 
  tft.drawFastVLine(SCALE_X(212), SCALE_Y(40), SCALE_Y(200), 0x4208); 
  tft.setTextColor(COLOR_TEXT);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("RESISTANCE", SCALE_X(53), SCALE_Y(50), 2);
  tft.drawString("CADENCE", SCALE_X(159), SCALE_Y(50), 2);
  tft.drawString("WATTS", SCALE_X(265), SCALE_Y(50), 2);
  tft.drawString("KCAL", SCALE_X(159), SCALE_Y(160), 2);
  tft.drawString("MILES", SCALE_X(265), SCALE_Y(160), 2);
  drawButton(15, 140, 76, 40, "+", BTN_UP_COLOR, false);
  drawButton(15, 190, 76, 40, "-", BTN_DOWN_COLOR, false);
  tft.fillRect(SCALE_X(250), SCALE_Y(2), SCALE_X(60), SCALE_Y(26), TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("STOP", SCALE_X(280), SCALE_Y(15), 2);
}

void updateDashboard(String statusMsg) {
  if (currentState != STATE_CONNECTED && statusMsg != "CONNECTING...") return;

  unsigned long elapsed = 0;
  if (workoutStartTime > 0) elapsed = (millis() - workoutStartTime) / 1000;
  int h = elapsed / 3600, m = (elapsed % 3600) / 60, s = elapsed % 60;
  char tBuf[10];
  if (h > 0) sprintf(tBuf, "%d:%02d:%02d", h, m, s);
  else sprintf(tBuf, "%02d:%02d", m, s);
  String timeStr = String(tBuf);

  if (statusMsg != lastStatus || timeStr != lastTimeStr) {
    tft.fillRect(0, 0, SCALE_X(248), SCALE_Y(30), COLOR_PANEL); 
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(ML_DATUM); 
    tft.drawString(statusMsg, SCALE_X(10), SCALE_Y(15), 2); 
    tft.setTextDatum(MR_DATUM);
    tft.drawString(timeStr, SCALE_X(240), SCALE_Y(15), 2);
    lastStatus = statusMsg;
    lastTimeStr = timeStr;
  }

  if (currentState != STATE_CONNECTED) return;
  tft.setTextDatum(MC_DATUM);
  
  if (currentResistance != lastResistance) {
    tft.fillRect(SCALE_X(5), SCALE_Y(65), SCALE_X(96), SCALE_Y(50), COLOR_BG);
    tft.setTextColor(COLOR_RES);
    tft.drawNumber(currentResistance, SCALE_X(53), SCALE_Y(90), 7); 
    lastResistance = currentResistance;
  }
  if (currentCadence != lastCadence) {
    tft.fillRect(SCALE_X(111), SCALE_Y(65), SCALE_X(96), SCALE_Y(50), COLOR_BG);
    tft.setTextColor(COLOR_CAD);
    tft.drawNumber(currentCadence, SCALE_X(159), SCALE_Y(90), 7); 
    lastCadence = currentCadence;
  }
  uint16_t wattsColor = getPowerColor(throttledWatts);
  if (throttledWatts != lastThrottledWatts || wattsColor != lastWattsColor) {
    tft.fillRect(SCALE_X(217), SCALE_Y(65), SCALE_X(96), SCALE_Y(50), COLOR_BG);
    tft.setTextColor(wattsColor);
    int precision = (throttledWatts < 10.0) ? 1 : 0;
    tft.drawFloat(throttledWatts, precision, SCALE_X(265), SCALE_Y(90), 7); 
    lastThrottledWatts = throttledWatts;
    lastWattsColor = wattsColor;
  }
  if (throttledKcals != lastKcals) {
    tft.fillRect(SCALE_X(110), SCALE_Y(175), SCALE_X(98), SCALE_Y(55), COLOR_BG);
    tft.setTextColor(COLOR_KCAL);
    tft.drawFloat(throttledKcals, (throttledKcals < 10.0 ? 1 : 0), SCALE_X(159), SCALE_Y(205), 7); 
    lastKcals = throttledKcals;
  }
  if (throttledMiles != lastMiles) {
    tft.fillRect(SCALE_X(216), SCALE_Y(175), SCALE_X(98), SCALE_Y(55), COLOR_BG);
    tft.setTextColor(COLOR_DIST);
    tft.drawFloat(throttledMiles, (throttledMiles < 10.0 ? 2 : 1), SCALE_X(265), SCALE_Y(205), 7); 
    lastMiles = throttledMiles;
  }
}

void setSleep(bool sleep) {
  if (sleep) {
    digitalWrite(TFT_BL, LOW);
    currentState = STATE_SLEEP;
#ifndef UI_DEBUG
    if (connected) pClient->disconnect();
#endif
  } else {
    digitalWrite(TFT_BL, HIGH);
    currentState = STATE_DISCONNECTED;
    drawConnectScreen();
    lastActivityTime = millis();
  }
}

void setResistance(int level) {
  if (level < 1) level = 1;
  if (level > 32) level = 32;
  currentResistance = level; 
#ifndef UI_DEBUG
  if (pRemoteWriteCharacteristic != nullptr && connected) {
    uint8_t packet[] = {0xF0, 0xB1, 0x01, (uint8_t)level, 0x00};
    packet[4] = packet[0] ^ packet[1] ^ packet[2] ^ packet[3];
    pRemoteWriteCharacteristic->writeValue(packet, sizeof(packet), true);
  }
#endif
}

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  if (length == 5 && pData[1] == 0xD2) currentResistance = pData[3];
  if (length >= 11 && pData[1] == 0xD1) currentCadence = (pData[9] << 8) | pData[10];
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {}
  void onDisconnect(BLEClient* pclient) {
    connected = false; workoutStartTime = 0; lastTimeStr = ""; totalKJ = 0; totalMiles = 0;
    if (currentState == STATE_CONNECTED) { currentState = STATE_DISCONNECTED; drawConnectScreen(); }
  }
};

class MyScanCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveName()) {
      String deviceName = advertisedDevice.getName().c_str();
      if (deviceName.indexOf("ECH") != -1) {
        if (pServerAddress == nullptr) pServerAddress = new BLEAddress(advertisedDevice.getAddress());
        doConnect = true; doScan = false; advertisedDevice.getScan()->stop();
      }
    }
  }
};

bool connectToServer() {
  updateDashboard("CONNECTING...");
  if (!pClient->connect(*pServerAddress)) return false;
  pClient->setMTU(517); delay(500); 
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) { pClient->disconnect(); return false; }
  pRemoteWriteCharacteristic = pRemoteService->getCharacteristic(writeCharUUID);
  if (pRemoteWriteCharacteristic != nullptr) {
    uint8_t initPacket[] = {0xF0, 0xB0, 0x01, 0x01, 0x40}; 
    pRemoteWriteCharacteristic->writeValue(initPacket, sizeof(initPacket), true);
    delay(200); 
  }
  pRemoteNotifyCharacteristic = pRemoteService->getCharacteristic(notifyCharUUID);
  if (pRemoteNotifyCharacteristic == nullptr) { pClient->disconnect(); return false; }
  if (pRemoteNotifyCharacteristic->canNotify()) pRemoteNotifyCharacteristic->registerForNotify(notifyCallback);
  connected = true; currentState = STATE_CONNECTED; drawStaticUI();
  lastPowerCalcTime = millis(); workoutStartTime = millis(); lastWattsUpdateMillis = millis();
  for(int i=0; i<AVG_WINDOW; i++) wattsBuffer[i] = 0;
  wattsSum = 0; bufferIndex = 0; totalKJ = 0; totalMiles = 0;
  throttledKcals = 0; throttledMiles = 0;
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT); 
  digitalWrite(TFT_BL, HIGH); 
  pinMode(BUZZER_PIN, OUTPUT);
  tft.init(); 
  tft.setRotation(1); 
  drawConnectScreen();

#ifdef CYD_35_CAPACITIVE
  Wire.begin(TS_SDA, TS_SCL);
  touch.begin();
  touch.setRotation(ROTATION_RIGHT);
#else
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSPI); 
  touch.setRotation(1);
#endif

#ifndef UI_DEBUG
  BLEDevice::init(""); pClient = BLEDevice::createClient(); pClient->setClientCallbacks(new MyClientCallback());
#endif
  lastActivityTime = millis();
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentState == STATE_SLEEP) {
    uint16_t tx, ty;
    if (checkTouch(tx, ty)) { beep(1000, 50); setSleep(false); delay(300); }
    return; 
  }
  if (currentState == STATE_DISCONNECTED) {
    uint16_t x, y;
    if (checkTouch(x, y)) {
      if (x > 30 && x < 290 && y > 110 && y < 180) {
        beep(1500, 50);
#ifdef UI_DEBUG
        currentCadence = 85; currentResistance = 20; connected = true; 
        currentState = STATE_CONNECTED; drawStaticUI(); 
        lastPowerCalcTime = millis(); workoutStartTime = millis(); lastWattsUpdateMillis = millis();
        for(int i=0; i<AVG_WINDOW; i++) wattsBuffer[i] = 0;
        totalKJ = 0; totalMiles = 0; throttledKcals = 0; throttledMiles = 0;
#else
        doScan = true; updateDashboard("SCANNING...");
#endif
      }
      lastActivityTime = currentMillis;
    }
    if (doScan) {
      BLEDevice::getScan()->start(5, false);
      if (doConnect) { if (!connectToServer()) { updateDashboard("RETRYING..."); delay(2000); doScan = true; } doConnect = false; }
    }
    if (currentMillis - lastActivityTime > SLEEP_TIMEOUT) setSleep(true);
    return;
  }
  if (currentState == STATE_CONNECTED) {
    currentWatts = (currentCadence * currentResistance * currentResistance) / 200.0;
    wattsSum -= wattsBuffer[bufferIndex]; wattsBuffer[bufferIndex] = currentWatts;
    wattsSum += currentWatts; bufferIndex = (bufferIndex + 1) % AVG_WINDOW;
    displayWatts = wattsSum / AVG_WINDOW;

    if (currentMillis - lastWattsUpdateMillis >= 1000) {
      throttledWatts = displayWatts;
      throttledKcals = totalKJ;
      throttledMiles = totalMiles;
      lastWattsUpdateMillis = currentMillis;
    }

    if (lastPowerCalcTime > 0) {
      unsigned long deltaT = currentMillis - lastPowerCalcTime;
      totalKJ += (currentWatts * deltaT) / 1000000.0;
      float speedMPH = 1.6 * sqrt(currentWatts);
      totalMiles += (speedMPH / 3600000.0) * deltaT;
    }
    lastPowerCalcTime = currentMillis;
    if (currentCadence > 0) lastActivityTime = currentMillis;

    uint16_t x, y;
    if (checkTouch(x, y)) {
      lastActivityTime = currentMillis;
      if (x > 15 && x < 91 && y > 140 && y < 180) {
        beep(2000); drawButton(15, 140, 76, 40, "+", BTN_UP_COLOR, true);
        setResistance(currentResistance + 1); delay(150);
        drawButton(15, 140, 76, 40, "+", BTN_UP_COLOR, false);
      }
      if (x > 15 && x < 91 && y > 190 && y < 230) {
        beep(1500); drawButton(15, 190, 76, 40, "-", BTN_DOWN_COLOR, true);
        setResistance(currentResistance - 1); delay(150);
        drawButton(15, 190, 76, 40, "-", BTN_DOWN_COLOR, false);
      }
      if (x > 250 && y < 30) { beep(500, 100); setSleep(true); }
    }
    updateDashboard(connected ? "CONNECTED" : "DISCONNECTED");
    if (currentMillis - lastActivityTime > SLEEP_TIMEOUT) setSleep(true);
  }
  delay(20);
}
