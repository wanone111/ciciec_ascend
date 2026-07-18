#!/usr/bin/env python3
"""Read frames from the configured Hikrobot GigE camera without loading models."""

import argparse
import os
import time
from pathlib import Path

import cv2

from capture.hik_gige_source import HikGigESource


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera-ip", default=os.environ.get("SMART_IP_HIK_CAMERA_IP", ""))
    parser.add_argument("--serial", default=os.environ.get("SMART_IP_HIK_SERIAL_NUMBER", ""))
    parser.add_argument("--packet-size", default=os.environ.get("SMART_IP_HIK_PACKET_SIZE", "0"))
    parser.add_argument("--sdk-root", default=os.environ.get("SMART_IP_HIK_SDK_ROOT", "/opt/MVS"))
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--save", type=Path, help="save the first valid BGR frame")
    args = parser.parse_args()
    os.environ["SMART_IP_HIK_PACKET_SIZE"] = args.packet_size
    source = HikGigESource(
        camera_ip=args.camera_ip, serial_number=args.serial, timeout_ms=1000,
        sdk_root=args.sdk_root, auto_exposure=True, auto_gain=True,
        latest_frame=False, acquisition_fps=0,
    )
    print("OPEN", source.device_info, flush=True)
    try:
        success = 0
        for index in range(args.frames):
            ok, frame, metadata = source.read_with_info()
            print("READ", index, ok, None if frame is None else frame.shape, metadata, flush=True)
            success += int(ok)
            if ok and args.save and success == 1:
                args.save.parent.mkdir(parents=True, exist_ok=True)
                if not cv2.imwrite(str(args.save), frame):
                    raise RuntimeError(f"failed to save {args.save}")
                print("SAVED", args.save, flush=True)
            if not ok:
                time.sleep(0.05)
        if not success:
            raise SystemExit("no frame was captured")
    finally:
        print("STATS", source.get_detailed_stats(), flush=True)
        source.release()


if __name__ == "__main__":
    main()
