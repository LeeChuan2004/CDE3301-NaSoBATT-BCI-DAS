#include <Arduino.h>
#include <daly-bms-uart.h>

#define BMS_SERIAL Serial1
//GENERAL KNOWLEDGE! The ESP-WROOM-32E module uses Espressif’s dual-core ESP32 and brings three hardware UART controllers — UART0, UART1 and UART2 — into the GPIO matrix.
//GENERAL KNOWLEDGE! Unlike many MCUs whose UART pins are hard-wired, the ESP32 lets any UART signal be routed to “almost any” GPIO through a switch fabric inside the chip, as confirmed in Espressif’s datasheet (“pins for UART can be chosen from any GPIOs via the GPIO Matrix”).
//GENERAL KNOWLEDGE! UART0, exposed as Serial, corresponds to GPIO3 (RX) and GPIO1 (TX) by default. They are hard-wired to the on-board USB-to-UART bridge so flashing firmware and viewing logs over the Arduino Serial Monitor work out of the box. 
//GENERAL KNOWLEDGE! UART1, exposed as Serial1, corresponds to GPIO9 (RX) and GPIO10 (TX) by default, but they clash with the SPI flash pins by default, so always relocate/remap UART1 to some other pins if you intend to use it.
//GENERAL KNOWLEDGE! UART2, exposed as Serial2, corresponds to GPIO16 (RX) and GPIO17 (TX) by default. It's a free hardware UART port often used for external peripherals.
//NOTE - However, Serial2 is being used by the TFT LCD display here, so let's try using UART1 which requires mapping to other GPIO pins!
//GENERAL KNOWLEDGE! You can freely remap the GPIO9 (RX) and GPIO10 (TX) default of UART1 by using SerialX.begin's (X is a number such that it's Serial, Serial1 or Serial2, so in this case it will be Serial1.begin) extra arguments.
//GENERAL KNOWLEDGE! You can remap to any other output-capable pin for TX and any input-capable pin for RX as long as you avoid:
//GENERAL KNOWLEDGE! GPIO 6-11 (SPI-flash lines) (Driving them will brick the boot process)
//GENERAL KNOWLEDGE! GPIO 34-39 (input-only, so only usable for RX)
//GENERAL KNOWLEDGE! GPIO0, 2, 4, 5, 12, 15 which are strapping pins
//GENERAL KNOWLEDGE! (Strapping pins are GPIO pins whose logic level is sampled by the ESP32’s boot-ROM during reset to “strap” — i.e., lock in — critical start-up options such as which flash voltage to use, which clock source to enable, and whether to enter the serial bootloader.
//GENERAL KNOWLEDGE! On the ESP-WROOM-32E those strapping pins are GPIO 0, 2, 4, 5, 12 and 15. Immediately after power-on or a hard reset the chip reads each of them, stores the results in internal latches, and only then releases the pins for normal GPIO use.
//GENERAL KNOWLEDGE! If an external circuit drives one of these lines to the wrong level at that instant, the ESP32 can fail to boot, stay in firmware-upload mode, or try to talk to the SPI flash at the wrong voltage.
//GENERAL KNOWLEDGE! Once the strap is latched the pins behave like ordinary I/O, so you may still use them after boot provided any connected device leaves them in their “safe” idle state (typically pull-ups on 0 and 2, pull-downs on 5 and 15, and the correct high/low for 12 and 4).
//GENERAL KNOWLEDGE! In short, strapping pins are boot-mode switches baked into the silicon, and every circuit that repurposes them must respect the required reset levels.)



Daly_BMS_UART bms(BMS_SERIAL);
// Create an instance bms of the Daly_BMS_UART class, initialising it with the Serial2 port.

const int LED = 27;
const int LED_test = 33;

void setup() {
  bms.Init(); 
  Serial.begin(9600);
  Serial1.begin(9600, SERIAL_8N1, 21, 22); // Map UART1's RX to GPIO21 and the TX to GPIO22
  //GENERAL KNOWLEDGE! Serial.begin is the call that brings a hardware-UART to life in the Arduino environment.
  //GENERAL KNOWLEDGE! Originally it's just Serial.begin(baud rate, optional config), baud rate is in bits per second and the optional config is for users to fill in one of the defined constants in HardwareSerial.h such as SERIAL_8N1 (8 data bits, no parity, 1 stop bit), SERIAL_7E1 (7 data bits, even parity, 1 stop bit), SERIAL_8O2 (8 data bits, odd parity, 2 stop bits), etc. 
  //GENERAL KNOWLEDGE! Espressif’s Arduino core overloads the function with extra parameters so any UART signal can be remapped through the chip’s GPIO-matrix:
  //GENERAL KNOWLEDGE! bool HardwareSerial::begin(
  //GENERAL KNOWLEDGE!      unsigned long baud,
  //GENERAL KNOWLEDGE!      uint32_t config = SERIAL_8N1,
  //GENERAL KNOWLEDGE!      int8_t rxPin    = -1,
  //GENERAL KNOWLEDGE!      int8_t txPin    = -1,
  //GENERAL KNOWLEDGE!      bool  invert    = false,
  //GENERAL KNOWLEDGE!      unsigned long timeout_ms = 0);
  //GENERAL KNOWLEDGE! rxPin / txPin – any free GPIO except the flash lines (6-11) and the input-only 34-39.  
  //GENERAL KNOWLEDGE! invert – set true if the remote device uses inverse signalling (idle = LOW).  
  //GENERAL KNOWLEDGE! timeout_ms – optional RX idle time before Serial.available() stops waiting.  
  //GENERAL KNOWLEDGE! The call returns true on success, so you can test pin/baud combinations at runtime.  
  pinMode(LED, OUTPUT);
  pinMode(LED_test, OUTPUT);
}

void loop() {
  bms.update();
  // This .update() call populates the entire get struct in the Daly_BMS_UART class. If you only need certain values (like
  // SOC & Voltage) you could use other public APIs, like getPackMeasurements(), which only query specific values from the BMS instead of all.

  if (bms.get.packVoltage > 0 && bms.get.packSOC > 0) {
    digitalWrite(LED, LOW);
    digitalWrite(LED_test, HIGH);
    Serial.println("Battery Capacity:              " + (String)bms.get.packSOC + "\% ");
    Serial.println("Battery Voltage:               " + (String)bms.get.packVoltage + "V ");
    Serial.println("Discharge MOSFET Status:       " + (String)bms.get.disChargeFetState);
    Serial.println("Charge MOSFET Status:          " + (String)bms.get.chargeFetState);
    delay(1000);
    digitalWrite(LED_test, LOW);
  } else if (bms.get.packVoltage == 0 && bms.get.packSOC == 0) {
    Serial.println("No data!");
    digitalWrite(LED, HIGH);
    delay(1000);
    digitalWrite(LED, LOW);
  } else {
    Serial.println("Invalid!");
    delay(1000);
  }
}