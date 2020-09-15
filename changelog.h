/*
  Shows SOC% (based on GIDs) inside large battery symbol, then in smaller text pages through Gids, kW and kWh

  Uses a 0.96 or 1.3 inch 128x64 pixel OLED display and install it INSIDE the top "eyebrow" dash covering
  up the efficiency trees part of the diasplay.
  Connect the OLED display to an Arduino Pro Mini using ribbon cable.

  Based on "CANa Display for Nissan LEAF"  https://ev-olution.yolasite.com/CANa.php � Copyright EV-OLUTION
  Modified by Paul Kennett October 2019 to talk to a standard/cheap 128x64 OLED dsiplay.
  See http://www.myonlinediary.com/index.php/Energy/SOCDisplay for my project page

  -----------------------------------------------------------------------------------------------------------------

   V41 - 28 Aug 2020
   Cleaning out old experimental junk
   add some experiment pages
   add ability to chage page by: (1) Put gear in Neutral, AC On, Recirc Air,
   then change fan speed to change pages, Full fan speed -> Reset Arduino

  V40 - 27 Aug 2020
  From turn on, screen shows my SOC% sourced from SocPct, until Gids data becomes available (sometimes several seconds)

  V39 - 26 Aug 2020
  removed a few page options
  Long press (2 secs) to reset, short press to cycle through pages - so now you can cycle through
  the pages without reseting

  V38 - 23 Aug 2020
  Changed button reading routine for one based on millis, can now do short and long press options
  preparing to add a customise_settings feature
  depricating the kW and A&V options
  new button routine does NOT speed up the firt reading of Gids - EV-SoC still comes up faster!
  considering reading EV-SoC and using that for mySoC and Range estimates until Gids available?

  V37
  Added Constants page

  V35 - 14 Aug 2020
  Does Odo work on EV-Can?

  V34 - 8 Aug 2020
  changed Page 7 to show myRange, mySOC, rawGids, SocPct, and kWh to amke it easier for me to record
  changed "Approx" to "Range" - it looks like mine is at least better than the Guess-o-meter

  V33 - 6 Aug 2020
  removed the battery shading
  tried changing my SOC to come from SocPct because I thought it would be faster - but it wasn't so revered to rawGids
  rearranged page order a wee bit

  V32C - 4 Aug 2020
  testing skewed LeafSOC to see if it can be a quicker SOC display

  V32B - 3 Aug 2020
  fixed the "8 (0) Gids" freeze bug (I was subtracting an INT from a FLOAT variable)
  ensured it does not display "0" data fields when there is no rawGids data yet

  V31 - 1 Aug 2020
  changing page selection from IF statments to SWITCH statements
  added slide switch on motherboard to activate "test mode" ON/OFF via D8

  V30 - 29 July 2020
  Add shading to battery image to also indicate SOC%
  Renamed "Range" on UI to "Approx:"
  Changed code that displays the main SOC% to use dtostrf(GidsPct, 3, 0, buffer) - more efficient code
  removed auto-displaying of regen when regening
  removed auto-displaying of Gids when below 25 Gids

  V29 - 25 July 2020
  Added my own Range guess based on Gids

  V28 - 18 July 2020
  Regen now pops up at < -1 kW

  V27 - 7 July 2020
  trying to optimise a wee bit - consolidate font sizes

  V27 - 7 July 2020
  Moved intro text to show during MCP2515 startup
  When rawGids < 25 display colour inverts as a warning and Gids-8 is shown in breckets with Gids info
    Note: the car enters Turtle mode at 6 to 8 Gids so it's useful to show (Gids-8) counting down to 0
    Eg. "20 (12) Gids!"

  V26 - 6 July 2020
  change secondary data feilds to automatically pop up to:
  - show kW when regening
  - show Gids when Gids are less than 25 (Very Low Battery Warning point is at 24 Gids)

  V25 - 4 July 2020
  Moved my version number and date to the 1 second "slpash screen"
  Display re-organisation so that it now only pages the smaller text below the battery. This means it always shows rthe battery SOC%.
  Paging is just to change the secondary info.
  Reinstate page state held in EEPROM

  v23
  use EEPROM to hold page state

  V22
  Tweak battery icon
  Added software reset on "page 5" and resets page counter to 1

  V21
  Right align kW and Amps

  V20
  Tried (and failed) to add Battery Temp page (requires requesting data from EV-CAN)

  V19
  Fiddling with layout

  V18
  Playing with EEPROM

  V17
  Added a button for switching pages

  V16
  Removed the kWh to further simplify the display

  V15 - 21 Oct 2019
  change to useing Gids% as state of charge?
  renamed variables Soc to rawGids
  renamed SocPct to GidsPct
  Set Max Gids to 226 (my Leaf shows 225 Gids at 94.8% actual SoC)
  Offset 0% Gids to be equivalent to 8 Gids
  My SOC% formula is now GidsPct = ((rawGids-8) / (MAX_GIDS-8)) * 100.0F; [8 Gids is about where Turtle mosde hits]
  Removed unused variables


  -----------------------------------------------------------------------------------------------------------------

  About defining Max Gids
  -----------------------
  #define MAX_GIDS 281.0F       // 281 Gids correspondes to a brand new 24 kWh Leaf battery
  or
  #define MAX_GIDS 225.0F       // The highest number of Gids in my Leaf - Oct 2019
  #define MAX_GIDS 220.0F       // The highest number of Gids in my Leaf - Jan 2020
  #define MAX_GIDS 214.0F       // The highest number of Gids in my Leaf - Aug 2020

  After slow charging my car to "100%" recently the actual SOC was 94.8% and 225 Gids. We know that
  the Leaf only ever charges to 95% in real life. Therefore by setting MAX_GIDS to 225 my display
  will show 100% when the battery is fully charged.
  Note: there is a lot of potential for confusion about what SOC% charge means.
  1. rawSOC pulls a SOC number out of the Leaf EV-Can bus. raw SOC ranges from 95% (full) to 5%
   (car stops moving)
  2. the Leaf dash on 2013 and later models shows a SOC number that is adjusted so that "100%" is
   displayed when the battery is charged to its maximum allowed. As the battery ages that will
   occur at slowly decreasing capacities. Plus Nissan does not allow the Leaf to charge the cells
   to the maximum 4.2V that they could be. 2013 dash SOC ranges from 100% to 0%.

  My goal is to have battery capacity represented by a single number that ranges from 0 to 100. Where
  100 means the battery is as full as it can be. And 0 occurs at "Turtle mode".
  I can either base this on raw Gids from the EV-Can bus or raw SOC from the EV-Can bus.
  Currently I'm using Gids as my base (becasue I'm less confident on the raw SOC number at the low end.)
  - Turtle mode occurs at 6-to-8 Gids, so 8 Gids is my 0%
  - below 8 Gids the car monitors the voltage of the cells in the main battery and stops the car when
  the weaks cells voltage drops below some threshold.
  I set my 100% to be the number of Gids that I get after a "100%" slow charge. In my case
  that was 225 Gids (the rawSOC from EV-Can was showing 94.8%) in Oct 2019
  So: my 0% = 8 Gids and my 100% = 225 - 8 Gids.
*/
