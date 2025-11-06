#include <math.h>
#include <esp_now.h>
#include <WiFi.h>

// GLOBALS
int previous_soc = -1;
int input_soc = -1;        // to store user input
int previous_chg = -1;     // 0 = not charging, 1 = charging
int input_chg = -1;        // to store user input
float input_I = -1.0;      // Current in Amperes, +ve charge, -ve discharge
float input_resmAh = -1.0; // Remaining capacity in mAh

const char *current_status_msg = "";
const char *previous_status_msg = "";

// Define a data structure
//! Has to be the same as the struct in the Initiator
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

    input_soc = (int)ceilf(BMSData.soc); // ceiling to nearest upper int
    input_chg = (BMSData.I > 0.5) ? 1 : 0;
    // +ve charging, -ve discharging
    // Charging: +0-2A
    // OFF: ±0.0003A
    // Discharging: IDLE -0.037 to -0.04A, Reverse & Forward: -2 to -21A
    // However, due to the BMS low current reading resolution, it is more reliable to put > +0.5A as charging, and anything smaller as not charging.
    statusMessage(input_soc, input_chg); // Updates current_status_message
    input_I = BMSData.I;                 // +A charge, -A discharge
    input_resmAh = BMSData.resmAh;

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

    // Update previous values
    previous_soc = input_soc;
    previous_chg = input_chg;
    previous_status_msg = current_status_msg;
}

// SETUP & LOOP
void setup()
{
    Serial.begin(115200);
    delay(50);

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
}
