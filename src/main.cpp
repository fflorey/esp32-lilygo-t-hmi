#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <base64.h>
#include "pins.h"
#include <TFT_eSPI.h>
#include <xpt2046.h>
#include <TJpg_Decoder.h>
#include <vector>

// =========================================================================
// 1. WiFi & Komoot Credentials
// =========================================================================
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define KOMOOT_EMAIL    "YOUR_KOMOOT_EMAIL"
#define KOMOOT_PASSWORD "YOUR_KOMOOT_PASSWORD"
#endif

// =========================================================================
// Hardware Display, Touch, and NVS Storage
// =========================================================================
TFT_eSPI tft = TFT_eSPI();
XPT2046 touch = XPT2046(SPI, TOUCHSCREEN_CS_PIN, TOUCHSCREEN_IRQ_PIN);
Preferences prefs;

// Touch Axis Inversion (false since calibration wizard aligns coordinates)
const bool INVERT_X = false;
const bool INVERT_Y = false;

// Calibration values (loaded from Flash NVS)
uint16_t cal_raw_xmin = 1788;
uint16_t cal_raw_xmax = 285;
uint16_t cal_raw_ymin = 1836;
uint16_t cal_raw_ymax = 311;

// Colors (RGB565)
#define COLOR_BG          0x0842  // Very dark slate/forest background
#define COLOR_CARD_BG     0x18E3  // Dark card background
#define COLOR_CARD_BDR    0x31E6  // Card border
#define COLOR_KOMOOT      0x2CD2  // Komoot Vibrant Green
#define COLOR_TEXT_MUTED  0x9CF3  // Muted grey
#define COLOR_ACCENT      0xFDE0  // Warm yellow

// =========================================================================
// Tour Data Structure & State
// =========================================================================
struct TourData {
  String name;
  String sport;
  String date;
  float distanceKm = 0.0f;
  int durationSec = 0;
  float elevationUp = 0.0f;
  float avgSpeedKmH = 0.0f;
  int kcal = 0;
  String mapImageUrl;
  uint8_t* jpgData = nullptr;
  size_t jpgSize = 0;

  TourData() = default;

  TourData(String n, String s, String d, float dist, int dur, float elev, float spd, int k, String url = "")
    : name(n), sport(s), date(d), distanceKm(dist), durationSec(dur), elevationUp(elev),
      avgSpeedKmH(spd), kcal(k), mapImageUrl(url), jpgData(nullptr), jpgSize(0) {}
};

std::vector<TourData> tours;
int currentTourIndex = 0;
bool isOnline = false;
bool isLiveKomoot = false;
bool showMapView = false; // Toggle state: false = Stats Grid, true = Route Map
String statusMessage = "Initializing...";

// Power Saving & Display Standby (2 minutes timeout)
const unsigned long SCREEN_TIMEOUT_MS = 2 * 60 * 1000;
unsigned long lastInteractionTime = 0;
bool isScreenOn = true;

// Callback for TJpg_Decoder to render decompressed JPEG blocks to TFT
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

// =========================================================================
// Helper Functions
// =========================================================================
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

void setScreenPower(bool on) {
  if (on == isScreenOn) return;
  isScreenOn = on;
  if (on) {
    setBrightness(16);
    Serial.println("Screen ON");
  } else {
    setBrightness(0);
    Serial.println("Screen OFF (Standby)");
  }
}

String formatDuration(int totalSeconds) {
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  if (hours > 0) {
    return String(hours) + "h " + String(minutes) + "m";
  } else {
    return String(minutes) + "m " + String(totalSeconds % 60) + "s";
  }
}

String formatSport(String rawSport) {
  if (rawSport == "racebike" || rawSport == "tour_racebiking") return "ROAD BIKE";
  if (rawSport == "gravel" || rawSport == "tour_gravelbiking") return "GRAVEL";
  if (rawSport == "touringbicycle" || rawSport == "tour_cycling") return "CYCLING";
  if (rawSport == "mtb" || rawSport == "tour_mountainbiking") return "MTB";
  if (rawSport == "e_touringbicycle" || rawSport == "e_bike") return "E-BIKE";
  if (rawSport == "hiking" || rawSport == "tour_hiking") return "HIKING";
  if (rawSport == "jogging" || rawSport == "tour_jogging") return "RUNNING";
  rawSport.toUpperCase();
  return rawSport;
}

String formatDate(String isoDate) {
  if (isoDate.length() >= 10) {
    String year = isoDate.substring(0, 4);
    String month = isoDate.substring(5, 7);
    String day = isoDate.substring(8, 10);
    return day + "." + month + "." + year;
  }
  return isoDate;
}

void loadDemoData() {
  isLiveKomoot = false;
  tours.clear();
  tours.push_back(TourData("Sunday Alpine Loop", "GRAVEL", "12.09.2026", 42.8f, 7820, 640.0f, 19.7f, 920));
  tours.push_back(TourData("Evening River Trail", "ROAD BIKE", "10.09.2026", 28.4f, 4210, 145.0f, 24.3f, 580));
  tours.push_back(TourData("Forest Hills Trail Run", "RUNNING", "08.09.2026", 10.2f, 3420, 210.0f, 10.7f, 710));
  currentTourIndex = 0;
}

// =========================================================================
// Image Download & PSRAM Caching
// =========================================================================
bool loadTourMapImage(TourData& tour) {
  if (tour.jpgData != nullptr && tour.jpgSize > 0) {
    return true; // Already cached in PSRAM!
  }
  if (tour.mapImageUrl.length() == 0 || !isOnline) {
    return false;
  }

  // Retry up to 3 times in case of transient TLS handshake reset (-80)
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("Fetching map image (attempt %d/3): %s\n", attempt, tour.mapImageUrl.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10000);

    HTTPClient http;
    http.setTimeout(12000);

    if (!http.begin(client, tour.mapImageUrl)) {
      Serial.println("http.begin failed for map image URL");
      delay(300);
      continue;
    }

    int httpCode = http.GET();
    if (httpCode != 200) {
      Serial.printf("Map image HTTP failed: %d\n", httpCode);
      http.end();
      delay(400);
      continue;
    }

    int totalBytes = http.getSize();
    WiFiClient *stream = http.getStreamPtr();

    if (totalBytes > 0) {
      uint8_t* buf = (uint8_t*)ps_malloc(totalBytes);
      if (!buf) {
        Serial.println("Failed to allocate PSRAM for map image!");
        http.end();
        return false;
      }

      size_t bytesRead = 0;
      uint32_t startWait = millis();
      while (bytesRead < (size_t)totalBytes && (millis() - startWait < 8000)) {
        int avail = stream->available();
        if (avail > 0) {
          int r = stream->read(buf + bytesRead, min(avail, (int)(totalBytes - bytesRead)));
          if (r > 0) {
            bytesRead += r;
            startWait = millis();
          }
        } else {
          if (!http.connected()) break;
          delay(5);
        }
      }
      http.end();

      if (bytesRead == (size_t)totalBytes) {
        tour.jpgData = buf;
        tour.jpgSize = bytesRead;
        Serial.printf("Map image successfully loaded (%d bytes)!\n", (int)bytesRead);
        return true;
      } else {
        Serial.printf("Incomplete map read: %d / %d bytes, retrying...\n", (int)bytesRead, totalBytes);
        free(buf);
      }
    } else {
      http.end();
    }

    delay(400);
  }

  Serial.println("Failed to download map image after 3 attempts.");
  return false;
}

// =========================================================================
// UI Drawing Functions
// =========================================================================
void drawButtons() {
  const int btnY = 266;
  const int btnH = 46;
  const int btnW = 68;

  // 1. PREV Button
  bool canPrev = (currentTourIndex > 0);
  tft.fillRoundRect(8, btnY, btnW, btnH, 8, canPrev ? COLOR_CARD_BG : 0x10A2);
  tft.drawRoundRect(8, btnY, btnW, btnH, 8, canPrev ? COLOR_CARD_BDR : 0x18E3);
  tft.setTextColor(canPrev ? TFT_WHITE : TFT_DARKGREY, canPrev ? COLOR_CARD_BG : 0x10A2);
  tft.drawCentreString("< PREV", 42, btnY + 14, 2);

  // 2. SYNC Button
  tft.fillRoundRect(86, btnY, btnW, btnH, 8, COLOR_KOMOOT);
  tft.drawRoundRect(86, btnY, btnW, btnH, 8, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, COLOR_KOMOOT);
  tft.drawCentreString("SYNC", 120, btnY + 14, 2);

  // 3. NEXT Button
  bool canNext = (currentTourIndex < (int)tours.size() - 1);
  tft.fillRoundRect(164, btnY, btnW, btnH, 8, canNext ? COLOR_CARD_BG : 0x10A2);
  tft.drawRoundRect(164, btnY, btnW, btnH, 8, canNext ? COLOR_CARD_BDR : 0x18E3);
  tft.setTextColor(canNext ? TFT_WHITE : TFT_DARKGREY, canNext ? COLOR_CARD_BG : 0x10A2);
  tft.drawCentreString("NEXT >", 198, btnY + 14, 2);
}

void drawHeader() {
  tft.fillRect(0, 0, 240, 30, COLOR_CARD_BG);
  tft.drawFastHLine(0, 30, 240, COLOR_CARD_BDR);

  tft.setTextColor(COLOR_KOMOOT, COLOR_CARD_BG);
  tft.drawString("KOMOOT", 10, 7, 4);

  if (isLiveKomoot) {
    // Green LIVE badge
    tft.fillRoundRect(146, 5, 86, 20, 10, 0x13A4);
    tft.drawRoundRect(146, 5, 86, 20, 10, COLOR_KOMOOT);
    tft.fillCircle(156, 15, 3, COLOR_KOMOOT);
    tft.setTextColor(TFT_WHITE, 0x13A4);
    tft.drawString("LIVE DATA", 164, 8, 2);
  } else {
    // Orange DEMO DATA badge
    tft.fillRoundRect(142, 5, 90, 20, 10, 0x39E0);
    tft.drawRoundRect(142, 5, 90, 20, 10, TFT_ORANGE);
    tft.setTextColor(TFT_ORANGE, 0x39E0);
    tft.drawCentreString("DEMO DATA", 187, 8, 2);
  }
}

void drawTourCard() {
  // Clear middle card area (y: 32 to 260)
  tft.fillRect(0, 32, 240, 230, COLOR_BG);

  if (tours.empty()) {
    tft.setTextColor(TFT_WHITE, COLOR_BG);
    tft.drawCentreString("No tours found", 120, 120, 4);
    tft.setTextColor(TFT_LIGHTGREY, COLOR_BG);
    tft.drawCentreString(statusMessage, 120, 160, 2);
    drawButtons();
    return;
  }

  TourData& tour = tours[currentTourIndex];

  // 1. Pagination indicator: "Tour 1 of 5" | Date
  tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
  String pageStr = "Tour " + String(currentTourIndex + 1) + " of " + String(tours.size());
  tft.drawString(pageStr, 12, 36, 2);
  tft.drawRightString(tour.date, 228, 36, 2);

  // 2. Main Card Container
  const int cardX = 8;
  const int cardY = 56;
  const int cardW = 224;
  const int cardH = 198;

  tft.fillRoundRect(cardX, cardY, cardW, cardH, 8, COLOR_CARD_BG);
  tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, COLOR_CARD_BDR);

  // 3. Sport Pill Badge
  tft.fillRoundRect(cardX + 10, cardY + 8, 96, 20, 10, COLOR_BG);
  tft.drawRoundRect(cardX + 10, cardY + 8, 96, 20, 10, COLOR_KOMOOT);
  tft.setTextColor(COLOR_KOMOOT, COLOR_BG);
  tft.drawCentreString(tour.sport, cardX + 58, cardY + 11, 2);

  // View toggle indicator pill on top-right of card: [ STATS ] or [ MAP ]
  tft.fillRoundRect(cardX + cardW - 74, cardY + 8, 66, 20, 10, showMapView ? COLOR_KOMOOT : 0x2145);
  tft.drawRoundRect(cardX + cardW - 74, cardY + 8, 66, 20, 10, showMapView ? TFT_WHITE : COLOR_TEXT_MUTED);
  tft.setTextColor(showMapView ? TFT_WHITE : COLOR_TEXT_MUTED, showMapView ? COLOR_KOMOOT : 0x2145);
  tft.drawCentreString(showMapView ? "MAP" : "STATS", cardX + cardW - 41, cardY + 11, 2);

  // 4. Tour Title
  tft.setTextColor(TFT_WHITE, COLOR_CARD_BG);
  String title = tour.name;
  if (title.length() > 22) title = title.substring(0, 20) + "..";
  tft.drawString(title, cardX + 10, cardY + 34, 4);

  // Subtle separator line
  tft.drawFastHLine(cardX + 8, cardY + 62, cardW - 16, COLOR_CARD_BDR);

  // =============================================================
  // View Branch: MAP VIEW vs STATS VIEW
  // =============================================================
  if (showMapView) {
    // -----------------------------------------------------------
    // A. MAP VIEW (Render Real Komoot JPEG)
    // -----------------------------------------------------------
    const int mapX = cardX + 2;
    const int mapY = cardY + 68;
    const int mapW = 220;
    const int mapH = 124;

    if (tour.jpgData == nullptr && tour.mapImageUrl.length() > 0 && isOnline) {
      tft.fillRect(mapX, mapY, mapW, mapH, COLOR_BG);
      tft.drawRect(mapX, mapY, mapW, mapH, COLOR_CARD_BDR);
      tft.setTextColor(COLOR_ACCENT, COLOR_BG);
      tft.drawCentreString("Downloading map...", 120, mapY + 52, 2);
    }

    bool hasImage = loadTourMapImage(tour);
    if (hasImage && tour.jpgData != nullptr && tour.jpgSize > 0) {
      TJpgDec.drawJpg(mapX, mapY, tour.jpgData, tour.jpgSize);
    } else {
      // Fallback placeholder map graphic
      tft.fillRect(mapX, mapY, mapW, mapH, COLOR_BG);
      tft.drawRect(mapX, mapY, mapW, mapH, COLOR_CARD_BDR);
      tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
      tft.drawCentreString("Map not available", 120, mapY + 45, 2);
      tft.drawCentreString("Tap card to switch", 120, mapY + 65, 2);
    }

    // Mini footer under map
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
    tft.drawString(String(tour.distanceKm, 1) + " km  |  +" + String((int)tour.elevationUp) + " m", cardX + 10, cardY + cardH - 18, 2);
    tft.setTextColor(COLOR_KOMOOT, COLOR_CARD_BG);
    tft.drawRightString("Tap for stats", cardX + cardW - 10, cardY + cardH - 18, 2);

  } else {
    // -----------------------------------------------------------
    // B. STATS VIEW (2x2 Grid of Metrics)
    // -----------------------------------------------------------
    int col1 = cardX + 12;
    int col2 = cardX + 120;
    int row1 = cardY + 74;
    int row2 = cardY + 126;

    // Stat 1: DISTANCE
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
    tft.drawString("DISTANCE", col1, row1, 2);
    tft.setTextColor(TFT_WHITE, COLOR_CARD_BG);
    tft.drawString(String(tour.distanceKm, 1) + " km", col1, row1 + 18, 2);

    // Stat 2: MOVING TIME
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
    tft.drawString("MOVING TIME", col2, row1, 2);
    tft.setTextColor(TFT_WHITE, COLOR_CARD_BG);
    tft.drawString(formatDuration(tour.durationSec), col2, row1 + 18, 2);

    // Stat 3: ELEVATION
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
    tft.drawString("ELEVATION", col1, row2, 2);
    tft.setTextColor(COLOR_ACCENT, COLOR_CARD_BG);
    tft.drawString("+" + String((int)tour.elevationUp) + " m", col1, row2 + 18, 2);

    // Stat 4: AVG SPEED
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
    tft.drawString("AVG SPEED", col2, row2, 2);
    tft.setTextColor(TFT_WHITE, COLOR_CARD_BG);
    tft.drawString(String(tour.avgSpeedKmH, 1) + " km/h", col2, row2 + 18, 2);

    // Hint text at bottom of card
    tft.setTextColor(COLOR_KOMOOT, COLOR_CARD_BG);
    tft.drawCentreString("[ Tap card to view Route Map ]", 120, cardY + cardH - 20, 2);
  }

  // Draw Bottom Nav Buttons
  drawButtons();
}

// =========================================================================
// Komoot API Client
// =========================================================================
bool fetchKomootTours() {
  if (WiFi.status() != WL_CONNECTED) {
    statusMessage = "WiFi not connected";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(12000);

  // Basic Auth Header
  String authRaw = String(KOMOOT_EMAIL) + ":" + String(KOMOOT_PASSWORD);
  String authHeader = "Basic " + base64::encode(authRaw);

  // -------------------------------------------------------------
  // Step 1: Query User Identifier (Account info)
  // -------------------------------------------------------------
  String accountUrl = "https://api.komoot.de/v006/account/email/" + String(KOMOOT_EMAIL) + "/";
  Serial.print("Querying Komoot account: ");
  Serial.println(accountUrl);

  if (!https.begin(client, accountUrl)) {
    statusMessage = "HTTPS connect failed";
    return false;
  }

  https.addHeader("Authorization", authHeader);
  https.addHeader("User-Agent", "KomootApp/1.0");
  https.addHeader("Accept", "application/hal+json,application/json");

  int httpCode = https.GET();
  if (httpCode != 200) {
    Serial.printf("Komoot Auth failed! HTTP Code: %d\n", httpCode);
    statusMessage = "Komoot login failed (" + String(httpCode) + ")";
    https.end();
    return false;
  }

  String accountPayload = https.getString();
  https.end();
  Serial.printf("Account payload received: %d bytes\n", accountPayload.length());

  DynamicJsonDocument accountDoc(2048);
  DeserializationError err = deserializeJson(accountDoc, accountPayload);

  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    statusMessage = "Auth JSON parse error";
    return false;
  }

  const char* username = accountDoc["username"];
  if (!username) {
    statusMessage = "No user ID found";
    return false;
  }

  String userId = String(username);
  Serial.println("Komoot User ID: " + userId);

  // -------------------------------------------------------------
  // Step 2: Fetch Latest 5 Recorded Tours
  // -------------------------------------------------------------
  String toursUrl = "https://api.komoot.de/v007/users/" + userId + "/tours/?type=tour_recorded&sort_field=date&sort_direction=desc&limit=5";
  Serial.print("Fetching tours: ");
  Serial.println(toursUrl);

  if (!https.begin(client, toursUrl)) {
    statusMessage = "Tour URL error";
    return false;
  }

  https.addHeader("Authorization", authHeader);
  https.addHeader("User-Agent", "KomootApp/1.0");
  https.addHeader("Accept", "application/hal+json,application/json");

  httpCode = https.GET();
  if (httpCode != 200) {
    Serial.printf("Tour fetch failed! HTTP Code: %d\n", httpCode);
    statusMessage = "Tours fetch failed (" + String(httpCode) + ")";
    https.end();
    return false;
  }

  String toursPayload = https.getString();
  https.end();
  Serial.printf("Tours payload received: %d bytes\n", toursPayload.length());

  // Filter only needed fields including map_image URL
  StaticJsonDocument<384> filter;
  JsonObject filterTour = filter["_embedded"]["tours"].createNestedObject();
  filterTour["name"] = true;
  filterTour["sport"] = true;
  filterTour["date"] = true;
  filterTour["distance"] = true;
  filterTour["duration"] = true;
  filterTour["time_in_motion"] = true;
  filterTour["elevation_up"] = true;
  filterTour["kcal_active"] = true;
  filterTour["map_image"]["src"] = true;

  DynamicJsonDocument toursDoc(16384);
  err = deserializeJson(toursDoc, toursPayload, DeserializationOption::Filter(filter));

  if (err) {
    Serial.print("Tours JSON parse error: ");
    Serial.println(err.c_str());
    statusMessage = "Tours JSON error";
    return false;
  }

  JsonArray toursList = toursDoc["_embedded"]["tours"].as<JsonArray>();
  if (toursList.isNull() || toursList.size() == 0) {
    statusMessage = "No recorded tours found";
    return false;
  }

  // Free existing cached image buffers
  for (auto& t : tours) {
    if (t.jpgData != nullptr) {
      free(t.jpgData);
      t.jpgData = nullptr;
    }
  }
  tours.clear();

  for (JsonObject t : toursList) {
    TourData td;
    td.name = t["name"] | "Tour";
    td.sport = formatSport(t["sport"] | "tour_cycling");
    td.date = formatDate(t["date"] | "");
    td.distanceKm = (t["distance"] | 0.0f) / 1000.0f;
    td.durationSec = t["time_in_motion"] | (t["duration"] | 0);
    td.elevationUp = t["elevation_up"] | 0.0f;
    td.kcal = t["kcal_active"] | 0;

    // Format map image URL (request 220x124 pixels)
    String rawImgUrl = t["map_image"]["src"] | "";
    if (rawImgUrl.length() > 0) {
      rawImgUrl.replace("{width}", "220");
      rawImgUrl.replace("{height}", "124");
      rawImgUrl.replace("{crop}", "false");
      td.mapImageUrl = rawImgUrl;
    }

    if (td.durationSec > 0 && td.distanceKm > 0) {
      td.avgSpeedKmH = td.distanceKm / (td.durationSec / 3600.0f);
    } else {
      td.avgSpeedKmH = 0.0f;
    }

    tours.push_back(td);
  }

  currentTourIndex = 0;
  isLiveKomoot = true;
  statusMessage = "Updated successfully";
  Serial.printf("Successfully loaded %d tours from Komoot!\n", tours.size());
  return true;
}

void syncWithKomoot() {
  tft.fillRect(0, 32, 240, 230, COLOR_BG);
  tft.setTextColor(COLOR_KOMOOT, COLOR_BG);
  tft.drawCentreString("CONNECTING...", 120, 120, 4);
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.drawCentreString("Fetching Komoot data", 120, 160, 2);

  bool success = false;
  if (isOnline) {
    success = fetchKomootTours();
  }

  if (!success) {
    if (tours.empty()) {
      loadDemoData();
    }
  }

  drawHeader();
  drawTourCard();
  lastInteractionTime = millis();
}

// =========================================================================
// Setup & Loop
// =========================================================================
void setup() {
  Serial.begin(115200);

  // 1. Power on peripherals (Display & Touch power latch)
  pinMode(PWR_EN_PIN, OUTPUT);
  digitalWrite(PWR_EN_PIN, HIGH);

  // Physical button (BOOT pin) for manual wake-up
  pinMode(BUTTON1_PIN, INPUT_PULLUP);

  // 2. Backlight setup
  pinMode(BK_LIGHT_PIN, OUTPUT);
  setBrightness(16);

  // 3. Initialize parallel ST7789 TFT display
  tft.begin();
  tft.setRotation(0); // 240 x 320 portrait
  tft.setSwapBytes(true);
  tft.fillScreen(COLOR_BG);

  // 4. Initialize TJpg_Decoder (swapBytes=false because tft.setSwapBytes(true) handles big-endian write)
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(tft_output);

  // 5. Initialize Touch
  SPI.begin(TOUCHSCREEN_SCLK_PIN, TOUCHSCREEN_MISO_PIN, TOUCHSCREEN_MOSI_PIN);
  touch.begin(240, 320);

  // 6. Load calibration from NVS
  prefs.begin("touch", true);
  if (prefs.getBool("valid", false)) {
    cal_raw_xmin = prefs.getUShort("xmin", cal_raw_xmin);
    cal_raw_xmax = prefs.getUShort("xmax", cal_raw_xmax);
    cal_raw_ymin = prefs.getUShort("ymin", cal_raw_ymin);
    cal_raw_ymax = prefs.getUShort("ymax", cal_raw_ymax);
    Serial.println("Loaded touch calibration from flash NVS.");
  }
  prefs.end();

  touch.setCal(cal_raw_xmin, cal_raw_xmax, cal_raw_ymin, cal_raw_ymax, 240, 320);
  touch.setRotation(0);

  drawHeader();

  // 7. Connect to Wi-Fi if configured
  if (String(WIFI_SSID) != "YOUR_WIFI_SSID") {
    tft.setTextColor(TFT_WHITE, COLOR_BG);
    tft.drawCentreString("Connecting to WiFi...", 120, 130, 2);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 25) {
      delay(400);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
      isOnline = true;
    } else {
      Serial.println("\nWiFi connection failed. Falling back to demo mode.");
      isOnline = false;
    }
  } else {
    Serial.println("WiFi not configured. Running in Demo Mode.");
    isOnline = false;
  }

  drawHeader();
  syncWithKomoot();
  lastInteractionTime = millis();
}

void loop() {
  // 1. Check physical BOOT button for wake-up / activity
  if (digitalRead(BUTTON1_PIN) == LOW) {
    lastInteractionTime = millis();
    if (!isScreenOn) {
      setScreenPower(true);
      delay(250); // Debounce button
      return;
    }
  }

  // 2. Check Touch Interaction
  if (touch.pressed()) {
    lastInteractionTime = millis();

    // If screen was off in standby, wake up and ignore first touch (prevents unintended button actions)
    if (!isScreenOn) {
      setScreenPower(true);
      delay(300); // Debounce wake-up touch
      return;
    }

    int x = touch.X();
    int y = touch.Y();

    if (INVERT_X) x = 240 - x;
    if (INVERT_Y) y = 320 - y;

    x = constrain(x, 0, 239);
    y = constrain(y, 0, 319);

    // Check Card Touch (y: 54 to 256, x: 8 to 232) -> TOGGLE MAP / STATS!
    if (y >= 54 && y <= 256 && x >= 8 && x <= 232) {
      showMapView = !showMapView;
      drawTourCard();
      delay(220); // Debounce card toggle
      return;
    }

    // Check Bottom Navigation Buttons (y: 260 to 318)
    if (y >= 260 && y <= 318) {
      // PREV Button (x: 8 to 76)
      if (x >= 8 && x <= 76) {
        if (currentTourIndex > 0) {
          currentTourIndex--;
          drawTourCard();
          delay(200); // Debounce
        }
        return;
      }

      // SYNC Button (x: 86 to 154)
      if (x >= 86 && x <= 154) {
        syncWithKomoot();
        delay(300); // Debounce
        return;
      }

      // NEXT Button (x: 164 to 232)
      if (x >= 164 && x <= 232) {
        if (currentTourIndex < (int)tours.size() - 1) {
          currentTourIndex++;
          drawTourCard();
          delay(200); // Debounce
        }
        return;
      }
    }
  }

  // 3. Automatic screen timeout after 2 minutes of inactivity
  if (isScreenOn && (millis() - lastInteractionTime >= SCREEN_TIMEOUT_MS)) {
    setScreenPower(false);
  }

  // Sleep longer in standby to conserve CPU power
  delay(isScreenOn ? 15 : 50);
}
