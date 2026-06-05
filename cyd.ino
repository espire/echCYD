#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

// --- Screen Setup ---
TFT_eSPI tft = TFT_eSPI();

// --- Echelon BLE Setup ---
static BLEUUID serviceUUID("0bf669f1-45f2-11e7-9598-0800200c9a66"); 
static BLEUUID writeCharUUID("0bf669f2-45f2-11e7-9598-0800200c9a66");  
static BLEUUID notifyCharUUID("0bf669f4-45f2-11e7-9598-0800200c9a66"); 

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = true;

static BLEAddress* pServerAddress = nullptr;
static BLERemoteCharacteristic* pRemoteNotifyCharacteristic;
static BLERemoteCharacteristic* pRemoteWriteCharacteristic;
BLEClient* pClient; 

// --- Live Metrics ---
int currentCadence = 0;
int currentResistance = 0;

// Integration variables for Total Rotations
int totalRotations = 0;
float fractionalRotations = 0.0;
unsigned long lastRotCalcTime = 0;

// Flicker filters
int lastCadence = -1;
int lastResistance = -1;
int lastRotations = -1;
String lastStatus = "";

// --- UI Functions ---
void drawStaticUI() {
  tft.fillScreen(TFT_BLACK);
  
  // Headers
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(2);
  tft.setCursor(20, 40);  tft.print("RESISTANCE");
  
  // Shifted left from 180 to 160 to prevent text wrapping
  tft.setCursor(160, 40); tft.print("CADENCE");
  tft.setCursor(160, 140); tft.print("ROTATIONS");

  // Draw the Touch Shifter Buttons vertically under Resistance
  tft.fillRect(20, 115, 80, 50, TFT_DARKGREEN); // Up Button
  tft.fillRect(20, 175, 80, 50, TFT_RED);       // Down Button
  
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(4); 
  tft.setCursor(45, 125); tft.print("+");
  tft.setCursor(45, 185); tft.print("-");
}

void updateDashboard(String statusMsg) {
  // Only redraw status bar if it changes
  if (statusMsg != lastStatus) {
    tft.fillRect(0, 0, 320, 30, TFT_DARKGREY); 
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextSize(2);
    tft.setCursor(10, 5);
    tft.print(statusMsg); 
    lastStatus = statusMsg;
  }

  // Draw Dynamic Numbers using opaque background (TFT_BLACK) to prevent flickering
  tft.setTextSize(6); 
  
  // Resistance Value
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(20, 65);
  tft.printf("%02d", currentResistance); 

  // Cadence Value (shifted left)
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(160, 65);
  tft.printf("%03d", currentCadence); 

  // Total Rotations Value (shifted left)
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.setCursor(160, 165);
  tft.printf("%04d", totalRotations); 
}

// --- Write Command: Set Digital Resistance ---
void setResistance(int level) {
  if (level < 1) level = 1;
  if (level > 32) level = 32;
  
  if (pRemoteWriteCharacteristic != nullptr) {
    uint8_t checksum = (0xF0 + 0xB1 + level) & 0xFF;
    uint8_t packet[] = {0xF0, 0xB1, (uint8_t)level, 0x00, checksum};
    pRemoteWriteCharacteristic->writeValue(packet, sizeof(packet), true);
    
    currentResistance = level; 
  }
}

// --- Read Command: Decode Live Data ---
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  // 1. Decode Resistance (D2 Packets)
  if (length == 5 && pData[1] == 0xD2) {
    currentResistance = pData[3];
  }

  // 2. Decode Cadence (D1 Packets)
  if (length >= 11 && pData[1] == 0xD1) {
    currentCadence = (pData[9] << 8) | pData[10];
  }
}

// --- BLE Callbacks ---
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {}
  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("Disconnected! Going back to scanning...");
    doScan = true; 
    
    // Reset filters on disconnect so the screen updates immediately on reconnect
    lastCadence = -1;
    lastResistance = -1;
    lastRotations = -1;
  }
};

class MyScanCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveName()) {
      String deviceName = advertisedDevice.getName().c_str();
      
      if (deviceName.indexOf("ECH") != -1) {
        Serial.print(">>> MATCH! Found Echelon Bike! Name: ");
        Serial.println(deviceName);
        
        if (pServerAddress == nullptr) {
          pServerAddress = new BLEAddress(advertisedDevice.getAddress());
        }
        
        doConnect = true;
        doScan = false;
        advertisedDevice.getScan()->stop();
      }
    }
  }
};

// --- Connection Logic ---
bool connectToServer() {
  updateDashboard("CONNECTING...");
  Serial.print("Connecting to EX-3 at: ");
  Serial.println(pServerAddress->toString().c_str());
  
  if (!pClient->connect(*pServerAddress)) {
    return false;
  }

  pClient->setMTU(517); 
  delay(3000); 

  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    pClient->disconnect();
    delay(1000); 
    return false;
  }

  pRemoteWriteCharacteristic = pRemoteService->getCharacteristic(writeCharUUID);
  if (pRemoteWriteCharacteristic != nullptr) {
    uint8_t initPacket[] = {0xF0, 0xB0, 0x01, 0x01, 0xA2}; 
    pRemoteWriteCharacteristic->writeValue(initPacket, sizeof(initPacket), true);
    delay(500); 
  }

  pRemoteNotifyCharacteristic = pRemoteService->getCharacteristic(notifyCharUUID);
  if (pRemoteNotifyCharacteristic == nullptr) {
    pClient->disconnect();
    delay(1000);
    return false;
  }

  if (pRemoteNotifyCharacteristic->canNotify()) {
    pRemoteNotifyCharacteristic->registerForNotify(notifyCallback);
  }

  connected = true;
  lastRotCalcTime = millis(); // Start the rotation timer
  updateDashboard("CONNECTED");
  return true;
}

// --- Main Setup & Loop ---
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1); 
  drawStaticUI();
  updateDashboard("INITIALIZING");

  BLEDevice::init("");
  
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyScanCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  
  updateDashboard("SCANNING...");
}

void loop() {
  // 1. Handle Bluetooth Connection States
  if (doConnect == true) {
    if (!connectToServer()) {
      updateDashboard("RETRYING...");
      delay(3000); 
      if (pServerAddress != nullptr) {
        delete pServerAddress;
        pServerAddress = nullptr;
      }
      doScan = true;
    }
    doConnect = false;
  }

  if (doScan) {
    BLEDevice::getScan()->start(5, false); 
    BLEDevice::getScan()->clearResults(); 
  }

  // 2. Handle Continuous Math & UI Updates
  if (connected) {
    unsigned long currentMillis = millis();
    
    // Calculate total rotations based on elapsed time and current RPM
    if (lastRotCalcTime > 0) {
      unsigned long deltaT = currentMillis - lastRotCalcTime;
      
      // Cadence / 60,000 gives us Revolutions per Millisecond
      fractionalRotations += (currentCadence / 60000.0) * deltaT;
      
      // When we hit a full rotation, roll it over to the integer display
      if (fractionalRotations >= 1.0) {
        int newRotations = (int)fractionalRotations;
        totalRotations += newRotations;
        fractionalRotations -= newRotations;
      }
    }
    lastRotCalcTime = currentMillis;

    // Check Touch Inputs for Digital Shifting
    uint16_t x = 0, y = 0;
    if (tft.getTouch(&x, &y)) {
      // Touch coordinates are mapped to the X/Y axes. 
      // Left Column (X = 20 to 100)
      if (x > 20 && x < 100) {
        // Up Button bounds
        if (y > 115 && y < 165) {
          setResistance(currentResistance + 1);
          delay(200); 
        }
        // Down Button bounds
        if (y > 175 && y < 225) {
          setResistance(currentResistance - 1);
          delay(200); 
        }
      }
    }

    // Update screen if ANY of the three numbers changed
    if (currentCadence != lastCadence || currentResistance != lastResistance || totalRotations != lastRotations) {
      updateDashboard("CONNECTED");
      lastCadence = currentCadence;
      lastResistance = currentResistance;
      lastRotations = totalRotations;
    }
  } else {
    // If disconnected, just ensure the UI reflects it
    updateDashboard("DISCONNECTED");
  }

  delay(20); // Small loop delay for stability
}
