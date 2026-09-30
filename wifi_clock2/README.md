# LED Clock for ESP32 using Arduino

This is an Arduino sketch for a WiFi clock adapted from a kit with
board marked HU-058 and bought from Amazon in the USA.

https://www.amazon.com/dp/B0CNSMH2FY?ref=ppx_pop_mob_ap_share

This kit contains 33 RGB LEDs connected to two AiP33628 LED driver
chips, of which 4*7 = 28 form four 7-segment displays suitable for
showing hours and minutes of time.  These items are pre-soldered onto
the kit PC board, leaving only simple soldering to complete the clock.

It also includes a pre-programmed microcontroller.  If you solder this
in place, you will have a clock as the kit makers intend.

But this software assumes you *did not do that*.  Instead connect pins
from an ESP32-C3 super mini board to the pads where the pre-programmed
microcontroller was supposed to go.  See project.pdf for more
information.  You can follow the information there and load this
program into the ESP32, and you will have a WiFi clock.

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

When the clock is in configuration mode, you will see "C0F6"
displayed.  In this mode, the clock is acting as a WiFi access point
using SSID "clock0".  Use a phone or a computer to connect to this
WiFi network.  Then, open a browser and go to "http://clock.local" (IP
address 192.168.199.1).

Some browsers may *really* think you should not go to a site using http
instead of https.  You will have to persuade them to let you do it.

Once the configuration page is open, select your WiFi network and
enter its password.  Also, select a timezone and choose a color for
the LEDs.

Your choices are saved without encryption in ESP32 preferences flash.

Note that there is nothing secure about this clock, a flaw shared by
many IoT devices.  I put mine on my guest WiFi that has no access to
my LAN.

### Clock Mode

It's just a clock.  It's also a bit USA-centric.  It supports only
12-hour mode, though adding a 24-hour time option would be easy.

Press the top button to toggle the display on and off.  Press the
lower button to cycle among the available colors.

The clock connects to your WiFI and contacts an NTP server in the USA
to get the time.  Once it has it, it turns off the WiFi.  In normal
operation, it reconnects every 6 hours to update the time (NTP sync).

Whenever an NTP sync succeeds it also stores the time in the boards's
DS1302 clock chip, backed up by a CR1220 battery in a holder on the
board.  When the clock powers on, it takes the time from the DS1302
until it completes an NTP sync.

There is a photoresistor that senses when the clock is in a dark room.
It dims the LEDs to their minimum in the dark.  But they are still
brighter than I would like.

## How it works

The key components are the AiP33628 LED drivers and the RGB LEDs.

## Items not populated on the clock board.

I did not populate the buzzer and associated transistor and resistor.
Nor did I populate the temperature sensor.  It's possible to extend
the project to use them, but why?  The buzzer does not have a nice
sound, and the temperature sensor will not be very accurate without
special calibration.