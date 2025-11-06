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

// #include <SPI.h>          // f.k. for Arduino-1.5.2
#include "Adafruit_GFX.h" // Hardware-specific library
#include <MCUFRIEND_kbv.h>
MCUFRIEND_kbv tft;

/* ---------- bring in every FreeFont header ---------- */
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMono24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoOblique9pt7b.h>
#include <Fonts/FreeMonoOblique12pt7b.h>
#include <Fonts/FreeMonoOblique18pt7b.h>
#include <Fonts/FreeMonoOblique24pt7b.h>
#include <Fonts/FreeMonoBoldOblique9pt7b.h>
#include <Fonts/FreeMonoBoldOblique12pt7b.h>
#include <Fonts/FreeMonoBoldOblique18pt7b.h>
#include <Fonts/FreeMonoBoldOblique24pt7b.h>

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansOblique9pt7b.h>
#include <Fonts/FreeSansOblique12pt7b.h>
#include <Fonts/FreeSansOblique18pt7b.h>
#include <Fonts/FreeSansOblique24pt7b.h>
#include <Fonts/FreeSansBoldOblique9pt7b.h>
#include <Fonts/FreeSansBoldOblique12pt7b.h>
#include <Fonts/FreeSansBoldOblique18pt7b.h>
#include <Fonts/FreeSansBoldOblique24pt7b.h>

#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerif24pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBold18pt7b.h>
#include <Fonts/FreeSerifBold24pt7b.h>
#include <Fonts/FreeSerifItalic9pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifItalic18pt7b.h>
#include <Fonts/FreeSerifItalic24pt7b.h>
#include <Fonts/FreeSerifBoldItalic9pt7b.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Fonts/FreeSerifBoldItalic18pt7b.h>
#include <Fonts/FreeSerifBoldItalic24pt7b.h>

#include <Fonts/Org_01.h>
#include <Fonts/Picopixel.h>
#include <Fonts/Tiny3x3a2pt7b.h>
#include <Fonts/TomThumb.h>

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

constexpr uint8_t  ROTATION          = 1;          // 0 for Portrait, 1 for Landscape (90 degrees), 2 for Reversed Portrait (180 degrees), 3 for Reversed Landscape (270 degrees))
constexpr uint16_t BACKGROUND_COLOUR = BLACK;
constexpr uint16_t FOREGROUND_COLOUR = WHITE;

/* ---------- Font table ---------- */
struct FontEntry {
    const GFXfont* font;
    const char*    name;
  };
  
  #define FE(font) { &font, #font }
  
  const FontEntry fonts[] PROGMEM = {
    /* FreeMono */
    FE(FreeMono9pt7b),   FE(FreeMono12pt7b),  FE(FreeMono18pt7b),  FE(FreeMono24pt7b),
    FE(FreeMonoBold9pt7b),  FE(FreeMonoBold12pt7b), FE(FreeMonoBold18pt7b), FE(FreeMonoBold24pt7b),
    FE(FreeMonoOblique9pt7b), FE(FreeMonoOblique12pt7b), FE(FreeMonoOblique18pt7b), FE(FreeMonoOblique24pt7b),
    FE(FreeMonoBoldOblique9pt7b), FE(FreeMonoBoldOblique12pt7b), FE(FreeMonoBoldOblique18pt7b), FE(FreeMonoBoldOblique24pt7b),
    /* FreeSans */
    FE(FreeSans9pt7b),  FE(FreeSans12pt7b),  FE(FreeSans18pt7b),  FE(FreeSans24pt7b),
    FE(FreeSansBold9pt7b),  FE(FreeSansBold12pt7b), FE(FreeSansBold18pt7b), FE(FreeSansBold24pt7b),
    FE(FreeSansOblique9pt7b), FE(FreeSansOblique12pt7b), FE(FreeSansOblique18pt7b), FE(FreeSansOblique24pt7b),
    FE(FreeSansBoldOblique9pt7b), FE(FreeSansBoldOblique12pt7b), FE(FreeSansBoldOblique18pt7b), FE(FreeSansBoldOblique24pt7b),
    /* FreeSerif */
    FE(FreeSerif9pt7b), FE(FreeSerif12pt7b), FE(FreeSerif18pt7b), FE(FreeSerif24pt7b),
    FE(FreeSerifBold9pt7b), FE(FreeSerifBold12pt7b), FE(FreeSerifBold18pt7b), FE(FreeSerifBold24pt7b),
    FE(FreeSerifItalic9pt7b), FE(FreeSerifItalic12pt7b), FE(FreeSerifItalic18pt7b), FE(FreeSerifItalic24pt7b),
    FE(FreeSerifBoldItalic9pt7b), FE(FreeSerifBoldItalic12pt7b), FE(FreeSerifBoldItalic18pt7b), FE(FreeSerifBoldItalic24pt7b),
    /* Others */
    FE(Org_01), FE(Picopixel), FE(Tiny3x3a2pt7b), FE(TomThumb)
  };

constexpr uint16_t N_FONTS = sizeof(fonts) / sizeof(fonts[0]);

/* ---------- helpers ---------- */
void showFont(const GFXfont* f, const char* name)
{
  const char* sample = "AaBbCc 0123 !@#?";
  tft.fillScreen(BACKGROUND_COLOUR);

  /* ─ name (default font) ───────────────────────── */
  tft.setFont();                      // back to classic 5×7
  tft.setTextSize(2);
  tft.setTextColor(FOREGROUND_COLOUR, BACKGROUND_COLOUR);
  tft.setCursor(4, 6);
  tft.print(name);

  /* ─ sample string in target font (centred) ───── */
  tft.setFont(f);
  tft.setTextColor(FOREGROUND_COLOUR, BACKGROUND_COLOUR);      
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(sample, 0, 0, &x1, &y1, &w, &h);
  int16_t cx = (tft.width()  - w) / 2;
  int16_t cy = (tft.height() + h) / 2;     // baseline
  tft.setCursor(cx, cy);
  tft.print(sample);

  delay(2000);
}

void setup()
{
  Serial.begin(9600);
  uint16_t ID = tft.readID();
  tft.begin(ID);
  tft.setRotation(ROTATION);
  tft.fillScreen(BACKGROUND_COLOUR);
  Serial.print("Found LCD ID 0x"); Serial.println(ID, HEX);
  Serial.println("Starting font showcase…");
}

void loop()
{
  for (uint16_t i = 0; i < N_FONTS; ++i)
  {
    FontEntry fe;
    memcpy_P(&fe, fonts + i, sizeof(fe));  // fetch from PROGMEM
    showFont(fe.font, fe.name);
  }
}
