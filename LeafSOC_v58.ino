// SOC% display for 2011-2012 Nissan Leafs
// =======================================
// Paul Kennett, 2019-2020
//
// Based on "CANa Display for Nissan LEAF"  https://ev-olution.yolasite.com/CANa.php © Copyright EV-OLUTION
//
// Note: later Leafs display SOC% on the main dash, so this solution is only useful for 2011-2012 Leafs
// Although... this also provides a Range Estimator that is better than the Leafs Guess-O-Meter
//
// This code works with 128*64 pixel OLED displays; 0.96 or 1.54 inch (SSD1306 driver) or 1.30 inch (SH1106 driver)
// For displays that use the SSD1306 driver uncomment line 109 and comment-out line 110
// For displays that use the SH1106 driver uncomment line 110 and comment-out line 109
// FYI: the MCP2515 is connected to the Leafs EV-CAN bus via the OBD2 socket

/*
   V58 - 6 July 2021
   Reorganising the pages a bit

   V57 - 5 July 2021
   Paging and settings function work great now. Yay!

   V56 - 3 July 2021
   Got paging and settings working badly. Pages updates only after *two* actions, so is always delayed  by one step.
   My kludge is to monitor the CCSetpiont and use that as the second action for switching pages.
   I need to find a better fix for this!

   V55 - 3 July 2021
   Trying to reinstate Settings mode (via Climater Control)

   V54 - 30 June 2021
   Changed paging code to use IF statemetns rather than Switch/Case - to fix a bug

   V53 - 24 June 2021
   fixing slow CAN bus reading by only writing to dispay when needed. 

   V52 - 6 June 2021
   Changed to solid small battery outline image - nicer design(?)

   V51 - 30 May 2021
   Removing rawSOC code
   Adding code to auto set MaxGids
   replaced EEPROM.write with EEPROM.update

   V50 - 23 May 2021
   fixed bug where Page value in EEPROM was out of bounds

   V49 - 20 May 2021
   Store MAX_GIDS variable in EEPROM, and made adjustable via Climate Controls
   Store km_per_dWh variable in EEPROM, and made adjustable via Climate Controls

   V47 - 16 May 2021
   Cleaned out the code for changing pages by a push button - hoping it would improve CAN data loading speed but it doesn't
   maybe it's a clash between MCP2515 and U8g2lib? Could switch to using Adafruit GFX lib instead. (RangePolt uses that library

   V46 - 16 May 2021
   Cleaned out the code for test-mode

   V45 - 10 May 2021
   Changed for 1.30inch OLED 128x64 display with

   V44 - 25 Apr 2021
   Minor visual tweaks (moved text down on Page 3, and fixed kWh over 10 alignment)

   V43 - 17 Apr 2021
   Added Page 3: Range page that fills display
   Added Page 4: Range page and small battery SOC% (replaces the crude data dump page)

   V42 - 16 Sept 2020
   Cleaning code before uploading to Github

   V41 - 28 Aug 2020
   Cleaning out old experimental junk
   added ability to change page by: (1) Put Climate Control On, Recirc Air,
   then change fan speed to change pages, fan speed 7 -> Reset Arduino
*/

#include <U8g2lib.h>           // Download code from https://github.com/olikraus/u8g2 and add to Ardunio libraries folder
#include <mcp_can.h>           // Download code from https://github.com/coryjfowler/MCP_CAN_lib add to Ardunio libraries folder
#include <EEPROM.h>            // The Arduino IDE already has this so you don't need to download it. 
#include "battery_large.h"     // large outline battery image (there must be a copy of this in with the LeafSOC code)
#include "battery_solid.h"     // small solid battery image (there must be a copy of this in with the LeafSOC code)

#define VERSION "LeafSOC v58"
#define DATE    "6 Jul 2021"
#define AUTHOR  "Paul Kennett"

#define KM_PER_KWH 6.4F        // km per kWh (from my car) used to calculate Range estimate (6.4 km.kWh is my average for the coldest month of 2019)
#define MAX_GIDS 1             // From my car. LeafSpy showed 225 Gids at 94.8% SOC on 12 Oct 2019.
#define GIDS_TURTLE 8          // the number of Gids at which Turtle mode kicks in (you might achieve 5-7 sometimes).
#define WH_PER_GID 75.0F       // Wh per Gid.  74.73 comes from one of the early Leaf hackinG pioneers. (Some people prefer 80.)
#define CAN0_INT 2             // [Why is this set using a define statement? Why bother?]
#define LINE1 16
#define LINE2 38
#define LINE3 60
#define LINE4 64

long unsigned int rxId;        // CAN message ID
unsigned char len = 0;         // data length
unsigned char rxBuf[8];        // a buffer for CAN data
uint16_t rawGids = 0;          // raw Gids
uint16_t rawGids2 = 0;         // compare with rawGids to see if value has changed
float rawGidsLow;              // raw Gids - 8 (the number of Gids to reach Turtle mode)
float GidsPct;                 // Gids in percentage, This is the main battery SOC % number that I display
uint16_t rawSoc;               // State of charge value directly from the EV-CAN
float SocPct;                  // Leaf SOC% from EV-Can. This corresponds to the LeafSpy app SOC%, but without rounding up
float SocPctSkewed;            // Skewing the top to show "100%" when battery is actually 95% and 0% when battery is 5%.
float kWh;                     // Energy left in main batery
float km_per_kWh;
byte km_per_dWh = KM_PER_KWH * 10; // using "deca"-Watthour because it's more efficient in EEPROM
byte MaxGids = MAX_GIDS;       // Default to MAX_GIDS value before reading the EEPROM
byte InitialGids = 0;          // Will be set to the first value of Gids read from the Can bus on startup
// byte GidTest = 2;              // temporary value used for comparing Gids in EEPROM to find highest value Gid
int range;                     // My esitmated range
char buffer[4];
int i;
int Page = 1;                  // Screen page; default to Page 1 before reading the EEPROM
byte BootCount = 5;

byte EEPROMaddr0 = 0;          // Address 0 in EEPROM used for the page number
byte EEPROMaddr1 = 1;          // Address 1 in EEPROM used for km_per_kWh
byte EEPROMaddr2 = 2;          // Address 2 in EEPROM used for MaxGids [I should upgrade this to a 2 byte INT at some point]
byte EEPROMaddr4 = 4;          // Boot counter
//                             // EEPROMaddr 5 to EEPROMaddr 95 used for Gids values from start-up (for 60 boots, then cyles around)

byte rawCCStatus1;             // Climate Control, if ON
byte rawCCVentIntake;          // Climate Control, and Intake is RECIRC
byte rawCCVentTarget;          // Climate Control, and Vent is FEET then enter "change settings" modes
byte rawCCFanSpeed;            // used to change page in EEPROM
byte rawCCButtonPress;         // alternates after every button press on the CC
byte rawCCButtonPress2;        // to compare if CCButtonPress has changed
byte rawCCSetpoint;            // used to set default setting in EEPROM
byte rawGearPos = 1;           // used to select which settings option to edit in EEPROM
byte rawGearPos2 = 1;          // preset to Neutral (1)

MCP_CAN CAN0(10);              // Set MCP CS to pin 10


// cs  = 7 [or 4]
// dc  = 6 [or 3]
// rst = 5 [or 9]
// UNCOMMENT whichever line below best matches your OLED display driver chip (ie SSD1306 or SH1106?)
// -------------------------------------------------------------------------------------------------
// U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R2, /* cs=*/ 7, /* dc=*/ 6, /* reset=*/ 5);  // for 0.96 and 1.54 inch OLED disaplys with SSD1306 driver chip
U8G2_SH1106_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R2, /* cs=*/ 7, /* dc=*/ 6, /* reset=*/ 5);      // for 1.30 inch OLED disaply with SH1106 driver chip
// -------------------------------------------------------------------------------------------------


void(* resetFunc) (void) = 0;


void setup() {


  // Run this line once first time with a new micro
  //  Clear_EEPROM();   // clean out Gid space in EEPROM


  u8g2.begin();
  u8g2.setFont(u8g2_font_logisoso16_tr); // Medium font

  if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK) {
    u8g2.firstPage();
    do {
      u8g2.setCursor(0, LINE1); u8g2.print(VERSION);
      u8g2.setCursor(0, LINE2); u8g2.print(DATE);
      u8g2.setCursor(0, LINE3); u8g2.print(AUTHOR);
    } while ( u8g2.nextPage() );
    delay(500); // was 1000 originally. If this is reduced to 0 the MCP2515 will sometimes fart out 1023 at startup)
  } else {
    u8g2.firstPage();
    do {
      u8g2.setFontMode(0); // Inverse font mode
      u8g2.setDrawColor(1); // White screen colour
      u8g2.drawBox(0, 0, 128, 64); // draw white box
      u8g2.setDrawColor(0);
      u8g2.setCursor(31, 18); u8g2.print("MCP2515");
      u8g2.setCursor(3, 40); u8g2.print("(CAN module)");
      u8g2.setCursor(15, 61); u8g2.print("start error");
    } while ( u8g2.nextPage() );
    delay(10000);
  }
  CAN0.setMode(MCP_NORMAL);                 // Set to normal mode so the MCP2515 sends acks to received data.
  pinMode(CAN0_INT, INPUT);

  if (EEPROM.read(EEPROMaddr0) > 6 ) {
    EEPROM.update(EEPROMaddr0, Page);
  }
  if (EEPROM.read(EEPROMaddr1) > 40  && EEPROM.read(EEPROMaddr1) < 100 ) {
    km_per_kWh = (EEPROM.read(EEPROMaddr1)) / 10.0F;
  } else {
    EEPROM.update(EEPROMaddr1, km_per_dWh); // used the first time you run this code on a new Arduino
  }
  if (EEPROM.read(EEPROMaddr2) > 100 && EEPROM.read(EEPROMaddr2) < 255 ) {
    MaxGids = (EEPROM.read(EEPROMaddr2));
  } else {
    EEPROM.update(EEPROMaddr2, MaxGids);   // only used for the first time you run this code on a new Arduino
  }
  if (EEPROM.read(EEPROMaddr4) > 64 ) {
    EEPROM.update(EEPROMaddr4, BootCount); // only used for the first time you run this code on a new Arduino
  } else {
    BootCount = EEPROM.read(EEPROMaddr4) + 1;
    EEPROM.update(EEPROMaddr4, BootCount);
  }

  // Read the array of MaxGid values and write the highest value into the EEPROM MaxGids location
  MaxGids = EEPROM.read(5);
  for (i = 6; i <= 64; i++) {
    if (EEPROM.read(i) > MaxGids) {
      MaxGids = EEPROM.read(i);
    }
  }
  EEPROM.update(EEPROMaddr2, MaxGids);

} //end setup loop


void loop() {

  // talking to the EV-CAN bus and calculating stuff
  if (!digitalRead(CAN0_INT)) {
    CAN0.readMsgBuf(&rxId, &len, rxBuf);        // Read data: len = data length, buf = data byte(s)

    if ((rxId & 0x40000000) == 0x40000000) {    // Determine if message is a remote request frame.
      // sprintf(msgString, " REMOTE REQUEST FRAME");
    } else {
      // Pick out the CAN messages we're interested in
      if (rxId == 0x5bc) {            // read GIDS from EV-CAN (500ms)
        rawGids = (rxBuf[0] << 2) | (rxBuf[1] >> 6);
        GidsPct = ((float)(rawGids - GIDS_TURTLE) / (MaxGids - GIDS_TURTLE)) * 100.0F;
        kWh = (((float)rawGids) * WH_PER_GID) / 1000.0F;
        range = km_per_kWh * ((rawGids - GIDS_TURTLE) * WH_PER_GID) / 1000;
      } else if (rxId == 0x54b) {     // Read Climate Controls from EV-Can (100ms)
        rawCCStatus1 = (rxBuf[0]);
        rawCCVentTarget = (rxBuf[2]);
        rawCCVentIntake  = (rxBuf[3]);
        rawCCFanSpeed = (rxBuf[4] >> 3);
        rawCCButtonPress = (rxBuf[7]);
      } else if (rxId == 0x55b) {     // read SOC from EV-CAN (100ms)
        rawSoc = (rxBuf[0] << 2) | (rxBuf[1] >> 6);
        SocPct = rawSoc / 10.0F;
        SocPctSkewed = (SocPct - 5.0F) * 1.1F; // adjusting so that it goes from 0 to 100%
      } else if (rxId == 0x11a) {     // Read Gear Position from EV-Can (10ms)
        rawGearPos = (rxBuf[0] >> 4); // 1 = Park, 2 = Reverse, 3 = Neutral, 4 = Drive
      } else if (rxId == 0x54a) {     // Read Climate Control Setpoint (target temp) from EV-Can (100ms)
        rawCCSetpoint = (rxBuf[4]);   // 18.0C = 36, 18.5C = 37 ... 31.5.0C = 63, 32.0C = 64
      }
    }
  }

  // If rawGids has a value and InitialGids is still zero then write first Gids value into EEPROM MaxGids array
  if (rawGids != 0 && InitialGids == 0) { // if InitialGids has not yet been set, then set it
    InitialGids = rawGids;
    EEPROM.update(BootCount, InitialGids);
  }

  //  EEPROM.update(EEPROMaddr0, 5); // used for testing only

  // if Gid have changed or a CC button was pressed or the Gear Position changes then print to the display
  if (rawGids2 != rawGids || rawCCButtonPress2 != rawCCButtonPress  || rawGearPos2 != rawGearPos) {
    Setup_mode_check();

    if (Page == 0) { // RESET the Arduino
      EEPROM.update(EEPROMaddr0, 1);
      resetFunc();
    }
    else if (Page == 1) {
      u8g2.firstPage();
      do {
        Setup_mode_check();
        u8g2.drawXBM( 0, 40, 56, 24, battery_solid_bits);
        u8g2.setFont(u8g2_font_logisoso32_tn); // Large numerals-only font
        u8g2.setCursor(39, 32);
        dtostrf(range, 3, 0, buffer);
        u8g2.print(buffer);
        u8g2.setFont(u8g2_font_logisoso16_tr); // Medium font
        u8g2.setCursor(0, 24);   u8g2.print("Range");
        u8g2.setCursor(103, 24); u8g2.print("km");
        u8g2.setCursor(5, LINE3);
        u8g2.setFontMode(1); // Black text
        u8g2.setDrawColor(2); // white background
        dtostrf(GidsPct, 3, 0, buffer);
        u8g2.print(buffer);
        u8g2.print("%"); // Battery SOC%
        u8g2.setFontMode(0);
        u8g2.setCursor(72, LINE3);
        if (kWh >= 10) {
          u8g2.setCursor(62, LINE3);
        }
        dtostrf(kWh, 3, 1, buffer);
        u8g2.print(buffer);
        u8g2.print("kWh");     // kWh calculated from Gids
      } while ( u8g2.nextPage() );
    }
    else if (Page == 2) {
      u8g2.firstPage();
      do {
        u8g2.drawXBMP( 0, 0, bitmap_width, bitmap_height, battery_large_bits);
        Setup_mode_check();
        // Print large SOC% numerals inside battery image
        u8g2.setFont(u8g2_font_logisoso26_tn); // Large numerals-only font
        u8g2.setCursor(41, 33);
        dtostrf(GidsPct, 3, 0, buffer);
        u8g2.print(buffer);
        u8g2.setFont(u8g2_font_logisoso16_tr); // Medium size font
        u8g2.setCursor(36, LINE3);
        dtostrf(kWh, 3, 1, buffer);
        u8g2.print(buffer);
        u8g2.print(" kWh");
      } while ( u8g2.nextPage() );
    }
    else if (Page == 3) {
      u8g2.firstPage();
      do {
        Setup_mode_check();
        u8g2.setFont(u8g2_font_logisoso32_tn); // Large numerals-only font
        u8g2.setCursor(39, 47);
        dtostrf(range, 3, 0, buffer);
        u8g2.print(buffer);  // u8g2.print(Range);
        u8g2.setFont(u8g2_font_logisoso16_tr); // Medium font
        u8g2.setCursor(0, 39);   u8g2.print("Range"); // Range
        u8g2.setCursor(103, 39); u8g2.print("km"); // Range
      } while ( u8g2.nextPage() );
    }
    else if (Page == 4) {
      u8g2.firstPage();
      do {
        Setup_mode_check();
        u8g2.setFont(u8g2_font_logisoso16_tr);
        u8g2.setCursor(0, LINE1); u8g2.print(VERSION);
        u8g2.setCursor(0, LINE2); u8g2.print(DATE);
        u8g2.setCursor(0, LINE3); u8g2.print(AUTHOR);
      } while ( u8g2.nextPage() );
    }
    else if (Page == 5) {
      u8g2.firstPage();
      do {
        Setup_mode_check();
        u8g2.drawHLine(0,  0, 128);
        u8g2.drawHLine(0, 21, 128);
        u8g2.drawHLine(0, 42, 128);
        u8g2.drawHLine(0, 63, 128);
        u8g2.drawVLine(0,   0, 63);
        u8g2.drawVLine(63,  0, 63);
        u8g2.drawVLine(127, 0, 63);
        u8g2.setCursor(2, 19); u8g2.print("BC:"); u8g2.print(BootCount);   u8g2.setCursor(65, 19); u8g2.print("rG:"); u8g2.print(rawGids);
        u8g2.setCursor(2, 40); u8g2.print("IG:"); u8g2.print(InitialGids); u8g2.setCursor(65, 40); u8g2.print("MG:"); u8g2.print(MaxGids);
        SocPctSkewed = (SocPct - 8.0F) * 1.1F; // adjusting so that it goes from 0 to 100%
        dtostrf(GidsPct, 3, 0, buffer);
        u8g2.setCursor(2, 61); u8g2.print("mS:"); u8g2.print(buffer);    u8g2.setCursor(65, 61); u8g2.print("sS:");  u8g2.print(SocPctSkewed, 0);

      } while ( u8g2.nextPage() );
    }
    else if (Page == 6) {
      u8g2.firstPage();
      do {
        Setup_mode_check();
        u8g2.setFontMode(0);
        u8g2.setCursor(0, LINE1); u8g2.print("Set default");
        // Put Gear position into NEUTRAL to edit km/kWh
        if (rawCCStatus1 == 0 && rawCCVentTarget == 152 && rawCCVentIntake == 9 && rawGearPos == 3) {
          //          rawCCButtonPress2 = 2;
          //          Setup_mode_check();
          km_per_dWh = rawCCSetpoint + 15;
          EEPROM.update(EEPROMaddr1, km_per_dWh);        // Adjust km_per_kWh
          km_per_kWh = (EEPROM.read(EEPROMaddr1)) / 10.0F;
          u8g2.setFontMode(1);                           // Inverse font colour
          u8g2.setDrawColor(2);                          // Inverse screen colour
          u8g2.drawBox(0, 20, 127, 21);
          u8g2.setCursor(0, LINE2); u8g2.print("km/kWh: ");  u8g2.print(km_per_kWh, 1);
        } else {
          u8g2.setFontMode(0);                           // standard white text on black background ("edit mode")
          u8g2.setCursor(0, LINE2); u8g2.print("km/kWh: ");  u8g2.print(km_per_kWh, 1);
        }
      } while ( u8g2.nextPage() );
    }
  }

  rawGids2 = rawGids;
  rawCCButtonPress2 = rawCCButtonPress;
  rawGearPos2 = rawGearPos;

} // END MAIN LOOP


void Setup_mode_check() {
  // Use the Climate Controls to change pages
  // Put Climate ON and AC OFF, and Air to RECIRC, and Vent to FEET, then fan speed sets Page number
  // When Fan Speed = 7 the Arduino will reset itself
  if (rawCCStatus1 == 0 && rawCCVentTarget == 152 && rawCCVentIntake == 9 && rawCCFanSpeed > 0 && rawCCFanSpeed < 7) {
    EEPROM.update(EEPROMaddr0, rawCCFanSpeed);
    Draw_dotted_box();
  } else if (rawCCStatus1 == 0 && rawCCVentTarget == 152 && rawCCVentIntake == 9 && rawCCFanSpeed == 7) {
    EEPROM.update(EEPROMaddr0, 0);
  }
  Page = EEPROM.read(EEPROMaddr0);
  //  u8g2.setCursor(0, LINE4); u8g2.print(Page);
}

void Draw_dotted_box() {
  for (byte i = 0; i < 63; i += 5) {
    u8g2.drawPixel(0, i);
    u8g2.drawPixel(127, i);
  }
  for (byte i = 0; i < 127; i += 5) {
    u8g2.drawPixel(i, 0);
    u8g2.drawPixel(i, 63);
  }
}

void Clear_EEPROM() {
  if (EEPROM.read(5) == 255) {
    for (i = 6; i <= 94; i++) {
      EEPROM.update(i, 0);
    }
  }
}
