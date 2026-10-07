#HAL module for the openmv h7 camera

from openmv import Camera
import time

camera_path = "/dev/ttyACM0"

internal_script = """
import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=2000)

while True:
    sensor.snapshot()
"""

class H7camera:

    camera = None

    def __init__(self, path: str = None) -> None:

        if path:
            self.camera = Camera(path)
        else:
            self.camera = Camera(camera_path)
        
        self.camera.connect()
        self.camera.stop()

        self.camera.exec(internal_script)

        return

    def __del__(self) -> None:
        if self.camera is not None:
            self.camera.disconnect()


    #returns a dictionary containing the image bytes and metadata
    # use image = np.frombuffer(frame["data"], dtype=np.uint8) to turn into np array
    def capture_image(self) -> dict:
        self.camera.streaming(True, raw=False, resolution=(512, 512))
        image = None
        while True:
            status = self.camera.read_status()

            if status.get("stream"):
                image = self.camera.read_frame()

                if image is not None:
                    break

            time.sleep(0.01)

        self.camera.streaming(False)
        
        return image
