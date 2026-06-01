# IoT Surveillance Car Using ESP32-CAM

An IoT-based surveillance car controlled through Wi-Fi using an ESP32-CAM, L298N motor driver, and four DC motors. The car hosts its own web control page, shows camera images in the browser, and allows the user to control movement from a phone or laptop.

> Current status: the main car control system is completed. AI camera processing has not been added yet.

## Project Overview

This project demonstrates a low-cost mobile surveillance system for simple remote observation. The ESP32-CAM works as the main controller, Wi-Fi access point, web server, and camera module.

The user connects to the ESP32-CAM Wi-Fi network, opens the browser control page, views camera images, and controls the car movement using web buttons.

## Main Features

- ESP32-CAM creates its own Wi-Fi access point.
- Browser-based control interface.
- Camera image display through repeated JPEG refresh.
- Forward, backward, left, right, and stop movement controls.
- L298N motor driver controls four DC motors as left and right motor pairs.
- UBEC 5V regulator provides stable power to the ESP32-CAM.
- No external router or server required.

## Hardware Components

| Component | Quantity | Purpose |
|---|---:|---|
| ESP32-CAM with OV2640 camera | 1 | Main controller, camera, Wi-Fi AP, and web server |
| L298N motor driver | 1 | Controls left and right motor pairs |
| DC motors with wheels | 4 | Provides movement |
| Robot car chassis | 1 | Mechanical body |
| UBEC 5V regulator | 1 | Stable 5V supply for ESP32-CAM |
| 18650 battery pack | 1 | Main power source |
| Jumper wires | Several | Wiring connections |
| FTDI programmer | 1 | Uploads code to ESP32-CAM |

## System Architecture

```text
Phone / Laptop
     |
     | Wi-Fi
     v
ESP32-CAM
Camera + Web Server + GPIO Control
     |
     | GPIO signals
     v
L298N Motor Driver
     |
     v
4 DC Motors
Left Pair + Right Pair
```

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

Important: do not connect the battery pack output directly to the ESP32-CAM 5V pin. The ESP32-CAM should receive only regulated 5V from the UBEC.

### L298N Motor Driver Connections

| ESP32-CAM Pin | L298N Pin | Purpose |
|---|---|---|
| GPIO4 | IN1 | Right motor pair direction input 1 |
| GPIO13 | IN2 | Right motor pair direction input 2 |
| GPIO14 | IN3 | Left motor pair direction input 1 |
| GPIO15 | IN4 | Left motor pair direction input 2 |

## Software

The project uses:

- Arduino IDE
- ESP32 board support package
- `esp_camera` library
- `WiFi` library
- `WebServer` library

The ESP32-CAM creates a Wi-Fi network:

```text
SSID: MyWiFiCar
Password: 12345678
```

After connecting to the Wi-Fi network, open:

```text
http://192.168.4.1
```

## Web Interface

The ESP32-CAM serves a browser page with:

- Camera image area
- Status display
- Forward button
- Backward button
- Left button
- Right button
- Stop button

Camera images are loaded from:

```text
/jpg
```

Movement commands are sent to:

```text
/cmd?k=MoveCar&v=<direction>
```

## Movement Logic

| Command | Motor Action |
|---|---|
| Forward | Both motor pairs rotate forward |
| Backward | Both motor pairs rotate backward |
| Left | Right motor pair forward, left motor pair backward |
| Right | Left motor pair forward, right motor pair backward |
| Stop | All motors stop |

Direction values used in the program:

```cpp
#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4
#define STOP 0
```

## Testing Procedure

1. Upload the program to the ESP32-CAM using the FTDI programmer.
2. Disconnect GPIO0 from GND after uploading.
3. Power the ESP32-CAM using the UBEC 5V output.
4. Check that the Wi-Fi network `MyWiFiCar` appears.
5. Connect a phone or laptop to the Wi-Fi network.
6. Open `http://192.168.4.1`.
7. Verify that the camera image appears.
8. Press each movement button and observe the motor response.

## Current Progress

Completed:

- Hardware design and wiring plan.
- ESP32-CAM Wi-Fi access point.
- Web control page.
- Camera image refresh.
- HTTP command handling.
- L298N motor control.
- Forward, backward, left, right, and stop movement logic.

Not completed yet:

- AI processing for camera images.
- Object detection, human detection, obstacle detection, or automatic driving behavior.

## Future Improvements

- Add AI camera processing.
- Detect people, objects, or obstacles.
- Improve camera refresh speed and stability.
- Add autonomous or assisted driving mode.
- Add battery level monitoring.
- Add stronger chassis testing and wireless range testing.

## Challenges

- ESP32-CAM needs stable 5V power, so the UBEC regulator is important.
- ESP32-CAM has limited memory, so camera resolution and JPEG quality must be balanced.
- Some ESP32-CAM GPIO pins are boot-sensitive, especially GPIO15.
- AI integration may require a lightweight model or external processing on a phone, laptop, or server.

## Team Members

| Student ID | Full Name |
|---|---|
| 104240148 | Ngo Duc Anh |
| 104240199 | Tran Vu Hai Dang |
| 17700 | Nguyen Pham Kien Trung |

## Conclusion

This project is a working proof-of-concept for a low-cost IoT surveillance car. The ESP32-CAM handles Wi-Fi, the web server, camera image capture, and motor commands. The current version supports manual wireless control and live camera viewing. The next major step is adding AI camera processing so the car can understand the environment instead of only displaying images.
