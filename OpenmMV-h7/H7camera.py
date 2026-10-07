from openmv import Camera
import time

camera_path = "/dev/ttyACM0"
class H7camera:

    camera = None

    def __init__(self, path: str = None):

        if path:
            self.camera = Camera(path)
        else:
            self.camera = Camera(camera_path)

        self.camera.connect()
        return self

    def __del__(self):
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
