LeafSOC embedded dash display
=============================
Updated 7 Julu 2021

See also: http://www.myonlinediary.com/index.php/Energy/SOCDisplay
And https://youtu.be/gQJ3Da4Tbn0?t=80

This project started out to simply add a SOC% display to my 2011 Nissan Leaf. It's grown to now include a range estimator
(slightly better than the Nissan "Guess-O-Meter").

The first generation Leafs did not show the battery State of Charge accurately on the dash. There's the 12 blue bars on the 
right by the range estimate but that's all. After 2012 they added a SOC% feature to the main dash. This project replicates 
that feature.

Most people just use the LeafSpy app on a phone to address this shortcoming, but I wated something integrated into the dash 
with no extra fiddling about. It should just turn on when the car does and require zero effort on my part.

This project was based on the CANa Display for Nissan LEAF project which is beautifully simple and straightforward. All I've 
done is change the code to make it work with a nice OLED display that will fit inside the eyebrow dash - and then fiddle with 
the code to add enhancements. It is powered by the OBD2 port in such a way that it only works when the car is turned on. 
[Note: some of the dongles that are used for LeafSpy can run 24/7 unless you manually turn them off - which is a security risk.]

Note: I'm a poor and slow programmer - my process is cut-n-paste and see if it works. So if you find stuff in the code that looks 
stupid it probably is. If you can see a better way to do it feel free to contribute your code improvements.

The code is now setup for a cheap 1.30 inch OLED with the SH1106 driver. If you want to use a 0.96 or 1.54 inch display (with the
SSD1306 driver) you'll just need to comment/uncomment the correct line to get the U8G2 graphics library working. It's explained
in my code fairly well.

How to change pages and set the default km/kWh value (which is used to calculate the range):
============================================================================================
To enter setup mode
1. Turn Climate ON
2. Turn AC OFF
3. Select Recirculation air
4. Select Feet only mode
To change pages
1. Press the Fan speed up or down to select pages
   1. Range & SOC% & kWh
   2. SOC% & kWh
   3. Range only
   4. Software version, date and author
   5. Misc data values for experimenting
   6. Set default km/kWh - to edit the value you need to:
   (a) put the car in Neutral, then
   (b) use the tempurature buttons to change the value up or down
   7. Reset the Arduino [Note: the Arduino will keep reseting itself util you change the fan speed or restart the car]

Thanks, Paul
paul at kennett dot co dot nz

Hardware
--------
These parts are available from Aliexpress, Amazon, EBay, TradeMe, Banggood, etc.
* 10-16V to 5V DC-DC step down Buck converter
* NiRen MCP2515_CAN bus board 8MHz
* Arduino Micro 328, 5V/16MHz version 
* OLED display 128x64 pixel, 1.30 inch
* OBD2 plug (male)
* 7-way ribbon cable, 60cm long
* 40 Pin 2.54 mm Right Angle Single Row Pin Header Male
* Dupont-style jumper wires
* a plastic box to house the electronic bits

Optional:
* 3D printed shroud to cover the display inside the dash (STL file included for the 0.96, 1.30 and 1.54 inch 128x64 OLED displays
* OBD2 Splitter Extension Cable
* PCB motherboard (Gerber file: https://github.com/PaulKennett/LeafRange/blob/main/Gerber_PCB_LeafRange%20Micro%20v0.3C_2022-04-28.zip)

Circuit description
-------------------
Power for the system is provided from the OBD2 port - pin 4 (ground) and pin 8 (switched +12 from battery). Note pin 8
is a switched +12 on the Leaf. The OBD2 standard power is on pin 16 - but it's always on 24/7.

The power goes straight into a step down module that provides 5V for everything; the MCP2515_CAN, the Arduino Pro Mini 
and the OLED display.

The Leaf EV-CAN signals come via OBD2 pins 12 and 13. The OBD2 standard CAN bus signals are on pins 6 and 14 - which, 
in the case of the Leaf gives the Car-CAN bus.

The EV-CAN signals go into the MCP2515_CAN module - which is connected to the Arduino via 5-wire SPI.

If you want a fancy looking setup use the PCB motherboard I've designed for the LeafRange project - it's the same 
hardware and has jumpers for switching between EV-CAN and Car-CAN data buses. See https://github.com/PaulKennett/LeafRange

3D printed frame
----------------
I've made some STL files so you can 3D print a nice covering frame for the display. I've made horzontal versions for 
0.96 inch, 1.30 inch and 1.54 inch 128x64 OLED displays. The 1.54 inch OLED display it'll *only* git horizontally
and you have to seperate the glass display from the PCB. The 1.30 inch display is the best option.

Installation
------------
I chose to install the OLED display over the top of the efficiency trees on the Leaf's "eyebrow" dash. The trees don't 
serve a critical function so it's no great loss. I stuck a piece of wide black electrical tape over the trees, then 
used double sided tape to stick the OLED display onto the black tape. (Note: electrical tape is usually easy to remove 
so you could remove all this if you needed to.)

To fit the 1.30 inch OLED display module inside the 3D printed frame I've provided you'll need to sand down the top 
right and bottom left of the PCB.

To install the display inside the dash you'll need to remove the dash and unclip the clear plastic front. See "Nissan 
Leaf Instrument Cluster Removal" on Youtube for an explanation of how to remove the dash. Then remove the black front
trim piece - it's also just clipped on.

I used a craft knife to trim 1mm off the inside edge of the dash to allow the 7-wire ribbon cable to exit cleanly. Clip 
the dash back in place and thread the ribbon cable down past the lower dash.

Next you'll want to remove the lower dash panel, see "Nissan Leaf 020 - ETC replacement options" on Youtube. Once that's 
open you can attach the OLED ribbon to the Arduino and zip-tie the electronic modules (in a plastic box maybe) somewhere 
where they won't rattle and then connect the OBD2 plug to the socket.

Paul Kennett
20 May 2021
