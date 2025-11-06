/*

 Example sketch for TFT_eSPI library.

 No fonts are needed.

 Draws a 3d rotating cube on the TFT screen.

 Original code was found at http://forum.freetronics.com/viewtopic.php?f=37&t=5495

 */

// Waveshare TFT LCD 3.5" 480x320 65k colours, Interface: SPI, Controller: ILI9486,
// https://shopee.sg/Touch-LCD-Shield-3.5-inch-for-Arduino-i.440521573.17478975100?sp_atk=eee62442-3704-4ede-883e-773af8ec4ae0&xptdk=eee62442-3704-4ede-883e-773af8ec4ae0
// http://www.waveshare.com/wiki/3.5inch_TFT_Touch_Shield

/* Pin connections to ESP32 with notes
5V → 5V
GND → GND
SCLK (D13): SPI Clock → 18
MISO (D12) SPI Data Input → 19 // If panel is write-only (display only, no touchscreen), no need to connect
MOSI (D11) SPI Data Output → 23
LCD_CS (D10) LCD Chip Select → 15
LCD_BL (D9) LCD Backlight → 5V
LCD_RST (D8) LCD Reset → 4
LCD_DC (D7) LCD Data/Command Selection → 2
TP stands for Touch Panel, not used.
SD refers to the micro SD card, not used.
*/

/*

 Example sketch for TFT_eSPI library.

 No fonts are needed.

 Draws a 3d rotating cube on the TFT screen.

 Original code was found at http://forum.freetronics.com/viewtopic.php?f=37&t=5495

 */

#define BLACK 0x0000
#define WHITE 0xFFFF

#include <SPI.h>

#include <TFT_eSPI.h> // Hardware-specific library

#include <math.h> // for sin, cos

TFT_eSPI tft = TFT_eSPI(); // Invoke custom library

int16_t h;
int16_t w;

int inc = -2;

float xx, xy, xz;
float yx, yy, yz;
float zx, zy, zz;

float fact;

int Xan, Yan;

int Xoff;
int Yoff;
int Zoff;

struct Point3d
{
    int x;
    int y;
    int z;
};

struct Point2d
{
    int x;
    int y;
};

int LinestoRender;    // lines to render.
int OldLinestoRender; // lines to render just in case it changes. this makes sure the old lines all get erased.

struct Line3d
{
    Point3d p0;
    Point3d p1;
};

struct Line2d
{
    Point2d p0;
    Point2d p1;
};

Line3d Lines[20];
Line2d Render[20];
Line2d ORender[20];

// --- Forward declarations (needed in .cpp builds) ---
void cube(void);
void SetVars(void);
void ProcessLine(struct Line2d *ret, struct Line3d vec);
void RenderImage(void);

/***********************************************************************************************************************************/
void setup()
{
    tft.init();

    h = tft.height();
    w = tft.width();

    tft.setRotation(1);

    tft.fillScreen(TFT_BLACK);

    cube();

    fact = 180 / 3.14159259; // conversion from degrees to radians.

    Xoff = 240; // Position the centre of the 3d conversion space into the centre of the TFT screen.
    Yoff = 160;
    Zoff = 550; // Z offset in 3D space (smaller = closer and bigger rendering)
}

/***********************************************************************************************************************************/
void loop()
{

    // Rotate around x and y axes in 1 degree increments
    Xan++;
    Yan++;

    Yan = Yan % 360;
    Xan = Xan % 360; // prevents overflow.

    SetVars(); // sets up the global vars to do the 3D conversion.

    // Zoom in and out on Z axis within limits
    // the cube intersects with the screen for values < 160
    Zoff += inc;
    if (Zoff > 500)
        inc = -1; // Switch to zoom in
    else if (Zoff < 160)
        inc = 1; // Switch to zoom out

    for (int i = 0; i < LinestoRender; i++)
    {
        ORender[i] = Render[i];            // stores the old line segment so we can delete it later.
        ProcessLine(&Render[i], Lines[i]); // converts the 3d line segments to 2d.
    }
    RenderImage(); // go draw it!

    delay(14); // Delay to reduce loop rate (reduces flicker caused by aliasing with TFT screen refresh rate)
}

/***********************************************************************************************************************************/
void RenderImage(void)
{
    // renders all the lines after erasing the old ones.
    // in here is the only code actually interfacing with the OLED. so if you use a different lib, this is where to change it.

    for (int i = 0; i < OldLinestoRender; i++)
    {
        tft.drawLine(ORender[i].p0.x, ORender[i].p0.y, ORender[i].p1.x, ORender[i].p1.y, BLACK); // erase the old lines.
    }

    for (int i = 0; i < LinestoRender; i++)
    {
        uint16_t color = TFT_BLUE;
        if (i < 4)
            color = TFT_RED;
        if (i > 7)
            color = TFT_GREEN;
        tft.drawLine(Render[i].p0.x, Render[i].p0.y, Render[i].p1.x, Render[i].p1.y, color);
    }
    OldLinestoRender = LinestoRender;
}

/***********************************************************************************************************************************/
// Sets the global vars for the 3d transform. Any points sent through "process" will be transformed using these figures.
// only needs to be called if Xan or Yan are changed.
void SetVars(void)
{
    float Xan2, Yan2, Zan2;
    float s1, s2, s3, c1, c2, c3;

    Xan2 = Xan / fact; // convert degrees to radians.
    Yan2 = Yan / fact;

    // Zan is assumed to be zero

    s1 = sin(Yan2);
    s2 = sin(Xan2);

    c1 = cos(Yan2);
    c2 = cos(Xan2);

    xx = c1;
    xy = 0;
    xz = -s1;

    yx = (s1 * s2);
    yy = c2;
    yz = (c1 * s2);

    zx = (s1 * c2);
    zy = -s2;
    zz = (c1 * c2);
}

/***********************************************************************************************************************************/
// processes x1,y1,z1 and returns rx1,ry1 transformed by the variables set in SetVars()
// fairly heavy on floating point here.
// uses a bunch of global vars. Could be rewritten with a struct but not worth the effort.
void ProcessLine(struct Line2d *ret, struct Line3d vec)
{
    float zvt1;
    int xv1, yv1, zv1;

    float zvt2;
    int xv2, yv2, zv2;

    int rx1, ry1;
    int rx2, ry2;

    int x1;
    int y1;
    int z1;

    int x2;
    int y2;
    int z2;

    int Ok;

    x1 = vec.p0.x;
    y1 = vec.p0.y;
    z1 = vec.p0.z;

    x2 = vec.p1.x;
    y2 = vec.p1.y;
    z2 = vec.p1.z;

    Ok = 0; // defaults to not OK

    xv1 = (x1 * xx) + (y1 * xy) + (z1 * xz);
    yv1 = (x1 * yx) + (y1 * yy) + (z1 * yz);
    zv1 = (x1 * zx) + (y1 * zy) + (z1 * zz);

    zvt1 = zv1 - Zoff;

    if (zvt1 < -5)
    {
        rx1 = 256 * (xv1 / zvt1) + Xoff;
        ry1 = 256 * (yv1 / zvt1) + Yoff;
        Ok = 1; // ok we are alright for point 1.
    }

    xv2 = (x2 * xx) + (y2 * xy) + (z2 * xz);
    yv2 = (x2 * yx) + (y2 * yy) + (z2 * yz);
    zv2 = (x2 * zx) + (y2 * zy) + (z2 * zz);

    zvt2 = zv2 - Zoff;

    if (zvt2 < -5)
    {
        rx2 = 256 * (xv2 / zvt2) + Xoff;
        ry2 = 256 * (yv2 / zvt2) + Yoff;
    }
    else
    {
        Ok = 0;
    }

    if (Ok == 1)
    {

        ret->p0.x = rx1;
        ret->p0.y = ry1;

        ret->p1.x = rx2;
        ret->p1.y = ry2;
    }
    // The ifs here are checks for out of bounds. needs a bit more code here to "safe" lines that will be way out of whack, so they don't get drawn and cause screen garbage.
}

/***********************************************************************************************************************************/
// line segments to draw a cube. basically p0 to p1. p1 to p2. p2 to p3 so on.
void cube(void)
{
    // Front Face.

    Lines[0].p0.x = -50;
    Lines[0].p0.y = -50;
    Lines[0].p0.z = 50;
    Lines[0].p1.x = 50;
    Lines[0].p1.y = -50;
    Lines[0].p1.z = 50;

    Lines[1].p0.x = 50;
    Lines[1].p0.y = -50;
    Lines[1].p0.z = 50;
    Lines[1].p1.x = 50;
    Lines[1].p1.y = 50;
    Lines[1].p1.z = 50;

    Lines[2].p0.x = 50;
    Lines[2].p0.y = 50;
    Lines[2].p0.z = 50;
    Lines[2].p1.x = -50;
    Lines[2].p1.y = 50;
    Lines[2].p1.z = 50;

    Lines[3].p0.x = -50;
    Lines[3].p0.y = 50;
    Lines[3].p0.z = 50;
    Lines[3].p1.x = -50;
    Lines[3].p1.y = -50;
    Lines[3].p1.z = 50;

    // back face.

    Lines[4].p0.x = -50;
    Lines[4].p0.y = -50;
    Lines[4].p0.z = -50;
    Lines[4].p1.x = 50;
    Lines[4].p1.y = -50;
    Lines[4].p1.z = -50;

    Lines[5].p0.x = 50;
    Lines[5].p0.y = -50;
    Lines[5].p0.z = -50;
    Lines[5].p1.x = 50;
    Lines[5].p1.y = 50;
    Lines[5].p1.z = -50;

    Lines[6].p0.x = 50;
    Lines[6].p0.y = 50;
    Lines[6].p0.z = -50;
    Lines[6].p1.x = -50;
    Lines[6].p1.y = 50;
    Lines[6].p1.z = -50;

    Lines[7].p0.x = -50;
    Lines[7].p0.y = 50;
    Lines[7].p0.z = -50;
    Lines[7].p1.x = -50;
    Lines[7].p1.y = -50;
    Lines[7].p1.z = -50;

    // now the 4 edge lines.

    Lines[8].p0.x = -50;
    Lines[8].p0.y = -50;
    Lines[8].p0.z = 50;
    Lines[8].p1.x = -50;
    Lines[8].p1.y = -50;
    Lines[8].p1.z = -50;

    Lines[9].p0.x = 50;
    Lines[9].p0.y = -50;
    Lines[9].p0.z = 50;
    Lines[9].p1.x = 50;
    Lines[9].p1.y = -50;
    Lines[9].p1.z = -50;

    Lines[10].p0.x = -50;
    Lines[10].p0.y = 50;
    Lines[10].p0.z = 50;
    Lines[10].p1.x = -50;
    Lines[10].p1.y = 50;
    Lines[10].p1.z = -50;

    Lines[11].p0.x = 50;
    Lines[11].p0.y = 50;
    Lines[11].p0.z = 50;
    Lines[11].p1.x = 50;
    Lines[11].p1.y = 50;
    Lines[11].p1.z = -50;

    LinestoRender = 12;
    OldLinestoRender = LinestoRender;
}
