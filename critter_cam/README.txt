Python3 script cam.pay implements a critter camera on a Raspberry Pi.

See YouTube video: https://youtu.be/98HG3qutKg0

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
