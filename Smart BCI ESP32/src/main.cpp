// Waveshare TFT LCD 3.5" 480x320 65k colours, Interface: SPI, Controller: ILI9486
// https://shopee.sg/Touch-LCD-Shield-3.5-inch-for-Arduino-i.440521573.17478975100?sp_atk=eee62442-3704-4ede-883e-773af8ec4ae0&xptdk=eee62442-3704-4ede-883e-773af8ec4ae0
// http://www.waveshare.com/wiki/3.5inch_TFT_Touch_Shield
// https://forum.arduino.cc/t/solved-waveshare-3-5inch-touch-lcd-shield-w-esp32/690369

/* Pin connections to ESP32 with notes
5V → 5V
GND → GND
SCLK (D13): SPI Clock → 18
MISO (D12) SPI Data Input → 19
MOSI (D11) SPI Data Output → 23
LCD_CS (D10) LCD Chip Select → 15
LCD_BL (D9) LCD Backlight → 5V
LCD_RST (D8) LCD Reset → 4
LCD_DC (D7) LCD Data/Command Selection → 2
TP stands for Touch Panel, not used.
SD refers to the micro SD card, not used.
*/

#include <SPI.h>
#include <TFT_eSPI.h>
TFT_eSPI tft; // uses pins/driver from User_Setup.h
#include <math.h>
extern const GFXfont FreeSansBold12pt7b;
extern const GFXfont FreeSansBold24pt7b;
#include <esp_now.h>
#include <WiFi.h>

// ==== BUTTON ISR: ESP32 FreeRTOS helpers for atomic access
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Assign human-readable names to some common 16-bit RGB565 colour values:
#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0xDFE0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define WHITE 0xFFFF
#define NAVY 0x000F      /*   0,   0, 128 */
#define DARKCYAN 0x03EF  /*   0, 128, 128 */
#define DARKGREEN 0x0400 /*   0, 128,   0 */
#define MAROON 0x7800    /* 128,   0,   0 */
#define PURPLE 0x780F    /* 128,   0, 128 */
#define OLIVE 0x7BE0     /* 128, 128,   0 */
#define LIGHTGREY 0xC618 /* 192, 192, 192 */
#define DARKGREY 0x7BEF  /* 128, 128, 128 */
#define ORANGE 0xFDA0    /* 255, 180,   0 */
#define DARKORANGE 0xF940
#define GREENYELLOW 0xB7E0 /* 180, 255,   0 */
#define PINK 0xFC9F
#define DARKRED 0x8800

// USER-CONFIGURABLE CONSTANTS
constexpr uint8_t ROTATION = 1; // 0 for Portrait, 1 for Landscape (90 degrees), 2 for Reversed Portrait (180 degrees), 3 for Reversed Landscape (270 degrees))
constexpr uint16_t BACKGROUND_COLOUR = BLACK;
constexpr uint16_t SOME_LINE_COLOUR = WHITE;
constexpr uint8_t BORDER_THICKNESS = 8; // pixels
constexpr uint8_t PERCENTAGE_SIZE = 3;
constexpr uint8_t CROSS_THICKNESS = 8; // must be ≥1
constexpr int16_t ICON_X = 35;         // x-coordinate of top-left corner of battery icon
constexpr int16_t ICON_Y = 70;         // y-coordinate of top-left corner of battery icon
constexpr int16_t ICON_W = 400;        // battery icon width
constexpr int16_t ICON_H = 150;        // battery icon height
constexpr int16_t TERM_W = 12;         // positive terminal width
constexpr int16_t DYSV8F_IO0 = 25;
constexpr int16_t DYSV8F_IO1 = 26;
constexpr int16_t DYSV8F_IO2 = 27;
constexpr int16_t BUTTON = 13;
constexpr int16_t MOSFET_GATE = 12;
constexpr uint32_t BMS_QUERY_MS = 250; // poll Daly BMS no faster than 4 Hz

// ==== BUTTON ISR: shared state (volatile) and debounce
static volatile bool g_buttonEvent = false;
static volatile uint32_t g_lastButtonIsrUs = 0;
static portMUX_TYPE g_isrMux = portMUX_INITIALIZER_UNLOCKED;
constexpr uint32_t BUTTON_DEBOUNCE_US = 500 * 1000; // 500 ms

// GLOBALS
int previous_soc = -1;
int input_soc = -1;        // to store user input
int previous_chg = -1;     // 0 = not charging, 1 = charging
int input_chg = -1;        // to store user input
float input_I = -1.0;      // Current in Amperes, +ve charge, -ve discharge
float input_resmAh = -1.0; // Remaining capacity in mAh
uint32_t lastBlinkMs = 0;
bool lowBlinkState = false; // toggles when blinking
bool isBatteryBarsWiped = false;
const char *current_status_msg = "";
const char *previous_status_msg = "";
uint8_t crcFailCnt = 0; // consecutive CRC error count
bool crcFailed = false;
int8_t lastLowSOCAudioPlayed = -1; // Remember last SoC that triggered "Battery Low. Please Charge" audio
bool audio_module_on = true;

// --- TTF/TTE helpers ---
char lastTopLine[48] = "";              // what we last drew in the top strip (for flicker-free updates)
constexpr float I_MIN_ABS_A = 0.3;      // ignore near-zero currents (A) to avoid silly times
constexpr float DISCH_MIN_ABS_A = 1.0f; // Discharge must be at least this strong to count

// Define a data structure
// Has to be the same as the struct in the Initiator
typedef struct struct_message
{
  bool bms_status;
  float soc;
  float I;
  float resmAh;
} struct_message;

// Create a structure object
struct_message BMSData;

void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength)
// Formats MAC Address
{
  snprintf(buffer, maxLength, "%02x:%02x:%02x:%02x:%02x:%02x", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}

//!
// --- TTE discharge-history (very fast attack, no decay when idle) ---
struct TTEHist
{
  float emaA; // EMA of discharge current magnitude (A)
  bool valid;
} g_tteHist = {0.0f, false};
// ---- Historical TTE Tuneables ---
constexpr float TTE_TAU_UP_S = 2.5f;    // fast attack (~95% in ~7.5 s) (make TTE_TAU_UP_S even smaller if you want it snappier)
constexpr float STEP_RATIO = 1.25f;     // consider it a "step up" if new draw >= 125% of EMA
constexpr float STEP_JUMP_GAIN = 0.80f; // jump 80% of the way immediately on step up
constexpr float TTE_BOOTSTRAP_A = 1.0f; // if EMA very small, snap to first real draw
//!
constexpr uint32_t HIST_TTE_DWELL_MS = 3000; // show historical TTE only after 3s
// ---- Historical TTE based on long-window robust draw ----
constexpr uint16_t HIST_SAMPLE_HZ = 1.0;                        // collect once per second
constexpr uint32_t HIST_WINDOW_SEC = 15 * 60;                   // 15 minutes window (adjust)
constexpr float HIST_KEEP_MIN_A = 1.0f;                         // ignore tiny discharge (decel/coast)
constexpr float HIST_TRIM_LOW_FRAC = 0.40f;                     // drop bottom HIST_TRIM_LOW_FRAC
constexpr float HIST_TRIM_HIGH_FRAC = 0.05f;                    // drop top HIST_TRIM_HIGH_FRAC
constexpr uint16_t HIST_MIN_SAMPLES = 10;                       // need at least 10s of history
constexpr uint16_t HIST_CAP = HIST_SAMPLE_HZ * HIST_WINDOW_SEC; // buffer size
static float g_histBuf[HIST_CAP];
static uint16_t g_histCount = 0, g_histHead = 0;
static uint32_t g_histLastPushMs = 0;

static inline void insertionSort(float *a, int n)
{
  for (int i = 1; i < n; ++i)
  {
    float key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > key)
    {
      a[j + 1] = a[j];
      --j;
    }
    a[j + 1] = key;
  }
}

static inline void histPushDischarge(float I_raw)
{
  if (I_raw < -DISCH_MIN_ABS_A)
  {
    const float Idis = -I_raw; // magnitude
    if (Idis >= HIST_KEEP_MIN_A)
    { // ignore tiny decel/coast
      const uint32_t now = millis();
      if (now - g_histLastPushMs >= (1000 / HIST_SAMPLE_HZ))
      {
        g_histBuf[g_histHead] = Idis;
        g_histHead = (g_histHead + 1) % HIST_CAP;
        if (g_histCount < HIST_CAP)
          ++g_histCount;
        g_histLastPushMs = now;
      }
    }
  }
}

static float histTypicalDrawA_trimmedMean()
{
  if (g_histCount < HIST_MIN_SAMPLES)
    return NAN;

  // Copy ring -> tmp (linearize)
  static float tmp[HIST_CAP];
  uint16_t n = g_histCount;
  uint16_t idx = (g_histHead + HIST_CAP - g_histCount) % HIST_CAP;
  for (uint16_t i = 0; i < n; ++i)
  {
    tmp[i] = g_histBuf[idx];
    idx = (idx + 1) % HIST_CAP;
  }

  // Sort and trimmed mean
  insertionSort(tmp, n);
  uint16_t lo = (uint16_t)floorf(n * HIST_TRIM_LOW_FRAC);
  uint16_t hi = n - (uint16_t)ceilf(n * HIST_TRIM_HIGH_FRAC);
  if (hi <= lo)
    return tmp[n / 2]; // fallback: median

  double sum = 0.0;
  uint16_t cnt = 0;
  for (uint16_t i = lo; i < hi; ++i)
  {
    sum += tmp[i];
    ++cnt;
  }
  return (cnt ? (float)(sum / cnt) : tmp[n / 2]);
}

//!
static inline void tteHistUpdate(float I_raw)
{
  if (I_raw < -DISCH_MIN_ABS_A)
  {
    const float Idis = -I_raw;  // positive magnitude
    static uint32_t lastMs = 0; // Note that the lastMs variable is a static variable, so it is only initialised once
    const uint32_t now = millis();
    const float dt = (lastMs == 0) ? (BMS_QUERY_MS / 1000.0f) : (now - lastMs) / 1000.0f; // If lastMs == 0 (only occurs in first call, i.e., during lastMs's initialisation), dt = BMS_QUERY_MS in seconds (0.25 s), else dt = elapsed time since last update in seconds
    lastMs = now;

    const float alpha = 1.0f - expf(-dt / TTE_TAU_UP_S);
    // Kinda similar to a log graph but starting from the origin.
    // The larger the value of dt (x-value), the closer alpha (y-value) gets to 1 (asymptote at 1).
    // The smaller the value of TTE_TAU_UP_S, the faster alpha approaches 1 (the graph approaches a step function)

    if (!g_tteHist.valid || g_tteHist.emaA < TTE_BOOTSTRAP_A) // If TTE history is not being shown or the current EMA current is less than bootstrap threshold
    {
      g_tteHist.emaA = Idis;  // Update EMA current to current raw current draw
      g_tteHist.valid = true; // Show TTE history
    }
    else if (Idis >= g_tteHist.emaA * STEP_RATIO) // If current raw current draw is significantly higher (≥125%) than current EMA current
    {
      // Update EMA current quickly by jumping most of the way (STEP_JUMP_GAIN = 80%) towards the value of the current raw current draw
      g_tteHist.emaA = g_tteHist.emaA + STEP_JUMP_GAIN * (Idis - g_tteHist.emaA);
    }
    else
    {
      // If the current raw current draw is not that big or smaller than the current EMA current, update EMA current normally according to alpha.
      // If dt is large (infrequent updates), alpha is large (approaching 1), so EMA current moves quickly towards current raw current draw.
      // If dt is small (frequent updates), alpha is small (positive decimal), so EMA current moves slowly towards current raw current draw.
      g_tteHist.emaA += alpha * (Idis - g_tteHist.emaA);
    }
  }
}
//!

//!
// --- Debug: print TTE history (throttled once/sec) ---
void debugPrintTTEHist()
{
  static uint32_t lastPrint = 0;
  const uint32_t PRINT_PERIOD_MS = 1000;
  uint32_t now = millis();
  if (now - lastPrint < PRINT_PERIOD_MS)
    return;
  lastPrint = now;

  Serial.print(F("👉 [TTEHist] valid="));
  Serial.print(g_tteHist.valid ? F("yes") : F("no"));
  Serial.print(F("  emaA="));
  if (g_tteHist.valid)
    Serial.print(g_tteHist.emaA, 3);
  else
    Serial.print(F("NA"));
  Serial.println(F(" A"));
}
//!

// COLOUR UTILITIES
uint16_t colourForSOC(int soc)
{
  if (soc == 0)
    return DARKRED;
  else if (soc <= 20)
    return RED;
  else if (soc <= 40)
    return DARKORANGE;
  else if (soc <= 60)
    return YELLOW;
  else if (soc <= 80)
    return GREEN;
  else if (soc <= 100)
    return DARKGREEN;
  else
    return PINK; // For troubleshooting purposes
}

// DRAW HELPERS
void drawBatteryIcon(int soc, int chg)
{
  // Erase previous drawing in icon area
  tft.fillRect(ICON_X - 2, ICON_Y - 2, ICON_W + TERM_W + 4, ICON_H + 4, BACKGROUND_COLOUR);
  isBatteryBarsWiped = true;

  // Outline & terminal
  tft.drawRect(ICON_X, ICON_Y, ICON_W, ICON_H, SOME_LINE_COLOUR);
  tft.fillRect(ICON_X + ICON_W, ICON_Y + ICON_H / 4, TERM_W, ICON_H / 2, SOME_LINE_COLOUR);

  // Cross if battery empty
  if (soc == 0 && !chg)
  {
    const uint16_t CROSS_COLOUR = DARKRED;
    int8_t half = CROSS_THICKNESS / 2; // centred offsets

    // First diagonal:    top-left ➜ bottom-right
    for (int8_t off = -half; off <= half; ++off)
      tft.drawLine(ICON_X + off, ICON_Y, ICON_X + ICON_W + off, ICON_Y + ICON_H, CROSS_COLOUR);

    // Second diagonal:   bottom-left ➜ top-right
    for (int8_t off = -half; off <= half; ++off)
      tft.drawLine(ICON_X + off, ICON_Y + ICON_H, ICON_X + ICON_W + off, ICON_Y, CROSS_COLOUR);
  }
}

void drawBorder(int soc)
{
  tft.fillRect(0, 0, tft.width(), BORDER_THICKNESS, colourForSOC(soc));
  tft.fillRect(0, 0, BORDER_THICKNESS, tft.height(), colourForSOC(soc));
  tft.fillRect(0, tft.height() - BORDER_THICKNESS, tft.width(), BORDER_THICKNESS, colourForSOC(soc));
  tft.fillRect(tft.width() - BORDER_THICKNESS, 0, BORDER_THICKNESS, tft.height(), colourForSOC(soc));
}

// Draw a "thick" line by filling a skinny quad made from 2 triangles.
static inline void drawThickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color, uint8_t thickness)
{
  if (thickness <= 1)
  {
    tft.drawLine(x0, y0, x1, y1, color);
    return;
  }

  float dx = (float)x1 - (float)x0;
  float dy = (float)y1 - (float)y0;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f)
    return;

  // Unit normal (perpendicular) vector
  float nx = -dy / len;
  float ny = dx / len;

  float hw = 0.5f * (float)thickness;

  // Offset endpoints to build a quad around the line
  int16_t x0a = (int16_t)lrintf((float)x0 + nx * hw);
  int16_t y0a = (int16_t)lrintf((float)y0 + ny * hw);
  int16_t x0b = (int16_t)lrintf((float)x0 - nx * hw);
  int16_t y0b = (int16_t)lrintf((float)y0 - ny * hw);

  int16_t x1a = (int16_t)lrintf((float)x1 + nx * hw);
  int16_t y1a = (int16_t)lrintf((float)y1 + ny * hw);
  int16_t x1b = (int16_t)lrintf((float)x1 - nx * hw);
  int16_t y1b = (int16_t)lrintf((float)y1 - ny * hw);

  // Fill the quad as two triangles
  tft.fillTriangle(x0a, y0a, x1a, y1a, x1b, y1b, color);
  tft.fillTriangle(x0a, y0a, x1b, y1b, x0b, y0b, color);
}

void drawLightningBolt()
{
  const uint16_t FILL_COLOUR = YELLOW;
  const uint16_t OUTLINE_COLOUR = BLACK;
  const uint8_t OUTLINE_THICK = 4; // <-- adjust 2..6 to taste

  const int16_t mx = ICON_X + ICON_W / 2;
  const int16_t my = ICON_Y + ICON_H / 2;

  int16_t Ax = mx - 140, Ay = my - 60;
  int16_t Bx = mx - 140, By = my + 30;
  int16_t Cx = mx + 20, Cy = my + 50;
  int16_t Dx = mx + 20, Dy = my + 20;
  int16_t Ex = mx + 140, Ey = my + 50;
  int16_t Fx = mx - 20, Fy = my - 60;
  int16_t Gx = mx - 20, Gy = my - 10;

  // Fill the bolt
  tft.fillTriangle(Ax, Ay, Bx, By, Cx, Cy, FILL_COLOUR);
  tft.fillTriangle(Gx, Gy, Dx, Dy, Cx, Cy, FILL_COLOUR);
  tft.fillTriangle(Gx, Gy, Dx, Dy, Fx, Fy, FILL_COLOUR);
  tft.fillTriangle(Ax, Ay, Gx, Gy, Cx, Cy, FILL_COLOUR);
  tft.fillTriangle(Dx, Dy, Ex, Ey, Fx, Fy, FILL_COLOUR);

  // Thick outline around the edges
  drawThickLine(Ax, Ay, Bx, By, OUTLINE_COLOUR, OUTLINE_THICK);
  drawThickLine(Bx, By, Cx, Cy, OUTLINE_COLOUR, OUTLINE_THICK);
  drawThickLine(Cx, Cy, Dx, Dy, OUTLINE_COLOUR, OUTLINE_THICK);
  drawThickLine(Dx, Dy, Ex, Ey, OUTLINE_COLOUR, OUTLINE_THICK);
  drawThickLine(Ex, Ey, Fx, Fy, OUTLINE_COLOUR, OUTLINE_THICK);
  drawThickLine(Fx, Fy, Gx, Gy, OUTLINE_COLOUR, OUTLINE_THICK);
  drawThickLine(Gx, Gy, Ax, Ay, OUTLINE_COLOUR, OUTLINE_THICK);
}

void drawBatteryBars(int soc)
{
  // Bars inside the battery
  uint16_t barColour = colourForSOC(soc);

  const uint8_t barsToFill = (soc + 19) / 20; // 0…5 (/ is integer division, i.e., divison with truncation))
  const int16_t BAR_W = (ICON_W - 12) / 5;    // leave small padding
  const int16_t BAR_H = ICON_H - 12;
  const int16_t BAR_Y = ICON_Y + 6;

  for (uint8_t i = 0; i < 5; ++i) // Prefix increment, but this loop still runs 5 times (0, 1, 2, 3, 4) (prefix or suffix does not affect iteration count)
  {
    int16_t barX = ICON_X + 10 + i * BAR_W;
    if (i < barsToFill && soc > 0)
      tft.fillRect(barX, BAR_Y, BAR_W - 6, BAR_H, barColour);
    else
      tft.drawRect(barX, BAR_Y, BAR_W - 6, BAR_H, DARKGREY); // grey hollow
  }
}

void drawPercentage(int soc)
{
  char buf[12];
  snprintf(buf, sizeof(buf), "%d%%", soc); // one string so everything matches

  const uint8_t SIZE = PERCENTAGE_SIZE; // try 5–8 to taste
  tft.setTextFont(4);
  tft.setTextSize(SIZE);
  tft.setTextColor(SOME_LINE_COLOUR, BACKGROUND_COLOUR);

  // Clear a band below the battery
  uint16_t h = tft.fontHeight(); // scaled height
  uint16_t clearY = ICON_Y + ICON_H + 2;
  uint16_t clearH = h + 16; // margin
  tft.fillRect(BORDER_THICKNESS + 1,
               clearY,
               tft.width() - 2 * BORDER_THICKNESS - 1,
               clearH,
               BACKGROUND_COLOUR);

  // Center the full "NN%" string
  tft.setTextDatum(MC_DATUM);
  tft.drawString(buf, tft.width() / 2, clearY + clearH / 2);

  // Restore defaults
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextFont(1);
}

void statusMessage(int soc, int chg)
{
  current_status_msg = "";

  if (chg && soc < 100)
  {
    current_status_msg = "BATTERY CHARGING";
  }
  else if (chg && soc == 100)
  {
    current_status_msg = "BATTERY FULL!";
  }
  else if (soc <= 20)
  {
    current_status_msg = "BATTERY LOW PLEASE CHARGE";
  }
}

void playAudio(const char *status_message)
{
  if (audio_module_on == false)
    return;

  digitalWrite(DYSV8F_IO0, HIGH);
  digitalWrite(DYSV8F_IO1, HIGH);
  digitalWrite(DYSV8F_IO2, HIGH);

  // If the strings are equal, the strcmp function returns 0.
  if (strcmp(status_message, "BATTERY CHARGING") == 0)
  {
    digitalWrite(DYSV8F_IO0, HIGH); // Plays "Charging"
    digitalWrite(DYSV8F_IO1, LOW);
    digitalWrite(DYSV8F_IO2, HIGH);
  }
  else if (strcmp(status_message, "BATTERY FULL!") == 0)
  {
    digitalWrite(DYSV8F_IO0, LOW); // Plays music and "Battery Full. Charging Complete"
    digitalWrite(DYSV8F_IO1, LOW);
    digitalWrite(DYSV8F_IO2, HIGH);
  }
  else if (strcmp(status_message, "BATTERY LOW PLEASE CHARGE") == 0)
  {
    digitalWrite(DYSV8F_IO0, LOW); // Plays "Battery Low. Please Charge"
    digitalWrite(DYSV8F_IO1, HIGH);
    digitalWrite(DYSV8F_IO2, HIGH);
  }
  else if (strcmp(status_message, "") == 0)
  {
    digitalWrite(DYSV8F_IO0, HIGH); // Plays silent audio file
    digitalWrite(DYSV8F_IO1, HIGH);
    digitalWrite(DYSV8F_IO2, LOW);
  }

  delay(10); // Must have delay, without it, the pin states changes too fast and the audio module does not register. (4ms is the borderline shortest delay needed)

  digitalWrite(DYSV8F_IO0, HIGH);
  digitalWrite(DYSV8F_IO1, HIGH);
  digitalWrite(DYSV8F_IO2, HIGH);
}

// Exponential moving average (EMA) of current with ~3s time constant (dt-aware)
float filteredCurrentA(float input_I)
{
  static bool init = false;
  static float ema = 0.0f;
  static uint32_t lastMs = 0;
  // Note that these are static variables, so they are only initialised once

  uint32_t now = millis();
  float dt = (lastMs == 0) ? (BMS_QUERY_MS / 1000.0f) : (now - lastMs) / 1000.0f;
  // If lastMs is zero (first call), assume dt = BMS_QUERY_MS (250ms), else dt = now - lastMs
  lastMs = now;

  // time constant tau ~ 3 s
  const float tau = 3.0f;
  float alpha = 1.0f - expf(-dt / tau);
  // Imagine a log graph starting at the origin (0, 0).
  // As dt increases, alpha approaches 1 asymptotically.
  // The smaller the value of tau, the faster alpha approaches 1 (the graph approaches a step function)

  if (!init) // If init == false (will only occur on first call, i.e., initialisation)
  {
    ema = input_I; // In first call, set EMA current to be the raw input current
    init = true;
  }
  else
  {
    ema += alpha * (input_I - ema);
    // In subsequent calls, calculate EMA current as such.
    // The larger the value of dt, the larger the value of alpha approaching 1, the updated EMA current value is closer to the current raw current draw.
    // The smaller the value of dt, the smaller the value of alpha (正小数), the updated EMA current value only inches towards the current raw current draw (less influenced by the current raw current draw)
    // dt represents the time elapsed since the last EMA update.
    // So if updates are infrequent (large dt), EMA current should respond more quickly to the current raw current draw.
    // If updates are frequent (small dt), EMA current should respond more slowly to the current raw current draw.
  }

  return ema;
}

void formatHoursToHM(float hours, char *out, size_t n)
{
  if (!(hours > 0.0f))
  {
    snprintf(out, n, "—");
    return;
  }
  unsigned long totalMin = (unsigned long)roundf(hours * 60.0f);
  unsigned int h = totalMin / 60;
  unsigned int m = totalMin % 60;
  if (h >= 100)
    snprintf(out, n, ">99h"); // clamp silly long values
  else
    snprintf(out, n, "%uh %02um", h, m);
}

// Returns hours to full
// Assumes positive current when charging
float computeTTF_Hours(float res_mAh, float soc_pct, float I_A)
{
  if (soc_pct >= 100.0f || I_A <= I_MIN_ABS_A)
    return NAN;
  if (soc_pct <= 0.5f)
    return NAN; // avoid divide-by-small; at 0% use TTE instead

  // Estimate nominal capacity from residual capacity and SOC: Nominal capacity ≈ residual capacity / (SOC/100).
  // Then mAh required to full = Nominal capacity - Residual capacity.
  float C_nom_mAh = res_mAh * (100.0f / soc_pct);
  float toFull_mAh = max(0.0f, C_nom_mAh - res_mAh);

  return (toFull_mAh / 1000.0f) / I_A; // hours
}

// Returns hours to empty
// Assumes negative current when discharging
float computeTTE_Hours(float res_mAh, float I_A)
{
  float I_dis_A = -I_A; // make positive for math
  if (I_dis_A <= DISCH_MIN_ABS_A)
    return NAN;
  return (res_mAh / 1000.0f) / I_dis_A; // hours
}

// --- median-of-5 helper for robust current ---
static inline float median5_ring(float x)
{
  static float ring[5] = {0, 0, 0, 0, 0};
  static int idx = 0, count = 0;
  ring[idx] = x;
  idx = (idx + 1) % 5;
  if (count < 5)
    count++;
  float tmp[5];
  for (int i = 0; i < count; i++)
    tmp[i] = ring[i];
  insertionSort(tmp, count);
  return tmp[count / 2]; // median for 1..5 samples
}

// --- stable "discharging" state with hysteresis + dwell ---
static bool g_dischargingStable = false;
static uint32_t g_stateSinceMs = 0;
// ---- TTE stabilization tunables ----
constexpr float I_DISCH_ENTER_A = -DISCH_MIN_ABS_A; // must be <= -DISCH_MIN_ABS_A to enter
constexpr float I_DISCH_EXIT_A = -DISCH_MIN_ABS_A;  // must be >= DISCH_MIN_ABS_A to exit
constexpr uint32_t STATE_DWELL_MS = 2000;
constexpr uint8_t TTE_QUANT_MIN = 10; // quantize TTE to 10-minute steps
// Asymmetric smoothing: fast drop, slower rise
constexpr float TTE_TAU_ATTACK_S = 5.0f;   // when TTE is decreasing
constexpr float TTE_TAU_RELEASE_S = 18.0f; // when TTE is increasing / relaxing

static inline void updateDischargeState(float I_robust)
{
  const uint32_t now = millis();
  bool wantDisch = (I_robust <= I_DISCH_ENTER_A);
  bool wantIdle = (I_robust >= I_DISCH_EXIT_A);

  if (!g_dischargingStable) // If g_dischargingStable == false
  {
    if (wantDisch)
    {
      if (g_stateSinceMs == 0)
        g_stateSinceMs = now;
      if (now - g_stateSinceMs >= STATE_DWELL_MS)
      {
        g_dischargingStable = true; // Have to make sure the current is persistently <= I_DISCH_ENTER_A for at least STATE_DWELL_MS before entering "discharging" state
        g_stateSinceMs = now;
      }
    }
    else
    {
      g_stateSinceMs = 0; // reset dwell timer
    }
  }
  else
  {
    if (wantIdle)
    {
      if (g_stateSinceMs == 0)
        g_stateSinceMs = now;
      if (now - g_stateSinceMs >= STATE_DWELL_MS)
      {
        g_dischargingStable = false; // Have to make sure the current is persistently >= I_DISCH_EXIT_A for at least STATE_DWELL_MS before exiting "discharging" state
        g_stateSinceMs = now;
      }
    }
    else
    {
      g_stateSinceMs = 0;
    }
  }
}

// ---- Robust CHARGING detection (reject regen spikes) ----
static bool g_chargingStable = false;
static uint32_t g_chgSinceMs = 0;
// Enter/exit thresholds (hysteresis)
constexpr float I_CHG_ENTER_A = +1.2f; // must be >= +1.2 A to call it "charging"
constexpr float I_CHG_EXIT_A = +0.4f;  // must fall to <= +0.4 A to exit charging
// How long the condition must persist to flip state
constexpr uint32_t CHG_DWELL_MS = 2000; // 1.2 s works well for brief brake blips
// Treat tiny +ve blips as zero (ignore back-EMF trickle)
constexpr float POSITIVE_CLAMP_A = +0.5f; // +0..+2 A -> 0 A for state logic
// ---- TTF stabilization (10-minute steps, asymmetric smoothing) ----
constexpr uint8_t TTF_QUANT_MIN = 10;      // quantize TTF to 10-minute steps
constexpr float TTF_TAU_ATTACK_S = 9.0f;   // when TTF is decreasing
constexpr float TTF_TAU_RELEASE_S = 36.0f; // when TTF increases

static inline void updateChargingState(float I_robust)
{
  // dwell/hysteresis logic
  const uint32_t now = millis();
  // Decide what the input "wants" to be
  const bool wantChg = (I_robust >= I_CHG_ENTER_A);
  const bool wantIdle = (I_robust <= I_CHG_EXIT_A);

  if (!g_chargingStable)
  {
    if (wantChg)
    {
      if (g_chgSinceMs == 0)
        g_chgSinceMs = now;
      if (now - g_chgSinceMs >= CHG_DWELL_MS)
      {
        g_chargingStable = true;
        g_chgSinceMs = now;
      }
    }
    else
    {
      g_chgSinceMs = 0;
    }
  }
  else
  {
    if (wantIdle)
    {
      if (g_chgSinceMs == 0)
        g_chgSinceMs = now;
      if (now - g_chgSinceMs >= CHG_DWELL_MS)
      {
        g_chargingStable = false;
        g_chgSinceMs = now;
      }
    }
    else
    {
      g_chgSinceMs = 0;
    }
  }
}

// TTE EMA + quantization with edge-aware seeding and asymmetric attack/release.
// Pass 'force_seed=true' on the instant we ENTER discharging so it “teleports”
// to the instantaneous TTE instead of gliding from a stale value.
static inline float smoothQuantizedTTE(float tte_raw_h, bool force_seed = false)
{
  static bool init = false;
  static float tte_disp_h = NAN;
  static uint32_t lastMs = 0;

  const uint32_t now = millis();
  const float dt = (lastMs == 0) ? (BMS_QUERY_MS / 1000.0f)
                                 : (now - lastMs) / 1000.0f;
  lastMs = now;

  const bool raw_ok = (tte_raw_h == tte_raw_h) && (tte_raw_h > 0.0f);

  // Seed hard on first use or explicit entry edge
  if ((!init || force_seed) && raw_ok)
  {
    tte_disp_h = tte_raw_h;
    init = true;
  }
  else if (init && raw_ok)
  {
    // Asymmetric smoothing: faster when TTE should drop, slower when it should rise
    const bool dropping = (tte_raw_h < tte_disp_h);
    const float tau = dropping ? TTE_TAU_ATTACK_S : TTE_TAU_RELEASE_S;
    const float alpha = 1.0f - expf(-dt / tau);
    tte_disp_h += alpha * (tte_raw_h - tte_disp_h);
  }
  if (!init || !(tte_disp_h == tte_disp_h) || tte_disp_h <= 0.0f)
    return NAN;

  // Quantize to calm the UI
  float minutes = tte_disp_h * 60.0f;
  minutes = roundf(minutes / TTE_QUANT_MIN) * TTE_QUANT_MIN;
  return minutes / 60.0f;
}

// TTF EMA + 10-min quantization with edge-aware seeding and asymmetric attack/release.
// Pass 'force_seed=true' on the instant we ENTER stable charging so it “teleports”
// to the instantaneous TTF instead of gliding from a stale value.
static inline float smoothQuantizedTTF(float ttf_raw_h, bool force_seed = false)
{
  static bool init = false;
  static float ttf_disp_h = NAN;
  static uint32_t lastMs = 0;

  const uint32_t now = millis();
  const float dt = (lastMs == 0) ? (BMS_QUERY_MS / 1000.0f)
                                 : (now - lastMs) / 1000.0f;
  lastMs = now;

  const bool raw_ok = (ttf_raw_h == ttf_raw_h) && (ttf_raw_h > 0.0f);

  // Seed hard on first use or explicit entry edge
  if ((!init || force_seed) && raw_ok)
  {
    ttf_disp_h = ttf_raw_h;
    init = true;
  }
  else if (init && raw_ok)
  {
    // Faster when TTF should DROP (charging effective), slower when it rises
    const bool dropping = (ttf_raw_h < ttf_disp_h);
    const float tau = dropping ? TTF_TAU_ATTACK_S : TTF_TAU_RELEASE_S;
    const float alpha = 1.0f - expf(-dt / tau);
    ttf_disp_h += alpha * (ttf_raw_h - ttf_disp_h);
  }

  if (!init || !(ttf_disp_h == ttf_disp_h) || ttf_disp_h <= 0.0f)
    return NAN;

  // Quantize to nearest 10 minutes
  float minutes = ttf_disp_h * 60.0f;
  minutes = roundf(minutes / TTF_QUANT_MIN) * TTF_QUANT_MIN;
  return minutes / 60.0f;
}

// Draws the correct top strip text
// Only redraws when content changes to prevent flicker.
void updateTopStripWithTTF_TTE()
{
  // Decide what we want to show
  char line[48] = "";
  bool wantSomething = false;

  // Priority of messages:
  // 1) If low (<=20%) -> keep "BATTERY LOW PLEASE CHARGE" (no TTE).
  // 2) If charging and <100% -> "BATTERY CHARGING • TTF Xh Ym".
  // 3) Else if discharging and >20% -> "TTE Xh Ym".
  // 4) Else show current_status_msg (may be empty).

  float I_raw = input_I;             // raw current value
  float I_med = median5_ring(I_raw); // median of 5 raw current values

  // Edge detection: rising edge into stable discharging
  static bool prevStable = false;
  bool enteringDischarge = (g_dischargingStable && !prevStable);
  prevStable = g_dischargingStable;

  // Edge detection: rising edge into stable charging
  static bool prevChargeStable = false;
  bool enteringCharge = (g_chargingStable && !prevChargeStable);
  prevChargeStable = g_chargingStable;

  if (I_med > 0.0f && I_med < POSITIVE_CLAMP_A) // Clamp brief positive blips/back-EMF to zero for TTE purposes
    I_med = 0.0f;                               // treat tiny +ve as 0 A

  float soc = (float)input_soc;
  float res_mAh = input_resmAh;

  updateDischargeState(I_med); // Maintain a stable "discharging" state with hysteresis + dwell

  // Learn from raw current value, don't use EMA (I_smooth), because the function already has its own EMA smoothing inside.
  //!
  tteHistUpdate(I_raw); // your fast-attack EMA (kept for now)
  //!
  histPushDischarge(I_raw); // NEW: feed historical ring buffer

  // --- Historical TTE dwell gate ---------------------------------------------
  static uint32_t histStartMs = 0;
  static bool histReady = false;
  const uint32_t nowMs = millis();

  // Base conditions (same as before, but without the dwell)
  bool histBaseCond =
      (!input_chg) &&           // not charging
      (soc > 20.0f) &&          // above low battery messaging
      (!g_dischargingStable) && // not stably discharging
      //!
      (g_tteHist.valid) &&
      (g_tteHist.emaA >= DISCH_MIN_ABS_A); // EMA magnitude strong enough
                                           //!
                                           //! (g_histCount >= HIST_MIN_SAMPLES); // enough history

  // Dwell logic
  if (histBaseCond)
  {
    if (histStartMs == 0)
      histStartMs = nowMs;
    if (!histReady && (nowMs - histStartMs >= HIST_TTE_DWELL_MS))
      histReady = true;
  }
  else
  {
    histStartMs = 0;
    histReady = false;
  }

  if (!strcmp(current_status_msg, "BATTERY LOW PLEASE CHARGE"))
  {
    snprintf(line, sizeof(line), "%s", current_status_msg);
    wantSomething = true;
  }
  else if (input_chg && soc < 100.0f && I_raw > I_MIN_ABS_A)
  {
    // TTF while charging: smooth + 10-min quantization, seed when we enter stable charging
    float I_for_ttf = filteredCurrentA(input_I);
    float ttf_h = computeTTF_Hours(res_mAh, soc, I_for_ttf);
    float ttf_disp_h = smoothQuantizedTTF(ttf_h, /*force_seed=*/enteringCharge);
    if (ttf_disp_h == ttf_disp_h)
    {
      char dur[16];
      formatHoursToHM(ttf_disp_h, dur, sizeof(dur));
      snprintf(line, sizeof(line), "BATTERY CHARGING - %s until full", dur);
      wantSomething = true;
    }
  }
  else if (!input_chg && soc > 20.0f)
  {

    if (g_dischargingStable)
    {
      // Live TTE with robust current; ignore tiny magnitudes
      float I_for_tte = (I_med <= -DISCH_MIN_ABS_A) ? I_med : NAN;
      float tte_live_h = (I_for_tte == I_for_tte) ? computeTTE_Hours(res_mAh, I_for_tte) : NAN;

      // Smooth + quantize for the display; seed on entry so we don't lag from a stale big value
      float tte_disp_h = smoothQuantizedTTE(tte_live_h, /*force_seed=*/enteringDischarge);
      if (tte_disp_h == tte_disp_h)
      {
        char dur[16];
        formatHoursToHM(tte_disp_h, dur, sizeof(dur));
        snprintf(line, sizeof(line), "Travel time left: %s", dur);
        wantSomething = true;
      }
    }
    else if (histReady)
    {
      // NEW: robust long-window typical draw
      float A_typ = histTypicalDrawA_trimmedMean();
      Serial.print("👉 Typical discharge current A (trimmed mean): ");
      Serial.println(A_typ);

      //!
      // If not enough history yet, fall back to your EMA-based estimate
      if (!(A_typ == A_typ))
        A_typ = g_tteHist.valid ? g_tteHist.emaA : NAN;
      //!

      if (A_typ == A_typ && A_typ >= DISCH_MIN_ABS_A)
      {
        float tte_h = computeTTE_Hours(res_mAh, -A_typ); // note the negative sign
        float tte_disp_h = smoothQuantizedTTE(tte_h, false);
        if (tte_disp_h == tte_disp_h)
        {
          char dur[16];
          formatHoursToHM(tte_disp_h, dur, sizeof(dur));
          snprintf(line, sizeof(line), "Travel time left (historical): %s", dur);
          wantSomething = true;
        }
      }
    }
  }
  else
  {
    if (current_status_msg && *current_status_msg)
    {
      snprintf(line, sizeof(line), "%s", current_status_msg);
      wantSomething = true;
    }
  }

  // Nothing to draw AND previously empty? bail
  if (!wantSomething && lastTopLine[0] == '\0')
    return;

  // If unchanged, do nothing
  if (wantSomething && (strcmp(line, lastTopLine) == 0))
    return;
  if (!wantSomething && lastTopLine[0] == '\0')
    return;

  // --- draw into the existing top strip area ---
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(SOME_LINE_COLOUR, BACKGROUND_COLOUR);

  const int16_t areaTop = BORDER_THICKNESS;
  const int16_t areaBottom = ICON_Y;

  // Clear the strip
  tft.fillRect(BORDER_THICKNESS + 1,
               areaTop,
               tft.width() - 2 * BORDER_THICKNESS - 1,
               areaBottom - areaTop,
               BACKGROUND_COLOUR);

  // Centered
  if (wantSomething)
  {
    tft.setTextDatum(MC_DATUM);
    tft.drawString(line, tft.width() / 2, (areaTop + areaBottom) / 2);
  }

  // Restore defaults
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(nullptr);
  tft.setTextFont(1);

  // Remember what we drew
  if (wantSomething)
  {
    strncpy(lastTopLine, line, sizeof(lastTopLine) - 1);
    lastTopLine[sizeof(lastTopLine) - 1] = '\0';
  }
  else
  {
    lastTopLine[0] = '\0';
  }
}

void drawStatus(const char *msg)
{
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(SOME_LINE_COLOUR, BACKGROUND_COLOUR);

  const int16_t areaTop = BORDER_THICKNESS;
  const int16_t areaBottom = ICON_Y;

  // Clear the strip
  tft.fillRect(BORDER_THICKNESS + 1,
               areaTop,
               tft.width() - 2 * BORDER_THICKNESS - 1,
               areaBottom - areaTop,
               BACKGROUND_COLOUR);

  // Centered draw using datum
  tft.setTextDatum(MC_DATUM);
  int16_t cx = tft.width() / 2;
  int16_t cy = (areaTop + areaBottom) / 2;
  tft.drawString(msg, cx, cy);

  // Restore defaults
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(nullptr);
  tft.setTextFont(1);
}

void updateScreenAndSpeaker()
{
  if (input_soc != previous_soc || input_chg != previous_chg)
  {
    if (lowBlinkState == true)
    {
      lowBlinkState = false;
      tft.invertDisplay(false);
    }

    // Redraw percentage only when the SoC value has changed.
    if (input_soc != previous_soc)
    {
      drawPercentage(input_soc);
    }

    // Redraw the coloured border, coloured bars, and cross only when the colour has changed (i.e., the current SoC value is within a different range from before)
    // or when the Charge Status has changed. (cross will only be drawn when the conditions within the function is fulfiled)
    if (colourForSOC(input_soc) != colourForSOC(previous_soc) || input_chg != previous_chg)
    {
      drawBorder(input_soc);
      drawBatteryIcon(input_soc, input_chg);
      drawBatteryBars(input_soc);
    }

    // Redraw the lightning bolt symbol only when it's charging and the symbol hasn't been drawn already
    if (input_chg)
    {
      if (isBatteryBarsWiped)
      {
        drawLightningBolt();
      }
    }

    if (input_soc != previous_soc || input_chg != previous_chg)
    {
      if (!strcmp(current_status_msg, "BATTERY FULL!"))
      {
        playAudio(current_status_msg);
        // Plays when charged to 100%
      }
      else if (input_chg == 0 && !strcmp(current_status_msg, "BATTERY LOW PLEASE CHARGE"))
      {
        bool onStep = (input_soc == 20) || (input_soc == 15) || (input_soc <= 10);
        if (onStep && input_soc != lastLowSOCAudioPlayed)
        {
          playAudio(current_status_msg);
          lastLowSOCAudioPlayed = input_soc;
          // "Battery Low. Please Charge" only at 20, 15, and every value 10..0, and only when NOT charging
        }
        // reset memory when we climb back out of the low zone
        if (input_soc > 20)
          lastLowSOCAudioPlayed = -1;
      }
      else if (!strcmp(current_status_msg, previous_status_msg) && !strcmp(current_status_msg, ""))
      {
        playAudio(current_status_msg);
      }
    }

    // Redraw the status message only when the message has changed.
    if (strcmp(current_status_msg, previous_status_msg) != 0)
    {
      drawStatus(current_status_msg);
      if (strcmp(current_status_msg, "BATTERY CHARGING") == 0)
      {
        playAudio(current_status_msg);
        // "Charging" is only played when the status changes to it.
      }
    }
  }

  // Always refresh the top strip with TTF/TTE logic (handles changing current)
  updateTopStripWithTTF_TTE();
}

void noDataText()
{
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1); // built-in font index
  tft.setTextSize(1); // ensure no leftover scaling
  tft.setFreeFont(nullptr);

  if (lowBlinkState)
  {
    lowBlinkState = false;
    tft.invertDisplay(false);
  }
  tft.fillScreen(BLACK);

  // Use a known font + datum
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextDatum(MC_DATUM);

  tft.setTextSize(1); // big error text is fine
  tft.setTextColor(RED, BLACK);
  tft.setCursor(30, tft.height() / 2);
  tft.print("No Data");
}

void LoadingDataText()
{
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1); // built-in font index
  tft.setTextSize(1); // ensure no leftover scaling
  tft.setFreeFont(nullptr);

  if (lowBlinkState)
  {
    lowBlinkState = false;
    tft.invertDisplay(false);
  }
  tft.fillScreen(BLACK);

  // Use a known font + datum
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextDatum(MC_DATUM);

  tft.setTextSize(1); // big error text is fine
  tft.setTextColor(RED, BLACK);
  tft.setCursor(30, tft.height() / 2);
  tft.print("Loading Data");
}

// Callback function that you want executed when data is received
// The arguments of the callback function are fixed to be this three.
void receiveCallback(const uint8_t *macAddr, const uint8_t *incomingData, int dataLen)
{
  // Only allow a maximum of 250 characters in the message
  if (dataLen > ESP_NOW_MAX_DATA_LEN)
  {
    return;
  }

  char macStr[18];
  formatMacAddress(macAddr, macStr, 18);
  // Send Debug log message to the serial port
  Serial.println("----------------------------------------------------");
  Serial.printf("Received message from: %s\n", macStr);

  memcpy(&BMSData, incomingData, sizeof(BMSData));
  Serial.print("Data received: ");
  Serial.println(dataLen);
  Serial.print("BMS Status Received: ");
  Serial.println(BMSData.bms_status);
  Serial.print("BMS SoC Received: ");
  Serial.println(BMSData.soc);
  Serial.print("BMS Current Received: ");
  Serial.println(BMSData.I);
  Serial.print("BMS Remaining Capacity (mAh) Received: ");
  Serial.println(BMSData.resmAh);
  Serial.println();

  if (BMSData.bms_status == 1 && crcFailCnt == 0)
  {
    if (crcFailed == true)
    {
      crcFailed = false;
      tft.fillScreen(BLACK);
    }

    input_soc = constrain((int)ceilf(BMSData.soc), 0, 100);

    // Build a robust current for state logic (median + clamp small +ve to 0)
    float I_med_all = median5_ring(BMSData.I); // you already have median5_ring(...)
    float I_chg_robust = I_med_all;
    if (I_chg_robust > 0.0f && I_chg_robust < POSITIVE_CLAMP_A)
      I_chg_robust = 0.0f; // ignore tiny +ve blips (regen/back-EMF trickle)

    // Update stable charging state and derive input_chg from it
    updateChargingState(I_chg_robust);
    input_chg = g_chargingStable ? 1 : 0;

    statusMessage(input_soc, input_chg); // Updates current_status_message
    input_I = BMSData.I;                 // +A charge, -A discharge
    input_resmAh = max(0.0f, BMSData.resmAh);

    Serial.print("Current SoC: ");
    Serial.println(input_soc);
    Serial.print("Current Charge Status: ");
    Serial.println(input_chg);
    Serial.print("Current Status Message: ");
    Serial.println(current_status_msg);
    Serial.print("Current: ");
    Serial.println(input_I);
    Serial.print("Remaining Capacity (mAh): ");
    Serial.println(input_resmAh);

    Serial.print("Previous SoC: ");
    Serial.println(previous_soc);
    Serial.print("Previous Charge Status: ");
    Serial.println(previous_chg);
    Serial.print("Previous Status Message: ");
    Serial.println(previous_status_msg);

    //!
    debugPrintTTEHist();
    //!

    updateScreenAndSpeaker();

    // Update previous values
    previous_soc = input_soc;
    previous_chg = input_chg;
    previous_status_msg = current_status_msg;
  }
  else if (BMSData.bms_status == 1 && crcFailCnt > 0)
  {
    crcFailCnt--;
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.print("CRC failure count: ");
    Serial.println(crcFailCnt);
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    LoadingDataText();
  }
  else if (BMSData.bms_status == 0)
  {
    crcFailCnt++;
    Serial.println();
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.print("CRC failure count: ");
    Serial.println(crcFailCnt);
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println();
    if (crcFailCnt > 5)
    {
      noDataText();
      crcFailCnt = 6;
      crcFailed = true;
    }
  }
}

// ==== BUTTON ISR: actual hardware ISR (do almost nothing here)
void IRAM_ATTR onButtonISR()
{
  uint32_t now = micros();
  // simple debounce in ISR; ignore events within 500 ms
  if (now - g_lastButtonIsrUs >= BUTTON_DEBOUNCE_US)
  {
    g_lastButtonIsrUs = now;
    g_buttonEvent = true; // signal main loop
  }
}

// ==== BUTTON ISR: helper to toggle MOSFET (called from loop, not from ISR)
void handleButtonToggle()
{
  // Toggle MOSFET gate output
  int current = digitalRead(MOSFET_GATE);
  int next = (current == HIGH) ? LOW : HIGH;
  digitalWrite(MOSFET_GATE, next);

  // Toggle audio module
  audio_module_on = (audio_module_on == true) ? false : true;
  if (audio_module_on == false)
  {
    digitalWrite(DYSV8F_IO0, HIGH);
    digitalWrite(DYSV8F_IO1, HIGH);
    digitalWrite(DYSV8F_IO2, LOW);
  }

  if (next == HIGH)
  {
    Serial.println("ON");
  }
  else
  {
    Serial.println("OFF");
  }
  Serial.println("MOSFET_GATE state:");
  Serial.println(digitalRead(MOSFET_GATE));
}

// SETUP & LOOP
void setup()
{
  Serial.begin(115200);
  delay(50);

  pinMode(BUTTON, INPUT_PULLDOWN);
  pinMode(MOSFET_GATE, OUTPUT);
  digitalWrite(MOSFET_GATE, HIGH); // Switch on load initially

  // ==== BUTTON ISR: attach interrupt on rising edge (matches INPUT_PULLDOWN)
  attachInterrupt(digitalPinToInterrupt(BUTTON), onButtonISR, RISING);

  Serial.println("TFT LCD Charge Indicator ready");

  tft.init();
  tft.setRotation(ROTATION);
  tft.fillScreen(BACKGROUND_COLOUR);

  pinMode(DYSV8F_IO0, OUTPUT);
  pinMode(DYSV8F_IO1, OUTPUT);
  pinMode(DYSV8F_IO2, OUTPUT);
  digitalWrite(DYSV8F_IO0, HIGH);
  digitalWrite(DYSV8F_IO1, HIGH);
  digitalWrite(DYSV8F_IO2, HIGH);

#define LCD_SELFTEST 1 // Note - Sanity check
#ifdef LCD_SELFTEST
  tft.fillScreen(RED);
  delay(300);
  tft.fillScreen(GREEN);
  delay(300);
  tft.fillScreen(BLUE);
  delay(300);
  tft.fillScreen(BLACK);
#endif

  // Set ESP32 as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Print own's MAC address
  Serial.print("My MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Disconnect from WiFi
  WiFi.disconnect();

  // Initialize ESP-NOW
  if (esp_now_init() == ESP_OK)
  {
    Serial.println("ESP-NOW Init Success");
    esp_now_register_recv_cb(receiveCallback);
  }
  else
  {
    Serial.println("ESP-NOW Init Failed");
    delay(3000);
    ESP.restart();
  }
}

void loop()
{
  delay(10);

  // ==== BUTTON ISR: consume edge events without blocking
  if (g_buttonEvent)
  {
    // clear atomically
    portENTER_CRITICAL(&g_isrMux);
    bool evt = g_buttonEvent;
    g_buttonEvent = false;
    portEXIT_CRITICAL(&g_isrMux);

    if (evt)
    {
      handleButtonToggle(); // no blocking; no wait-for-release loops
    }
  }

  // Blinking behaviour for low battery (< = 20) and not charging
  uint32_t now = millis();
  if (!input_chg && input_soc <= 20 && now - lastBlinkMs >= 1000 && crcFailed == false)
  {
    tft.invertDisplay(!lowBlinkState);
    lowBlinkState = !lowBlinkState;
    lastBlinkMs = now;
  }

  // // Note - Sanity Check
  // tft.fillScreen(RED);
  // delay(300);
  // tft.fillScreen(GREEN);
  // delay(300);
  // tft.fillScreen(BLUE);
  // delay(300);
  // tft.fillScreen(BLACK);
}
