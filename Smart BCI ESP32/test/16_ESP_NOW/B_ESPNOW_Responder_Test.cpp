/*
  ESP-NOW Demo - Receive
  esp-now-demo-rcv.ino
  Reads data from Initiator

  DroneBot Workshop 2022
  https://dronebotworkshop.com
*/

// Include Libraries
#include <esp_now.h>
#include <WiFi.h>

// Define a data structure
//! Has to be the same as the struct in the Initiator
typedef struct struct_message
{
    char a[32];
    int b;
    float c;
    bool d;
} struct_message;

// Create a structured object
struct_message myData;

void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength)
// Formats MAC Address
{
    snprintf(buffer, maxLength, "%02x:%02x:%02x:%02x:%02x:%02x", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}

// Callback function that you want executed when data is received
// The arguments of the callback function are fixed to be this three.
void receiveCallback(const uint8_t *macAddr, const uint8_t *incomingData, int dataLen)
{
    // Only allow a maximum of 250 characters in the message + a null terminating byte
    char buffer[ESP_NOW_MAX_DATA_LEN + 1];
    int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
    strncpy(buffer, (const char *)incomingData, msgLen);

    // Make sure we are null terminated
    buffer[msgLen] = 0;

    char macStr[18];
    formatMacAddress(macAddr, macStr, 18);
    // Send Debug log message to the serial port
    Serial.printf("Received message from: %s - %s\n", macStr, buffer);

    memcpy(&myData, incomingData, sizeof(myData));
    Serial.print("Data received: ");
    Serial.println(dataLen);
    Serial.print("Character Value: ");
    Serial.println(myData.a);
    Serial.print("Integer Value: ");
    Serial.println(myData.b);
    Serial.print("Float Value: ");
    Serial.println(myData.c);
    Serial.print("Boolean Value: ");
    Serial.println(myData.d);
    Serial.println();
}

void setup()
{
    // Set up Serial Monitor
    Serial.begin(9600);

    Serial.println("ESP-NOW Responder Demo");

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
        // Bind the send callback
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
    // Nothing happens in the Loop, as everything is handled in the callback function.
}