# echCYD

I bought an Echelon EX-3 instead of a higher-end model to save money thinking I wouldn't mind connecting a tablet with the Echelon app, but I was wrong. Having a simple built-in screen for simple biking data is essential. This project attempts to fix that.

## Features
* Current resistance setting
* Current cadence (RPM)
* Total rotations

## Prerequisites & Hardware
1. Get a **"CYD" (Cheap Yellow Display)** ESP32 device with a 2.8-inch screen (e.g., [AliExpress Link](https://www.aliexpress.us/item/3256804785406072.html)).
2. An ESP32-compatible development environment (Arduino IDE or Arduino CLI).

---

## Setup Instructions

### 1. Sketch Directory Structure
Arduino requires the main `.ino` file to reside in a folder with the exact same name.
* Move `cyd.ino` into a subfolder named `cyd`, or create a symlink:
  ```bash
  mkdir -p cyd
  ln -s ../cyd.ino cyd/cyd.ino
  ```

### 2. Install Board Support
Ensure you have the ESP32 board platform installed.
* **Arduino IDE**: Go to **Preferences** -> **Additional Boards Manager URLs** and add:
  `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
  Then open the Boards Manager and install **esp32**.
* **Arduino CLI**:
  ```bash
  arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  arduino-cli core update-index
  arduino-cli core install esp32:esp32
  ```

### 3. Install Libraries & Configure Display
You need to install two libraries:
1. **TFT_eSPI** (for screen rendering)
2. **XPT2046_Touchscreen** (for touch control)

#### For Arduino IDE:
1. Go to **Sketch** -> **Include Library** -> **Manage Libraries...**
2. Search for and install **TFT_eSPI** and **XPT2046_Touchscreen**.
3. Locate the installed `TFT_eSPI` library folder on your computer (typically under `Documents/Arduino/libraries/TFT_eSPI/`).
4. Replace the default `User_Setup.h` file in that folder with the custom `User_Setup.h` file provided in this repository under `libraries/TFT_eSPI/User_Setup.h`.

#### For Arduino CLI:
```bash
# Install the libraries
arduino-cli lib install TFT_eSPI XPT2046_Touchscreen

# Overwrite the default User_Setup.h configuration
cp libraries/TFT_eSPI/User_Setup.h ~/Arduino/libraries/TFT_eSPI/User_Setup.h
```

---

## Compiling & Uploading

With your CYD device connected:

* **Arduino IDE**: Select **ESP32 Dev Module** as the target board, select the correct serial port, compile, and upload.
* **Arduino CLI**:
  ```bash
  # Compile the sketch
  arduino-cli compile --fqbn esp32:esp32:esp32 cyd
  
  # Upload the sketch (replace /dev/ttyUSB0 with your actual port)
  arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 cyd
  ```

---

## UI Debug / Simulation Mode

By default, the code has UI debug mode enabled near the top of [cyd.ino](file:///home/eli/git/echCYD/cyd.ino):
```cpp
#define UI_DEBUG
```
This bypasses Bluetooth and simulates a workout with dummy cadence and resistance data when you tap "START WORKOUT" so you can test the interface.

To connect to your actual Echelon bike, comment out this line before compiling:
```cpp
// #define UI_DEBUG
```

---

## Feedback

I welcome your feedback or contributions. Please report any issues or send pull requests.

