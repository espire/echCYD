// --- ECHCYD RECOMMENDED CONFIG FOR TFT_eSPI ---
// Copy this content into: /home/eli/Arduino/libraries/TFT_eSPI/User_Setup.h

#define USER_SETUP_ID 1

// --- Driver ---
#define ILI9341_2_DRIVER     // Most CYDs use this version of the ILI9341

// --- Display Pins (Standard CYD) ---
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15  // Display Chip Select
#define TFT_DC    2  // Data/Command
#define TFT_RST  -1  // Set to -1 if connected to ESP32 RST pin
#define TFT_BL   21  // Backlight pin (matches what we used in code)

// --- Fonts ---
// Enabling these will ensure drawString/drawNumber with font IDs work
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font
#define LOAD_FONT2  // Font 2. Small 16 pixel high font
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font
#define LOAD_FONT6  // Font 6. Large 48 pixel font
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font
#define LOAD_FONT8  // Font 8. Large 75 pixel font
#define LOAD_GFXFF  // FreeFonts.

// --- Bus Speed ---
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
