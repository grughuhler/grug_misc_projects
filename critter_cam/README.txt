Python3 script cam.pay implements a critter camera on a Raspberry Pi.

See YouTube video: https://youtu.be/98HG3qutKg0

There is a better way to control the IR LED illuminators.  See end
of this document.

Tested on Raspbian 12 (bookworm) and 13 (trixie).  Will fail on
Raspbian 11

Prerequisites

  sudo apt install python3-gpiozero python3-picamera2 python3-smbus
  Use raspi-config to enable i2c
  Hardware: TF-Luna connected via i2c, an RPI Camera with "IRCUT"
            control.

Running

  Run with ./cam.py or ./cam.py --day

  The --day option leaves the IR filter in place and uses automatic
  camera settings.

Function

  The program continuously queries a TF-Luna to see if there is an
  object within a range that you set by editing cam.py.  If so, it
  turns on LED IR illuminators using GPIO 4.  It then takes pictures
  at an adjustable rate until the object no longer has the correct
  range.  A minute after than, the illuminators are turned off.

  Photos are written to files in the directory from which cam.py was
  run.  They are named critter_<timestamp>.jpg.

  You need to carefully aim the TF-Luna and adjust parameters in
  cam.py for distance and camera settings.

Better IR LED Control
---------------------

The video treats the IR LED illuminators as black boxes and uses a
MOSFET to turn them on an off.  I have since reverse engineered
them, and there is a better way.  See better_ir_led_control.jpg.

Once you desolder the LED module photoresistors, you will see a square
hole and a round hole.  The square hole connects to the base of an NPN
transistor with a 470K pull-up.  You can connect this to a GPIO on the
Raspberry Pi, but configure the GPIO to emulate open drain.  You can
google how to do this.  When the GPIO is set LOW, it actively pulls the
signal to ground.  This turns off the LED.  When it is set HIGH, it
goes into a high Z state (because of open drain) and allows the signal
to be pulled high by the 470K resistor.  This turns on the LED.

Software cam.y MUST be modified to emulate open-drain (not done in
current code).  Also, never drive the IR modules with more than 3.3V
or two bad things are likely: 1) you will blow up the RPI GPIO,
2) the IR LED will likely burn out.  The IR module uses a poor
design without proper current limiting to the LED.
