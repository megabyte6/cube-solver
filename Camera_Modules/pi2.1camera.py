# HAL module for the Raspberry Pi Camera Module v2.1.
# BeagleY-AI must run 'beagle-camera-setup' before the code works
import subprocess
from pathlib import Path

import cv2
import numpy as np


camera_path = "/dev/video-imx219-cam0"
subdev_path = "/dev/v4l-imx219-subdev0"

WIDTH, HEIGHT = 1920, 1080
RAW_FILE = "capture.raw"

# The IMX219 stream is Bayer data. Select the conversion that matches the
# sensor's pattern and tune these gains for the camera's lighting.
BAYER_CONVERSION = cv2.COLOR_BayerBG2BGR
BLUE_GAIN = 1.5
GREEN_GAIN = 1.0
RED_GAIN = 1.2


def run_command(command: list[str]) -> None:
    subprocess.run(command, check=True)


class Pi21camera:

    def __init__(
        self,
        path: str = camera_path,
        subdev: str = subdev_path,
        raw_file: str = RAW_FILE,
    ) -> None:
        self.camera = path
        self.subdev = subdev
        self.raw_file = Path(raw_file)

    def __del__(self) -> None:
        return

    # Returns a dictionary containing the BGR image bytes and metadata.
    # Use np.frombuffer(frame["data"], dtype=np.uint8).reshape(
    #     frame["height"], frame["width"], 3
    # ) to turn the data into a NumPy image.
    def capture_image(self) -> dict:
        run_command([
            "v4l2-ctl",
            "-d",
            self.subdev,
            "--set-ctrl=exposure=1759,analogue_gain=100",
        ])

        run_command([
            "v4l2-ctl",
            "-d",
            self.camera,
            f"--set-fmt-video=width={WIDTH},height={HEIGHT},pixelformat=RGGB",
            "--stream-mmap",
            "--stream-count=1",
            f"--stream-to={self.raw_file}",
        ])

        raw = np.fromfile(self.raw_file, dtype=np.uint8)
        if raw.size != WIDTH * HEIGHT:
            raise RuntimeError(f"Unexpected raw frame size: {raw.size}")

        bayer = raw.reshape(HEIGHT, WIDTH)
        bgr = cv2.cvtColor(bayer, BAYER_CONVERSION).astype(np.float32)

        bgr[:, :, 0] *= BLUE_GAIN
        bgr[:, :, 1] *= GREEN_GAIN
        bgr[:, :, 2] *= RED_GAIN
        bgr = np.clip(bgr, 0, 255).astype(np.uint8)

        hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)
        hsv[:, :, 1] = np.clip(
            hsv[:, :, 1].astype(np.float32) * 2,
            0,
            255,
        ).astype(np.uint8)
        bgr = cv2.cvtColor(hsv, cv2.COLOR_HSV2BGR)

        return {
            "data": bgr.tobytes(),
            "width": WIDTH,
            "height": HEIGHT,
            "format": "BGR888",
        }


# Alternate spelling for callers that include the dot in the camera name.
Pi2_1camera = Pi21camera
