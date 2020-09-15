// SOC% display for 2011-2012 Nissan Leafs
// =======================================
// Paul Kennett, 2019-2020
//
// Based on "CANa Display for Nissan LEAF"  https://ev-olution.yolasite.com/CANa.php © Copyright EV-OLUTION
//
// Note: later Leafs display SOC% on the main dash, so this solution is only useful for 2011-2012 Leafs
//
/*
   V41 Simple - 7 Sept 2020
   Reduced to just doing the SOC% - noting else
   removed everything else
   Larger battery image and font so it can use the small 0.96 displays

   V41 - 28 Aug 2020
   Cleaning out old experimental junk
   add some experiment pages
   add ability to chage page by: (1) Put gear in Neutral, AC On, Recirc Air,
   then change fan speed to change pages, Full fan speed -> Reset Arduino

*/

#include <Arduino.h>
#include <U8g2lib.h>           // graphics library
#include <mcp_can.h>           // OBD2 module
#include <SPI.h>               // for talking to a SPI devices 
#include <EEPROM.h>            // read/write to the EEPROM memory
#include "battery_128x56_XBMP.h" // battery image
#include "changelog.h"         // changelog and verbose notes


#define KM_KWH 6.9F            // km per kWh (from my car) used to calculate Range estimate (6.9 km.kWh is the average from May 2019 to Apr 2020)
#define WH_FACTOR 74.73F       // Wh per Gid. Some people prefer 80. 74.73 may be unrealistic precision (2 decimal places really!?)
#define MAXGIDS 215.0F         // From my car. LeafSpy showed 225 Gids at 94.8% Actual SOC on 12 Oct 2019
#define raw_Gids_turtle 8       // raw Gids-8 (the number of Gids to reach Turtle mode)
// Seems like 215 is now about my max on 26 July 2020
// See my comments ramble in readme.h file for more on defining MAXGIDS
#define CAN0_INT 2             // Set INT to pin 2 ////////// why is this set using a define statement??
#define LINE1 16
#define LINE2 38
#define LINE3 60
#define LINE4 64
#define sensorPin A0            // used to play with test mode Gids value
// could also use it for knock detection with a piezo
long unsigned int rxId;        // CAN message ID
unsigned char len = 0;         // data length
unsigned char rxBuf[8];
uint16_t rawGids;              // raw Gids
float rawGidsLow;              // raw Gids-8 (the number of Gids to reach Turtle mode)
float GidsPct;                 // Gids in percentage
uint16_t rawSoc;               // Actual state of charge
float SocPct;                  // Leaf SOC% from EV-Can. This corresponds to the LeafSpy app SOC%, but without rounding up
float SocPctSkewed;                  // Leaf SOC% from EV-Can. This corresponds to the LeafSpy app SOC%, but without rounding up
float kWh;                     // Energy left in main batery
char buffer[4];
int i;
int EEPROMaddr0 = 0;           // Address 0 in EEPROM used for Screen page number
int Page = 0;                  // Screen page state

static const int buttonPin = 9;                     // switch pin
int buttonStatePrevious = HIGH;                     // previousstate of the switch
unsigned long minButtonLongPressDuration = 2000;    // Time we wait before we see the press as a long press
unsigned long buttonLongPressMillis;                // Time in ms when we the button was pressed
bool buttonStateLongPress = false;                  // True if it is a long press
const int intervalButton = 50;                      // Time between two readings of the button state
unsigned long previousButtonMillis;                 // Timestamp of the latest reading
unsigned long buttonPressDuration;
unsigned long currentMillis;          // Variabele to store the number of milleseconds since the Arduino has started

byte rawCCStatus1;
byte rawCCVentTarget;
byte rawCCFanSpeed;
byte rawCCVentIntake;

MCP_CAN CAN0(10);              // Set MCP CS to pin 10

U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 7, /* dc=*/ 6, /* reset=*/ 5);        // OLED 128x64 moved SPI pins to keep the MCP_CAN module SPI pins unchanged


void(* resetFunc) (void) = 0;


void draw() {
  u8g2.drawXBMP( 0, 0, bitmap_width, bitmap_height, bitmap_bits);
}


void readButtonState() {
  if (currentMillis - previousButtonMillis > intervalButton) {
    int buttonState = digitalRead(buttonPin);
    if (buttonState == LOW && buttonStatePrevious == HIGH && !buttonStateLongPress) {
      buttonLongPressMillis = currentMillis;
      buttonStatePrevious = LOW; // Button pressed
    }
    buttonPressDuration = currentMillis - buttonLongPressMillis;
    if (buttonState == LOW && !buttonStateLongPress && buttonPressDuration >= minButtonLongPressDuration) {
      buttonStateLongPress = true; // Button long pressed
      EEPROM.update(EEPROMaddr0, 0);
    }
    if (buttonState == HIGH && buttonStatePrevious == LOW) {
      buttonStatePrevious = HIGH;
      buttonStateLongPress = false;
      if (buttonPressDuration < minButtonLongPressDuration) {
        EEPROM.update(EEPROMaddr0, ((EEPROM.read(EEPROMaddr0)) + 1));
        if ((EEPROM.read(EEPROMaddr0)) > 4) {
          EEPROM.update(EEPROMaddr0, 1);
        }
      }
    }
    previousButtonMillis = currentMillis;
  }
}



void setup() {
  pinMode(8, INPUT_PULLUP); // slide switch used to enter testing mode (pull D8 LOW)
  pinMode(9, INPUT_PULLUP); // push button used to page between secondary data options

  u8g2.begin();
  u8g2.setFont(u8g2_font_logisoso16_tr); // Medium font

  if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK) {
    u8g2.firstPage();
    do {
      u8g2.setCursor(0, LINE1);
      u8g2.print("LeafSOC V41");
      u8g2.setCursor(0, LINE2);
      u8g2.print("7 Sep 2020");
      u8g2.setCursor(0, LINE3);
      u8g2.print("Paul Kennett");
      if (digitalRead(8) == LOW) { // if in test mode draw a dotted frame around the screen
        for (byte i = 0; i < 63; i += 5) {
          u8g2.drawPixel(0, i);
        }
        for (byte i = 0; i < 63; i += 5) {
          u8g2.drawPixel(127, i);
        }
        for (byte i = 0; i < 127; i += 5) {
          u8g2.drawPixel(i, 0);
        }
        for (byte i = 0; i < 127; i += 5) {
          u8g2.drawPixel(i, 63);
        }
      }
    } while ( u8g2.nextPage() );
    delay(1000); // was 1000 originally (Note: When this is reduced to 0 the MCP2515 will sometimes fart out 1023 on startup)
  }
  else {
    u8g2.firstPage();
    do {
      u8g2.setCursor(0, LINE1);
      u8g2.print("MCP2515");
      u8g2.setCursor(0, LINE2);
      u8g2.print("start");
      u8g2.setCursor(0, LINE3);
      u8g2.print("ERROR!");
    } while ( u8g2.nextPage() );
    delay(10000);
  }
  CAN0.setMode(MCP_NORMAL);                     // Set to normal mode so the MCP2515 sends acks to received data.
  pinMode(CAN0_INT, INPUT);
} //end setup loop


void loop() {
  u8g2.firstPage();
  do {
      draw();

    // talking to the EV-CANbus and calculating stuff
    if (!digitalRead(CAN0_INT))
    {
      CAN0.readMsgBuf(&rxId, &len, rxBuf);        // Read data: len = data length, buf = data byte(s)

      if ((rxId & 0x40000000) == 0x40000000) {    // Determine if message is a remote request frame.
        // sprintf(msgString, " REMOTE REQUEST FRAME");
      }
      else
      {
        // GIDS from EV-CAN (500ms)
        if (rxId == 0x5bc) {
          rawGids = (rxBuf[0] << 2) | (rxBuf[1] >> 6);
          GidsPct = ((rawGids - 8) / (MAXGIDS - 8)) * 100.0F;
          kWh = (((float)rawGids) * WH_FACTOR) / 1000.0F;
        }
        // SOC from EV-CAN (100ms)
        if (rxId == 0x55b) {
          rawSoc = (rxBuf[0] << 2) | (rxBuf[1] >> 6);
          SocPct = rawSoc / 10.0F;
          SocPctSkewed = (SocPct - 5.0F) * 1.1F; // adjustuing so that it matches my SOC value
        }
        if (rxId == 0x54b) {
          rawCCStatus1 = (rxBuf[0]);
          rawCCVentTarget = (rxBuf[2]);
          rawCCVentIntake  = (rxBuf[3]);
          rawCCFanSpeed = (rxBuf[4] >> 3);
        }

      }
    }

    // Paul's testing mode ===========================================================================
    if (digitalRead(8) == LOW) { //if in test mode
      rawGids = (analogRead(sensorPin) / 4.65) + 1; //Read the potentiometer to generate rawGid numbers
      kWh = (((float)rawGids) * WH_FACTOR) / 1000.0F;
      GidsPct = ((rawGids - 8.0F) / (MAXGIDS - 8.0F)) * 100.0F;
      //draw a frame if in test mode
      for (byte i = 0; i < 63; i += 5) {
        u8g2.drawPixel(0, i);
      }
      for (byte i = 0; i < 63; i += 5) {
        u8g2.drawPixel(127, i);
      }
      for (byte i = 0; i < 127; i += 5) {
        u8g2.drawPixel(i, 0);
      }
      for (byte i = 0; i < 127; i += 5) {
        u8g2.drawPixel(i, 63);
      }
    }
    // end testing mode ==============================================================================

    // Use the Climate Controls to change pages!
    // Gear in Neutral, Climate On, Air to Recirc, then fan speed -> Page number (up to 4)
    // And if Fan Speed = 7 then the Arduino will reset itself
    if (rawCCStatus1 == 0 && rawCCVentTarget == 152 && rawCCVentIntake == 9 && rawCCFanSpeed < 5) {
      EEPROM.update(EEPROMaddr0, rawCCFanSpeed);
    } else if (rawCCStatus1 == 0 && rawCCVentTarget == 152 && rawCCVentIntake == 9 && rawCCFanSpeed == 7) {
      EEPROM.update(EEPROMaddr0, 0);
    }
    Page = EEPROM.read(EEPROMaddr0);


    // Print large SOC% numerals inside battery image
    u8g2.setFont(u8g2_font_logisoso32_tr); // Large numerals-only font
    if (rawGids != 0) {
      dtostrf(GidsPct, 3, 0, buffer);
    } else if (rawSoc != 0) {
      dtostrf(SocPctSkewed, 3, 0, buffer);
    }
    u8g2.setCursor(25, 44);
    if (rawGids != 0) { // only print numerals if there's some data and Page doesn't need the battery image
      u8g2.print(buffer);
    } else {
      u8g2.print(" --");
    }
 //   u8g2.setFont(u8g2_font_logisoso16_tr); // Medium font

    switch (Page) {

      case 0: // RESET the Arduino
        EEPROM.update(EEPROMaddr0, 1);
        resetFunc();
        break;

      case 1:  // Blank
        // intentionally blank -  no secondary data at all
        break;

    }
  } while ( u8g2.nextPage()  );

  // Button scan - used to update Page number in EEPROM
  currentMillis = millis();    // store the current time
  readButtonState();           // read the button state

} // END MAIN LOOP
