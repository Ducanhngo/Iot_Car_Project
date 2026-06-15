# IoT Surveillance Car Using ESP32-CAM

An IoT-based surveillance car controlled through Wi-Fi using an ESP32-CAM, an L298N motor driver, four DC motors, and optional pan/tilt servos. The ESP32-CAM creates its own Wi-Fi access point, hosts a browser control page, streams live camera video, and controls the motors directly.

The current version supports manual driving, live MJPEG camera streaming, browser-side motion detection, pan/tilt control, WASD keyboard control, and left/right motor speed balancing.

## Current Status

Completed:

- ESP32-CAM Wi-Fi access point.
- Web control page hosted directly by the ESP32-CAM.
- MJPEG video stream for higher FPS.
- Forward, backward, left, right, and stop controls.
- WASD keyboard control in the browser.
- Pan/tilt servo controls.
- Browser-side motion/change detection overlay.
- Left and right motor speed sliders for correcting unbalanced movement.
- Optional Python/OpenCV laptop controller.

Not included yet:

- Full AI object recognition.
- Autonomous driving.
- Battery percentage measurement.

## Main Features

- No external router or cloud server required.
- ESP32-CAM runs as a Wi-Fi access point.
- Browser interface works from a phone or laptop.
- MJPEG stream runs on port `81`.
- Main web control page runs on port `80`.
- Left and right motor speed can be adjusted independently.
- Motion detection runs in the browser or optional Python app, not on the ESP32-CAM CPU.
- Optional Python app can be used for OpenCV testing and stronger laptop-side processing.

## Hardware Components

| Component | Quantity | Purpose |
|---|---:|---|
| ESP32-CAM with OV2640 camera | 1 | Main controller, camera, Wi-Fi AP, and web server |
| L298N motor driver | 1 | Controls left and right motor pairs |
| DC motors with wheels | 4 | Provides movement |
| Robot car chassis | 1 | Mechanical body |
| SG90 / small servo | 2 | Optional camera pan/tilt |
| Pan/tilt bracket | 1 | Optional camera mount |
| UBEC 5V regulator | 1 | Stable 5V supply for ESP32-CAM |
| 18650 battery pack | 1 | Main motor power source |
| Jumper wires | Several | Wiring connections |
| FTDI programmer | 1 | Uploads code to ESP32-CAM |

## Software Requirements

### Arduino Side

Install these in Arduino IDE:

- ESP32 board support package.
- `ESP32Servo` library.

The sketch also uses these built-in ESP32 libraries:

- `esp_camera`
- `WiFi`
- `WebServer`

Recommended board selection:

```text
Board: AI Thinker ESP32-CAM
Upload Speed: 115200
Partition Scheme: Huge APP or a scheme large enough for camera code
```

### Optional Python App

The optional laptop controller is in:

```text
App (Optional)/car_control.py
```

Install Python dependencies:

```bash
pip install opencv-python requests numpy
```

Run it with:

```bash
python "App (Optional)/car_control.py" --ip 192.168.4.1
```

If your ESP32-CAM prints a different IP in Serial Monitor, use that IP instead:

```bash
python "App (Optional)/car_control.py" --ip YOUR_ESP32_IP
```

## Wi-Fi and Web Access

The ESP32-CAM creates this Wi-Fi network:

```text
SSID: MyWiFiCar
Password: 12345678
```

After connecting your phone or laptop to that Wi-Fi network, open the ESP32-CAM IP in a browser.

The default ESP32 SoftAP IP is usually:

```text
http://192.168.4.1/
```

If this does not work, open Serial Monitor after boot and check the printed IP address.

## Web Endpoints

| Endpoint | Purpose |
|---|---|
| `/` | Main web control page |
| `/jpg` | Single JPEG camera frame fallback |
| `:81/stream` | MJPEG camera stream |
| `/cmd?k=<key>&v=<value>` | Sends a car, servo, power, or speed command |
| `/detect?v=<0_or_1>&score=<score>` | Receives browser/Python detection state |
| `/data` | Returns JSON status data |
| `/test` | Simple connection and status test |

Example command:

```text
http://192.168.4.1/cmd?k=MoveCar&v=1
```

## Command Values

### Movement Commands

| Direction | Value | Motor Action |
|---|---:|---|
| Stop | `0` | All motors stop |
| Forward | `1` | Left and right motor pairs forward |
| Backward | `2` | Left and right motor pairs backward |
| Left | `3` | Right pair forward, left pair backward |
| Right | `4` | Left pair forward, right pair backward |

Code definitions:

```cpp
#define STOP  0
#define UP    1
#define DOWN  2
#define LEFT  3
#define RIGHT 4
```

### Other Commands

| Command Key | Value Range | Purpose |
|---|---:|---|
| `MoveCar` | `0` to `4` | Controls car direction |
| `Power` | `0` or `1` | Software motor power off/on |
| `Pan` | `0` to `180` | Pan servo angle |
| `Tilt` | `0` to `180` | Tilt servo angle |
| `Center` | `1` | Centers pan and tilt to 90 degrees |
| `LeftSpeed` | `0` to `255` | PWM speed for left motor pair |
| `RightSpeed` | `0` to `255` | PWM speed for right motor pair |
| `MinDuty` | `0` to `255` | Minimum PWM duty used when motor speed is non-zero |

## Browser Controls

The main web page supports both buttons and keyboard control.

| Key | Action |
|---|---|
| `W` | Forward |
| `S` | Backward |
| `A` | Turn left |
| `D` | Turn right |
| `Space` or `X` | Stop |
| `J` / `L` | Pan left/right |
| `I` / `K` | Tilt up/down |
| `C` | Center camera |

Click once on the page before using the keyboard so the browser gives the page keyboard focus.

## Optional Python App Controls

The optional Python app uses OpenCV and HTTP commands. It is useful when you want laptop-side camera processing or debugging.

| Key | Action |
|---|---|
| `W` | Forward |
| `S` | Backward |
| `A` | Turn left |
| `D` | Turn right |
| `Space` or `X` | Stop |
| `J` / `L` | Pan left/right |
| `I` / `K` | Tilt up/down |
| `C` | Center camera |
| `[` / `]` | Decrease/increase left motor speed |
| `;` / `'` | Decrease/increase right motor speed |
| `-` / `=` | Decrease/increase both motor speeds |
| `P` | Toggle software power |
| `M` | Toggle optional OpenCV motion detection |
| `Q` | Quit |

The Python app is optional. The browser page is the main controller.

## Motor Speed Balancing

If the car does not drive straight, adjust the left and right motor speeds.

Example: if the car curves to the right when moving forward, the left motor is probably faster. Lower the left motor speed or raise the right motor speed.

Good starting values:

```text
Left speed: 200
Right speed: 200
```

If the left motor is faster, try:

```text
Left speed: 180
Right speed: 200
```

The code controls speed with PWM on the existing L298N input pins, so no extra ESP32-CAM GPIO pins are required for ENA/ENB speed control.

## Wiring

### Power Wiring

| From | To |
|---|---|
| Battery pack + | L298N +12V / VMS |
| Battery pack - | L298N GND |
| Battery pack + | UBEC IN+ |
| Battery pack - | UBEC IN- |
| UBEC OUT+ 5V | ESP32-CAM 5V |
| UBEC OUT- GND | ESP32-CAM GND |
| UBEC OUT- GND | L298N GND |

Important: do not connect the raw battery pack output directly to the ESP32-CAM 5V pin. The ESP32-CAM should receive regulated 5V from the UBEC.

### L298N Motor Driver Connections

| ESP32-CAM Pin | L298N Pin | Purpose |
|---|---|---|
| GPIO4 | IN1 | Right motor pair input 1 |
| GPIO13 | IN2 | Right motor pair input 2 |
| GPIO14 | IN3 | Left motor pair input 1 |
| GPIO15 | IN4 | Left motor pair input 2 |

### Pan/Tilt Servo Connections

| ESP32-CAM Pin | Servo | Note |
|---|---|---|
| GPIO2 | Pan servo signal | May affect boot if pulled incorrectly |
| GPIO3 / U0R | Tilt servo signal | Disconnect while uploading code |
| 5V regulated supply | Servo VCC | External 5V recommended |
| GND | Servo GND | Must share ground with ESP32-CAM |

Servo warning: GPIO3 is also the ESP32-CAM UART RX pin. Disconnect the tilt servo signal while uploading code with the FTDI programmer.

## System Architecture

```text
Phone / Laptop Browser
        |
        | Wi-Fi AP: MyWiFiCar
        v
ESP32-CAM
  - Web page on port 80
  - MJPEG camera stream on port 81
  - /cmd HTTP command endpoint
  - /data status endpoint
        |
        | GPIO PWM + direction signals
        v
L298N Motor Driver
        |
        v
4 DC Motors
Left Pair + Right Pair
```

Optional laptop app:

```text
Python/OpenCV App
        |
        | Reads :81/stream
        | Sends /cmd HTTP commands
        v
ESP32-CAM
```

## Camera FPS Tuning

The current code uses MJPEG streaming instead of repeated `/jpg` refresh. This gives better FPS and lower browser overhead.

Balanced quality setting:

```cpp
#define CAMERA_FRAME_SIZE     FRAMESIZE_QVGA
#define CAMERA_JPEG_QUALITY   20
```

Higher FPS setting:

```cpp
#define CAMERA_FRAME_SIZE     FRAMESIZE_QQVGA
#define CAMERA_JPEG_QUALITY   24
```

Notes:

- Higher JPEG quality number means lower image quality but faster transmission.
- QVGA is `320x240`.
- QQVGA is `160x120`.
- Keep only one video client connected for best FPS.
- Browser detection and Python detection should not both run at the same time if you want maximum FPS.

## Testing Procedure

1. Open `ESP32_CAM/Car.ino` in Arduino IDE.
2. Select the AI Thinker ESP32-CAM board.
3. Connect GPIO0 to GND for upload mode.
4. Upload the code using the FTDI programmer.
5. Disconnect GPIO0 from GND after uploading.
6. Power the car using the UBEC 5V output for ESP32-CAM.
7. Open Serial Monitor at `115200` baud.
8. Confirm that the ESP32-CAM prints its Wi-Fi AP and IP address.
9. Connect your phone or laptop to `MyWiFiCar`.
10. Open the printed IP address in a browser.
11. Check the camera stream.
12. Test movement buttons and WASD control.
13. Adjust left/right motor speed until the car drives straight.

## Troubleshooting

### The web page does not open

- Make sure you are connected to `MyWiFiCar`.
- Try `http://192.168.4.1/`.
- Check Serial Monitor for the actual IP address.
- Make sure the ESP32-CAM has stable 5V power.

### Camera is slow

- Use only one video client at a time.
- Switch from QVGA to QQVGA.
- Increase `CAMERA_JPEG_QUALITY` to `24` or higher.
- Keep the laptop/phone close to the ESP32-CAM.
- Try Wi-Fi channel `1`, `6`, or `11` in `WiFi.softAP(...)`.

### Car turns when it should go straight

- Use the left/right speed sliders.
- Lower the faster side.
- Check motor wiring and wheel friction.
- Make sure the battery can supply enough current.

### Upload fails

- Disconnect the servo connected to GPIO3/U0R while uploading.
- Make sure GPIO0 is connected to GND only during upload.
- Press reset at the start of upload if needed.

### ESP32-CAM resets when motors start

- Use a separate regulated 5V supply for the ESP32-CAM.
- Connect all grounds together.
- Add a capacitor near ESP32-CAM 5V/GND.
- Do not power the ESP32-CAM from the L298N 5V output unless it is stable enough.

## Team Members

| Student ID | Full Name |
|---|---|
| 104240148 | Ngo Duc Anh |
| 104240199 | Tran Vu Hai Dang |
| 17700 | Nguyen Pham Kien Trung |

## Conclusion

This project is now a working IoT surveillance car with live camera streaming, browser control, motor speed balancing, pan/tilt support, and optional laptop-side OpenCV control. The ESP32-CAM handles the web server, camera stream, and real-time motor commands, while heavier detection should run in the browser or optional Python app to avoid slowing down the ESP32-CAM.
