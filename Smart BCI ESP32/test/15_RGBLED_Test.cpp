#include <Arduino.h>
#include <math.h>
#include <daly-bms-uart.h>

#define BMS_SERIAL Serial1
Daly_BMS_UART bms(BMS_SERIAL);

constexpr uint32_t BMS_QUERY_MS = 250; // poll Daly BMS no faster than 4 Hz

// GLOBALS
int previous_soc = -1;
int input_soc = -1;        // to store user input
int previous_chg = -1;     // 0 = not charging, 1 = charging
int input_chg = -1;        // to store user input
float input_I = -1.0;      // Current in Amperes, +ve charge, -ve discharge
float input_resmAh = -1.0; // Remaining capacity in mAh
const char *current_status_msg = "";
const char *previous_status_msg = "";
uint32_t lastBMSQuery = 0; // Timestamp of last BMS query
uint8_t crcFailCnt = 0;    // consecutive CRC error count
// NOTE - Auto test globals
int test_case = 1; // 1 is discharge, 2 is charge
uint32_t AUTO_TEST_MS = 2000;
uint32_t lastAutoTestms = 0;

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

// ===== RGB LED (COMMON CATHODE) on GPIOs 19 (R), 18 (G), 5 (B) =====
#define LED_R_PIN 19
#define LED_G_PIN 18
#define LED_B_PIN 5

// ESP32 LEDC PWM (8-bit 0..255)
#define LEDC_FREQ_HZ 5000 // 5 kHz PWM frequency
#define LEDC_RES_BITS 8
#define LEDC_MAX ((1 << LEDC_RES_BITS) - 1)
#define LEDC_CH_R 0
#define LEDC_CH_G 1
#define LEDC_CH_B 2

// --- Patterns ---
// Rule 1: "three quick successions" red, repeating
const uint16_t LOW_TRIPLE_MS[] = {120, 120, 120, 120, 120, 120, 1000};           // on,off x3 + gap
const uint8_t LOW_TRIPLE_LEN = sizeof(LOW_TRIPLE_MS) / sizeof(LOW_TRIPLE_MS[0]); // number of elements in the LOW_TRIPLE_MS array

// Rule 2: normal flash rate while charging
const uint16_t CHG_ON_MS = 1000;
const uint16_t CHG_OFF_MS = 1000;

enum LedMode
{
    LED_OFF_MODE,
    LED_FLASH3_RED_MODE,
    LED_FLASH_CHG_MODE,
    LED_SOLID_FULL_MODE
};

LedMode g_ledMode = LED_OFF_MODE;
LedMode g_prevLedMode = LED_OFF_MODE;

// State for triple-burst
uint8_t g_seqIndex = 0;
uint32_t g_seqStampMs = 0;

// State for normal flash
bool g_chgOnPhase = false;
uint32_t g_chgStampMs = 0;

// --- helpers for PWM (common cathode = "direct" 0..255) ---
inline void ledcWrite255(uint8_t ch, uint8_t v) { ledcWrite(ch, v); }
inline void ledSetRGB255(uint8_t r, uint8_t g, uint8_t b)
{
    ledcWrite255(LEDC_CH_R, r);
    ledcWrite255(LEDC_CH_G, g);
    ledcWrite255(LEDC_CH_B, b);
}
inline void ledAllOff() { ledSetRGB255(0, 0, 0); }

// Map SoC to LED color (Rule 2/3)
struct RGB
{
    uint8_t r, g, b;
};
RGB colourForSOC_LED(int soc)
{
    if (soc <= 20)
        return {255, 0, 0}; // RED
    else if (soc <= 40)
        return {255, 80, 0}; // DARKORANGE
    else if (soc <= 60)
        return {255, 220, 0}; // YELLOW
    else if (soc <= 80)
        return {0, 255, 0}; // GREEN
    else
        return {0, 128, 0}; // DARKGREEN
}

// Decide LED mode from your 4 rules
LedMode computeLedMode(int soc, int chg, const char *msg)
{
    // 3) Fully charged: solid DARKGREEN
    if (chg == 1 && soc == 100 && msg && strcmp(msg, "BATTERY FULL!") == 0)
        return LED_SOLID_FULL_MODE;

    // 1) Low SoC (<=20) and not charging: triple red
    if (soc <= 20 && chg == 0 && msg && strcmp(msg, "BATTERY LOW PLEASE CHARGE") == 0)
        return LED_FLASH3_RED_MODE;

    // 2) Charging (not full): normal flashing in SoC color
    if (chg == 1 && soc < 100)
        return LED_FLASH_CHG_MODE;

    // 4) Otherwise off
    return LED_OFF_MODE;
}

// Non-blocking LED driver (call every loop after you update input_soc/input_chg/current_status_msg)
void updateLED()
{
    LedMode want = computeLedMode(input_soc, input_chg, current_status_msg);

    if (want != g_prevLedMode)
    {
        // Reset per-mode state when changing modes
        g_seqIndex = 0;
        g_seqStampMs = millis();
        g_chgOnPhase = false;
        g_chgStampMs = millis();
        g_prevLedMode = want;
    }
    g_ledMode = want;

    uint32_t now = millis();

    switch (g_ledMode)
    {
    case LED_OFF_MODE:
        ledAllOff();
        break;

    case LED_SOLID_FULL_MODE:
    {
        // DARKGREEN
        RGB c = colourForSOC_LED(100);
        ledSetRGB255(c.r, c.g, c.b);
        break;
    }

    case LED_FLASH3_RED_MODE:
    {
        // Indices 0,2,4 are ON steps, others are OFF (incl. long gap)
        bool on = (g_seqIndex < 6) && ((g_seqIndex % 2) == 0);
        ledSetRGB255(on ? 255 : 0, 0, 0);
        if (now - g_seqStampMs >= LOW_TRIPLE_MS[g_seqIndex])
        {
            g_seqIndex = (g_seqIndex + 1) % LOW_TRIPLE_LEN;
            g_seqStampMs = now;
        }
        break;
    }

    case LED_FLASH_CHG_MODE:
    {
        // Normal flash with color by SoC
        RGB c = colourForSOC_LED(input_soc);
        uint16_t phaseDur = g_chgOnPhase ? CHG_ON_MS : CHG_OFF_MS;
        if (g_chgOnPhase)
            ledSetRGB255(c.r, c.g, c.b);
        else
            ledAllOff();
        if (now - g_chgStampMs >= phaseDur)
        {
            g_chgOnPhase = !g_chgOnPhase;
            g_chgStampMs = now;
        }
        break;
    }
    }
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

void splashAndHandshake()
{
    const uint32_t TIMEOUT_MS = 10000;
    uint8_t goodFrames = 0, dot = 0;

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
            Serial.println("LOADING...");
            lastSpin = millis();
        }
        delay(2);
    }

    if (goodFrames >= 5)
    {
        Serial.println("BMS Connected!");
        previous_soc = -1;
        previous_chg = -1;
        previous_status_msg = "";
    }
    else
    {
        Serial.println("BMS Error");
        delay(1000);
        Serial.println("RETRYING");
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

    // --- LEDC PWM init ---
    ledcSetup(LEDC_CH_R, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcSetup(LEDC_CH_G, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcSetup(LEDC_CH_B, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcAttachPin(LED_R_PIN, LEDC_CH_R);
    ledcAttachPin(LED_G_PIN, LEDC_CH_G);
    ledcAttachPin(LED_B_PIN, LEDC_CH_B);
    ledAllOff();

    bms.Init();
    Serial1.begin(9600, SERIAL_8N1, 21, 22);

    // splashAndHandshake();

    // NOTE - Auto test
    input_soc = 101;
    input_chg = 0;
    statusMessage(input_soc, input_chg);
    test_case = 1;
}

void loop()
{
    delay(100);

    // queryBMS();

    // NOTE - Real readings
    // input_soc = (int)ceilf(bms.get.packSOC); // ceiling to nearest lower int
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
    // input_soc = 59;
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

    // --- LED update (non-blocking, follows your 4 rules) ---
    updateLED();

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

    // Update previous values
    previous_soc = input_soc;
    previous_chg = input_chg;
    previous_status_msg = current_status_msg;
}
