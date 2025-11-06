// Waveshare TFT LCD 3.5" 480x320 65k colours, Interface: SPI, Controller: ILI9486
// https://shopee.sg/Touch-LCD-Shield-3.5-inch-for-Arduino-i.440521573.17478975100?sp_atk=eee62442-3704-4ede-883e-773af8ec4ae0&xptdk=eee62442-3704-4ede-883e-773af8ec4ae0
// http://www.waveshare.com/wiki/3.5inch_TFT_Touch_Shield
// https://forum.arduino.cc/t/solved-waveshare-3-5inch-touch-lcd-shield-w-esp32/690369

/* Pin connections to ESP32 with notes
5V → 5V
GND → GND
SCLK (D13): SPI Clock → 18
MISO (D12) SPI Data Input → 19 // If panel is write-only (display only, no touchscreen), no need to connect
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
#include <daly-bms-uart.h>
extern const GFXfont FreeSansBold12pt7b;
extern const GFXfont FreeSansBold24pt7b;

#define BMS_SERIAL Serial1
Daly_BMS_UART bms(BMS_SERIAL);

// Assign human-readable names to some common 16-bit RGB565 colour values:
#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
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
constexpr uint8_t PERCENTAGE_SIZE = 7;
constexpr uint8_t CROSS_THICKNESS = 8; // must be ≥1
constexpr int16_t ICON_X = 35;         // x-coordinate of top-left corner of battery corner
constexpr int16_t ICON_Y = 70;         // y-coordinate of top-left corner of battery corner
constexpr int16_t ICON_W = 400;        // battery icon width
constexpr int16_t ICON_H = 150;        // battery icon height
constexpr int16_t TERM_W = 12;         // positive terminal width
constexpr int16_t DYSV8F_IO0 = 25;
constexpr int16_t DYSV8F_IO1 = 26;
constexpr int16_t DYSV8F_IO2 = 27;
constexpr uint32_t BMS_QUERY_MS = 250; // poll Daly BMS no faster than 4 Hz

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
bool isLightningBoltWiped = false;
const char *current_status_msg = "";
const char *previous_status_msg = "";
uint32_t lastBMSQuery = 0;         // Timestamp of last BMS query
uint8_t crcFailCnt = 0;            // consecutive CRC error count
int8_t lastLowSOCAudioPlayed = -1; // Remember last SoC that triggered "Battery Low. Please Charge" audio
// --- TTF/TTE helpers ---
char lastTopLine[48] = "";         // what we last drew in the top strip (for flicker-free updates)
constexpr float I_MIN_ABS_A = 0.3; // ignore near-zero currents (A) to avoid silly times
// NOTE - Auto test globals
int test_case = 1; // 1 is discharge, 2 is charge
uint32_t AUTO_TEST_MS = 1000;
uint32_t lastAutoTestms = 0;

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
    isLightningBoltWiped = true;

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

void drawLightningBolt()
{
    const uint16_t FILL_COLOUR = YELLOW;   // bright yellow RGB565
    const uint16_t OUTLINE_COLOUR = BLACK; // black

    // Centre reference of the battery icon
    const int16_t mx = ICON_X + ICON_W / 2;
    const int16_t my = ICON_Y + ICON_H / 2;

    int16_t Ax = mx - 140, Ay = my - 60;
    int16_t Bx = mx - 140, By = my + 30;
    int16_t Cx = mx + 20, Cy = my + 50;
    int16_t Dx = mx + 20, Dy = my + 20;
    int16_t Ex = mx + 140, Ey = my + 50;
    int16_t Fx = mx - 20, Fy = my - 60;
    int16_t Gx = mx - 20, Gy = my - 10;

    tft.fillTriangle(Ax, Ay, Bx, By, Cx, Cy, FILL_COLOUR);
    tft.fillTriangle(Gx, Gy, Dx, Dy, Cx, Cy, FILL_COLOUR);
    tft.fillTriangle(Gx, Gy, Dx, Dy, Fx, Fy, FILL_COLOUR);
    tft.fillTriangle(Ax, Ay, Gx, Gy, Cx, Cy, FILL_COLOUR);
    tft.fillTriangle(Dx, Dy, Ex, Ey, Fx, Fy, FILL_COLOUR);

    tft.drawLine(Ax, Ay, Bx, By, OUTLINE_COLOUR);
    tft.drawLine(Bx, By, Cx, Cy, OUTLINE_COLOUR);
    tft.drawLine(Cx, Cy, Dx, Dy, OUTLINE_COLOUR);
    tft.drawLine(Dx, Dy, Ex, Ey, OUTLINE_COLOUR);
    tft.drawLine(Ex, Ey, Fx, Fy, OUTLINE_COLOUR);
    tft.drawLine(Fx, Fy, Gx, Gy, OUTLINE_COLOUR);
    tft.drawLine(Gx, Gy, Ax, Ay, OUTLINE_COLOUR);
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
            tft.drawRect(barX, BAR_Y, BAR_W - 6, BAR_H, 0x7BEF); // grey hollow
    }
}

void drawPercentage(int soc)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%d%%", soc); // one string so everything matches

    // Use GLCD Font 1 (has '%') and scale it up
    const uint8_t SIZE = 3; // try 5–8 to taste
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
    digitalWrite(DYSV8F_IO0, HIGH);
    digitalWrite(DYSV8F_IO1, HIGH);
    digitalWrite(DYSV8F_IO2, HIGH);

    if (status_message == "BATTERY CHARGING")
    {
        digitalWrite(DYSV8F_IO0, HIGH); // Plays "Charging"
        digitalWrite(DYSV8F_IO1, LOW);
        digitalWrite(DYSV8F_IO2, HIGH);
    }
    else if (status_message == "BATTERY FULL!")
    {
        digitalWrite(DYSV8F_IO0, LOW); // Plays music and "Battery Full. Charging Complete"
        digitalWrite(DYSV8F_IO1, LOW);
        digitalWrite(DYSV8F_IO2, HIGH);
    }
    else if (status_message == "BATTERY LOW PLEASE CHARGE")
    {
        digitalWrite(DYSV8F_IO0, LOW); // Plays "Battery Low. Please Charge"
        digitalWrite(DYSV8F_IO1, HIGH);
        digitalWrite(DYSV8F_IO2, HIGH);
    }
    else if (status_message == "")
    {
        digitalWrite(DYSV8F_IO0, HIGH); // Plays nothing (there's not actually a fourth mp3 file)
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
    // Note that these are static variables, so they are only initialised once and retain their values between calls.

    uint32_t now = millis();
    float dt = (lastMs == 0) ? (BMS_QUERY_MS / 1000.0f) : (now - lastMs) / 1000.0f;
    // If lastMs is zero (first call), assume dt = BMS_QUERY_MS (250ms), else dt = now - lastMs
    lastMs = now;

    // time constant tau ~ 3 s
    const float tau = 3.0f;
    float alpha = 1.0f - expf(-dt / tau);

    if (!init) // Will only be true on first call
    {
        ema = input_I;
        init = true;
    }
    else
    {
        ema += alpha * (input_I - ema);
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

// Returns TTF using residual capacity + SOC; assumes constant positive current
// res_mAh: remaining capacity in mAh, soc_pct: 0..100, I_A: +A while charging
float computeTTF_Hours(float res_mAh, float soc_pct, float I_A)
{
    if (soc_pct >= 100.0f || I_A <= I_MIN_ABS_A)
        return NAN;
    if (soc_pct <= 0.5f)
        return NAN; // avoid divide-by-small; at 0% use TTE instead

    // Estimate nominal capacity from residual and SOC
    // Nominal capacity ≈ residual capacity / (SOC/100). Then mAh required to full = Nominal capacity - Residual capacity.
    float C_nom_mAh = res_mAh * (100.0f / soc_pct);
    float toFull_mAh = max(0.0f, C_nom_mAh - res_mAh);

    return (toFull_mAh / 1000.0f) / I_A; // hours
}

// Returns hours to empty using residual capacity; assumes constant negative current
float computeTTE_Hours(float res_mAh, float I_A)
{
    float I_dis_A = -I_A; // make positive for math
    if (I_dis_A <= I_MIN_ABS_A)
        return NAN;
    return (res_mAh / 1000.0f) / I_dis_A; // hours
}

// Draws the correct top strip text (BATTERY CHARGING • TTF …, LOW message, TTE …, or blank)
// Only redraws when content changes to prevent flicker.
void updateTopStripWithTTF_TTE()
{
    // Decide what we want to show
    char line[48] = "";
    bool wantSomething = false;

    // Priority of messages:
    // 1) If low (<=20%) -> keep your "BATTERY LOW PLEASE CHARGE" (no TTE).
    // 2) If charging and <100% -> "BATTERY CHARGING • TTF Xh Ym".
    // 3) Else if discharging and >20% -> "TTE Xh Ym".
    // 4) Else show current_status_msg (may be empty).
    float I = filteredCurrentA(input_I);
    float soc = (float)input_soc;
    float res_mAh = input_resmAh;

    if (!strcmp(current_status_msg, "BATTERY LOW PLEASE CHARGE"))
    {
        snprintf(line, sizeof(line), "%s", current_status_msg);
        wantSomething = true;
    }
    else if (input_chg && soc < 100.0f && I > I_MIN_ABS_A)
    {
        float ttf_h = computeTTF_Hours(res_mAh, soc, I);
        char dur[16];
        formatHoursToHM(ttf_h, dur, sizeof(dur));
        snprintf(line, sizeof(line), "BATTERY CHARGING - %s until full", dur);
        wantSomething = true;
    }
    else if (!input_chg && soc > 20.0f && I < -I_MIN_ABS_A)
    {
        float tte_h = computeTTE_Hours(res_mAh, I);
        char dur[16];
        formatHoursToHM(tte_h, dur, sizeof(dur));
        snprintf(line, sizeof(line), "Travel time left: %s", dur);
        wantSomething = true;
    }
    else
    {
        // fall back to whatever status you already set (e.g., BATTERY FULL!, or blank)
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
    const uint16_t h = tft.fontHeight();

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
            if (current_status_msg == "BATTERY CHARGING")
            {
                playAudio(current_status_msg);
                // "Charging" is only played when the status changes to it.
            }
        }
    }

    // Always refresh the top strip with TTF/TTE logic (handles changing current)
    updateTopStripWithTTF_TTE();
}

void splashAndHandshake()
{
    // --- normalize text state in case previous screens changed it ---
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

    const uint32_t TIMEOUT_MS = 10000;
    uint8_t goodFrames = 0, dot = 0;

    // Use a known font + datum for LOADING
    tft.setFreeFont(&FreeSansBold24pt7b);
    tft.setTextColor(SOME_LINE_COLOUR, BACKGROUND_COLOUR);
    tft.setTextDatum(MC_DATUM);

    // Precompute clear area for the loading text
    uint16_t h = tft.fontHeight();
    int16_t clearY = ICON_Y + ICON_H / 2 - h / 2 - 2;
    int16_t clearH = h + 4;

    uint32_t start = millis(), lastSpin = 0;
    while ((millis() - start) < TIMEOUT_MS && goodFrames < 5)
    {
        if (millis() - lastBMSQuery >= BMS_QUERY_MS)
        {
            bool ok = bms.update();
            lastBMSQuery = millis();
            goodFrames = ok ? (goodFrames + 1) : 0;
        }

        if (millis() - lastSpin >= 300)
        {
            char msg[14]; // "LOADING..." + NUL
            strcpy(msg, "LOADING");
            for (uint8_t i = 0; i <= dot; i++)
                msg[7 + i] = '.';
            msg[8 + dot] = '\0';

            tft.fillRect(ICON_X + 2, clearY, ICON_W - 4, clearH, BACKGROUND_COLOUR);
            tft.drawString(msg, ICON_X + ICON_W / 2, clearY + clearH / 2);

            dot = (dot + 1) % 3;
            lastSpin = millis();
        }
        delay(2);
    }

    // Restore defaults for the rest of the UI
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(nullptr);
    tft.setTextFont(1);
    tft.setTextSize(1);

    if (goodFrames >= 5)
    {
        tft.fillScreen(BLACK);
        previous_soc = -1;
        previous_chg = -1;
        previous_status_msg = "";
    }
    else
    {
        tft.fillScreen(BLACK);
        tft.setTextSize(4); // big error text is fine
        tft.setTextColor(RED, BLACK);
        tft.setCursor(30, tft.height() / 2);
        tft.print("BMS Error");
        delay(1000);
        tft.setCursor(30, tft.height() / 1.5);
        tft.print("RETRYING");
        delay(3000);
        tft.setTextSize(1); // <-- reset so next run stays normal
        tft.fillScreen(BLACK);
        splashAndHandshake(); // try again
    }
}

void queryBMS()
{
    if (millis() - lastBMSQuery >= BMS_QUERY_MS) // Query the BMS for new data every 250ms
    {
        bool ok = bms.update();
        // This .update() call populates the entire get struct in the Daly_BMS_UART class. If you only need certain values (like
        // SOC & Voltage) you could use other public APIs, like getPackMeasurements(), which only query specific values from the BMS instead of all.
        // The method returns true if the entire get struct is successfully populated.
        lastBMSQuery = millis();
        if (ok)
        {
            crcFailCnt = 0;
        }
        else
        {
            crcFailCnt++;
            if (crcFailCnt > 10)
            {
                // NOTE - Serial Monitor Print
                // Serial.println(F("UART desync – re‑initialising Serial1"));
                Serial1.flush();
                Serial1.end();
                delay(20);
                Serial1.begin(9600, SERIAL_8N1, 21, 22);
                splashAndHandshake(); // Return to the splash and handshake process
                crcFailCnt = 0;
            }
            queryBMS(); // Recursion
        }
    }
}

// SETUP & LOOP
void setup()
{
    // NOTE - Serial Monitor Print
    Serial.begin(9600);
    Serial.print("\nTFT LCD Battery Indicator ready.\n");

    bms.Init();
    Serial1.begin(9600, SERIAL_8N1, 21, 22); // Map UART1's RX to GPIO21 and the TX to GPIO22

    tft.init();
    tft.setRotation(ROTATION);
    tft.fillScreen(BACKGROUND_COLOUR);

    pinMode(DYSV8F_IO0, OUTPUT);
    pinMode(DYSV8F_IO1, OUTPUT);
    pinMode(DYSV8F_IO2, OUTPUT);
    digitalWrite(DYSV8F_IO0, HIGH);
    digitalWrite(DYSV8F_IO1, HIGH);
    digitalWrite(DYSV8F_IO2, HIGH);

    // Note - Sanity Check
    tft.fillScreen(RED);
    delay(300);
    tft.fillScreen(GREEN);
    delay(300);
    tft.fillScreen(BLUE);
    delay(300);
    tft.fillScreen(BLACK);

    splashAndHandshake();

    // NOTE - Auto test
    input_soc = 101;
    input_chg = 0;
    statusMessage(input_soc, input_chg);
    test_case = 1;
}

void loop()
{
    delay(100);

    queryBMS();

    // NOTE - Real readings
    // input_soc = (int)ceilf(bms.get.packSOC); // ceiling to nearest upper int
    // input_chg = (bms.get.packCurrent > 0.5) ? 1 : 0;
    // // +ve charging, -ve discharging
    // // Charging: +0-2A
    // // OFF: ±0.0003A
    // // Discharging: IDLE -0.037 to -0.04A, Reverse & Forward: -2 to -21A
    // // However, due to the BMS low current reading resolution, it is more reliable to put > +0.5A as charging, and anything smaller as not charging.
    // statusMessage(input_soc, input_chg); // Updates current_status_message
    // input_I = bms.get.packCurrent;       // +A charge, -A discharge
    // input_resmAh = bms.get.resCapacitymAh;

    // NOTE - Manual Test
    // input_soc = 100;
    // input_chg = 1;
    // statusMessage(input_soc, input_chg);
    // input_I = 10;
    // input_resmAh = 11000;

    // NOTE - Auto Test
    if (millis() - lastAutoTestms >= AUTO_TEST_MS)
    {
        if ((previous_soc <= 21 && test_case == 1) || (previous_soc >= 94 && test_case == 2))
        {
            AUTO_TEST_MS = 4000;
        }
        else
        {
            AUTO_TEST_MS = 500;
        }
        if (previous_soc == 0)
        {
            test_case = 2; // Charge test
        }
        if (previous_soc == 100)
        {
            test_case = 1; // Discharge test
        }

        if (test_case == 1) // Discharge test
        {
            input_soc--;
            input_chg = 0;
            statusMessage(input_soc, input_chg);
            input_I = -10; // -10A discharge
            input_resmAh = 11000 * input_soc / 100;
        }
        else if (test_case == 2) // Charge test
        {
            input_soc++;
            input_chg = 1;
            statusMessage(input_soc, input_chg);
            input_I = 10; // +10A charge
            input_resmAh = 11000 * input_soc / 100;
        }
        lastAutoTestms = millis();
    }

    // NOTE - Serial Monitor Print
    Serial.print("Current SoC: ");
    Serial.println(input_soc);
    Serial.print("Current Charge Status: ");
    Serial.println(input_chg);
    Serial.print("Current Status Message: ");
    Serial.println(current_status_msg);
    Serial.print("Previous SoC: ");
    Serial.println(previous_soc);
    Serial.print("Previous Charge Status: ");
    Serial.println(previous_chg);
    Serial.print("Previous Status Message: ");
    Serial.println(previous_status_msg);

    Serial.print("Current: ");
    Serial.println(input_I);
    Serial.print("Remaining Capacity (mAh): ");
    Serial.println(input_resmAh);

    updateScreenAndSpeaker();

    // Update previous values
    previous_soc = input_soc;
    previous_chg = input_chg;
    previous_status_msg = current_status_msg;

    // Blinking behaviour for low battery (< = 20) and not charging
    uint32_t now = millis();
    if (!input_chg && input_soc <= 20 && now - lastBlinkMs >= 1000)
    {
        tft.invertDisplay(!lowBlinkState);
        lowBlinkState = !lowBlinkState;
        lastBlinkMs = now;
    }
}
