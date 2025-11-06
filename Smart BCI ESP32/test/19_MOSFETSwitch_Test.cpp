#include <Arduino.h>

constexpr int16_t BUTTON = 13;
constexpr int16_t MOSFET_GATE = 12;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    pinMode(BUTTON, INPUT_PULLDOWN);
    pinMode(MOSFET_GATE, OUTPUT);
    digitalWrite(MOSFET_GATE, LOW); // Switch off load initially
}

void loop()
{
    if (digitalRead(BUTTON) == HIGH)
    {
        if (digitalRead(MOSFET_GATE) == false)
        {
            Serial.println("ON");
            digitalWrite(MOSFET_GATE, HIGH);
            Serial.println("MOSFET_GATE state:");
            Serial.println(digitalRead(MOSFET_GATE));
            delay(100); // Debounce delay
            while (digitalRead(BUTTON) == HIGH)
            {
                // Wait for button release
            }
        }
        else
        {
            Serial.println("OFF");
            digitalWrite(MOSFET_GATE, LOW);
            Serial.println("MOSFET_GATE state:");
            Serial.println(digitalRead(MOSFET_GATE));
            delay(100); // Debounce delay
            while (digitalRead(BUTTON) == HIGH)
            {
                // Wait for button release
            }
        }
        delay(100);
    }
}