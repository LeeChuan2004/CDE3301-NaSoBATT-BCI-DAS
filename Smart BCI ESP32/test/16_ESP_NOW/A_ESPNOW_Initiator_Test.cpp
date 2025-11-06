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

/*
  ESP-NOW Demo - Transmit
  esp-now-demo-xmit.ino
  Initiator sending data to Responder

  DroneBot Workshop 2022
  https://dronebotworkshop.com
*/

// Include Libraries
#include <esp_now.h>
#include <WiFi.h>

// Variables for test data
int int_value;
float float_value;
bool bool_value = true;

//! MAC Address of responder - edit as required
uint8_t responderAddress[] = {0x38, 0x18, 0x2B, 0xF0, 0x8C, 0x14}; // 38:18:2B:F0:8C:14

// Define a data structure
typedef struct struct_message
{
    char a[32];
    int b;
    float c;
    bool d;
} struct_message;

// Create a structure object
struct_message myData;

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

void setup()
{
    // Set up Serial Monitor
    Serial.begin(9600);

    Serial.println("ESP-NOW Initiator Demo");

    // Set ESP32 as a Wi-Fi Station
    WiFi.mode(WIFI_STA);

    // Print MAC addresses
    Serial.print("My MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Responder MAC Address: ");
    char macStr[18];
    formatMacAddress(responderAddress, macStr, 18);
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
    memcpy(peerInfo.peer_addr, responderAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    // Add peer
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add peer");
        return;
    }
}

void loop()
{
    // Create test data

    // Generate a random integer
    int_value = random(1, 20);

    // Use integer to make a new float
    float_value = 1.3 * int_value;

    // Invert the boolean value
    bool_value = !bool_value;

    // Input the test data into the struct
    strcpy(myData.a, "Welcome to the Workshop!");
    myData.b = int_value;
    myData.c = float_value;
    myData.d = bool_value;

    // Send message via ESP-NOW
    esp_err_t result = esp_now_send(responderAddress, (uint8_t *)&myData, sizeof(myData));

    delay(500);

    // Print results to serial monitor
    if (result == ESP_OK)
    {
        Serial.println("Sending confirmed");
        Serial.println("Sent data: ");
        Serial.print("String: ");
        Serial.println(myData.a);
        Serial.print("Integer: ");
        Serial.println(myData.b);
        Serial.print("Float: ");
        Serial.println(myData.c);
        Serial.print("Boolean: ");
        Serial.println(myData.d);
        Serial.println();
    }
    else if (result == ESP_ERR_ESPNOW_NOT_INIT)
    {
        Serial.println("ESP-NOW not Init.");
        Serial.println();
    }
    else if (result == ESP_ERR_ESPNOW_ARG)
    {
        Serial.println("Invalid Argument");
        Serial.println();
    }
    else if (result == ESP_ERR_ESPNOW_INTERNAL)
    {
        Serial.println("Internal Error");
        Serial.println();
    }
    else if (result == ESP_ERR_ESPNOW_NO_MEM)
    {
        Serial.println("ESP_ERR_ESPNOW_NO_MEM");
        Serial.println();
    }
    else if (result == ESP_ERR_ESPNOW_NOT_FOUND)
    {
        Serial.println("Peer not found.");
        Serial.println();
    }
    else
    {
        Serial.println("Unknown error");
        Serial.println();
    }

    delay(1000);
}