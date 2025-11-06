// ESP-NOW vs. Bluetooth/WiFi
// 1. No router, no AP, no SSID, no DHCP needed compared to WiFi
// 2. No pairing needed compared to Bluetooth
// 3. Very low power
// 4. Very low latency
// 5. Simple to set up
// 6. Quick connection time

// Some notes on ESP-NOW:
// - like wireless mice
// - Limited to 250 bytes per packet
// - When Initiators communicate with Responders, they need to know the Responder’s MAC Address.
// A MAC, or Media Access Control, Address is a unique 6-digit hexadecimal number assigned to every device on a network.
// It is generally burned into the device by the manufacturer, although it is possible to manually set it.
// - ESP-NOW uses callback functions to handle the sending and receiving of data automatically, similar to the pub-sub model.
// A callback is a bit like an interrupt, it is generated every time a specific event has occurred.
// Create your own callback functions to handle sending and receiving data, bind them to esp_now_register_send_cb() and esp_now_register_recv_cb() and they will be called automatically when a packet is sent/received.

// Include Libraries
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <math.h>
#include <daly-bms-uart.h>
#define BMS_SERIAL Serial1
Daly_BMS_UART bms(BMS_SERIAL);

// 0 = ACTUAL (read BMS), 1 = MANUAL (fixed values), 2 = AUTO (sweep)
#define TEST_MODE 2
#if (TEST_MODE < 0) || (TEST_MODE > 2)
#error "TEST_MODE must be 0 (ACTUAL), 1 (MANUAL), or 2 (AUTO)"
#endif

// NOTE - Auto test globals
int test_case = 1; // 1 is discharge, 2 is charge
uint32_t AUTO_TEST_MS = 1000;
uint32_t lastAutoTestms = 0;

// ------------------------------------- ESP-NOW Setup -------------------------------------

//! MAC Address of responder - edit as required
uint8_t responderAddress_ESA[] = {0x38, 0x18, 0x2B, 0xF0, 0x8C, 0x14};    // 38:18:2B:F0:8C:14
uint8_t responderAddress_LilyGo[] = {0xEC, 0xE3, 0x34, 0x9A, 0xAD, 0x4C}; // EC:E3:34:9A:AD:4C

// how often to ESP-NOW send
constexpr uint32_t SEND_PERIOD_MS = 500;
uint32_t lastSendMs = 0;

// Define a data structure
typedef struct struct_message
{
    bool bms_status;
    float soc;
    float I;
    float resmAh;
} struct_message;

// Create a structure object
struct_message BMSData;

// Peer info
esp_now_peer_info_t peerInfo;

// Helper function to format MAC Addresses
void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength)
{
    snprintf(buffer, maxLength, "%02x:%02x:%02x:%02x:%02x:%02x", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}

// Callback function that you want to call when data is sent
// The arguments of the callback function are fixed to be this two.
void sendingCallback(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    char macStr[18];
    formatMacAddress(mac_addr, macStr, 18);
    Serial.print("Last Packet Sent to: ");
    Serial.println(macStr);
    Serial.print("Last Packet Send Status: ");
    // Serial.println(status);
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Send Success" : "Send Fail");
}

// ------------------------------------- BMS Input Setup -------------------------------------

float input_soc = -1.0f; // raw SoC from BMS (float) //! all the previous shit do at responder side
float input_I = -1.0f;   // +A charge, -A discharge
float input_resmAh = -1.0f;
bool bms_ok = false;

// ------------------------------------- RGB LED Setup -------------------------------------

#define LED_R_PIN 19
#define LED_G_PIN 18
#define LED_B_PIN 5

#define LEDC_FREQ_HZ 5000
#define LEDC_RES_BITS 8
#define LEDC_MAX ((1 << LEDC_RES_BITS) - 1)
#define LEDC_CH_R 0
#define LEDC_CH_G 1
#define LEDC_CH_B 2

inline void ledcWrite255(uint8_t ch, uint8_t v)
{
    ledcWrite(ch, v);
}

inline void ledSetRGB255(uint8_t r, uint8_t g, uint8_t b)
{
    ledcWrite255(LEDC_CH_R, r);
    ledcWrite255(LEDC_CH_G, g);
    ledcWrite255(LEDC_CH_B, b);
}

inline void ledAllOff()
{
    ledSetRGB255(0, 0, 0);
}

struct RGB
{
    uint8_t r, g, b;
};

RGB colourForSOC_LED(int soc)
{
    if (soc <= 40)
        return {255, 0, 0}; // red
    if (soc <= 80)
        return {255, 220, 0}; // yellow
    return {0, 255, 0};       // green
}

enum class Status : uint8_t
{
    None = 0,
    Charging,
    Full,
    Low
};

enum LedMode : uint8_t
{
    LED_OFF_MODE,
    LED_FLASH3_RED_MODE,
    LED_FLASH_CHG_MODE,
    LED_SOLID_FULL_MODE
};

constexpr uint16_t LOW_TRIPLE_MS[] = {120, 120, 120, 120, 120, 120, 1000};
constexpr uint8_t LOW_TRIPLE_LEN = sizeof(LOW_TRIPLE_MS) / sizeof(LOW_TRIPLE_MS[0]);
constexpr uint16_t CHG_ON_MS = 2000, CHG_OFF_MS = 2000;

Status g_status = Status::None;
Status g_prevStatus = Status::None;
LedMode g_ledMode = LED_OFF_MODE;
LedMode g_prevLedMode = LED_OFF_MODE;

uint8_t g_seqIndex = 0; // triple-flash state
uint32_t g_seqStampMs = 0;
bool g_chgOnPhase = false; // charge flash state
uint32_t g_chgStampMs = 0;

template <typename T>
inline T clamp(T v, T lo, T hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void statusMessage(int soc, int chg)
{
    if (chg && soc < 100)
        g_status = Status::Charging;
    else if (chg && soc == 100)
        g_status = Status::Full;
    else if (soc <= 20)
        g_status = Status::Low;
    else
        g_status = Status::None;
}

LedMode computeLedMode(int soc, int chg, Status s)
{
    if (s == Status::Full && chg == 1 && soc == 100)
        return LED_SOLID_FULL_MODE;
    if (s == Status::Low && chg == 0 && soc <= 20)
        return LED_FLASH3_RED_MODE;
    if (chg == 1 && soc < 100)
        return LED_FLASH_CHG_MODE;
    return LED_OFF_MODE;
}

void updateLED(int soc, int chg)
{
    LedMode want = computeLedMode(soc, chg, g_status);

    if (want != g_prevLedMode)
    {
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
        RGB c = colourForSOC_LED(100);
        ledSetRGB255(c.r, c.g, c.b);
        break;
    }

    case LED_FLASH3_RED_MODE:
    {
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
        RGB c = colourForSOC_LED(soc);
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

void setup()
{
    // Set up Serial Monitor
    Serial.begin(115200);
    delay(50);

    Serial.println("Battery Box ESP-NOW Initiator + RGB LED Begin");

    ledcSetup(LEDC_CH_R, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcSetup(LEDC_CH_G, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcSetup(LEDC_CH_B, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcAttachPin(LED_R_PIN, LEDC_CH_R);
    ledcAttachPin(LED_G_PIN, LEDC_CH_G);
    ledcAttachPin(LED_B_PIN, LEDC_CH_B);
    ledAllOff();

    // Set ESP32 as a Wi-Fi Station
    WiFi.mode(WIFI_STA);

    // Print MAC addresses
    Serial.print("My MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Responder MAC Address (ESA): ");
    char macStr[18];
    formatMacAddress(responderAddress_ESA, macStr, 18);
    Serial.println(macStr);
    Serial.print("Responder MAC Address (LilyGo): ");
    macStr[18];
    formatMacAddress(responderAddress_LilyGo, macStr, 18);
    Serial.println(macStr);

    // Disconnect from WiFi
    WiFi.disconnect();

    // Initialize ESP-NOW
    if (esp_now_init() == ESP_OK)
    {
        Serial.println("ESP-NOW Init Success");
        // Bind the send callback
        esp_now_register_send_cb(sendingCallback);
    }
    else
    {
        Serial.println("ESP-NOW Init Failed");
        delay(3000);
        ESP.restart();
    }

    // Register peer
    memcpy(peerInfo.peer_addr, responderAddress_ESA, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    // Add peer
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add peer (ESA)");
        return;
    }

    memcpy(peerInfo.peer_addr, responderAddress_LilyGo, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    // Add peer
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add peer (LilyGo)");
        return;
    }

    bms.Init();
    Serial1.begin(9600, SERIAL_8N1, 21, 22); // Map UART1's RX to GPIO21 and the TX to GPIO22

#if TEST_MODE == 2 // AUTO
    bms_ok = true; // we’ll generate fake data
    input_soc = 101;
    test_case = 1;
#endif
}

void loop()
{
#if TEST_MODE == 0 // ACTUAL
    // NOTE - Actual raw readings from BMS
    bms_ok = bms.update();
    input_soc = bms.get.packSOC;   //! ceiling to nearest upper int do at responder side
    input_I = bms.get.packCurrent; // +A charge, -A discharge //! condition for input_chg do at responder side
    input_resmAh = bms.get.resCapacitymAh;

#elif TEST_MODE == 1 // MANUAL
    // NOTE - Manual Test
    bms_ok = true;
    input_soc = 20;
    input_I = 0.55;
    input_resmAh = 11000;

#elif TEST_MODE == 2 // AUTO
    // NOTE - Auto Test
    if (millis() - lastAutoTestms >= AUTO_TEST_MS)
    {
        if ((input_soc <= 21 && test_case == 1) || (input_soc >= 94 && test_case == 2))
        {
            AUTO_TEST_MS = 3000;
        }
        else
        {
            AUTO_TEST_MS = 500;
        }
        if (input_soc == 0)
        {
            test_case = 2; // Charge test
            delay(1000);
        }
        if (input_soc == 100)
        {
            test_case = 1; // Discharge test
            delay(1000);
        }

        if (test_case == 1) // Discharge test
        {
            input_soc--;
            input_I = -10; // -10A discharge
            input_resmAh = 11000 * input_soc / 100;
        }
        else if (test_case == 2) // Charge test
        {
            input_soc++;
            input_I = 10; // +10A charge
            input_resmAh = 11000 * input_soc / 100;
        }
        lastAutoTestms = millis();
    }
#endif

    uint32_t now = millis();

    if (now - lastSendMs >= SEND_PERIOD_MS)
    {
        Serial.println("----------------------------------------------------");
        Serial.print("BMS Status: ");
        Serial.println(bms_ok);
        Serial.print("BMS SoC: ");
        Serial.println(input_soc);
        Serial.print("BMS Current: ");
        Serial.println(input_I);
        Serial.print("BMS Remaining Capacity (mAh): ");
        Serial.println(input_resmAh);
        Serial.println();

        // Input the test data into the struct
        BMSData.bms_status = bms_ok;
        BMSData.soc = input_soc;
        BMSData.I = input_I;
        BMSData.resmAh = input_resmAh;

        // Send message to ESA via ESP-NOW
        Serial.println("-------------------------------------------------------------- Sending to ESA --------------------------------------------------------------");
        esp_err_t result = esp_now_send(responderAddress_ESA, (uint8_t *)&BMSData, sizeof(BMSData));

        // Print results to serial monitor
        if (result == ESP_OK)
        {
            Serial.println("Sending confirmed");
            Serial.println("Sent data: ");
            Serial.print("BMS Status: ");
            Serial.println(BMSData.bms_status);
            Serial.print("BMS SoC: ");
            Serial.println(BMSData.soc);
            Serial.print("BMS Current: ");
            Serial.println(BMSData.I);
            Serial.print("BMS Remaining Capacity (mAh): ");
            Serial.println(BMSData.resmAh);
        }
        else if (result == ESP_ERR_ESPNOW_NOT_INIT)
        {
            Serial.println("ESP-NOW not Init.");
        }
        else if (result == ESP_ERR_ESPNOW_ARG)
        {
            Serial.println("Invalid Argument");
        }
        else if (result == ESP_ERR_ESPNOW_INTERNAL)
        {
            Serial.println("Internal Error");
        }
        else if (result == ESP_ERR_ESPNOW_NO_MEM)
        {
            Serial.println("ESP_ERR_ESPNOW_NO_MEM");
        }
        else if (result == ESP_ERR_ESPNOW_NOT_FOUND)
        {
            Serial.println("Peer not found.");
        }
        else
        {
            Serial.println("Unknown error");
        }

        delay(100);

        // Send message to LilyGo via ESP-NOW
        Serial.println("-------------------------------------------------------------- Sending to LilyGo --------------------------------------------------------------");
        result = esp_now_send(responderAddress_LilyGo, (uint8_t *)&BMSData, sizeof(BMSData));

        // Print results to serial monitor
        if (result == ESP_OK)
        {
            Serial.println("Sending confirmed");
            Serial.println("Sent data: ");
            Serial.print("BMS Status: ");
            Serial.println(BMSData.bms_status);
            Serial.print("BMS SoC: ");
            Serial.println(BMSData.soc);
            Serial.print("BMS Current: ");
            Serial.println(BMSData.I);
            Serial.print("BMS Remaining Capacity (mAh): ");
            Serial.println(BMSData.resmAh);
        }
        else if (result == ESP_ERR_ESPNOW_NOT_INIT)
        {
            Serial.println("ESP-NOW not Init.");
        }
        else if (result == ESP_ERR_ESPNOW_ARG)
        {
            Serial.println("Invalid Argument");
        }
        else if (result == ESP_ERR_ESPNOW_INTERNAL)
        {
            Serial.println("Internal Error");
        }
        else if (result == ESP_ERR_ESPNOW_NO_MEM)
        {
            Serial.println("ESP_ERR_ESPNOW_NO_MEM");
        }
        else if (result == ESP_ERR_ESPNOW_NOT_FOUND)
        {
            Serial.println("Peer not found.");
        }
        else
        {
            Serial.println("Unknown error");
        }

        lastSendMs = now;
    }

    int soc_i = clamp((int)ceilf(input_soc), 0, 100);
    int chg = (input_I > 0.5f) ? 1 : 0; // >+0.5A = charging
    statusMessage(soc_i, chg);          // sets g_status
    updateLED(soc_i, chg);              // non-blocking LED state machine

    delay(1);
}