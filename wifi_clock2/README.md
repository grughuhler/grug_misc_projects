# LED Clock for ESP32 using Arduino

This is an Arduino sketch for a WiFi clock adapted from a kit with a
board marked HU-058 and bought from AlliExpress in the USA.  I have
also seen it on Amazon for a higher price.

https://www.amazon.com/dp/B0CNSMH2FY?ref=ppx_pop_mob_ap_share

Adrian's digital basement on YouTube has a relvant video:
https://youtu.be/vgfV_IGmhGA?si=3Ben4BfNi_WUBw8p. Adrian used software
for Home Assistant.  

The kit contains 33 RGB LEDs connected to two AiP33628 LED driver
chips, of which 4*7 = 28 form four 7-segment displays suitable for
showing hours and minutes of time.  These items are pre-soldered onto
the kit PC board, leaving only simple soldering to complete the clock.

It also includes a pre-programmed microcontroller.  If you solder this
into place, you will have a clock as the kit makers intend.

But this software assumes you *did not do that*.  Instead connect pins
from an ESP32-C3 super mini board to the pads where the pre-programmed
microcontroller was supposed to go.  Unlike Adrian, I built a small PC
board to help with this.  The gerber files are in the zip file.  You
can use this to order a board from Osh Park, DK Red, PCBWay, etc.

## Operation of the Clock

The clock has two modes: configuration and normal clock mode

### Configuration Mode

You must configure the clock with WiFi credentials and a timezone
setting before it will operate.  It must connect to the internet via
WiFi because it uses the NTP protocol to obtain the time.  There is
no other way to set the clock.

To do this, you must enter configuration mode.  Do this by pressing
either clock button while powering on or by pressing and holding
either button until it reboots into configuration mode.

When the clock is in configuration mode, you will see "COnF"
displayed.  In this mode, the clock is acting as a WiFi access point
using SSID "clock1".  Use a phone or a computer to connect to this
WiFi network.  Then, open a browser and go to "http://clock.local" (IP
address 192.168.199.1).

Some browsers may *really* think you should not go to a site using http
instead of https.  You will have to persuade them to let you do it.

Once the configuration page is open, select your WiFi network and
enter its password.  Also, select a timezone and choose a color for
the LEDs.  Then, clink on save and reboot.

Your choices are saved without encryption in ESP32 preferences flash.

Note that there is nothing secure about this clock, a flaw shared by
many IoT devices.  I put mine on my guest WiFi that has no access to
my LAN.

### Clock Mode

Press and release the top button to cycle the clock color among red,
green, and blue.

The bottom button cycles among displaying the time, the temperature,
and turning the display off (the clock remain on).

The clock connects to your WiFI and contacts an NTP server to get the
time.  Once it has it, it turns off the WiFi.  In normal operation, it
reconnects every 6 hours to update the time (NTP sync).

Whenever an NTP sync succeeds it also stores the time in the boards's
DS1302 clock chip, backed up by a CR1220 battery in a holder on the
board.  When the clock powers on, it takes the time from the DS1302
until it completes an NTP sync.

There is a photoresistor that senses when the clock is in a dark room.
It dims the LEDs to their minimum in the dark.  But they are still
brighter than I would like.

