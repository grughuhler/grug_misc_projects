Python3 script cam.pay implements a critter camera on a Raspberry Pi.

See YouTube videos: https://youtu.be/98HG3qutKg0 (stills cam.py)
                    https://youtu.be/oKp8FjOudYY (video vid_crit.py)

Program cam.py
--------------

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

I decided to remove this section becuase the suggestion is somewhat dangerous
due to the lack of LED current limiting in the Arducam design.  The approach
described in the video is probably safer, but a completely different design
for the IR illuminator would be my preference.

Program vid_crit.py
-------------------

This program is similar to cam.py but takes video instead of still
images.  When the TF-Luna detects a target within a distance range, it
starts a 30 second video capture.

If run with --day, the LEDs remain off and the IR filter is in place.

The TF-Luna is disabled while video is being captured.  It starts
again right after a video capture completes and can start another
video.

The LEDs will not run for more than two minutes.  After running for
two minutes, there is a 1 minute cooldown period.

The TF-Luna distance range to start a video is defined by constants in
the code.

For infrared video at night, the IMX462-based camera is far superior
to the OVS5647-based camera.

Use a Raspberry Pi Zero 2 or a more powerful Raspberry Pi.

Program vid_crit.py has been tested only on Raspbian Trixie and only
using the home-made IR illuminators described in the second video.
