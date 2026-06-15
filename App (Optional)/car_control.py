import argparse
import os
import queue
import threading
import time
from dataclasses import dataclass

import cv2
import numpy as np
import requests

# ============================================================
# OPTIONAL LAPTOP CONTROLLER FOR ESP32-CAM CAR
# Compatible with the current ESP32_CAM/Car.ino firmware.
#
# Default control protocol: HTTP /cmd
# Reason: the current firmware does NOT implement UDP commands.
#
# Camera:
#   MJPEG stream: http://<ESP32_IP>:81/stream
#   JPG fallback:  http://<ESP32_IP>/jpg
# ============================================================

# Reduce buffering for OpenCV FFmpeg builds. This must be set before opening the stream.
os.environ.setdefault(
    "OPENCV_FFMPEG_CAPTURE_OPTIONS",
    "fflags;nobuffer|flags;low_delay|framedrop",
)


@dataclass
class AppConfig:
    esp_ip: str = os.getenv("ESP32_IP", "192.168.4.1")
    display_width: int = 640
    display_height: int = 480
    use_motion_detection: bool = False
    process_motion_every_n_frames: int = 8
    motion_pause_while_driving: bool = True
    motion_cooldown_seconds: float = 0.8
    motion_width: int = 120
    motion_height: int = 90
    motion_min_area: int = 80
    motion_threshold: int = 35
    command_timeout: float = 0.15
    command_worker_timeout: float = 0.1
    max_command_queue: int = 40
    speed_step: int = 5
    servo_step: int = 5


class Esp32CarController:
    def __init__(self, config: AppConfig):
        self.config = config

        self.base_url = f"http://{config.esp_ip}"
        self.stream_url = f"{self.base_url}:81/stream"
        self.jpg_url = f"{self.base_url}/jpg"
        self.test_url = f"{self.base_url}/test"
        self.cmd_url = f"{self.base_url}/cmd"
        self.data_url = f"{self.base_url}/data"

        self.window_name = "ESP32-CAM Optional Laptop Controller"

        self.running = True
        self.pan = 90
        self.tilt = 90
        self.left_speed = 200
        self.right_speed = 200
        self.min_duty = 80
        self.power_on = True
        self.current_move = 0
        self.last_camera_move_time = 0.0

        self.latest_frame = None
        self.latest_frame_id = 0
        self.latest_frame_lock = threading.Lock()

        self.camera_fps = 0
        self.stream_status = "starting"

        self.command_queue: queue.Queue[tuple[str, int]] = queue.Queue(
            maxsize=config.max_command_queue
        )
        self.command_session = requests.Session()
        self.data_session = requests.Session()

    # ---------------- HTTP / COMMANDS ----------------
    def test_connection(self) -> bool:
        print("Testing ESP32 connection:", self.test_url)
        try:
            response = requests.get(self.test_url, timeout=2)
            print(response.text)
            return response.ok
        except requests.exceptions.RequestException as exc:
            print("Cannot reach ESP32:", exc)
            print("Make sure your laptop is connected to the MyWiFiCar Wi-Fi network.")
            print("Default AP IP is usually http://192.168.4.1")
            print("If your ESP32 uses another IP, run:")
            print("  python car_control.py --ip YOUR_ESP32_IP")
            return False

    def queue_cmd(self, key: str, value: int) -> None:
        # Avoid building an old backlog of movement commands.
        if key == "MoveCar":
            self._drop_old_move_commands()

        try:
            self.command_queue.put_nowait((key, int(value)))
        except queue.Full:
            # Drop oldest command, then try once more.
            try:
                self.command_queue.get_nowait()
                self.command_queue.put_nowait((key, int(value)))
            except queue.Empty:
                pass
            except queue.Full:
                pass

    def _drop_old_move_commands(self) -> None:
        kept: list[tuple[str, int]] = []

        try:
            while True:
                item = self.command_queue.get_nowait()
                if item[0] != "MoveCar":
                    kept.append(item)
        except queue.Empty:
            pass

        for item in kept:
            try:
                self.command_queue.put_nowait(item)
            except queue.Full:
                break

    def command_worker(self) -> None:
        while self.running:
            try:
                key, value = self.command_queue.get(timeout=self.config.command_worker_timeout)
            except queue.Empty:
                continue

            try:
                self.command_session.get(
                    self.cmd_url,
                    params={"k": key, "v": value, "t": int(time.time() * 1000)},
                    timeout=self.config.command_timeout,
                )
            except requests.exceptions.RequestException:
                pass

    def refresh_data_from_esp32(self) -> None:
        try:
            response = self.data_session.get(
                self.data_url,
                params={"t": int(time.time() * 1000)},
                timeout=0.3,
            )
            if not response.ok:
                return

            data = response.json()
            self.pan = int(data.get("pan", self.pan))
            self.tilt = int(data.get("tilt", self.tilt))
            self.left_speed = int(data.get("leftSpeed", self.left_speed))
            self.right_speed = int(data.get("rightSpeed", self.right_speed))
            self.min_duty = int(data.get("minDuty", self.min_duty))
            self.power_on = bool(data.get("power", self.power_on))
        except (requests.exceptions.RequestException, ValueError, TypeError):
            pass

    def data_worker(self) -> None:
        while self.running:
            self.refresh_data_from_esp32()
            time.sleep(2.0)

    # ---------------- CAR ACTIONS ----------------
    def move(self, value: int) -> None:
        value = int(value)
        if value < 0 or value > 4:
            value = 0

        if value != self.current_move:
            self.current_move = value
            self.queue_cmd("MoveCar", value)

    def stop(self) -> None:
        self.current_move = 0
        # Send stop several times because stop is safety-critical.
        for _ in range(3):
            self.queue_cmd("MoveCar", 0)
            time.sleep(0.01)

    def set_pan(self, value: int) -> None:
        value = max(0, min(180, int(value)))
        if value == self.pan:
            return

        self.pan = value
        self.last_camera_move_time = time.time()
        self.queue_cmd("Pan", value)

    def set_tilt(self, value: int) -> None:
        value = max(0, min(180, int(value)))
        if value == self.tilt:
            return

        self.tilt = value
        self.last_camera_move_time = time.time()
        self.queue_cmd("Tilt", value)

    def center_camera(self) -> None:
        self.pan = 90
        self.tilt = 90
        self.last_camera_move_time = time.time()
        self.queue_cmd("Center", 1)

    def set_left_speed(self, value: int) -> None:
        value = max(0, min(255, int(value)))
        self.left_speed = value
        self.queue_cmd("LeftSpeed", value)

    def set_right_speed(self, value: int) -> None:
        value = max(0, min(255, int(value)))
        self.right_speed = value
        self.queue_cmd("RightSpeed", value)

    def set_both_speeds(self, value: int) -> None:
        value = max(0, min(255, int(value)))
        self.left_speed = value
        self.right_speed = value
        self.queue_cmd("LeftSpeed", value)
        self.queue_cmd("RightSpeed", value)

    def toggle_power(self) -> None:
        self.power_on = not self.power_on
        self.queue_cmd("Power", 1 if self.power_on else 0)
        if not self.power_on:
            self.stop()

    # ---------------- CAMERA THREAD ----------------
    def update_latest_frame(self, frame: np.ndarray) -> None:
        with self.latest_frame_lock:
            self.latest_frame = frame
            self.latest_frame_id += 1

    def get_latest_frame(self) -> tuple[np.ndarray | None, int]:
        with self.latest_frame_lock:
            if self.latest_frame is None:
                return None, self.latest_frame_id
            return self.latest_frame.copy(), self.latest_frame_id

    def open_stream_capture(self) -> cv2.VideoCapture:
        try:
            cap = cv2.VideoCapture(self.stream_url, cv2.CAP_FFMPEG)
        except Exception:
            cap = cv2.VideoCapture(self.stream_url)

        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        return cap

    def camera_worker(self) -> None:
        print("Trying MJPEG stream:", self.stream_url)
        cap = self.open_stream_capture()

        frame_counter = 0
        fps_time = time.time()
        failed_reads = 0
        self.stream_status = "MJPEG"

        while self.running:
            ret, frame = cap.read()

            if ret and frame is not None:
                failed_reads = 0
                self.update_latest_frame(frame)
                frame_counter += 1

                now = time.time()
                if now - fps_time >= 1.0:
                    self.camera_fps = frame_counter
                    frame_counter = 0
                    fps_time = now
                continue

            failed_reads += 1
            if failed_reads >= 25:
                self.stream_status = "reconnecting"
                cap.release()
                time.sleep(0.2)
                cap = self.open_stream_capture()
                failed_reads = 0
                self.stream_status = "MJPEG"
            else:
                time.sleep(0.01)

        cap.release()

    # ---------------- MOTION DETECTION ----------------
    def detect_motion(self, frame: np.ndarray, background_subtractor) -> list[tuple[int, int, int, int, float]]:
        boxes = []
        cfg = self.config

        small = cv2.resize(frame, (cfg.motion_width, cfg.motion_height))
        gray = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY)
        gray = cv2.GaussianBlur(gray, (5, 5), 0)

        mask = background_subtractor.apply(gray)
        _, mask = cv2.threshold(mask, cfg.motion_threshold, 255, cv2.THRESH_BINARY)

        kernel = np.ones((3, 3), np.uint8)
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)
        mask = cv2.dilate(mask, kernel, iterations=1)

        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        h_original, w_original = frame.shape[:2]
        scale_x = w_original / cfg.motion_width
        scale_y = h_original / cfg.motion_height

        for contour in contours:
            area = cv2.contourArea(contour)
            if area < cfg.motion_min_area:
                continue

            x, y, w, h = cv2.boundingRect(contour)
            x1 = int(x * scale_x)
            y1 = int(y * scale_y)
            x2 = int((x + w) * scale_x)
            y2 = int((y + h) * scale_y)
            boxes.append((x1, y1, x2, y2, area))

        return boxes

    # ---------------- DISPLAY ----------------
    def make_display(self, frame: np.ndarray) -> tuple[np.ndarray, float, int, int]:
        h, w = frame.shape[:2]
        scale = min(self.config.display_width / w, self.config.display_height / h)
        new_w = int(w * scale)
        new_h = int(h * scale)

        resized = cv2.resize(frame, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
        display = np.zeros((self.config.display_height, self.config.display_width, 3), dtype=np.uint8)

        x_offset = (self.config.display_width - new_w) // 2
        y_offset = (self.config.display_height - new_h) // 2
        display[y_offset:y_offset + new_h, x_offset:x_offset + new_w] = resized

        return display, scale, x_offset, y_offset

    def draw_motion_boxes(
        self,
        display: np.ndarray,
        boxes: list[tuple[int, int, int, int, float]],
        scale: float,
        x_offset: int,
        y_offset: int,
    ) -> None:
        for x1, y1, x2, y2, _area in boxes:
            dx1 = int(x1 * scale) + x_offset
            dy1 = int(y1 * scale) + y_offset
            dx2 = int(x2 * scale) + x_offset
            dy2 = int(y2 * scale) + y_offset
            cv2.rectangle(display, (dx1, dy1), (dx2, dy2), (255, 0, 0), 2)
            cv2.putText(
                display,
                "MOVING",
                (dx1, max(25, dy1 - 8)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                (255, 0, 0),
                2,
            )

    def draw_overlay(self, display: np.ndarray, motion_count: int, motion_status: str) -> None:
        cv2.putText(
            display,
            "WASD move | X/Space stop | J/L pan | I/K tilt | C center | Q quit",
            (16, 32),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.52,
            (0, 255, 0),
            2,
        )
        cv2.putText(
            display,
            "[/] left speed  |  ;/' right speed  |  -/= both speed  |  P power  |  M motion",
            (16, 62),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.48,
            (0, 255, 0),
            1,
        )
        cv2.putText(
            display,
            f"Move:{self.current_move}  FPS:{self.camera_fps}  Mode:{self.stream_status}  Power:{'ON' if self.power_on else 'OFF'}",
            (16, 94),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (0, 255, 255),
            2,
        )
        cv2.putText(
            display,
            f"Pan:{self.pan} Tilt:{self.tilt}  LeftSpeed:{self.left_speed} RightSpeed:{self.right_speed}  Motion:{motion_status} Count:{motion_count}",
            (16, 126),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (255, 255, 0),
            2,
        )

    # ---------------- KEYBOARD ----------------
    def handle_keyboard(self, key: int) -> bool:
        step = self.config.speed_step
        servo_step = self.config.servo_step

        if key == ord("w"):
            self.move(1)
        elif key == ord("s"):
            self.move(2)
        elif key == ord("a"):
            self.move(3)
        elif key == ord("d"):
            self.move(4)
        elif key == ord("x") or key == 32:
            self.stop()
        elif key == ord("j"):
            self.set_pan(self.pan - servo_step)
        elif key == ord("l"):
            self.set_pan(self.pan + servo_step)
        elif key == ord("i"):
            self.set_tilt(self.tilt + servo_step)
        elif key == ord("k"):
            self.set_tilt(self.tilt - servo_step)
        elif key == ord("c"):
            self.center_camera()
        elif key == ord("["):
            self.set_left_speed(self.left_speed - step)
        elif key == ord("]"):
            self.set_left_speed(self.left_speed + step)
        elif key == ord(";"):
            self.set_right_speed(self.right_speed - step)
        elif key == ord("'"):
            self.set_right_speed(self.right_speed + step)
        elif key == ord("-"):
            self.set_both_speeds(min(self.left_speed, self.right_speed) - step)
        elif key == ord("=") or key == ord("+"):
            self.set_both_speeds(max(self.left_speed, self.right_speed) + step)
        elif key == ord("p"):
            self.toggle_power()
        elif key == ord("m"):
            self.config.use_motion_detection = not self.config.use_motion_detection
        elif key == ord("q"):
            self.stop()
            return False

        return True

    # ---------------- MAIN LOOP ----------------
    def run(self) -> None:
        if not self.test_connection():
            return

        self.refresh_data_from_esp32()

        threading.Thread(target=self.command_worker, daemon=True).start()
        threading.Thread(target=self.data_worker, daemon=True).start()
        threading.Thread(target=self.camera_worker, daemon=True).start()

        self.center_camera()
        self.stop()

        background_subtractor = cv2.createBackgroundSubtractorMOG2(
            history=40,
            varThreshold=50,
            detectShadows=False,
        )

        cv2.namedWindow(self.window_name, cv2.WINDOW_NORMAL)
        cv2.resizeWindow(self.window_name, self.config.display_width, self.config.display_height)

        last_seen_frame_id = -1
        frame_counter = 0
        last_motion_boxes = []

        print("Starting controller window...")
        print("Click the OpenCV window, then use WASD.")

        try:
            while True:
                frame, frame_id = self.get_latest_frame()

                if frame is None:
                    key = cv2.waitKey(1) & 0xFF
                    if key == ord("q"):
                        break
                    time.sleep(0.005)
                    continue

                if frame_id == last_seen_frame_id:
                    key = cv2.waitKey(1) & 0xFF
                    if key != 255 and not self.handle_keyboard(key):
                        break
                    time.sleep(0.001)
                    continue

                last_seen_frame_id = frame_id
                frame_counter += 1

                motion_status = "OFF"
                motion_boxes = []

                if self.config.use_motion_detection:
                    motion_status = "ON"
                    camera_recently_moved = (
                        time.time() - self.last_camera_move_time < self.config.motion_cooldown_seconds
                    )

                    if self.config.motion_pause_while_driving and self.current_move != 0:
                        motion_status = "PAUSED CAR"
                        motion_boxes = []
                    elif camera_recently_moved:
                        motion_status = "PAUSED CAM"
                        motion_boxes = []
                    elif frame_counter % self.config.process_motion_every_n_frames == 0:
                        last_motion_boxes = self.detect_motion(frame, background_subtractor)
                        motion_boxes = last_motion_boxes
                    else:
                        motion_boxes = last_motion_boxes

                display, scale, x_offset, y_offset = self.make_display(frame)
                self.draw_motion_boxes(display, motion_boxes, scale, x_offset, y_offset)
                self.draw_overlay(display, len(motion_boxes), motion_status)

                cv2.imshow(self.window_name, display)

                key = cv2.waitKey(1) & 0xFF
                if key != 255 and not self.handle_keyboard(key):
                    break

        finally:
            self.stop()
            time.sleep(0.2)
            self.running = False
            cv2.destroyAllWindows()


def parse_args() -> AppConfig:
    parser = argparse.ArgumentParser(description="Optional laptop controller for ESP32-CAM car")
    parser.add_argument(
        "--ip",
        default=os.getenv("ESP32_IP", "192.168.4.1"),
        help="ESP32-CAM IP address. Default: ESP32_IP env var or 192.168.4.1",
    )
    parser.add_argument("--width", type=int, default=640, help="Display window width")
    parser.add_argument("--height", type=int, default=480, help="Display window height")
    parser.add_argument(
        "--motion",
        action="store_true",
        help="Enable optional OpenCV motion detection at startup",
    )

    args = parser.parse_args()

    return AppConfig(
        esp_ip=args.ip,
        display_width=args.width,
        display_height=args.height,
        use_motion_detection=args.motion,
    )


def main() -> None:
    config = parse_args()
    app = Esp32CarController(config)
    app.run()


if __name__ == "__main__":
    main()
