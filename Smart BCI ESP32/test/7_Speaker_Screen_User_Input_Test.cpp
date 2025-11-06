/* Pin connections to ESP32 with notes
LCD_RD → 2 (Read)
LCD_WR → 4 (Write)
LCD_RS → 15 (Register Select, determines whether the LCD is expecting a command or data (instruction register or data register), often a.k.a. LCD_CD for Command/Data)
LCD_CS → 33 (Chip Select, enables or disables communication with the LCD, doesn't seem to be needed though, can be left unconnected)
LCD_RST → 32 (Reset, can also be connected to the ESP32's reset pin, i.e., the EN pin)
LCD_D1 → 13
LCD_D0 → 12
LCD_D7 → 14
LCD_D6 → 27
LCD_D5 → 16
LCD_D4 → 17
LCD_D3 → 25
LCD_D2 → 26
It seems like the 5V pin need not be connected.
The RESET pin too, doesn't seem to be needed.
*/

#include "Adafruit_GFX.h"               // Hardware-specific library
#include <MCUFRIEND_kbv.h>
#include <math.h>
MCUFRIEND_kbv tft;
#include <Fonts/FreeSansBold12pt7b.h>   // for status message
#include <Fonts/FreeSansBold24pt7b.h>   // for SoC %

// Assign human-readable names to some common 16-bit RGB565 colour values:
#define	BLACK   0x0000
#define	BLUE    0x001F
#define	RED     0xF800
#define	GREEN   0x57E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF
#define NAVY        0x000F      /*   0,   0, 128 */
#define DARKCYAN    0x03EF      /*   0, 128, 128 */
#define DARKGREEN   0x0400      /*   0, 128,   0 */
#define MAROON      0x7800      /* 128,   0,   0 */
#define PURPLE      0x780F      /* 128,   0, 128 */
#define OLIVE       0x7BE0      /* 128, 128,   0 */
#define LIGHTGREY   0xC618      /* 192, 192, 192 */
#define DARKGREY    0x7BEF      /* 128, 128, 128 */
#define ORANGE      0xFDA0      /* 255, 180,   0 */
#define DARKORANGE  0xF940   
#define GREENYELLOW 0xB7E0      /* 180, 255,   0 */
#define PINK        0xFC9F  
#define DARKRED     0x8800

// USER-CONFIGURABLE CONSTANTS
constexpr uint8_t  ROTATION          = 1;          // 0 for Portrait, 1 for Landscape (90 degrees), 2 for Reversed Portrait (180 degrees), 3 for Reversed Landscape (270 degrees))
constexpr uint16_t BACKGROUND_COLOUR = BLACK;
constexpr uint16_t SOME_LINE_COLOUR  = WHITE;
constexpr uint8_t BORDER_THICKNESS   = 8;          // pixels
constexpr uint8_t PERCENTAGE_SIZE    = 7;
constexpr uint8_t CROSS_THICKNESS    = 8;          // must be ≥1
constexpr int16_t  ICON_X            = 35;         // x-coordinate of top-left corner of battery corner
constexpr int16_t  ICON_Y            = 70;         // y-coordinate of top-left corner of battery corner
constexpr int16_t  ICON_W            = 400;        // battery icon width
constexpr int16_t  ICON_H            = 150;        // battery icon height
constexpr int16_t  TERM_W            = 12;         // positive terminal width
constexpr int16_t DYSV8F_IO0         = 18;
constexpr int16_t DYSV8F_IO1         = 19;
// Serial input prompt
const char *SERIAL_PROMPT =
  "\nEnter SoC (0-100) and Charging Status (0 or 1) separated by space.\n"
  "Example: 75 0\n> ";

// GLOBALS
int  previous_soc         = -1;    
int input_soc             = -1;            // to store user input
int previous_chg          = -1;            // 0 = not charging, 1 = charging
int input_chg             = -1;            // to store user input
uint32_t lastBlinkMs      =  0;
bool lowBlinkState        = false;         // toggles when blinking
bool isBatteryBarsWiped   = false;
bool isLightningBoltWiped = false;
const char* current_status_msg  = "";
const char* previous_status_msg = "";

// COLOUR UTILITIES
uint16_t colourForSOC(int soc)
{
  if      (soc == 0)          return DARKRED;     
  else if (soc <= 20)         return RED;          
  else if (soc <= 40)         return DARKORANGE;           
  else if (soc <= 60)         return YELLOW;          
  else if (soc <= 80)         return GREEN;           
  else if (soc <= 100)        return DARKGREEN;
  else                        return PINK;        // For troubleshooting purposes
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
    int8_t half = CROSS_THICKNESS / 2;               // centred offsets
      
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
  const uint16_t FILL_COLOUR = YELLOW;     // bright yellow RGB565
  const uint16_t OUTLINE_COLOUR = BLACK;   // black

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
    uint16_t barColour   = colourForSOC(soc);

    const uint8_t barsToFill = (soc + 19) / 20;         // 0…5 (/ is integer division, i.e., divison with truncation))
    const int16_t BAR_W   = (ICON_W - 12) / 5;          // leave small padding
    const int16_t BAR_H   = ICON_H - 12;
    const int16_t BAR_Y   = ICON_Y + 6;
  
    for (uint8_t i = 0; i < 5; ++i)                     // Prefix increment, but this loop still runs 5 times (0, 1, 2, 3, 4) (prefix or suffix does not affect iteration count)
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
  char buf[8];
  sprintf(buf, "%3d%%", soc);                 // e.g. " 75%"

  // use FreeSansBold 24 pt
  tft.setFont(&FreeSansBold24pt7b);           // proportional glyphs
  tft.setTextSize(2);                         // scale by 2
  tft.setTextColor(SOME_LINE_COLOUR, BACKGROUND_COLOUR);

  // Measure text so we can centre it
  int16_t x1, y1;  uint16_t w, h;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);

  // Baseline 20 px below the battery icon
  int16_t txtX = (tft.width()  - w) / 2;
  int16_t txtY = ICON_Y + ICON_H + 7 + h;    // y is baseline

  // Clear the old area then draw
  tft.fillRect(BORDER_THICKNESS + 1,
               ICON_Y + ICON_H + 2,          // a hair above
               tft.width() - 2*BORDER_THICKNESS - 1,
               h + 12,                       // extra margin
               BACKGROUND_COLOUR);

  tft.setCursor(txtX, txtY);
  tft.print(buf);

  tft.setFont();                             // restore classic 5×7
}

void statusMessage(int soc, int chg)
{

  current_status_msg = "";

  if (chg && soc < 95)
  {
    current_status_msg = "BATTERY CHARGING";                     
  }
  else if (chg && soc >= 95)
  {
    current_status_msg = "BATTERY FULL!";
  }
  else if (soc <= 20)
  {
    current_status_msg = "BATTERY LOW PLEASE CHARGE";
  }
}

void playAudio(const char* status_message)
{
  digitalWrite(DYSV8F_IO0, HIGH); 
  digitalWrite(DYSV8F_IO1, HIGH);

  if (status_message == "BATTERY CHARGING")
  {
    digitalWrite(DYSV8F_IO0, HIGH);          // Plays "Charging"
    digitalWrite(DYSV8F_IO1, LOW);
  }
  else if (status_message == "BATTERY FULL!")
  {
    digitalWrite(DYSV8F_IO0, LOW);           // Plays "Battery Full. Charging Complete"
    digitalWrite(DYSV8F_IO1, LOW);
  }
  else if (status_message == "BATTERY LOW PLEASE CHARGE")
  {
    digitalWrite(DYSV8F_IO0, LOW);           // Plays "Battery Low. Please Charge"
    digitalWrite(DYSV8F_IO1, HIGH);
  }

  delay(10); // Must have delay, without it, the pin states changes too fast and the audio module does not register. (4ms is the borderline shortest delay needed)

  digitalWrite(DYSV8F_IO0, HIGH); 
  digitalWrite(DYSV8F_IO1, HIGH);
}

void drawStatus(const char* msg) 
{
  // Use FreeSans Bold 12 pt
  tft.setFont(&FreeSansBold12pt7b);          //  ≈17 px high
  tft.setTextSize(1);                        // native size
  tft.setTextColor(SOME_LINE_COLOUR, BACKGROUND_COLOUR);

  // Measure string
  int16_t  x1, y1;  uint16_t w, h;
  tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);

  // Horizontal centre
  int16_t statusX = (tft.width() - w) / 2;

  // Vertical centre between top border and battery icon
  const int16_t areaTop    = BORDER_THICKNESS;   // 8 px
  const int16_t areaBottom = ICON_Y;             // 70 px by default
  const int16_t areaMid    = (areaTop + areaBottom) / 2;

  // Align baseline so bbox centre sits on areaMid
  int16_t baselineY = areaMid - (h / 2) - y1;

  // Clear that strip
  tft.fillRect(BORDER_THICKNESS + 1,
               areaTop,
               tft.width() - 2*BORDER_THICKNESS-1,
               areaBottom - areaTop,
               BACKGROUND_COLOUR);

  // Draw
  tft.setCursor(statusX, baselineY);
  tft.print(msg);

  tft.setFont();                                // restore default font
}

// SERIAL INPUT HANDLER
void tryReadSerialAndUpdateDisplay() 
{
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.isEmpty()) return;

  float input_socFloat = -1.0f;

  // Parse: <float> <int>
  // e.g. "75.5 1" or "42 0"
  int parsed = sscanf(line.c_str(), "%f %d", &input_socFloat, &input_chg);
  if (parsed != 2) {                             // wrong format
    Serial.println("Invalid input. Format: <0-100(float)> <0|1>");
    Serial.print(SERIAL_PROMPT);
    return;
  }

  input_soc = (int)floorf(input_socFloat);       // floor to nearest lower int

  Serial.print("Input SoC:              ");
  Serial.println(input_socFloat);
  Serial.print("Previous SoC:           ");
  Serial.println(previous_soc);
  Serial.print("Input Charge Status:    ");
  Serial.println(input_chg);
  Serial.print("Previous Charge Status: ");
  Serial.println(previous_chg);

  if (input_soc < 0 || input_soc > 100 || (input_chg != 0 && input_chg != 1))
  {
    Serial.println("Invalid input. Format: <0-100> <0|1>");
    Serial.print(SERIAL_PROMPT);
    return;
  }

  if (lowBlinkState == true);
  {
    lowBlinkState = false;
    tft.invertDisplay(false);
  } 

  if (input_soc != previous_soc)                  // Redraw percentage only when the SoC value has changed.
  {
    drawPercentage(input_soc);
  }

  if (colourForSOC(input_soc) != colourForSOC(previous_soc) || input_chg != previous_chg) // Redraw the coloured border, coloured bars, and cross only when the colour has changed (i.e., the current SoC value is within a different range from before) or when the Charge Status has changed. (cross will only be drawn when the conditions within the function is fulfiled)
  {
    drawBorder(input_soc);
    drawBatteryIcon(input_soc, input_chg);
    drawBatteryBars(input_soc);
  }

  if (input_chg) // Redraw the lightning bolt symbol only when it's charging and the symbol hasn't been drawn already
  {
    if (isBatteryBarsWiped)
    {
      drawLightningBolt(); 
    }
  }

  statusMessage(input_soc, input_chg); // Updates current_status_message
  if (current_status_msg == "BATTERY LOW PLEASE CHARGE" || current_status_msg == "BATTERY FULL!") // Plays every time there's a user input and the status is still true (I want "Battery Low. Please Charge" and "Battery Full. Charging Complete" to play every time the SoC changes when the state is still true, for example charging from 95% to 96%, for discharging from 20% to 19%.)
  {
    playAudio(current_status_msg);
  }
  if (current_status_msg != previous_status_msg) // Redraw the status message only when the message has changed.
  {
    drawStatus(current_status_msg);
    if (current_status_msg == "BATTERY CHARGING") //"Charging" is only played when the status changes to it.
    {
      playAudio(current_status_msg);
    }
  }
  

  // update previous values
  previous_soc = input_soc;         
  previous_chg = input_chg;
  previous_status_msg = current_status_msg;

  Serial.print("Updated.\n");
  Serial.print(SERIAL_PROMPT);
}

// SETUP & LOOP
void setup()
{
  Serial.begin(9600);
  Serial.print("\nTFT LCD Battery Indicator ready.\n");
  Serial.print(SERIAL_PROMPT);

  uint16_t ID = tft.readID();
  tft.begin(ID);
  tft.setRotation(ROTATION);
  tft.fillScreen(BACKGROUND_COLOUR);

  pinMode(DYSV8F_IO0, OUTPUT);
  pinMode(DYSV8F_IO1, OUTPUT);
  digitalWrite(DYSV8F_IO0, HIGH); 
  digitalWrite(DYSV8F_IO1, HIGH);

  drawBatteryIcon(input_soc, input_chg);
}

void loop()
{
  tryReadSerialAndUpdateDisplay(); // reads values for three global variables input_soc, input_chg and current_status_msg

  // ── Blinking behaviour for low battery (< = 20) and not charging ───────
  uint32_t now = millis();
  if (!input_chg && input_soc <= 20 && now - lastBlinkMs >= 1000) {
      tft.invertDisplay(!lowBlinkState);
      lowBlinkState = !lowBlinkState;
      lastBlinkMs = now;
  }
}
