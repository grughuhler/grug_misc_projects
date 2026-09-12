#!/usr/bin/env python3
"""
===============================================================================
IMX462 & TF-Luna Automated Recording Script (Raspbian Trixie / Debian 13)
===============================================================================

Required Packages:
    Install system dependencies via apt:
        sudo apt update
        sudo apt install -y python3-picamera2 python3-libcamera python3-gpiozero python3-smbus2 i2c-tools ffmpeg

    Enable I2C and Camera interfaces (if not already enabled):
        sudo raspi-config nonint do_i2c 0
        sudo raspi-config nonint do_camera 0

Usage:
    Day Mode:   python3 vid_crit.py --day
    Night Mode: python3 vid_crit.py
===============================================================================
"""

import argparse
import datetime
import os
import signal
import subprocess
import sys
import threading
import time
from gpiozero import DigitalOutputDevice, LED
from libcamera import Transform
from picamera2 import Picamera2
from picamera2.encoders import H264Encoder
from picamera2.outputs import FileOutput
import smbus2

# =============================================================================
# HARDWARE CONFIGURATION & GPIO PIN DEFINITIONS
# =============================================================================
PIN_IRCUT = 4          # HIGH = Daylight filter engaged, LOW = Night IR pass-through
PIN_IR_LED = 27        # HIGH = IR LEDs illuminated, LOW = Off

# =============================================================================
# TF-LUNA I2C CONFIGURATION & TARGET DISTANCE RANGE
# =============================================================================
I2C_BUS_ID = 1
TFLUNA_I2C_ADDR = 0x10    # Default TF-Luna I2C 7-bit slave address
TFLUNA_REG_ENABLE = 0x25  # Register 0x25: 0x00 = Laser OFF, 0x01 = Laser ON

# detection distance thresholds (inches)
TARGET_MIN_INCHES = 55.0
TARGET_MAX_INCHES = 105.0

# Conversion to centimeters for TF-Luna comparison
INCHES_TO_CM = 2.54
TARGET_MIN_CM = TARGET_MIN_INCHES * INCHES_TO_CM
TARGET_MAX_CM = TARGET_MAX_INCHES * INCHES_TO_CM

# False Positive Filtering Thresholds
MIN_SIGNAL_AMP = 500             # Rejects weak reflections (dust, small airborne insects)
MAX_SIGNAL_AMP = 30000           # Rejects optical saturation anomalies
REQUIRED_CONSECUTIVE_HITS = 3    # Requires 3 consecutive valid detections (150ms persistence)

# =============================================================================
# TIMING & RECORDING CONSTANTS
# =============================================================================
VIDEO_DURATION_SEC = 30.0
VIDEO_PRE_RECORD_DELAY_SEC = 0.5   # 0.5s pause after LED activation in night mode
MAX_CONSECUTIVE_VIDEOS = 4         # 4 videos @ 30s = 2 minutes max continuous LED operation
COOLDOWN_DURATION_SEC = 60.0       # 1 minute forced thermal cooldown
SENSOR_POLL_INTERVAL_SEC = 0.05    # Polling cadence when searching for targets (20 Hz)
SENSOR_REARM_DELAY_SEC = 0.05      # 50ms delay for sensor stabilization after laser re-enable

# Video Resolution & Encoding Parameters
VIDEO_WIDTH = 1920
VIDEO_HEIGHT = 1080
VIDEO_BITRATE = 6000000            # 6 Mbps (Optimized for Pi Zero 2 write speeds)
KEYFRAME_INTERVAL = 30             # 1 IDR keyframe every 30 frames (1.0s)

# =============================================================================
# CAMERA CONTROL PROFILES (EXPLICIT SETTINGS)
# =============================================================================
DAY_CAMERA_CONTROLS = {
    "FrameDurationLimits": (33333, 33333),  # Locked 30 FPS (33.33ms)
    "AeEnable": True,                       # Auto-exposure active
    "AeMeteringMode": 0,                    # 0: Centre-Weighted, 1: Spot, 2: Matrix
    "AeExposureMode": 0,                    # 0: Normal
    "AwbEnable": True,                      # Auto white balance active
    "AwbMode": 0,                           # 0: Auto
    "Brightness": 0.0,                      # Default baseline (-1.0 to 1.0)
    "Contrast": 1.0,                        # Default baseline (0.0 to 32.0)
    "Saturation": 1.0,                      # Natural color reproduction
    "Sharpness": 1.0                        # Standard edge sharpness
}

NIGHT_CAMERA_CONTROLS = {
    "FrameDurationLimits": (33333, 33333),  # Locked 30 FPS (Prevents motion blur)
    "AeEnable": True,                       # AE enabled with maximum exposure headroom
    "ExposureTime": 33000,                  # Allow exposure up to the full ~33ms frame window
    "AnalogueGain": 4.0,                    # High gain floor for dim IR illumination
    "AwbEnable": False,                     # Disable AWB to prevent chromatic hunting under IR
    "ColourGains": (1.0, 1.0),              # Neutral color channel weighting
    "Brightness": 0.1,                      # Boost shadow lift
    "Contrast": 1.25,                       # Heightened contrast for subject separation
    "Saturation": 0.0,                      # Full desaturation for clean monochrome IR capture
    "Sharpness": 1.5                        # Enhanced edge delineation for low-light detail
}


def set_tfluna_laser(bus: smbus2.SMBus, enable: bool, address: int = TFLUNA_I2C_ADDR):
    """Enables (0x01) or disables (0x00) the TF-Luna laser emitter via I2C register 0x25."""
    try:
        bus.write_byte_data(address, TFLUNA_REG_ENABLE, 0x01 if enable else 0x00)
    except OSError as e:
        print(f"[Warning] Could not set TF-Luna laser state ({'ON' if enable else 'OFF'}): {e}")


def read_tfluna_distance(bus: smbus2.SMBus, address: int = TFLUNA_I2C_ADDR):
    """
    Reads standard 6-byte distance frame from TF-Luna via I2C.
    Returns distance in centimeters, or None if bus error or out-of-spec amplitude.
    """
    try:
        data = bus.read_i2c_block_data(address, 0x00, 6)
        dist_cm = data[0] | (data[1] << 8)
        amp = data[2] | (data[3] << 8)
        
        # Validate amplitude against lower (particles/bugs) and upper (saturation) bounds
        if MIN_SIGNAL_AMP <= amp <= MAX_SIGNAL_AMP:
            return dist_cm
        return None
    except OSError:
        return None


def is_target_in_range(bus: smbus2.SMBus, required_hits: int = REQUIRED_CONSECUTIVE_HITS) -> bool:
    """
    Checks if target remains in range across multiple consecutive readings.
    Requires continuous presence to reject transient spikes and passing insects.
    """
    for _ in range(required_hits):
        dist_cm = read_tfluna_distance(bus)
        if dist_cm is not None and (TARGET_MIN_CM <= dist_cm <= TARGET_MAX_CM):
            time.sleep(SENSOR_POLL_INTERVAL_SEC)
        else:
            return False  # Failed confirmation; abort sequence immediately
            
    return True


def remux_to_mp4_async(h264_path: str, mp4_path: str):
    """
    Remuxes raw H.264 stream into an MP4 container in a background thread
    to prevent blocking subsequent captures or dropping initial pipe frames.
    """
    def _remux():
        try:
            cmd = [
                "ffmpeg", "-y",
                "-fflags", "+genpts",
                "-r", "30",
                "-i", h264_path,
                "-c:v", "copy",
                mp4_path
            ]
            subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            if os.path.exists(h264_path):
                os.remove(h264_path)
            print(f"[Remux] Container finalized -> {mp4_path}")
        except Exception as e:
            print(f"[Remux Error] Failed to containerize {h264_path}: {e}")

    thread = threading.Thread(target=_remux, daemon=True)
    thread.start()


def main():
    parser = argparse.ArgumentParser(description="Automated IMX462 TF-Luna Video Trigger")
    parser.add_argument("--day", action="store_true", help="Enable daylight mode (IR filter ON, LEDs OFF)")
    args = parser.parse_args()
    is_day_mode = args.day

    # Initialize GPIO Devices
    ircut = DigitalOutputDevice(PIN_IRCUT, active_high=True, initial_value=False)
    ir_led = LED(PIN_IR_LED, active_high=True, initial_value=False)

    # Apply hardware filter state
    if is_day_mode:
        ircut.on()   # Engage IRCUT filter (block IR)
        ir_led.off() # Ensure LEDs remain disabled
        active_controls = DAY_CAMERA_CONTROLS
        print("[System] Mode: DAYLIGHT (IRCUT Filter: ON | IR LEDs: OFF)")
    else:
        ircut.off()  # Retract IRCUT filter (pass IR)
        ir_led.off() # LEDs initialized to off until trigger
        active_controls = NIGHT_CAMERA_CONTROLS
        print("[System] Mode: NIGHT (IRCUT Filter: OFF | IR LEDs: TRIGGER-CONTROLLED)")

    print(f"[System] Detection window: {TARGET_MIN_INCHES}\" to {TARGET_MAX_INCHES}\" "
          f"({TARGET_MIN_CM:.1f} cm - {TARGET_MAX_CM:.1f} cm)")
    print(f"[System] Debounce filter: {REQUIRED_CONSECUTIVE_HITS} consecutive hits (Amp >= {MIN_SIGNAL_AMP})")

    # Initialize I2C Bus & ensure laser is active
    i2c_bus = smbus2.SMBus(I2C_BUS_ID)
    set_tfluna_laser(i2c_bus, True)

    # Initialize Picamera2 with 180° rotation (hflip + vflip via libcamera Transform)
    picam2 = Picamera2()
    video_config = picam2.create_video_configuration(
        main={"size": (VIDEO_WIDTH, VIDEO_HEIGHT), "format": "YUV420"},
        controls=active_controls,
        transform=Transform(hflip=True, vflip=True)
    )
    picam2.configure(video_config)
    picam2.start()

    consecutive_videos = 0
    is_recording = False

    def cleanup_and_exit(signum=None, frame=None):
        print("\n[System] Terminating. Releasing camera, I2C, and resetting GPIOs to inputs...")
        try:
            if is_recording:
                picam2.stop_recording()
            picam2.stop()
            picam2.close()
        except Exception:
            pass
        try:
            set_tfluna_laser(i2c_bus, True)
            i2c_bus.close()
        except Exception:
            pass
        ircut.close()
        ir_led.close()
        sys.exit(0)

    # Register termination signals
    signal.signal(signal.SIGINT, cleanup_and_exit)
    signal.signal(signal.SIGTERM, cleanup_and_exit)

    try:
        while True:
            # 1. Thermal Management / Cooldown Enforcer (Night Mode only)
            if not is_day_mode and consecutive_videos >= MAX_CONSECUTIVE_VIDEOS:
                print(f"[Thermal Safety] Reached {MAX_CONSECUTIVE_VIDEOS} consecutive recordings (2 min). "
                      f"Cooling down IR LEDs for {int(COOLDOWN_DURATION_SEC)} seconds...")
                ir_led.off()
                time.sleep(COOLDOWN_DURATION_SEC)
                consecutive_videos = 0
                print("[Thermal Safety] Cooldown complete. Resuming target detection.")

            # 2. Check for target with debounce confirmation
            target_detected = is_target_in_range(i2c_bus)

            if target_detected:
                # Night-mode LED management prior to recording
                if not is_day_mode:
                    if not ir_led.is_active:
                        ir_led.on()

                # Generate timestamped filenames
                timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
                raw_h264_file = f"temp_{timestamp}.h264"
                final_mp4_file = f"video_{timestamp}.mp4"
                print(f"[Record] Confirmed target in range. Recording {VIDEO_DURATION_SEC}s -> {final_mp4_file}")

                # Disable TF-Luna laser during recording
                set_tfluna_laser(i2c_bus, False)

                # Capture directly to disk with FileOutput
                is_recording = True
                encoder = H264Encoder(bitrate=VIDEO_BITRATE, iperiod=KEYFRAME_INTERVAL)
                output = FileOutput(raw_h264_file)
                picam2.start_recording(encoder, output)
                time.sleep(VIDEO_DURATION_SEC)
                picam2.stop_recording()
                is_recording = False

                # Re-enable TF-Luna laser immediately and pause briefly for measurement acquisition
                set_tfluna_laser(i2c_bus, True)
                time.sleep(SENSOR_REARM_DELAY_SEC)

                # Convert to MP4 in the background
                remux_to_mp4_async(raw_h264_file, final_mp4_file)

                consecutive_videos += 1

                # 3. Post-capture seamless rollover check
                if is_target_in_range(i2c_bus):
                    if consecutive_videos < MAX_CONSECUTIVE_VIDEOS:
                        print("[Record] Target continuously present. Proceeding to next capture without LED interruption.")
                        continue
                else:
                    if not is_day_mode:
                        ir_led.off()
                    consecutive_videos = 0
                    print("[Status] Target cleared. Awaiting next trigger...")

            else:
                # Idle state
                if not is_day_mode and ir_led.is_active:
                    ir_led.off()
                consecutive_videos = 0
                time.sleep(SENSOR_POLL_INTERVAL_SEC)

    except Exception as e:
        print(f"[Error] Unexpected exception: {e}")
    finally:
        cleanup_and_exit()


if __name__ == "__main__":
    main()
