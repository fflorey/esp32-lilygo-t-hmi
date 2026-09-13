# Demo 1: Interactive Touch & Draw with 2-Point Calibration

This demo for the **LILYGO T-HMI (ESP32-S3)** features:
- High-speed 8-bit parallel ST7789 TFT display driver
- XPT2046 SPI resistive touch driver
- On-screen Nintendo DS-style 2-Point Touch Calibration Wizard
- Permanent calibration storage in ESP32 Flash NVS (Preferences)
- Symmetrically bounded canvas with pixel-perfect brush clamping
- Color palette selector: Red, Green, Blue, Yellow, Clear

## How to activate this demo:
In `platformio.ini` in the project root, set:
```ini
[platformio]
src_dir = demos/01_touch_draw
```
Or copy `demos/01_touch_draw/main.cpp` back into `src/main.cpp`.

## Re-calibration:
Press the physical **BOOT button** (GPIO 0) anytime to re-run the 2-point crosshairs wizard.
