// All the mcufriend.com UNO shields have the same pinout.
// i.e. control pins A0-A4.  Data D2-D9.  microSD D10-D13.
// Touchscreens are normally A1, A2, D7, D6 but the order varies
//
// This demo should work with most Adafruit TFT libraries
// If you are not using a shield,  use a full Adafruit constructor()
// e.g. Adafruit_TFTLCD tft(LCD_CS, LCD_CD, LCD_WR, LCD_RD, LCD_RESET);

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

// Original pin connections to Arduino UNO
// You don't actually have to define them unless you are not using the mcufriend sheild.
#define LCD_CS A3 // Chip Select goes to Analog 3
#define LCD_CD A2 // Command/Data goes to Analog 2
#define LCD_WR A1 // LCD Write goes to Analog 1
#define LCD_RD A0 // LCD Read goes to Analog 0
#define LCD_RESET A4 // Can alternately just connect to Arduino's reset pin

#include <SPI.h>          // f.k. for Arduino-1.5.2
#include "Adafruit_GFX.h"// Hardware-specific library
#include <MCUFRIEND_kbv.h>
MCUFRIEND_kbv tft;
//#include <Adafruit_TFTLCD.h>
//Adafruit_TFTLCD tft(LCD_CS, LCD_CD, LCD_WR, LCD_RD, LCD_RESET);

// Assign human-readable names to some common 16-bit color values:
#define	BLACK   0x0000
#define	BLUE    0x001F
#define	RED     0xF800
#define	GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

#ifndef min
//If min is not defined
#define min(a, b) (((a) < (b)) ? (a) : (b))
//GENERAL KNOWLEDGE! This is a function-like macro
//GENERAL KNOWLEDGE! If a < b, then return a, else return b.
#endif

// Function and variable declarations
void setup(void);
void loop(void);
unsigned long testFillScreen();
//GENERAL KNOWLEDGE! unsigned long is 32 bits (4 bytes)
unsigned long testText();
unsigned long testLines(uint16_t color);
unsigned long testFastLines(uint16_t color1, uint16_t color2);
unsigned long testRects(uint16_t color);
unsigned long testFilledRects(uint16_t color1, uint16_t color2);
unsigned long testFilledCircles(uint8_t radius, uint16_t color);
unsigned long testCircles(uint8_t radius, uint16_t color);
unsigned long testTriangles();
unsigned long testFilledTriangles();
unsigned long testRoundRects();
unsigned long testFilledRoundRects();
void progmemPrint(const char *str);
void progmemPrintln(const char *str);

void runtests(void);

uint16_t g_identifier;

extern const uint8_t hanzi[];
void showhanzi(unsigned int x, unsigned int y, unsigned char index)
{
    uint8_t i, j, c, first = 1;
    uint8_t *temp = (uint8_t*)hanzi;
    uint16_t color;
    tft.setAddrWindow(x, y, x + 31, y + 31); //设置区域
    temp += index * 128;
    for (j = 0; j < 128; j++)
    {
        c = pgm_read_byte(temp);
        for (i = 0; i < 8; i++)
        {
            if ((c & (1 << i)) != 0)
            {
                color = RED;
            }
            else
            {
                color = BLACK;
            }
            tft.pushColors(&color, 1, first);
            first = 0;
        }
        temp++;
    }
}

void setup(void) {
    Serial.begin(9600);
    uint32_t when = millis(); // Current time in milliseconds, by the number of milliseconds since the Arduino board began running the current program.
    //    while (!Serial) ;   // hangs a Leonardo until you connect a Serial　(Leonardo refers to the Arduino Leonardo board)
    if (!Serial) delay(5000);           //allow some time for Leonardo
    Serial.println("Serial took " + String((millis() - when)) + "ms to start"); // Printed to the Serial Monitor, not the TFT LCD display. Those with tft.println will print to the TFT LCD display.
    //    tft.reset();                 //hardware reset
    uint16_t ID = tft.readID(); //
    Serial.print("ID = 0x");
    Serial.println(ID, HEX);
    if (ID == 0xD3D3) ID = 0x9481; // write-only shield
//    ID = 0x9329;                             // force ID
    tft.begin(ID);
}

#if defined(MCUFRIEND_KBV_H_) // defined in MCUFRIEND_kbv.h
uint16_t scrollbuf[320];    // my biggest screen is 320x480
#define READGRAM(x, y, buf, w, h)  tft.readGRAM(x, y, buf, w, h)
#else
uint16_t scrollbuf[320];    // Adafruit only does 240x320
// Adafruit can read a block by one pixel at a time
int16_t  READGRAM(int16_t x, int16_t y, uint16_t *block, int16_t w, int16_t h)
{
    uint16_t *p;
    for (int row = 0; row < h; row++) {
        p = block + row * w;
        for (int col = 0; col < w; col++) {
            *p++ = tft.readPixel(x + col, y + row);
        }
    }
}
#endif

// Note that even though the function name has "Scroll" in it, it does not actually scroll the defined rectangular window by itsef, it only shifts/displaces/moves it, a for loop or something is needed to actually scroll the window.  
// x and y defines the top-left corner of the rectangular window to shift, measured from the current origin (top-left corner) of the display with the current orientation.
// wid and ht is the width and height of the rectangular window to shift in pixels.
// dx is the number of pixels to shift horizontally with wrap-around (+ve: shift left, 0: no horizontal shift, -ve: not handled)) 
// dy is the number of pixels to shift vertically with wrap-around (+ve: shift up, 0: no vertical shift, -ve: not handled).
// buf is a pointer to a buffer that will be used to store the pixels in the window before shifting.
// GENERAL KNOWLEDGE! I think just knowing how to use the function is enough, no need to dive too deep into the implementation details, don't torture yourself in a rabbit hole.
void windowScroll(int16_t x, int16_t y, int16_t wid, int16_t ht, int16_t dx, int16_t dy, uint16_t *buf)
{
    if (dx) for (int16_t row = 0; row < ht; row++) {
            READGRAM(x, y + row, buf, wid, 1); 
            tft.setAddrWindow(x, y + row, x + wid - 1, y + row); 
            tft.pushColors(buf + dx, wid - dx, 1);
            tft.pushColors(buf + 0, dx, 0);
        }
    if (dy) for (int16_t col = 0; col < wid; col++) {
            READGRAM(x + col, y, buf, 1, ht);
            tft.setAddrWindow(x + col, y, x + col, y + ht - 1);
            tft.pushColors(buf + dy, ht - dy, 1);
            tft.pushColors(buf + 0, dy, 0);
        }
}

// printmsg is a convenient function to print left-to-right horizontal messages (according to the current orientation) with your desired text colour and text background colour,
// Instead of having to setTextColor, setCursor, and println every time you want to print a message.
// row is the y-coordinate (vertical coordinates, according to screen orientation) in pixels where the message will be printed.
// msg is the message to be printed, a string (remember, a string is a char array).
void printmsg(int row, const char *msg)
{
    tft.setTextColor(YELLOW, BLACK); // First parameter is 16-bit colour to draw text with, second parameter is the 16-bit colour to draw background/fill with
    tft.setCursor(0, row); // First parameter is the x-coordinate, second parameter is the y-coordinate, both in pixels.
    tft.println(msg);
}

void loop(void) {

    // Just some self-declared and initialised variables.
    uint8_t aspect;
    uint16_t pixel;
    const char *aspectname[] = {
        "PORTRAIT", "LANDSCAPE", "PORTRAIT_REV", "LANDSCAPE_REV"
    };
    const char *colorname[] = { "BLUE", "GREEN", "RED", "GRAY" };
    uint16_t colormask[] = { 0x001F, 0x07E0, 0xF800, 0xFFFF }; // Blue, Gree, Red, White/Grey
    uint16_t dx, rgb, n, wid, ht, msglin;

    tft.setRotation(0); // 0 for Portrait, 1 for Landscape (90 degrees), 2 for Portrait (reversed, 180 degrees), 3 for Landscape (reversed, 270 degrees))

    runtests(); //NOTE - Code that displays stuff: The first 12 seconds, with different colours, lines, geometries and shit.

    delay(2000);

    if (tft.height() > 64) { // Height of the display in pixels, accounting for current rotation
        for (uint8_t cnt = 0; cnt < 4; cnt++) { // Outer for loop for the 4 colours and orientation (Blue-Portrait first, then Green-Lanscape, Red-Reversed Portrait and Grey-Reversed Landscape)
            aspect = (cnt + 0) & 3; // Bitwise AND, 0 & 3 = 0, 1 & 3 = 1, 2 & 3 = 2, 3 & 3 = 3 (4 & 3 = 0, 5 & 3 = 1, 6 & 3 = 2, 7 & 3 = 3, so this bitwise AND operation forces aspect to be between [0, 3] only, kinda redundant since cnt is already locked in [0, 3] tho)
            tft.setRotation(aspect);
            wid = tft.width();
            ht = tft.height();
            msglin = (ht > 160) ? 200 : 112; // If the height of the display > 160 pixels, then msglin = 200 pixels, else msglin = 112 pixels.

            // The below is the remaining of the display test, with colour grades, scrolling, the Tencent penguin and shit
            
            testText(); //NOTE - Code that displays stuff: The Hello World!, 123.45, DEADBEEF, the paragraph of Vogon poetry
            
            dx = wid / 32; // Divides the width of the display into 32 parts
            for (n = 0; n < 32; n++) { // Inner for loop for the 32 colour grades
                rgb = n * 8;
                rgb = tft.color565(rgb, rgb, rgb); // This return colours along the grey scale (black to white) in 16-bit values
                tft.fillRect(n * dx, 48, dx, 63, rgb & colormask[aspect]); // NOTE - Code that displays stuff: Prints the colour grades in filled rectangles
                // In order of the parameters:
                // x-coordinate of the left-hand edge of the rectangle to be filled (measured from the display origin, which is the top-left corner of the display, according to the current display orientation),
                // y-coordinate of the top edge of the rectangle to be filled (Hence the first two parameters is the top-left corner of the rectangle to be filled)
                // width (number of columns) of the rectangle in pixels (-ve values mean extend up)
                // height (number of rows) of the rectangle in pixels (-ve values mean extend left)
                // 16-bit RGB565 colour to fill the rectangle with.
            }
            
            tft.drawRect(0, 48 + 63, wid, 1, WHITE); //NOTE - Code that displays stuff: The white line (1 pixel tall rectangular box) below the colour grades.
            // This code actually draws a rectangle with no fill colour
            // In order of the parameters:
            // Top left corner x coordinate (left edge) 
            // Top left corner y coordinate (top edge)
            // Width in pixels
            // Height in pixels
            // Colour 16-bit 5-6-5 Color to draw with
            
            tft.setTextSize(2);
            tft.setTextColor(colormask[aspect], BLACK); // Colour of text then colour of background
            tft.setCursor(0, 72);
            tft.print(colorname[aspect]); //NOTE - Code that displays stuff: Print the name of the current colour
            
            tft.setTextColor(WHITE); // No background colour (transparent background)
            tft.println(" COLOR GRADES"); //NOTE - Code that displays stuff

            tft.setTextColor(WHITE, BLACK);
            printmsg(184, aspectname[aspect]); //NOTE - Code that displays stuff: Print the name of the current display orientation
            // Parameters: y-coordinate (vertical coordinates, according to display orientation) in pixels where the message will be printed.
            // message to be printed (in this case the orientation of the display), a string (remember, a string is a char array).

            delay(1000);

            tft.drawPixel(0, 0, YELLOW); //NOTE - Code that displays stuff: Draw exactly one pixel at (x, y) (in this case the top left corner depending on the orientation) with the colour being input (in this case, yellow).
            pixel = tft.readPixel(0, 0); // Gives you the colour of the pixel at (x, y) (in this case the top left corner depending on the orientation).
            Serial.println(pixel, HEX); // Prints the colour of the pixel in hexadecimal format to the Serial Monitor.
            //! I can't fucking see the pixel though, but I guess it's there because the right colour is printed to the Serial Monitor.

            tft.setTextSize((ht > 160) ? 2 : 1); //for messages

#if defined(MCUFRIEND_KBV_H_) //NOTE - Code that displays stuff: Displays the Tencent penguin, WiFi icon, or an emoji, depending on the conditional statements.
#if 1 // If MCUFRIEND_KBV_H_ is defined (which it is, in MCUFRIEND_kbv.h), then this inner condition will be examined. However, since it is hardcoded to be always true, it will always be executed and the sunsequent elif's are ignored.
            extern const uint8_t penguin[]; // Declares that there is a read-only array of bytes (unsigned 8-bit integers) called penguin elsewhere
            tft.setAddrWindow(wid - 40 - 40, 20 + 0, wid - 1 - 40, 20 + 39);
            // Tells the display controller where the upcoming pixel stream should go.
            // (wid - 40 - 40, 20)` becomes the top-left corner.
            // (wid - 1 - 40, 20 + 39)` is the bottom-right corner.
            // Thus the window is exactly 40 × 40 pixels.
            // `wid` is the current display width according to the current orientation
            // Subtracting twice 40 leaves room for another icon at the far right.
            // From now until the window is changed again every pixel sent across the bus is auto-incremented across that 40 × 40 rectangle, no extra coordinates needed.
            tft.pushColors(penguin, 1600, 1);
            // Push the pixels
            // Parameters in order: data = penguin (the unsigned 8-bit integers array of the Tencent penguin); number of pixels to send = 1600 (40 × 40); first = 1 (true, this is the first burst in this window, so the function issues the display’s Memory Write command before streaming the bytes. Subsequent bursts to the same window can pass `first = 0` to skip the command and save time.)
#elif 1
            extern const uint8_t wifi_full[];
            tft.setAddrWindow(wid - 40 - 40, 20 + 0, wid - 40 - 40 + 31, 20 + 31);
            tft.pushColors(wifi_full, 1024, 1, true);
#elif 1
            extern const uint8_t icon_40x40[];
            tft.setAddrWindow(wid - 40 - 40, 20 + 0, wid - 1 - 40, 20 + 39);
            tft.pushColors(icon_40x40, 1600, 1);
#endif // End of #ifdef #endif preprocessor directive

            tft.setAddrWindow(0, 0, wid - 1, ht - 1); // Encompass the whole display
            if (aspect & 1) tft.drawRect(wid - 1, 0, 1, ht, WHITE);
            // Remember from above, aspect is locked in [0, 3] only
            // so (aspect & 1) will be 0 for Blue-Portrait (0: 00) and Red-Portrait Reversed (2: 10), and 1 for Green-Landscape (1: 01) and Grey-Landscape Reversed (3: 11).
            // In order of the parameters of the drawRect function: Top left corner (left edge) x coordinate, Top left corner (top edge) y coordinate, Width in pixels, Height in pixels, colour 16-bit 5-6-5 Color to draw with
            //NOTE - Code that displays stuff: so for Green-Lanscape and Grey-Reversed Landscape, this draw a straight vertical line (1 pixel wide rectangle) on the right edge of the display, from the top to the bottom.
            else tft.drawRect(0, ht - 1, wid, 1, WHITE);
            //NOTE - Code that displays stuff: For Blue-Portrait and Red-Portrait Reversed, this draw a straight horizontal line (1 pixel wide rectangle) on the bottom edge of the display, from left to right.
            
            printmsg(msglin, "VERTICAL SCROLL UP"); //NOTE - Code that displays stuff: Print the message "VERTICAL SCROLL UP" in yellow with a black background at the row specified by msglin (pixel 200 or 112 along the vertical axis from the top left corner, depending on the height of the display).
            uint16_t maxscroll;
            if (tft.getRotation() & 1) maxscroll = wid;
            // getRotation returns the number of the current orientation of the display, 0 for Portrait, 1 for Landscape (90 degrees), 2 for Portrait (reversed, 180 degrees), 3 for Landscape (reversed, 270 degrees)
            //GENERAL KNOWLEDGE! Bitwise AND a number with 1 isolates the LSB (bit 0) of the number and kills off (sets to 0) all other bits of the number.
            //GENERAL KNOWLEDGE! In binary, since odd numbers end with 1 and even numbers end with 0,
            //GENERAL KNOWLEDGE! this means that if the number is odd (1: Landscape or 3: Reversed Landscape), then the result of bitwise AND with 1 will be 1,
            //GENERAL KNOWLEDGE! if the number is even (0: Portrait or 2: Reversed Portrait), then the result of bitwise AND with 1 will be 0.
            // Hence, this if-else conditional statement sets maxscroll to the width of the display in pixels if the display is in Landscape or Reversed Landscape orientation,
            // and to the height of the display in pixels if it is in Portrait or Reversed Portrait orientation.
            else maxscroll = ht;
            for (uint16_t i = 1; i <= maxscroll; i++) {
                tft.vertScroll(0, maxscroll, i);  //NOTE - Code that displays stuff: Scrolling the entire display up/left
                // vertScroll moves a rectangular strip of the display along the long side of the screen (up or down in Portrait, left or right in Landscape)
                // Parameters in order:
                // top: the height of the top fixed area in pixels (a band at the top that never scrolls) (for Portrait and Reversed Portrait, the top fixed area is at their respective top edges; for Landscape and Reversed Landscape, the top fixed area is at their respective left edges)
                // scrollines: the height of the scroll area in pixels from the top fixed area (from this the bottom fixed area is whatever remains = full-screen height/width - top - scrollines)
                // offset: how far to scroll the scroll area in pixels (+n scrolls the area up/left, -n scrolls the area down/right, 0 disables scrolling) (must satisfy -scrollines < offset < scrollines, if not, it is treated as 0))
                delay(10); // Without the delay, the scroll will be so fast you can't see it
            }

            delay(1000);

			printmsg(msglin, "VERTICAL SCROLL DN"); //NOTE - Code that displays stuff: Print the message "VERTICAL SCROLL DN" in yellow with a black background at the row specified by msglin (pixel 200 or 112 along the vertical axis from the top left corner, depending on the height of the display).
            for (uint16_t i = 1; i <= maxscroll; i++) {
                tft.vertScroll(0, maxscroll, 0 - (int16_t)i); //NOTE - Code that displays stuff: Scrolling the entire display down/right
                delay(10);
            }

			tft.vertScroll(0, maxscroll, 0);

            printmsg(msglin, "SCROLL DISABLED   "); //NOTE - Code that displays stuff: Print the message "SCROLL DISABLED" in yellow with a black background at the row specified by msglin (pixel 200 or 112 along the vertical axis from the top left corner, depending on the height of the display).

            delay(1000);

            if ((aspect & 1) == 0) { // Portrait
                tft.setTextColor(BLUE, BLACK); // Overriden by the setTextColor in the printmsg function
                printmsg(msglin, "ONLY THE COLOR BAND"); //NOTE - Code that displays stuff: Print the message "ONLY THE COLOR BAND" in yellow with a black background at the row specified by msglin (pixel 200 or 112 along the vertical axis from the top left corner, depending on the height of the display).
                for (uint16_t i = 1; i <= 64; i++) { // The colour band is 63 pixels tall, starting from pixel 48 on the vertical axis from the top of the display.
                    tft.vertScroll(48, 64, i);
                    delay(20);
                }
                delay(1000);
            }
#endif
            tft.setTextColor(YELLOW, BLACK);
            if (pixel == YELLOW) {
                printmsg(msglin, "SOFTWARE SCROLL    "); //NOTE - Code that displays stuff: Print the message "SOFTWARE SCROLL" in yellow with a black background at the row specified by msglin (pixel 200 or 112 on the vertical axis from the top left corner, depending on the height of the display).
#if 0
                // diagonal scroll of block
                for (int16_t i = 45, dx = 2, dy = 1; i > 0; i -= dx) {
                    windowScroll(24, 8, 90, 40, dx, dy, scrollbuf);
                }
#else
                // plain horizontal scroll of block
                n = (wid > 320) ? 320 : wid; // This is why when in the Landscape orientations, the horizontal software scroll only scrolls within a portion of the entire width.
                for (int16_t i = n, dx = 4, dy = 0; i > 0; i -= dx) { // NOTE - Code that displays stuff: Shift the defined rectangular window (which is 320 pixels wide or as wide as the width of the screen, whichever is smaller) to the left by 4 pixels at a time, doing so until the entire rectangular window has been shifted out of the screen, thereby achieving a horizontal scroll effect.
                    windowScroll(0, 200, n, 16, dx, dy, scrollbuf);
                    // Note that even though the function name has "Scroll" in it, it does not actually scroll the defined rectangular window by itsef, it only shifts/displaces/moves it, a for loop or something is needed to actually scroll the window like how it is done here.
                    // Parameters in order:
                    // x and y defines the top-left corner of the rectangular window to shift, measured from the current origin (top-left corner) of the display with the current orientation.
                    // wid and ht is the width and height of the rectangular window to shift in pixels.
                    // dx is the number of pixels to shift horizontally with wrap-around (+ve: shift left, 0: no horizontal shift, -ve: not handled)) 
                    // dy is the number of pixels to shift vertically with wrap-around (+ve: shift up, 0: no vertical shift, -ve: not handled).
                    // buf is a pointer to a buffer that will be used to store the pixels in the window before shifting.
                }
#endif
            }
            else if (pixel == CYAN)
                tft.println("readPixel() reads as BGR"); //NOTE - Code that displays stuff
            else if ((pixel & 0xF8F8) == 0xF8F8)
                tft.println("readPixel() should be 24-bit"); //NOTE - Code that displays stuff
            else {
                tft.print("readPixel() reads 0x"); //NOTE - Code that displays stuff
                tft.println(pixel, HEX); //NOTE - Code that displays stuff
            }
            delay(5000);
        }
    }
    printmsg(msglin, "INVERT DISPLAY "); //NOTE - Code that displays stuff: Print the message "INVERT DISPLAY" in yellow with a black background at the row specified by msglin (pixel 200 or 112 on the vertical axis from the top left corner, depending on the height of the display).
    tft.invertDisplay(true); //NOTE - Code that displays stuff: Invert the colours of the entire display
    delay(2000);
    tft.invertDisplay(false);
}

//GENERAL KNOWLEDGE! A struct (or structure) is a user-defined data type, basically a class without a constructor and methods, just attributes (or the correct term here is: members).
//GENERAL KNOWLEDGE! Members can be of different data types, including other structs, arrays, pointers, etc.
//GENERAL KNOWLEDGE! No memory is allocated until you declare a variable of the struct type.
typedef struct {
    PGM_P msg; // PGM_P is a pointer to a string stored in program memory (flash memory) instead of RAM, so that it does not take up RAM space.
    uint32_t ms; // "ms" is misleading as the display tests actually return the time taken in microseconds, not milliseconds.
} TEST; //GENERAL KNOWLEDGE! TEST is an alias of this struct, used to create instances of this struct like the immediate line below.
TEST result[12]; //GENERAL KNOWLEDGE! An array of 12 TEST structs known as "result", each with a message and a time in milliseconds.

#define RUNTEST(n, str, test) { result[n].msg = PSTR(str); result[n].ms = test; delay(500); }

void runtests(void)
{
    uint8_t i, len = 24, cnt;
    uint32_t total;
    // Populate each of the 12 TEST structs in the result array with a message and the time taken to execute the corresponding test function (after executing the corresponding test function).
    RUNTEST(0, "FillScreen               ", testFillScreen()); //NOTE - Code that displays stuff
    RUNTEST(1, "Text                     ", testText()); //NOTE - Code that displays stuff
    RUNTEST(2, "Lines                    ", testLines(CYAN)); //NOTE - Code that displays stuff
    RUNTEST(3, "Horiz/Vert Lines         ", testFastLines(RED, BLUE)); //NOTE - Code that displays stuff
    RUNTEST(4, "Rectangles (outline)     ", testRects(GREEN)); //NOTE - Code that displays stuff
    RUNTEST(5, "Rectangles (filled)      ", testFilledRects(YELLOW, MAGENTA)); //NOTE - Code that displays stuff
    RUNTEST(6, "Circles (filled)         ", testFilledCircles(10, MAGENTA)); //NOTE - Code that displays stuff
    RUNTEST(7, "Circles (outline)        ", testCircles(10, WHITE)); //NOTE - Code that displays stuff
    RUNTEST(8, "Triangles (outline)      ", testTriangles()); //NOTE - Code that displays stuff
    RUNTEST(9, "Triangles (filled)       ", testFilledTriangles()); //NOTE - Code that displays stuff
    RUNTEST(10, "Rounded rects (outline)  ", testRoundRects()); //NOTE - Code that displays stuff
    RUNTEST(11, "Rounded rects (filled)   ", testFilledRoundRects()); //NOTE - Code that displays stuff

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    // The fillScreen function is the fillRect function where the rectangle to be filled is the entire display.

    tft.setTextColor(GREEN);

    tft.setCursor(0, 0);

    uint16_t wid = tft.width(); // Get width of the display in pixels, accounting for current rotation

    if (wid > 176) {
        tft.setTextSize(2);
#if defined(MCUFRIEND_KBV_H_)
        tft.print("MCUFRIEND "); //NOTE - Code that displays stuff
#if MCUFRIEND_KBV_H_ != 0 // MCUFRIEND_KBV_H_ is defined to be 300 in MCUFRIEND_kbv.h: #define MCUFRIEND_KBV_H_   300
        tft.print(0.01 * MCUFRIEND_KBV_H_, 2); //NOTE - Code that displays stuff
        // The 2 at the second parameter of print is the number of decimal places.
#else
        tft.print("for"); //NOTE - Code that displays stuff
#endif
        tft.println(" UNO"); //NOTE - Code that displays stuff
#else
        tft.println("Adafruit-Style Tests"); //NOTE - Code that displays stuff
#endif
    } else len = wid / 6 - 8; // Executes only when the display is narrower than 176 pixels.
    // A character printed at text size 1 is 6 pixels wide (1 is 6x8, 2 is 12x16, 3 is 18x24, etc.)
    // Hence wid / 6 is the maximum number of characters that can be printed in a single line of narrower than 176 pixels.
    // - 8 to leave 8 characters for the time field.
    // Hence, len is the maximum number of characters that can be printed in a single line of the display after accounting for 8 characters worth of the time field, if the display is narrower than 176 pixels.
    // In hindsight, len is the maximum number of characters allocated for the msg field of each of the 12 TEST structs in the result array, i.e., the name of the display tests, if the display is narrower than 176 pixels.

    tft.setTextSize(1);
    total = 0;
    for (i = 0; i < 12; i++) { // For each of the 12 TEST structs in the result array, i.e., for each of the 12 display tests that were run above
        PGM_P str = result[i].msg; 
        char c;
        if (len > 24) { // If the display is wide enough (> 192 pixels wide), front-pad the display test names with number indexing, e.g. 0:, 1:, 2:, etc, for easy reading.
            // However, this conditional statement will never be executed since len is hard capped to 24 above, len can only be 24 (when wid > 176) or smaller than 21 (when wid <= 176)
            if (i < 10) tft.print(" ");
            tft.print(i);
            tft.print(": ");
        }
        uint8_t cnt = len;
        while ((c = pgm_read_byte(str++)) && cnt--) tft.print(c); //NOTE - Code that displays stuff: Prints each display test name
        tft.print(" ");
        tft.println(result[i].ms); //NOTE - Code that displays stuff: Prints the time taken to execute each display test in microseconds.
        total += result[i].ms;
    }

    tft.setTextSize(2);
    tft.print("Total:"); //NOTE - Code that displays stuff
    tft.print(0.000001 * total); //NOTE - Code that displays stuff
    tft.println("sec"); //NOTE - Code that displays stuff

    g_identifier = tft.readID();
    tft.print("ID: 0x"); //NOTE - Code that displays stuff
    tft.println(tft.readID(), HEX); //NOTE - Code that displays stuff

//    tft.print("Reg(00):0x");
//    tft.println(tft.readReg(0x00), HEX);

    tft.print("F_CPU:"); //NOTE - Code that displays stuff
    tft.print(0.000001 * F_CPU); //NOTE - Code that displays stuff
#if defined(__OPTIMIZE_SIZE__)
    tft.println("MHz -Os"); //NOTE - Code that displays stuff
#else
    tft.println("MHz"); //NOTE - Code that displays stuff
#endif

    delay(10000);
}

// Standard Adafruit tests.  will adjust to screen size

unsigned long testFillScreen() {
    unsigned long start = micros();
    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    tft.fillScreen(RED); //NOTE - Code that displays stuff
    tft.fillScreen(GREEN); //NOTE - Code that displays stuff
    tft.fillScreen(BLUE); //NOTE - Code that displays stuff
    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    return micros() - start;
}

unsigned long testText() {
    unsigned long start;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff

    start = micros();

    tft.setCursor(0, 0);
    tft.setTextColor(WHITE);  tft.setTextSize(1); // 1 is default 6x8, 2 is 12x16, 3 is 18x24, etc.
    tft.println("Hello World!"); //NOTE - Code that displays stuff

    tft.setTextColor(YELLOW); tft.setTextSize(2);
    tft.println(123.45); //NOTE - Code that displays stuff

    tft.setTextColor(RED);    tft.setTextSize(3);
    tft.println(0xDEADBEEF, HEX); //NOTE - Code that displays stuff

    tft.println();

    tft.setTextColor(GREEN);
    tft.setTextSize(5);
    tft.println("Groop"); //NOTE - Code that displays stuff
    tft.setTextSize(2);
    tft.println("I implore thee,"); //NOTE - Code that displays stuff
    tft.setTextSize(1);
    tft.println("my foonting turlingdromes."); //NOTE - Code that displays stuff
    tft.println("And hooptiously drangle me"); //NOTE - Code that displays stuff
    tft.println("with crinkly bindlewurdles,"); //NOTE - Code that displays stuff
    tft.println("Or I will rend thee"); //NOTE - Code that displays stuff
    tft.println("in the gobberwarts"); //NOTE - Code that displays stuff
    tft.println("with my blurglecruncheon,"); //NOTE - Code that displays stuff
    tft.println("see if I don't!"); //NOTE - Code that displays stuff

    return micros() - start; // In all display test functions here, the time taken to execute the function is returned in microseconds.
}

unsigned long testLines(uint16_t color) {
    unsigned long start, t;
    int           x1, y1, x2, y2,
                  w = tft.width(),
                  h = tft.height();

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff

    x1 = y1 = 0;
    y2    = h - 1;
    start = micros();
    for (x2 = 0; x2 < w; x2 += 6) tft.drawLine(x1, y1, x2, y2, color); //NOTE - Code that displays stuff
    /*
    void Adafruit_GFX::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    @brief    Draw a line
    @param    x0  Start point x coordinate
    @param    y0  Start point y coordinate
    @param    x1  End point x coordinate
    @param    y1  End point y coordinate
    @param    color 16-bit 5-6-5 Color to draw with
    */
    x2    = w - 1;
    for (y2 = 0; y2 < h; y2 += 6) tft.drawLine(x1, y1, x2, y2, color); //NOTE - Code that displays stuff
    t     = micros() - start; // fillScreen doesn't count against timing

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff

    x1    = w - 1;
    y1    = 0;
    y2    = h - 1;
    start = micros();
    for (x2 = 0; x2 < w; x2 += 6) tft.drawLine(x1, y1, x2, y2, color); //NOTE - Code that displays stuff
    x2    = 0;
    for (y2 = 0; y2 < h; y2 += 6) tft.drawLine(x1, y1, x2, y2, color); //NOTE - Code that displays stuff
    t    += micros() - start;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff

    x1    = 0;
    y1    = h - 1;
    y2    = 0;
    start = micros();
    for (x2 = 0; x2 < w; x2 += 6) tft.drawLine(x1, y1, x2, y2, color); //NOTE - Code that displays stuff
    x2    = w - 1;
    for (y2 = 0; y2 < h; y2 += 6) tft.drawLine(x1, y1, x2, y2, color); //NOTE - Code that displays stuff
    t    += micros() - start;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff

    x1    = w - 1;
    y1    = h - 1;
    y2    = 0;
    start = micros();
    for (x2 = 0; x2 < w; x2 += 6) tft.drawLine(x1, y1, x2, y2, color); //NOTE - Code that displays stuff
    x2    = 0;
    for (y2 = 0; y2 < h; y2 += 6) tft.drawLine(x1, y1, x2, y2, color); //NOTE - Code that displays stuff

    return micros() - start;
}

unsigned long testFastLines(uint16_t color1, uint16_t color2) {
    unsigned long start;
    int           x, y, w = tft.width(), h = tft.height();

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    start = micros();
    for (y = 0; y < h; y += 5) tft.drawFastHLine(0, y, w, color1); //NOTE - Code that displays stuff
    // drawFastHLine uses the fillRect function to draw a horizontal line (1 pixel tall rectangle)
    // Parameters in order (Similar to fillRect):
    // x-coordinate of the left-most pixel of the horizontal line
    // y-coordinate of the left-most pixel of the horizontal line
    // width (number of columns, length) of the horizontal line
    // 16-bit RGB565 colour of the horizontal line
    for (x = 0; x < w; x += 5) tft.drawFastVLine(x, 0, h, color2); //NOTE - Code that displays stuff
    // drawFastVLine uses the fillRect function to draw a vertical line (1 pixel wide rectangle)
    // Parameters in order (Similar to fillRect):
    // x-coordinate of the top-most pixel of the vertical line
    // y-coordinate of the top-most pixel of the vertical line
    // height (number of rows, length) of the vertical line
    // 16-bit RGB565 colour of the vertical line

    // The drawFastHLine and drawFastVLine functions are optimized versions of the drawLine function that use the fillRect function to draw horizontal and vertical lines respectively, which is faster than using the drawLine function.
    //! You can actually see the speed difference, it's insane.
    // Though, as the name of the functions suggest, you are only limited to drawing horizontal and vertical lines only, not diagonal lines.

    return micros() - start;
}

unsigned long testRects(uint16_t color) {
    unsigned long start;
    int           n, i, i2,
                  cx = tft.width()  / 2,
                  cy = tft.height() / 2;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    n     = min(tft.width(), tft.height());
    start = micros();
    for (i = 2; i < n; i += 6) {
        i2 = i / 2;
        tft.drawRect(cx - i2, cy - i2, i, i, color); //NOTE - Code that displays stuff
    }

    return micros() - start;
}

unsigned long testFilledRects(uint16_t color1, uint16_t color2) {
    unsigned long start, t = 0;
    int           n, i, i2,
                  cx = tft.width()  / 2 - 1,
                  cy = tft.height() / 2 - 1;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    n = min(tft.width(), tft.height());
    for (i = n; i > 0; i -= 6) {
        i2    = i / 2;
        start = micros();
        tft.fillRect(cx - i2, cy - i2, i, i, color1); //NOTE - Code that displays stuff
        t    += micros() - start;
        // Outlines are not included in timing results
        tft.drawRect(cx - i2, cy - i2, i, i, color2); //NOTE - Code that displays stuff
    }

    return t;
}

unsigned long testFilledCircles(uint8_t radius, uint16_t color) {
    unsigned long start;
    int x, y, w = tft.width(), h = tft.height(), r2 = radius * 2;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    start = micros();
    for (x = radius; x < w; x += r2) {
        for (y = radius; y < h; y += r2) {
            tft.fillCircle(x, y, radius, color); //NOTE - Code that displays stuff
            /* void Adafruit_GFX::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) 
            @brief    Draw a circle with filled color
            @param    x0   Center-point x coordinate
            @param    y0   Center-point y coordinate
            @param    r   Radius of circle
            @param    color 16-bit 5-6-5 Color to fill with
            */
        }
    }

    return micros() - start;
}

unsigned long testCircles(uint8_t radius, uint16_t color) {
    unsigned long start;
    int           x, y, r2 = radius * 2,
                        w = tft.width()  + radius,
                        h = tft.height() + radius;

    // Screen is not cleared for this one -- this is
    // intentional and does not affect the reported time.
    start = micros();
    for (x = 0; x < w; x += r2) {
        for (y = 0; y < h; y += r2) {
            tft.drawCircle(x, y, radius, color); //NOTE - Code that displays stuff
            /*
            void Adafruit_GFX::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) 
            @brief    Draw a circle outline
            @param    x0   Center-point x coordinate
            @param    y0   Center-point y coordinate
            @param    r   Radius of circle
            @param    color 16-bit 5-6-5 Color to draw with
            */
        }
    }

    return micros() - start;
}

unsigned long testTriangles() {
    unsigned long start;
    int           n, i, cx = tft.width()  / 2 - 1,
                        cy = tft.height() / 2 - 1;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    n     = min(cx, cy);
    start = micros();
    for (i = 0; i < n; i += 5) {
        tft.drawTriangle(
            cx    , cy - i, // peak
            cx - i, cy + i, // bottom left
            cx + i, cy + i, // bottom right
            tft.color565(0, 0, i)); //NOTE - Code that displays stuff
            /*
            void Adafruit_GFX::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
            @brief   Draw a triangle with no fill color
            @param    x0  Vertex #0 x coordinate
            @param    y0  Vertex #0 y coordinate
            @param    x1  Vertex #1 x coordinate
            @param    y1  Vertex #1 y coordinate
            @param    x2  Vertex #2 x coordinate
            @param    y2  Vertex #2 y coordinate
            @param    color 16-bit 5-6-5 Color to draw with
            */
    }

    return micros() - start;
}

unsigned long testFilledTriangles() {
    unsigned long start, t = 0;
    int           i, cx = tft.width()  / 2 - 1,
                     cy = tft.height() / 2 - 1;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    start = micros();
    for (i = min(cx, cy); i > 10; i -= 5) {
        start = micros();
        tft.fillTriangle(cx, cy - i, cx - i, cy + i, cx + i, cy + i,
                         tft.color565(0, i, i)); //NOTE - Code that displays stuff
        /*
        void Adafruit_GFX::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
        @brief     Draw a triangle with color-fill
        @param    x0  Vertex #0 x coordinate
        @param    y0  Vertex #0 y coordinate
        @param    x1  Vertex #1 x coordinate
        @param    y1  Vertex #1 y coordinate
        @param    x2  Vertex #2 x coordinate
        @param    y2  Vertex #2 y coordinate
        @param    color 16-bit 5-6-5 Color to fill/draw with
        */
        t += micros() - start;
        tft.drawTriangle(cx, cy - i, cx - i, cy + i, cx + i, cy + i,
                         tft.color565(i, i, 0)); //NOTE - Code that displays stuff
    }

    return t;
}

unsigned long testRoundRects() {
    unsigned long start;
    int           w, i, i2, red, step,
                  cx = tft.width()  / 2 - 1,
                  cy = tft.height() / 2 - 1;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    w     = min(tft.width(), tft.height());
    start = micros();
    red = 0;
    step = (256 * 6) / w;
    for (i = 0; i < w; i += 6) {
        i2 = i / 2;
        red += step;
        tft.drawRoundRect(cx - i2, cy - i2, i, i, i / 8, tft.color565(red, 0, 0)); //NOTE - Code that displays stuff
        /*
        void Adafruit_GFX::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color)
        @brief   Draw a rounded rectangle with no fill color
        @param    x   Top left corner x coordinate
        @param    y   Top left corner y coordinate
        @param    w   Width in pixels
        @param    h   Height in pixels
        @param    r   Radius of corner rounding
        @param    color 16-bit 5-6-5 Color to draw with
        */
    }

    return micros() - start;
}

unsigned long testFilledRoundRects() {
    unsigned long start;
    int           i, i2, green, step,
                  cx = tft.width()  / 2 - 1,
                  cy = tft.height() / 2 - 1;

    tft.fillScreen(BLACK); //NOTE - Code that displays stuff
    start = micros();
    green = 256;
    step = (256 * 6) / min(tft.width(), tft.height());
    for (i = min(tft.width(), tft.height()); i > 20; i -= 6) {
        i2 = i / 2;
        green -= step;
        tft.fillRoundRect(cx - i2, cy - i2, i, i, i / 8, tft.color565(0, green, 0)); //NOTE - Code that displays stuff
        /*
        void Adafruit_GFX::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color)
        @brief   Draw a rounded rectangle with fill color
        @param    x   Top left corner x coordinate
        @param    y   Top left corner y coordinate
        @param    w   Width in pixels
        @param    h   Height in pixels
        @param    r   Radius of corner rounding
        @param    color 16-bit 5-6-5 Color to draw/fill with
        */
    }

    return micros() - start;
}

