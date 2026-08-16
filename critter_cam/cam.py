#!/usr/bin/python3

# Run with --day for day operation

import argparse
from datetime import datetime
import time
from gpiozero import LED
from libcamera import Transform
from picamera2 import Picamera2
from smbus2 import SMBus

# --- CONFIGURATION ---
TFLUNA_I2C_ADDR = 0x10  # Default I2C address for TF-Luna

# Set TF-Luna distance range here
MIN_DIST_CM = 155  # Minimum distance trigger (cm)
MAX_DIST_CM = 260  # Maximum distance trigger (cm)

MIN_SIGNAL_STRENGTH = 140  # Ignore weak reflections / ambient sunlight
COOLDOWN_SECONDS = 1.0  # Pause between captures when triggered (sec)

LIGHT_GPIO_PIN = 4  # GPIO pin controlling the light
LIGHT_ON_DURATION = 60.0  # Seconds (1 minute) light remains on after capture
IRCUT_GPIO_PIN = 27 # High means IR filter is in place (for daylight shots)


def init_camera(is_day):
    """Initializes Picamera2 headless for fast capturing on Pi Zero."""
    picam2 = Picamera2()

    # Limit buffers to 2 to optimize RAM usage on 512MB Pi Zero
    still_config = picam2.create_still_configuration(
        buffer_count=2, transform=Transform(vflip=1, hflip=1)
    )
    picam2.configure(still_config)

    # Start headless (skips display driver preview probing delay)
    picam2.start(show_preview=False)

    if is_day:
        # Set parameters for daylight operation here
        picam2.set_controls({
            "AeEnable": True,
            "ExposureValue": 0.0,
            "AwbEnable": True,
            "NoiseReductionMode": 0,  # Turn off ISP denoise to save CPU
        })
    else:
        # Set parameters for night here.  Not using auto.  You must
        # experiment
        picam2.set_controls({
            "ExposureTime": 50000, # In microsec
            "AnalogueGain": 4.0,
            "AeEnable": False,
            "ExposureValue": 4.0,
            "AwbEnable": False,
            "ColourGains": (1.5, 1.5),
            "NoiseReductionMode": 0,  # Turn off ISP denoise to save CPU
        })
    time.sleep(0.2)
    return picam2


def read_tfluna(bus):
    """Reads distance (cm) and signal strength (flux) from the TF-Luna.

    Returns (distance, strength) or (None, None) if read fails.
    """
    try:
        # Read 4 registers starting at 0x00:
        # 0x00: Dist_L, 0x01: Dist_H, 0x02: Flux_L, 0x03: Flux_H
        data = bus.read_i2c_block_data(TFLUNA_I2C_ADDR, 0x00, 4)
        distance = data[0] + (data[1] << 8)
        strength = data[2] + (data[3] << 8)
        return distance, strength
    except Exception:
        return None, None


def capture_photo(picam2):
    """Saves a timestamped JPEG without overwriting previous files."""
    now = datetime.now()
    timestamp = (
        now.strftime("%Y%m%d_%H%M%S_") + f"{now.microsecond // 1000:03d}"
    )
    filename = f"critter_{timestamp}.jpg"

    picam2.capture_file(filename)
    print(f"[TRIGGERED] Image saved to {filename}")


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Critter cam"
    )

    parser.add_argument(
        "--day",
        action="store_true",
        help="Configure for daylight operation",
    )

    return parser.parse_args()

def main():
    args = parse_arguments()

    print("Initializing GPIO 4 Light Control...")
    # Explicitly set initial_value=False and force off state immediately
    light = LED(LIGHT_GPIO_PIN, initial_value=False)
    light.off()
    
    ircut = LED(IRCUT_GPIO_PIN, initial_value=True);
    if args.day:
        ircut.on()
    else:
        ircut.off()

    print("Initializing camera...")
    cam = init_camera(args.day)

    print("Opening I2C bus...")
    bus = SMBus(1)  # Raspberry Pi I2C Bus 1

    last_capture_time = 0
    light_off_time = 0  # Timestamp when the light should turn off

    print("Monitoring distance (4 Hz)... Press Ctrl+C to stop.")
    try:
        while True:
            loop_start = time.time()

            # 1. Manage Light Auto-Off Timer
            if light.is_lit and loop_start >= light_off_time:
                light.off()
                print("[LIGHT] 1 minute timer expired. Light turned OFF.")

            # 2. Read Distance Sensor
            dist, flux = read_tfluna(bus)

            if dist is not None and flux is not None:
                # Filter out bad signal readings (0xFFFF or low flux)
                if flux >= MIN_SIGNAL_STRENGTH and dist > 0:
                    print(f"Distance: {dist} cm | Signal: {flux}")

                    # Check if target is in distance range
                    if MIN_DIST_CM <= dist <= MAX_DIST_CM:
                        if (loop_start - last_capture_time) >= COOLDOWN_SECONDS:

                            # Turn light ON before capture if it's currently off
                            if not light.is_lit:
                                print("[LIGHT] Turning ON before capture...")
                                light.on()
                                # Short 50ms pause to ensure light/LED is fully energized
                                time.sleep(0.5)

                            # Capture the image
                            capture_photo(cam)

                            # Record completion time and set light off time to 60s from NOW
                            completion_time = time.time()
                            last_capture_time = completion_time
                            light_off_time = (
                                completion_time + LIGHT_ON_DURATION
                            )

                            off_str = datetime.fromtimestamp(
                                light_off_time
                            ).strftime("%H:%M:%S")
                            print(
                                f"[LIGHT] Timer reset. Light will stay ON until {off_str}"
                            )

                        else:
                            print(" -> Target in range, but in cooldown.")

            # Sleep enough to maintain ~4 Hz sampling rate (0.25s period)
            elapsed = time.time() - loop_start
            sleep_time = max(0.0, 0.25 - elapsed)
            time.sleep(sleep_time)

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        # Cleanup hardware on exit
        light.off()
        light.close()
        bus.close()
        cam.stop()

if __name__ == "__main__":
    main()
