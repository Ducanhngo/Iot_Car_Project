import cv2
import requests
import time
import numpy as np
import threading
import queue

# ---------------- ESP32 SETTINGS ----------------
ESP_IP = "10.76.104.1"

STREAM_URL = f"http://{ESP_IP}:81/stream"
JPG_URL = f"http://{ESP_IP}/jpg"
CMD_URL = f"http://{ESP_IP}/cmd"

# ---------------- WINDOW SETTINGS ----------------
WINDOW_NAME = "ESP32-CAM Fast Moving Object Detection"

# Bigger window size
DISPLAY_WIDTH = 1280
DISPLAY_HEIGHT = 720

# ---------------- PERFORMANCE SETTINGS ----------------
USE_MOTION_DETECTION = True

# Higher = better FPS, slower motion detection update
PROCESS_MOTION_EVERY_N_FRAMES = 5

# Pause motion detection after pan/tilt movement
MOTION_COOLDOWN_SECONDS = 1.0

# Pause motion detection while car moves
MOTION_PAUSE_WHILE_DRIVING = True

# Small image for motion detection = faster FPS
MOTION_WIDTH = 160
MOTION_HEIGHT = 120

MOTION_MIN_AREA = 120
MOTION_THRESHOLD = 35

# ---------------- CONTROL STATE ----------------
pan = 90
tilt = 90
last_move = 0
last_camera_move_time = 0

running = True

latest_frame = None
latest_frame_id = 0
camera_fps = 0

latest_frame_lock = threading.Lock()
cmd_queue = queue.Queue(maxsize=30)


# ---------------- COMMAND THREAD ----------------
def command_worker():
    session = requests.Session()

    while running:
        try:
            k, v = cmd_queue.get(timeout=0.1)
        except queue.Empty:
            continue

        try:
            session.get(CMD_URL, params={"k": k, "v": v}, timeout=0.12)
        except requests.exceptions.RequestException:
            pass


def send_cmd_fast(k, v):
    try:
        while cmd_queue.qsize() > 3:
            cmd_queue.get_nowait()
    except queue.Empty:
        pass

    try:
        cmd_queue.put_nowait((k, v))
    except queue.Full:
        pass


def move(value):
    global last_move

    if value != last_move:
        send_cmd_fast("MoveCar", value)
        last_move = value


def set_pan(value):
    global pan, last_camera_move_time

    pan = max(0, min(180, int(value)))
    send_cmd_fast("Pan", pan)
    last_camera_move_time = time.time()


def set_tilt(value):
    global tilt, last_camera_move_time

    tilt = max(0, min(180, int(value)))
    send_cmd_fast("Tilt", tilt)
    last_camera_move_time = time.time()


def center_camera():
    global pan, tilt, last_camera_move_time

    pan = 90
    tilt = 90
    send_cmd_fast("Center", 1)
    last_camera_move_time = time.time()


# ---------------- CAMERA THREAD ----------------
def update_latest_frame(frame):
    global latest_frame, latest_frame_id

    with latest_frame_lock:
        latest_frame = frame
        latest_frame_id += 1


def camera_worker():
    global running, camera_fps

    print("Trying MJPEG stream:", STREAM_URL)

    cap = cv2.VideoCapture(STREAM_URL)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    stream_ok = False
    start = time.time()

    while time.time() - start < 5:
        ret, frame = cap.read()

        if ret and frame is not None:
            stream_ok = True
            print("Stream connected.")
            update_latest_frame(frame)
            break

    frame_counter = 0
    fps_time = time.time()

    if stream_ok:
        while running:
            ret, frame = cap.read()

            if ret and frame is not None:
                update_latest_frame(frame)
                frame_counter += 1

                now = time.time()
                if now - fps_time >= 1.0:
                    camera_fps = frame_counter
                    frame_counter = 0
                    fps_time = now
            else:
                time.sleep(0.005)

        cap.release()
        return

    cap.release()
    print("Stream failed. Using /jpg fallback.")

    session = requests.Session()

    while running:
        try:
            response = session.get(JPG_URL, timeout=0.4)

            if response.status_code == 200:
                img_array = np.frombuffer(response.content, dtype=np.uint8)
                frame = cv2.imdecode(img_array, cv2.IMREAD_COLOR)

                if frame is not None:
                    update_latest_frame(frame)
                    frame_counter += 1

                    now = time.time()
                    if now - fps_time >= 1.0:
                        camera_fps = frame_counter
                        frame_counter = 0
                        fps_time = now

        except requests.exceptions.RequestException:
            time.sleep(0.02)


def get_latest_frame():
    with latest_frame_lock:
        if latest_frame is None:
            return None, latest_frame_id

        return latest_frame.copy(), latest_frame_id


# ---------------- MOTION DETECTION ----------------
def detect_motion(frame, background_subtractor):
    motion_boxes = []

    small = cv2.resize(frame, (MOTION_WIDTH, MOTION_HEIGHT))
    gray = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY)
    gray = cv2.GaussianBlur(gray, (5, 5), 0)

    mask = background_subtractor.apply(gray)
    _, mask = cv2.threshold(mask, MOTION_THRESHOLD, 255, cv2.THRESH_BINARY)

    kernel = np.ones((3, 3), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)
    mask = cv2.dilate(mask, kernel, iterations=1)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    h_original, w_original = frame.shape[:2]

    scale_x = w_original / MOTION_WIDTH
    scale_y = h_original / MOTION_HEIGHT

    for contour in contours:
        area = cv2.contourArea(contour)

        if area < MOTION_MIN_AREA:
            continue

        x, y, w, h = cv2.boundingRect(contour)

        x1 = int(x * scale_x)
        y1 = int(y * scale_y)
        x2 = int((x + w) * scale_x)
        y2 = int((y + h) * scale_y)

        motion_boxes.append((x1, y1, x2, y2, area))

    return motion_boxes


# ---------------- DISPLAY ----------------
def make_large_display(frame):
    original_h, original_w = frame.shape[:2]

    scale = min(DISPLAY_WIDTH / original_w, DISPLAY_HEIGHT / original_h)

    new_w = int(original_w * scale)
    new_h = int(original_h * scale)

    resized = cv2.resize(frame, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

    display = np.zeros((DISPLAY_HEIGHT, DISPLAY_WIDTH, 3), dtype=np.uint8)

    x_offset = (DISPLAY_WIDTH - new_w) // 2
    y_offset = (DISPLAY_HEIGHT - new_h) // 2

    display[y_offset:y_offset + new_h, x_offset:x_offset + new_w] = resized

    return display, scale, x_offset, y_offset


def draw_motion_boxes_on_display(display, motion_boxes, scale, x_offset, y_offset):
    for x1, y1, x2, y2, area in motion_boxes:
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
            0.65,
            (255, 0, 0),
            2
        )


def draw_overlay(display, motion_count, motion_status):
    cv2.putText(
        display,
        "WASD move | X/Space stop | J/L pan | I/K tilt | C center | Q quit",
        (25, 40),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.75,
        (0, 255, 0),
        2
    )

    cv2.putText(
        display,
        f"Pan:{pan}  Tilt:{tilt}  RealCamFPS:{camera_fps}",
        (25, 80),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.75,
        (0, 255, 255),
        2
    )

    cv2.putText(
        display,
        f"Motion:{motion_status}  Count:{motion_count}",
        (25, 120),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.75,
        (255, 0, 0),
        2
    )


# ---------------- KEYBOARD ----------------
def handle_keyboard(key):
    if key == ord("w"):
        move(1)

    elif key == ord("s"):
        move(2)

    elif key == ord("a"):
        move(3)

    elif key == ord("d"):
        move(4)

    elif key == ord("x") or key == 32:
        move(0)

    elif key == ord("j"):
        set_pan(pan - 5)

    elif key == ord("l"):
        set_pan(pan + 5)

    elif key == ord("i"):
        set_tilt(tilt + 5)

    elif key == ord("k"):
        set_tilt(tilt - 5)

    elif key == ord("c"):
        center_camera()

    elif key == ord("q"):
        move(0)
        return False

    return True


# ---------------- MAIN ----------------
def main():
    global running

    print("Testing ESP32 connection...")

    try:
        test = requests.get(f"http://{ESP_IP}/test", timeout=2)
        print(test.text)
    except requests.exceptions.RequestException:
        print("Cannot reach ESP32.")
        print("Check:", f"http://{ESP_IP}/test")
        return

    threading.Thread(target=command_worker, daemon=True).start()
    threading.Thread(target=camera_worker, daemon=True).start()

    center_camera()
    move(0)

    background_subtractor = cv2.createBackgroundSubtractorMOG2(
        history=50,
        varThreshold=50,
        detectShadows=False
    )

    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(WINDOW_NAME, DISPLAY_WIDTH, DISPLAY_HEIGHT)

    last_seen_frame_id = -1
    frame_counter = 0
    last_motion_boxes = []

    print("Starting window...")

    while True:
        frame, frame_id = get_latest_frame()

        if frame is None:
            key = cv2.waitKey(1) & 0xFF

            if key == ord("q"):
                break

            time.sleep(0.005)
            continue

        if frame_id == last_seen_frame_id:
            key = cv2.waitKey(1) & 0xFF

            if key != 255:
                keep_running = handle_keyboard(key)

                if not keep_running:
                    break

            time.sleep(0.001)
            continue

        last_seen_frame_id = frame_id
        frame_counter += 1

        motion_status = "ON"
        motion_boxes = []

        camera_recently_moved = time.time() - last_camera_move_time < MOTION_COOLDOWN_SECONDS

        if USE_MOTION_DETECTION:
            if MOTION_PAUSE_WHILE_DRIVING and last_move != 0:
                motion_status = "PAUSED CAR"
                motion_boxes = []

            elif camera_recently_moved:
                motion_status = "PAUSED CAM"
                motion_boxes = []

            elif frame_counter % PROCESS_MOTION_EVERY_N_FRAMES == 0:
                last_motion_boxes = detect_motion(frame, background_subtractor)
                motion_boxes = last_motion_boxes

            else:
                motion_boxes = last_motion_boxes

        else:
            motion_status = "OFF"

        display, scale, x_offset, y_offset = make_large_display(frame)

        draw_motion_boxes_on_display(display, motion_boxes, scale, x_offset, y_offset)
        draw_overlay(display, len(motion_boxes), motion_status)

        cv2.imshow(WINDOW_NAME, display)

        key = cv2.waitKey(1) & 0xFF

        if key != 255:
            keep_running = handle_keyboard(key)

            if not keep_running:
                break

    move(0)
    time.sleep(0.2)
    running = False

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()