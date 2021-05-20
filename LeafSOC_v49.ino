// SOC% display for 2011-2012 Nissan Leafs
// =======================================
// Paul Kennett, 2019-2020
//
// Based on "CANa Display for Nissan LEAF"  https://ev-olution.yolasite.com/CANa.php © Copyright EV-OLUTION
//
// Note: later Leafs display SOC% on the main dash, so this solution is only useful for 2011-2012 Leafs
//
// This code works with 128*64 pixel OLED displays; 0.96 or 1.54 inch (SSD1306 driver) OR 1.30 inch (SH1106 driver)
// For displays that use the SSD1306 driver uncomment line 104 and comment-out line 105
// For displays that use the SH1106 driver uncomment line 105 and comment-out line 104
// Note: MCP2515 is connected to EV-CAN via the OBD2 socket

/*
   V49 - 20 May 2021
   Store MAX_GIDS variable in EEPROM (and adjustable)
   Store km_per_kWh variable (uctually km_per_dWh) in EEPROM (and adjustable)

   V47 - 16 May 2021
   Cleaned out the code for changing pages by a push button - hoping it would improve initial CAN data loading but it doesn't
   maybe it's a clash between MCP2515 and U8g2lib? Could switch to using Adafruit GFX lib instead. (RangePolt uses that library

   V46 - 16 May 2021
   Cleaned out the code for test-mode

   V45 - 10 May 2021
   Changed for 1.30inch OLED 128x64 display with

   V44 - 25 Apr 2021
   Minor visual tweaks (moved text down on Page 3, and fixed kWh over 10 alignment)

   V43 - 17 Apr 2021
   Added Page 3: Range page that fills display
   Added Page 4: Range page and small battry SOC% (repaces the crude data dump page)

   V42 - 16 Sept 2020
   Cleaning code before uploading to Github

   V41 - 28 Aug 2020
   Cleaning out old experimental junk
   added ability to change page by: (1) Put Climate Control On, Recirc Air,
   then change fan speed to change pages, Full fan speed -> Reset Arduino

*/

#include <Arduino.h>
#include <U8g2lib.h>           // graphics library
#include <mcp_can.h>           // OBD2 module
#include <SPI.h>               // for talking to a SPI devices 
//#include <Wire.h>            // for talking to an I2C display
#include <EEPROM.h>            // read/write to the EEPROM memory
#include "battery_large.h"     // battery image
#include "battery_small.h"     // battery image
//#include "changelog.h"         // changelog and verbose notes


//#define KM_PER_KWH 6.9F          // km per kWh (from my car) used to calculate Range estimate (6.9 km.kWh is my average from May 2019 to Apr 2020)
#define KM_PER_KWH 6.5F            // km per kWh (from my car) used to calculate Range estimate (6.4 km.kWh is my average for the coldest month of 2019)
#define MAX_GIDS 206               // From my car. LeafSpy showed 225 Gids at 94.8% SOC on 12 Oct 2019
// 215 as at 26 July 2020
// 205 mas at Feb 2021 :(
// See my  ramble in readme.h file for more on defining MAX_GIDS
// [It would be better if I could set this from inside the car, as needed, or automatically]
#define GIDS_TURTLE 8              // the number of Gids at which Turtle mode kicks in (you might achieve 5-7 sometimes).
#define WH_PER_GID 74.73F          // Wh per Gid.  74.73 comes from one of the early Leaf hackinG pioneers. (Some people prefer 80.)
#define CAN0_INT 2                 // [Why is this set using a define statement? Why bother?]
#define LINE1 16
#define LINE2 38
#define LINE3 60
#define LINE4 64

long unsigned int rxId;        // CAN message ID
unsigned char len = 0;         // data length
unsigned char rxBuf[8];        // a buffer for CAN data
uint16_t rawGids;              // raw Gids
float rawGidsLow;              // raw Gids - 8 (the number of Gids to reach Turtle mode)
float GidsPct;                 // Gids in percentage, This is the main battery SOC % number that I display
uint16_t rawSoc;               // State of charge from the EV-CAN
float SocPct;                  // Leaf SOC% from EV-Can. This corresponds to the LeafSpy app SOC%, but without rounding up
float SocPctSkewed;            // Skewing the top to show "100%" when battery is actually 95% and 0% when battery is 5%.
// The Leaf doesn't actually charge above 95% or allow you to go below 5%
float kW;                      // Power used from main battery
float kWh;                     // Energy left in main batery
float km_per_kWh;
byte km_per_dWh = KM_PER_KWH * 10; // using "deca"-Watthour because it's more efficient in EEPROM
byte MaxGids = MAX_GIDS;       // Default to MAX_GIDS value before reading the EEPROM
int Range;                     // My esitmated range
char buffer[4];
int i;

byte EEPROMaddr0 = 0;           // Address 0 in EEPROM used for page number
byte EEPROMaddr1 = 1;           // Address 1 in EEPROM used for km_per_kWh
byte EEPROMaddr2 = 2;           // Address 2 in EEPROM used for MAX_GIDS [I should upgrade this to a 2 byte INT at some point]
// byte EEPROMaddr4 = 4;        // not used yet

int Page = 1;                   // Screen page; default to Page 1 before reading the EEPROM

byte rawCCStatus1;              // Climate Control, if ON
byte rawCCVentIntake;           // Climate Control, and this is RECIRC
byte rawCCVentTarget;           // Climate Control, and this is FEET then enter "change settings" modes
byte rawCCFanSpeed;             // used to change page in EEPROM
byte rawCCSetpoint;             // used to set default setting in EEPROM
byte rawGearPos;                // used to set which settings option to edit in EEPROM

MCP_CAN CAN0(10);              // Set MCP CS to pin 10

// UNCOMMENT the line below that best matches your OLED disaplys and driver chip
// U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R2, /* cs=*/ 7, /* dc=*/ 6, /* reset=*/ 5);        // for 0.96 and 1.54 inch OLED disaplys with SSD1306 driver chip
U8G2_SH1106_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R2, /* cs=*/ 7, /* dc=*/ 6, /* reset=*/ 5);        // for 1.30 inch OLED disaply with SH1106 driver chip
//                                              R0/1/2/3 = rotation of display


void(* resetFunc) (void) = 0;


void draw() {
  // graphic commands to redraw the complete screen should be placed here
  u8g2.drawXBMP( 0, 0, bitmap_width, bitmap_height, battery_large_bits);
}


void setup() {
  pinMode(9, INPUT_PULLUP); // push button used to switch between pages

  u8g2.begin();
  // u8g2.setFont(u8g2_font_helvR10_tr); // Small font
  u8g2.setFont(u8g2_font_logisoso16_tr); // Medium font

  if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK) {

    u8g2.firstPage();
    do {
      u8g2.setCursor(0, LINE1); u8g2.print("LeafSOC V49");
      u8g2.setCursor(0, LINE2); u8g2.print("20 May 2021");
      u8g2.setCursor(0, LINE3); u8g2.print("Paul Kennett");
    } while ( u8g2.nextPage() );
    delay(1500); // was 1000 originally (Note: If this is reduced to 0 the MCP2515 will sometimes fart out 1023 on startup)
  }
  else {
    u8g2.firstPage();
    do {
      u8g2.setCursor(0, LINE1); u8g2.print("MCP2515");
      u8g2.setCursor(0, LINE2); u8g2.print("start");
      u8g2.setCursor(0, LINE3); u8g2.print("ERROR!");
    } while ( u8g2.nextPage() );
    delay(30000);
  }
  CAN0.setMode(MCP_NORMAL);                     // Set to normal mode so the MCP2515 sends acks to received data.
  pinMode(CAN0_INT, INPUT);

  if (EEPROM.read(EEPROMaddr1) > 40  && EEPROM.read(EEPROMaddr1) < 100 ) {
    km_per_kWh = (EEPROM.read(EEPROMaddr1)) / 10.0F;
  } else {
    EEPROM.write(EEPROMaddr1, km_per_dWh); // for the first time you run this code on a new Arduino
  }

  if (EEPROM.read(EEPROMaddr2) > 100 && EEPROM.read(EEPROMaddr2) < 255 ) {
    MaxGids = (EEPROM.read(EEPROMaddr2));
  } else {
    EEPROM.write(EEPROMaddr2, MaxGids);   // only used for the first time you run this code on a new Arduino
  }



} //end setup loop


void loop() {
  u8g2.firstPage();
  do {
    if (Page < 3) {
      draw();
    }
    // talking to the EV-CAN bus and calculating stuff
    if (!digitalRead(CAN0_INT))
    {
      CAN0.readMsgBuf(&rxId, &len, rxBuf);        // Read data: len = data length, buf = data byte(s)

      if ((rxId & 0x40000000) == 0x40000000) {    // Determine if message is a remote request frame.
        // sprintf(msgString, " REMOTE REQUEST FRAME");
      }
      else
      {
        // read GIDS from EV-CAN (500ms)
        if (rxId == 0x5bc) {
          rawGids = (rxBuf[0] << 2) | (rxBuf[1] >> 6);
          GidsPct = ((float)(rawGids - GIDS_TURTLE) / (MaxGids - GIDS_TURTLE)) * 100.0F;
          kWh = (((float)rawGids) * WH_PER_GID) / 1000.0F;
          Range = km_per_kWh * ((rawGids - GIDS_TURTLE) * WH_PER_GID) / 1000;
        }
        // read SOC from EV-CAN (100ms)
        if (rxId == 0x55b) {
          rawSoc = (rxBuf[0] << 2) | (rxBuf[1] >> 6);
          SocPct = rawSoc / 10.0F;
          SocPctSkewed = (SocPct - 5.0F) * 1.1F; // adjustuing so that it matches my SOC value
        }
        // Read Climate Controls
        if (rxId == 0x54b) {
          rawCCStatus1 = (rxBuf[0]);
          rawCCVentTarget = (rxBuf[2]);
          rawCCVentIntake  = (rxBuf[3]);
          rawCCFanSpeed = (rxBuf[4] >> 3);
        }
        // Read experiments
        if (rxId == 0x11a) {
          rawGearPos = (rxBuf[0] >> 4); // 1 = Park, 2 = Reverse, 3 = Neutral, 4 = Drive
        }
        if (rxId == 0x54a) {
          rawCCSetpoint = (rxBuf[4]);  // 18.0C = 36, 18.5C = 37 ... 31.5.0C = 63, 32.0C = 64
        }
      }
    }

    // Use the Climate Controls to change pages
    // Put Climate ON and AC OFF, and Air to RECIRC, and Vent to FEET, and Gear in Park, then fan speed sets Page number
    // When Fan Speed = 7 then the Arduino will reset itself
    if (rawCCStatus1 == 0 && rawCCVentTarget == 152 && rawCCVentIntake == 9 && rawCCFanSpeed < 7 && rawGearPos == 1) {
      EEPROM.update(EEPROMaddr0, rawCCFanSpeed);
      for (byte i = 0; i < 63; i += 5) {
        u8g2.drawPixel(0, i);
        u8g2.drawPixel(127, i);
      }
      for (byte i = 0; i < 127; i += 5) {
        u8g2.drawPixel(i, 0);
        u8g2.drawPixel(i, 63);
      }
    } else if (rawCCStatus1 == 0 && rawCCVentTarget == 152 && rawCCVentIntake == 9 && rawCCFanSpeed == 7 && rawGearPos == 1) {
      EEPROM.update(EEPROMaddr0, 0);
    }
    Page = EEPROM.read(EEPROMaddr0);

    // Print large SOC% numerals inside battery image
    u8g2.setFont(u8g2_font_logisoso26_tn); // Large numerals-only font
    if (rawGids != 0) {
      dtostrf(GidsPct, 3, 0, buffer);
    } else if (rawSoc != 0) {
      dtostrf(SocPctSkewed, 3, 0, buffer);
    }
    u8g2.setCursor(41, 33);
    if (rawGids != 0 && Page < 3) { // only print the SOC% inside the large battery image on the first two pages
      u8g2.print(buffer);
    } else if (Page < 3) {
      u8g2.print(" --");
    }

    u8g2.setFont(u8g2_font_logisoso16_tr); // Medium size font


    switch (Page) {

      case 0: // RESET the Arduino
        EEPROM.update(EEPROMaddr0, 1);
        resetFunc();
        break;

      case 1: // Large SOC% and small kWh
        //Range = km_per_kWh * ((rawGids - GIDS_TURTLE) * WH_PER_GID) / 1000;
        u8g2.setCursor(36, LINE3);
        if (rawGids != 0) { // don't print buffer if there's no data yet
          dtostrf(kWh, 3, 1, buffer);
          u8g2.print(buffer);
        } else {
          u8g2.print("--.-");
        }
        u8g2.print(" kWh");
        break;

      case 2: // Large SOC% and small Gids
        dtostrf(rawGids, 3, 0, buffer);     //put Gids into a right aligned 3 character-long array ("buffer") so that it's right aligned
        if (rawGids < 25 && rawGids != 0) { // If Gids are less than 25 then invert the color of the display to warn the driver!
          u8g2.setFontMode(1); // Inverse font colour
          u8g2.setDrawColor(2); // Inverse screen colour
          u8g2.drawBox(0, 0, 128, LINE4);
          u8g2.setCursor(7, LINE4 - 1);
          u8g2.print(rawGids);
          u8g2.print(" (");
          rawGidsLow = rawGids - 8.0F;
          u8g2.print(rawGidsLow, 0);
          u8g2.print(") Gids!");
        } else {                            // normal white chars on black screen
          u8g2.setFontMode(0);
          u8g2.setCursor(36, LINE4 - 1);
          if (rawGids != 0) {               // don't print buffer if there's no data yet
            u8g2.print(buffer);
          } else {
            u8g2.print(" --");
          }
          u8g2.print(" Gids");
        }
        break;

      case 3: // Large Range only
        u8g2.setFont(u8g2_font_logisoso32_tn); // Large numerals-only font
        u8g2.setCursor(39, 52);
        if (rawGids != 0) { // only numbers if there is data to print
          dtostrf(Range, 3, 0, buffer);
          u8g2.print(buffer);
          // u8g2.print(Range);
        } else {
          u8g2.print(" --"); // otherwise print "--"
        }
        u8g2.setFont(u8g2_font_logisoso16_tr); // Medium font
        u8g2.setCursor(0, 44);   u8g2.print("Range"); // Range
        u8g2.setCursor(103, 44); u8g2.print("km"); // Range
        break;

      case 4: // Large Range and SOC% and kWh
        u8g2.drawXBM( 0, 40, 56, 24, battery_small_bits);
        u8g2.setFont(u8g2_font_logisoso32_tn); // Large numerals-only font
        u8g2.setCursor(39, 32);
        if (rawGids != 0) { // only numbers if there is data to print
          dtostrf(Range, 3, 0, buffer);
          u8g2.print(buffer);
        } else {
          u8g2.print(" --");
        }
        u8g2.setFont(u8g2_font_logisoso16_tr); // Medium font
        u8g2.setCursor(0, 24);   u8g2.print("Range");
        u8g2.setCursor(103, 24); u8g2.print("km");
        u8g2.setCursor(5, LINE3);
        if (rawGids != 0) {
          dtostrf(GidsPct, 3, 0, buffer);
        } else if (rawSoc != 0) {
          dtostrf(SocPctSkewed, 3, 0, buffer);
        }
        if (rawGids != 0) { // only numbers if there is data to print
          u8g2.print(buffer);
        } else {
          u8g2.print(" --");
        }
        u8g2.print("%"); // Battery SOC%
        u8g2.setCursor(72, LINE3);
        if (kWh >= 10) {
          u8g2.setCursor(62, LINE3);
        }
        dtostrf(kWh, 3, 1, buffer);
        u8g2.print(buffer);
        u8g2.print("kWh"); // kWh calculated from Gids
        break;

      case 5: // experimental data
        u8g2.setCursor(0, LINE1);
        u8g2.print("Climate: ");
        if (rawCCStatus1 == 0) {
          u8g2.print("On");
        } else if (rawCCStatus1 == 1) {
          u8g2.print("Off");
        } else {
          u8g2.print(rawCCStatus1);
        }
        u8g2.setCursor(0, LINE2);
        u8g2.print("Intake: ");
        if (rawCCVentIntake == 9) {
          u8g2.print("Recirc.");
        } else if (rawCCVentIntake == 18) {
          u8g2.print("Fresh");
        } else if (rawCCVentIntake == 146) {
          u8g2.print("Defrost");
        } else {
          u8g2.print(rawCCVentIntake);
        }
        u8g2.setCursor(0, LINE3);
        u8g2.print("Vents: ");
        if (rawCCVentTarget == 128) {
          u8g2.print("Off");
        } else if (rawCCVentTarget == 136) {
          u8g2.print("Face");
        } else if (rawCCVentTarget == 144) {
          u8g2.print("Fc & Ft");
        } else if (rawCCVentTarget == 152) {
          u8g2.print("Feet");
        } else if (rawCCVentTarget == 160) {
          u8g2.print("Ft & WS");
        } else {
          u8g2.print(rawCCVentTarget);
        }
        break;

      case 6: // Adjust km_per_kWh and Max Gids
        u8g2.setFontMode(0);
        u8g2.setCursor(0, LINE1); u8g2.print("Set defaults");    

        // Gear position must be in Park to edit kw/kWh
        if (rawCCStatus1 == 0 && rawCCVentTarget == 152 && rawCCVentIntake == 9 && rawGearPos == 1) {
          km_per_dWh = rawCCSetpoint + 15;
          EEPROM.update(EEPROMaddr1, km_per_dWh);        // Adjust km_per_kWh
          km_per_kWh = (EEPROM.read(EEPROMaddr1)) / 10.0F;
          u8g2.setFontMode(1);                           // Inverse font colour
          u8g2.setDrawColor(2);                          // Inverse screen colour
          u8g2.drawBox(0,20,127,21);
          u8g2.setCursor(0, LINE2); u8g2.print("km/kWh: ");  u8g2.print(km_per_kWh, 1);
        } else {
         u8g2.setFontMode(0);                           // standard white text on black background ("edit mode")
          u8g2.setCursor(0, LINE2); u8g2.print("km/kWh: ");  u8g2.print(km_per_kWh, 1);
        }
        // Gear position must be in Neutral to edit MaxGids
        if (rawCCStatus1 == 0 && rawCCVentTarget == 152 && rawCCVentIntake == 9 && rawGearPos == 3) {
          MaxGids = 150 + (rawCCSetpoint - 36) * 5;
          EEPROM.update(EEPROMaddr1, MaxGids);
          u8g2.setFontMode(1);                           // Inverse font colour
          u8g2.setDrawColor(2);                          // Inverse screen colour
          u8g2.drawBox(0,42,127,21);
          u8g2.setFontMode(1);                           // black text on white background ("edit mode")
          u8g2.setCursor(0, LINE3); u8g2.print("Max Gids: "); u8g2.print(MaxGids);
        } else {
          u8g2.setFontMode(0);                           // standard white text
          u8g2.setCursor(0, LINE3); u8g2.print("Max Gids: "); u8g2.print(MaxGids);
        }

        break;
    }
  } while ( u8g2.nextPage()  );


} // END MAIN LOOP
