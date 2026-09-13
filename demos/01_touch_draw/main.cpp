#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include "pins.h"
#include <TFT_eSPI.h>
#include <xpt2046.h>

// Hardware display and touch instances
TFT_eSPI tft = TFT_eSPI();
XPT2046 touch = XPT2046(SPI, TOUCHSCREEN_CS_PIN, TOUCHSCREEN_IRQ_PIN);
Preferences prefs;

// Touch Axis Inversion Settings (standard portrait)
const bool INVERT_X = false;
const bool INVERT_Y = false;

// Active calibration points (will be loaded from flash or defaults)
uint16_t cal_raw_xmin = 1788;
uint16_t cal_raw_xmax = 285;
uint16_t cal_raw_ymin = 1836;
uint16_t cal_raw_ymax = 311;

// Palette Colors
uint16_t currentColor = TFT_RED;
int prevX = -1;
int prevY = -1;

// Backlight pulse brightness controller (0 - 16 steps)
void setBrightness(uint8_t value) {
  static uint8_t steps = 16;
  static uint8_t _brightness = 0;

  if (_brightness == value) return;
  if (value > 16) value = 16;
  if (value == 0) {
    digitalWrite(BK_LIGHT_PIN, 0);
    delay(3);
    _brightness = 0;
    return;
  }
  if (_brightness == 0) {
    digitalWrite(BK_LIGHT_PIN, 1);
    _brightness = steps;
    delayMicroseconds(30);
  }
  int from = steps - _brightness;
  int to = steps - value;
  int num = (steps + to - from) % steps;
  for (int i = 0; i < num; i++) {
    digitalWrite(BK_LIGHT_PIN, 0);
    digitalWrite(BK_LIGHT_PIN, 1);
  }
  _brightness = value;
}

// Canvas geometry constants (symmetrically centered on 240x320 display)
const int CANVAS_X = 2;
const int CANVAS_Y = 102;
const int CANVAS_W = 236;
const int CANVAS_H = 216;
const int BRUSH_R  = 3;

void drawCanvasBackground() {
  // Clear inside the canvas only (preserves the outer border)
  tft.fillRect(CANVAS_X + 1, CANVAS_Y + 1, CANVAS_W - 2, CANVAS_H - 2, TFT_BLACK);
  tft.drawRect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, TFT_DARKGREY);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("Touch & draw here!", 120, 200, 2);
}

void drawPaletteButtons() {
  const int btnY = 54;
  const int btnH = 40;
  const int btnW = 42;

  // Button 1: RED
  tft.fillRoundRect(5, btnY, btnW, btnH, 6, TFT_RED);
  tft.drawRoundRect(5, btnY, btnW, btnH, 6, currentColor == TFT_RED ? TFT_WHITE : TFT_DARKGREY);

  // Button 2: GREEN
  tft.fillRoundRect(52, btnY, btnW, btnH, 6, TFT_GREEN);
  tft.drawRoundRect(52, btnY, btnW, btnH, 6, currentColor == TFT_GREEN ? TFT_WHITE : TFT_DARKGREY);

  // Button 3: BLUE
  tft.fillRoundRect(99, btnY, btnW, btnH, 6, TFT_BLUE);
  tft.drawRoundRect(99, btnY, btnW, btnH, 6, currentColor == TFT_BLUE ? TFT_WHITE : TFT_DARKGREY);

  // Button 4: YELLOW
  tft.fillRoundRect(146, btnY, btnW, btnH, 6, TFT_YELLOW);
  tft.drawRoundRect(146, btnY, btnW, btnH, 6, currentColor == TFT_YELLOW ? TFT_WHITE : TFT_DARKGREY);

  // Button 5: CLEAR
  tft.fillRoundRect(193, btnY, btnW, btnH, 6, TFT_DARKGREY);
  tft.drawRoundRect(193, btnY, btnW, btnH, 6, TFT_LIGHTGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString("CLR", 202, btnY + 12, 2);
}

void drawUI() {
  tft.fillScreen(TFT_BLACK);

  // 1. Top Header Banner
  tft.fillRect(0, 0, 240, 26, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("Demo for the T-HMI", 8, 2, 4);

  // 2. System HUD Area
  tft.fillRect(0, 28, 240, 22, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("PSRAM: " + String(ESP.getFreePsram() / 1024) + "K", 8, 31, 2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Touch: (---, ---)", 125, 31, 2);

  // 3. Buttons & Canvas
  drawPaletteButtons();
  drawCanvasBackground();
}

// -------------------------------------------------------------
// Interactive 2-Point Touch Calibration Wizard
// -------------------------------------------------------------
void runCalibrationWizard() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("TOUCH CALIBRATION", 120, 90, 4);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawCentreString("Tap crosshair precisely", 120, 130, 2);

  // Point 1: Top-Left (30, 40)
  const int p1_x = 30;
  const int p1_y = 40;
  tft.drawCircle(p1_x, p1_y, 14, TFT_RED);
  tft.drawFastHLine(p1_x - 18, p1_y, 36, TFT_RED);
  tft.drawFastVLine(p1_x, p1_y - 18, 36, TFT_RED);
  tft.fillCircle(p1_x, p1_y, 3, TFT_WHITE);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("1. Tap here", p1_x + 22, p1_y - 6, 2);

  // Wait for touch on Point 1
  uint32_t raw1_x = 0, raw1_y = 0;
  int samples = 0;
  while (!touch.pressed()) { delay(10); }
  while (touch.pressed()) {
    raw1_x += touch.RawX();
    raw1_y += touch.RawY();
    samples++;
    delay(10);
  }
  raw1_x /= max(1, samples);
  raw1_y /= max(1, samples);
  delay(300); // Debounce

  // Clear Point 1
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("TOUCH CALIBRATION", 120, 90, 4);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawCentreString("Now tap the bottom crosshair", 120, 130, 2);

  // Point 2: Bottom-Right (210, 280)
  const int p2_x = 210;
  const int p2_y = 280;
  tft.drawCircle(p2_x, p2_y, 14, TFT_RED);
  tft.drawFastHLine(p2_x - 18, p2_y, 36, TFT_RED);
  tft.drawFastVLine(p2_x, p2_y - 18, 36, TFT_RED);
  tft.fillCircle(p2_x, p2_y, 3, TFT_WHITE);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("2. Tap here", p2_x - 90, p2_y - 6, 2);

  // Wait for touch on Point 2
  uint32_t raw2_x = 0, raw2_y = 0;
  samples = 0;
  while (!touch.pressed()) { delay(10); }
  while (touch.pressed()) {
    raw2_x += touch.RawX();
    raw2_y += touch.RawY();
    samples++;
    delay(10);
  }
  raw2_x /= max(1, samples);
  raw2_y /= max(1, samples);
  delay(300); // Debounce

  // Extrapolate to full screen bounds (0..240 and 0..320)
  float dx_px = p2_x - p1_x; // 180
  float dy_px = p2_y - p1_y; // 240
  float dx_raw = (float)((int32_t)raw2_x - (int32_t)raw1_x);
  float dy_raw = (float)((int32_t)raw2_y - (int32_t)raw1_y);

  // Calculate boundary raw values
  int32_t raw_x0 = raw1_x - (int32_t)(p1_x * (dx_raw / dx_px));
  int32_t raw_x240 = raw2_x + (int32_t)((240 - p2_x) * (dx_raw / dx_px));
  int32_t raw_y0 = raw1_y - (int32_t)(p1_y * (dy_raw / dy_px));
  int32_t raw_y320 = raw2_y + (int32_t)((320 - p2_y) * (dy_raw / dy_px));

  cal_raw_xmin = constrain(raw_x0, 100, 3900);
  cal_raw_xmax = constrain(raw_x240, 100, 3900);
  cal_raw_ymin = constrain(raw_y0, 100, 3900);
  cal_raw_ymax = constrain(raw_y320, 100, 3900);

  // Save to persistent Flash memory
  prefs.begin("touch", false);
  prefs.putUShort("xmin", cal_raw_xmin);
  prefs.putUShort("xmax", cal_raw_xmax);
  prefs.putUShort("ymin", cal_raw_ymin);
  prefs.putUShort("ymax", cal_raw_ymax);
  prefs.putBool("valid", true);
  prefs.end();

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawCentreString("Calibration Saved!", 120, 140, 4);
  Serial.printf("Calibrated: X=(%d,%d), Y=(%d,%d)\n", cal_raw_xmin, cal_raw_xmax, cal_raw_ymin, cal_raw_ymax);
  delay(1200);

  // Apply calibration
  touch.setCal(cal_raw_xmin, cal_raw_xmax, cal_raw_ymin, cal_raw_ymax, 240, 320);
  touch.setRotation(0);
}

void setup() {
  Serial.begin(115200);

  // Setup Boot Button (GPIO 0) to allow re-calibration anytime
  pinMode(BUTTON1_PIN, INPUT_PULLUP);

  // Power on peripherals (Display & Touch power latch)
  pinMode(PWR_EN_PIN, OUTPUT);
  digitalWrite(PWR_EN_PIN, HIGH);

  // Backlight setup
  pinMode(BK_LIGHT_PIN, OUTPUT);
  setBrightness(16);

  // Initialize parallel ST7789 TFT display
  tft.begin();
  tft.setRotation(0); // 240 x 320 portrait
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  // Initialize SPI for XPT2046 Resistive Touch
  SPI.begin(TOUCHSCREEN_SCLK_PIN, TOUCHSCREEN_MISO_PIN, TOUCHSCREEN_MOSI_PIN);
  touch.begin(240, 320);

  // Check if saved calibration exists in flash
  prefs.begin("touch", true);
  bool hasCal = prefs.getBool("valid", false);
  if (hasCal) {
    cal_raw_xmin = prefs.getUShort("xmin", cal_raw_xmin);
    cal_raw_xmax = prefs.getUShort("xmax", cal_raw_xmax);
    cal_raw_ymin = prefs.getUShort("ymin", cal_raw_ymin);
    cal_raw_ymax = prefs.getUShort("ymax", cal_raw_ymax);
    Serial.println("Loaded touch calibration from flash NVS.");
  }
  prefs.end();

  // If no calibration saved, or if BOOT button (GPIO 0) is held during boot:
  if (!hasCal || digitalRead(BUTTON1_PIN) == LOW) {
    runCalibrationWizard();
  } else {
    touch.setCal(cal_raw_xmin, cal_raw_xmax, cal_raw_ymin, cal_raw_ymax, 240, 320);
    touch.setRotation(0);
  }

  Serial.println("LILYGO T-HMI ready!");
  drawUI();
}

void loop() {
  // If user presses the BOOT button (GPIO 0), start re-calibration
  if (digitalRead(BUTTON1_PIN) == LOW) {
    delay(100);
    if (digitalRead(BUTTON1_PIN) == LOW) {
      runCalibrationWizard();
      drawUI();
      return;
    }
  }

  if (touch.pressed()) {
    int x = touch.X();
    int y = touch.Y();

    // 1:1 hardware coordinate mapping with axis inversion
    if (INVERT_X) x = 240 - x;
    if (INVERT_Y) y = 320 - y;

    x = constrain(x, 0, 239);
    y = constrain(y, 0, 319);

    int rawX = touch.RawX();
    int rawY = touch.RawY();

    // Print to serial
    Serial.printf("RAW -> X:%4d, Y:%4d | SCREEN -> X:%3d, Y:%3d\n", rawX, rawY, x, y);

    // Display live touch coordinates on the HUD
    tft.fillRect(0, 28, 240, 22, TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("RAW:" + String(rawX) + "," + String(rawY), 6, 31, 2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("SCR:" + String(x) + "," + String(y), 130, 31, 2);

    // Check if palette buttons were tapped (y: 50 to 98)
    if (y >= 50 && y <= 98) {
      if (x >= 5 && x <= 47) {
        currentColor = TFT_RED;
      } else if (x >= 52 && x <= 94) {
        currentColor = TFT_GREEN;
      } else if (x >= 99 && x <= 141) {
        currentColor = TFT_BLUE;
      } else if (x >= 146 && x <= 188) {
        currentColor = TFT_YELLOW;
      } else if (x >= 193 && x <= 235) {
        drawCanvasBackground();
      }
      drawPaletteButtons();
      prevX = -1;
      prevY = -1;
      delay(120);
      return;
    }

    // Check if touch is inside the drawing canvas area
    if (x >= CANVAS_X && x <= (CANVAS_X + CANVAS_W) &&
        y >= CANVAS_Y && y <= (CANVAS_Y + CANVAS_H)) {
      
      // Clamp the brush center so the circle (radius BRUSH_R) stays flush inside the border
      int drawX = constrain(x, CANVAS_X + BRUSH_R + 1, CANVAS_X + CANVAS_W - BRUSH_R - 2);
      int drawY = constrain(y, CANVAS_Y + BRUSH_R + 1, CANVAS_Y + CANVAS_H - BRUSH_R - 2);

      if (prevX != -1 && prevY != -1) {
        tft.drawLine(prevX, prevY, drawX, drawY, currentColor);
        tft.fillCircle(drawX, drawY, BRUSH_R, currentColor);
      } else {
        tft.fillCircle(drawX, drawY, BRUSH_R, currentColor);
      }
      prevX = drawX;
      prevY = drawY;
    }
  } else {
    prevX = -1;
    prevY = -1;
  }

  delay(10);
}
