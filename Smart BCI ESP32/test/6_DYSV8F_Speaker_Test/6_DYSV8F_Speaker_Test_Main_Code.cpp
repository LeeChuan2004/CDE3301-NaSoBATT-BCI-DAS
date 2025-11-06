#include <Arduino.h>

const int DYSV8F_IO0 = 25;
const int DYSV8F_IO1 = 26;
const int DYSV8F_IO2 = 27;

void setup() {
    pinMode(DYSV8F_IO0, OUTPUT);
    pinMode(DYSV8F_IO1, OUTPUT);
    pinMode(DYSV8F_IO2, OUTPUT);
    digitalWrite(DYSV8F_IO0, HIGH); 
    digitalWrite(DYSV8F_IO1, HIGH);
    digitalWrite(DYSV8F_IO2, HIGH);
  }
  
void loop() {
  digitalWrite(DYSV8F_IO0, HIGH); // Plays battery full music
  digitalWrite(DYSV8F_IO1, HIGH);
  digitalWrite(DYSV8F_IO2, LOW);
  delay(1000); 
  }

  // Using the IO Integrated Mode 0 (000 on configuration pins)
  // When a new signal is received at the IO pins, will stop current song immediately and change to new song.
  // Integrated: Indexing of audio tracks in the DYSV8F 8MB flash memory follows binary format but inversed.
  // i.e., Track 1: 11111110; Track 2: 11111101; Track 3: 11111100; ... (LSB on the right side)
  // Mode 0: Even after all IO pins are high (all pulled to high or not connected which means no song is selected), the current song will continue playing till the end.
  // (Mode 1: When all IO pins are high, the current song will stop immediately)
  // After changing modes by flipping the configuration pins, you need to connect and disconnect the power supply into the DY2V8F module to reset.
