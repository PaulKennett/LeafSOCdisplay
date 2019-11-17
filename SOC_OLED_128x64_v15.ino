// SOC% display for 2011 and 2012 Nissan Leafs
// ===========================================
//
// Note: later Leafs display SOC% on the main dash, so this solution is only useful for 2011-2012 Leafs
//
// Shows SOC%, Gids and kWh on a single OLED 128x64 display
//
// Use a 1.3 inch 128x64 pixel OLED display and install it INSIDE the top "eyebrow" dash covering 
// up the efficiency trees part of the diasplay.
// Connect the OLED display to an Arduino Pro Mini using ribbon cable.
//
// Based on "CANa Display for Nissan LEAF"  https://ev-olution.yolasite.com/CANa.php © Copyright EV-OLUTION
//
// Modified by Paul Kennett October 2019 to talk to a standard/cheap 128x64 OLED dsiplay.
// I also I've removed the input push-button switch that allowed you to select addition pages.
//
// V15
// 21 Oct 2019 - change to useing Gids% as state of charge?
// renamed variables Soc to rawGids
// renamed SocPct to GidsPct
// Set Max Gids to 226 (my Leaf shows 225 Gids at 94.8% actual SoC)
// Offset 0% Gids to be equivalent to 8 Gids 
// My SOC% formula is now GidsPct = ((rawGids-8) / (MAX_GIDS-8)) * 100.0F;
// Removed unused variables


#include<Arduino.h>
#include<U8g2lib.h>             // graphics library
#include <mcp_can.h>            // OBD2 module
#include<SPI.h>                 // for talking to SPI devices 
//#include<Wire.h>              // for talking to I2C devices


// CANa V1.0 variables
long unsigned int rxId;        // CAN message ID
unsigned char len = 0;         // data length
unsigned char rxBuf[8];
char msgString[128];           // Array to store serial string
float BattVolts;               // Main battery voltage
uint16_t rawGids;              // Gids
float GidsPct;                 // Gids in percentage
uint16_t rawSoc;               // Actual state of charge
float SocPct;                  // Acual state of charge percentage. This corresponds to the LeafSpy app SOC%, but without rounding up
float Amps;                    // Main battery current
float kWh;                     // Energy left in main batery

/*
#define MAX_GIDS 281.0F       // 281 Gids correspondes to a brand new 24 kWh Leaf battery
After slow charging my car to "100%" the actual SOC was 94.8% and 225 Gids. We know that 
the Leaf only ever charges to 95% in real life so my full battery is about 226 Gids capacity.
So by setting MAX_GIDS to 226 my display will show 100% when the battery is fully charged.
Note: there is a lot of potential for confusion about what SOC% charge means.
1. rawSOC pulls a SOC number out of the Leaf EV-Can bus. raw SOC ranges from 95% (full) to 5% 
   (dead flat)
2. the Leaf dash on 2013 and later models shows a SOC number that is adjusted so that "100%" is 
   displayed when the battery is charged to its maximum allowed. As the battery ages that will 
   occur at slowly decreasing capacities. Plus Nissan does not allow the Leaf to charge the cells 
   to the maximum 4.2V that they could be. 2013 dash SOC ranges from 100% to 0%.

My goal is to have battery capacity represented in a single number that ranges from 0 to 100. Where 
100 means the battery is as full as it can be. And 0 occurs at "Turtle mode".
I can either base this on raw Gids from the EV-Can bus or raw SOC from the EV-Can bus. 
Currently I'm using Gids as my base (becasue I'm less confident on the raw SOC number at the low end.)
- Turtle mode occurs at 8 Gids, so that is my 0%
- below 8 Gids the car monitors the voltage of the cells in the main battery and stops the car when 
  the weaks cells voltage drops below some threshold.
I set my 100% to be (just above) the number of Gids that I get after a "100%" slow charge. In my case 
that was 225 Gids and the rawSOC from EV-Can was showing 94.8%
So: my 0% = 8 Gids and my 100% = 226 - 8 Gids.
*/


#define MAX_GIDS 226.0F         // My Leaf Gids at 94.8% Actual SOC on 12 Oct 2019 was 225
#define KW_FACTOR 74.73F       // Some people prefer 80
#define CAN0_INT 2             // Set INT to pin 2
MCP_CAN CAN0(10);               // Set CS to pin 10
// end CANa variables


// U8G2 for Arduino Pro Mini //////////////////////////////
//  U8G2_PCD8544_84X48_1_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);           // Nokia 5110 Display
//  U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R2, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);   // OLED 128x64 standard connections
U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R2, /* cs=*/ 7, /* dc=*/ 6, /* reset=*/ 5);        // OLED 128x64 moved SPI pins to keep the MCP_CAN module SPI pins unchanged

// U8G2 for Arduino Pro Micro //////////////////////////////
// ??



// Battery image bitmap
#define bitmap_width 128
#define bitmap_height 44
static const unsigned char bitmap_bits[] U8X8_PROGMEM = {
  0x00, 0x00, 0x00, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x1E, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x03, 
  0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 
  0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 
  0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 
  0x00, 0x80, 0x0D, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x06, 0xFC, 0x87, 0x0D, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0xFE, 0x8F, 0x0D, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 
  0xFE, 0xCF, 0x1F, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0xFC, 0x06, 0xCC, 0x1F, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x06, 0xE0, 0x06, 0xCC, 0x1F, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0x06, 0xC0, 
  0x06, 0xCC, 0x1F, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x33, 0x03, 0xC0, 0xFE, 0xCF, 0x1F, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0x03, 0xC0, 0xFE, 0xCF, 0x1F, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB3, 0x01, 0xC0, 
  0xFE, 0x9F, 0x0F, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x9E, 0x01, 0xC0, 0xFE, 0x3F, 0x07, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0xC0, 0xFE, 0x2F, 0x02, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0xC0, 
  0xFE, 0x2F, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x60, 0x00, 0xC0, 0xFE, 0x2F, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0xC0, 0xFE, 0x2F, 0x02, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0xC0, 
  0xFE, 0x6F, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x30, 0x00, 0xE0, 0xFE, 0xCF, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0xFC, 0xFF, 0x1F, 0x00, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x7E, 
  0xFF, 0x1F, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0xCC, 0x03, 0x0E, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x6C, 0x06, 0x06, 0x00, 0x00, 0x00, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x06, 0x06, 
  0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x66, 0x06, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x06, 0x06, 0x00, 0x00, 0x00, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3, 0x03, 0x06, 
  0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 
  0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x1E, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x03, 
  0x00, 0x00, 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, };




void draw(void) {
 // graphic commands to redraw the complete screen should be placed here  
 u8g2.drawXBMP( 0, 0, bitmap_width, bitmap_height, bitmap_bits);
}
 
void setup(void) {
 u8g2.begin();

// Welcome startup page 
  u8g2.setFont(u8g2_font_helvR10_tr); // Small font

  u8g2.firstPage();
  do {
    u8g2.setCursor(0, 15);
    u8g2.print("EV-olution.Net");
    u8g2.setCursor(0, 35);
    u8g2.print("CANa  Ver: 1.0");
    u8g2.setCursor(0, 55);
    u8g2.print("PK Ver: 13");
  } while ( u8g2.nextPage() );
  delay(1); // how long the welcome screen is on. Make it 1000 during testing, then reduce to 1 when finished 
// end Welcome page

// Coneection to MCP CANbus module success notice
  if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK){
       u8g2.firstPage();
      do {
        u8g2.setCursor(0, 20);
        u8g2.print("MCP2515 start up");
        u8g2.setCursor(0, 40);
        u8g2.print("OK");
      } while ( u8g2.nextPage() );
        delay(1);                                // Could reduce this to 0, was 1000 originally 
  }
  else {
       u8g2.firstPage();
      do {
        u8g2.setCursor(0, 20);
        u8g2.print("MCP2515 start up");
        u8g2.setCursor(0, 40);
        u8g2.print("ERROR!");
      } while ( u8g2.nextPage() );
        delay(5000);
  }
  CAN0.setMode(MCP_NORMAL);                     // Set operation mode to normal so the MCP2515 sends acks to received data.
  pinMode(CAN0_INT, INPUT);                     // Configuring pin for /INT input

} //end setup loop



void loop(void) {

 u8g2.firstPage();
 
 do {
      if(SocPct != 0){                          // don't print battery image if there's no data yet
         draw();
      }
      
// talking to the CANbus and caclulating stuff
  if(!digitalRead(CAN0_INT))                    // If CAN0_INT pin is low, read receive buffer
  {
    CAN0.readMsgBuf(&rxId, &len, rxBuf);        // Read data: len = data length, buf = data byte(s)
    
    if((rxId & 0x40000000) == 0x40000000){      // Determine if message is a remote request frame.
      sprintf(msgString, " REMOTE REQUEST FRAME");
    } 
    else {
      // If MCP is connected to Vehicle-CAN on OBD2 socket then just show 12V battery voltage
      // For the full functionality you need to connect to the EV-Can pin on the OBD2 socket 
      if(rxId == 0x292){
      //  Batt12 = (rxBuf[3]) / 10.00;
      }

////////////////// For all next readings MCP must be connected to EV CAN //////////////////////////
      // GIDS and kWh remaining
      if(rxId == 0x5bc) {
        rawGids = (rxBuf[0] << 2) | (rxBuf[1] >> 6);
        GidsPct = ((rawGids-8) / (MAX_GIDS-8)) * 100.0F;
        kWh = (((float)rawGids) * KW_FACTOR) / 1000.0F;
      }
      // Actual SOC from EV-CAN
      if(rxId == 0x55b) {
        rawSoc = (rxBuf[0] << 2) | (rxBuf[1] >> 6);
        SocPct = rawSoc / 10.0F;
      }
    }
  }

    
// Paul's code updated for 128x64 OLED display


// just for testing
SocPct = 94.8F; rawGids = 5; kWh = (((float)rawGids) * KW_FACTOR) / 1000.0F; 
GidsPct = ((rawGids-8) / (MAX_GIDS-8)) * 100.0F;
// end just for testing


      if(GidsPct != 0){ // don't print Soc numerals if there's no data yet
         //  Main Gids% numerals
         u8g2.setFont(u8g2_font_logisoso28_tn); //Large numerals only
         u8g2.setCursor(35, 36); // x, y for lower left corner of digits
         if (GidsPct < 10) {
            u8g2.print(GidsPct);       
         } else if (GidsPct < 100) {
            u8g2.print(GidsPct, 1); 
         } else {
            u8g2.print(GidsPct, 0); 
         }
      }

      if(rawGids != 0){ // don't print Gids and kWh if there's no data yet
      // Print Gids and kWhs
      u8g2.setFont(u8g2_font_helvR12_tr); // Small font
      if(rawGids < 10){
        u8g2.setCursor(18, 64); // set print location for single digit Gids
      }
      else if(rawGids < 100){ // set print location for double digit Gids
        u8g2.setCursor(9, 64);
      }
      else { 
        u8g2.setCursor(0, 64); // set print location for triple digit Gids
      }
      u8g2.print(rawGids); // the value of Gids
      
      u8g2.setFont(u8g2_font_helvR10_tr); // Smaller font
      u8g2.setCursor(29, 64);
      u8g2.print("Gids"); // the units label

      // print Actual SOC% from EV-Can
      u8g2.setFont(u8g2_font_helvR12_tr); // Small font
      if(SocPct < 10){
         u8g2.setCursor(78, 64);
      } else {
         u8g2.setCursor(69, 64);
      }
      u8g2.print(SocPct,1);

      u8g2.setFont(u8g2_font_helvR10_tr); // Smaller font
      u8g2.setCursor(102, 64);
      u8g2.print("SoC"); //units label
      } else { // when there's no don't show anything
         /*
         u8g2.setFont(u8g2_font_helvR12_tr); // Small font
         u8g2.setCursor(0, 60);
         u8g2.print("Loading..."); 
         */
       }

    } while( u8g2.nextPage()  );
 
}
