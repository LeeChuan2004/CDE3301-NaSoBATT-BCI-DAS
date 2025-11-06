#include <Arduino.h>
#include <daly-bms-uart.h>

#define BMS_SERIAL Serial1

Daly_BMS_UART bms(BMS_SERIAL);
// Create an instance bms of the Daly_BMS_UART class, initialising it with the Serial2 port.

void setup()
{
    bms.Init();
    Serial.begin(9600);
    Serial1.begin(9600, SERIAL_8N1, 21, 22); // Map UART1's RX to GPIO21 and the TX to GPIO22
}

void loop()
{
    bms.update();
    // This .update() call populates the entire get struct in the Daly_BMS_UART class. If you only need certain values (like
    // SOC & Voltage) you could use other public APIs, like getPackMeasurements(), which only query specific values from the BMS instead of all.

    if (bms.get.packVoltage > 0)
    {
        Serial.println("Current:               " + (String)bms.get.packCurrent + "A "); // +ve charging, -ve discharging
        //! Conclusion: The BMS current reading resolution too large, cannot detect small currents when the mobility scooter is switched on but IDLE.
        delay(250);
    }
    else if (bms.get.packVoltage == 0 && bms.get.packSOC == 0)
    {
        Serial.println("No data!");
    }
    else
    {
        Serial.println("Invalid!");
        delay(1000);
    }
}