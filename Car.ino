#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// ---------------- AI THINKER ESP32-CAM PIN MAP ----------------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ---------------- L298N MOTOR PINS ----------------
// Keep ENA and ENB jumpers installed on the L298N.
// These pins control direction only.
#define RIGHT_IN1 4
#define RIGHT_IN2 13
#define LEFT_IN3  14
#define LEFT_IN4  15

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4
#define STOP 0

const char* ssid = "MyWiFiCar";
const char* password = "12345678";

WebServer server(80);
int currentDir = STOP;

// ---------------- HTML PAGE ----------------
const char* htmlHomePage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<style>
body {
  margin: 0;
  padding: 10px;
  background: #f6f6f6;
  color: #111;
  font-family: Arial, sans-serif;
  text-align: center;
  user-select: none;
  -webkit-user-select: none;
}

h2 {
  margin: 8px 0 10px 0;
}

.video-box {
  max-width: 850px;
  margin: 0 auto 12px auto;
  background: #111;
  border: 2px solid #222;
  padding: 6px;
}

.video-box img {
  display: block;
  width: 100%;
  max-height: 52vh;
  object-fit: contain;
  background: #000;
}

.status {
  max-width: 850px;
  min-height: 34px;
  margin: 10px auto;
  padding: 8px;
  background: #fff;
  border: 1px solid #777;
  font-weight: bold;
}

.pad {
  display: grid;
  grid-template-columns: 76px 76px 76px;
  grid-template-rows: 76px 76px 76px;
  gap: 8px;
  justify-content: center;
  margin-top: 14px;
}

.btn {
  width: 76px;
  height: 76px;
  border: 0;
  border-radius: 12px;
  background: #111;
  color: red;
  font-size: 18px;
  font-weight: bold;
  box-shadow: 4px 4px #888;
  cursor: pointer;
}

.btn:active {
  transform: translate(4px, 4px);
  box-shadow: none;
}
</style>
</head>
<body>
<h2>ESP32-CAM Car</h2>

<div class="video-box">
  <img id="cam" src="/jpg?t=0" alt="Camera">
</div>

<div class="status" id="status">Ready</div>

<div class="pad">
  <div></div>
  <button class="btn"
    onmousedown="drive(1)" onmouseup="stopDrive()" onmouseleave="stopDrive()"
    ontouchstart="drive(1); event.preventDefault();" ontouchend="stopDrive(); event.preventDefault();" ontouchcancel="stopDrive();">
    FWD
  </button>
  <div></div>

  <button class="btn"
    onmousedown="drive(3)" onmouseup="stopDrive()" onmouseleave="stopDrive()"
    ontouchstart="drive(3); event.preventDefault();" ontouchend="stopDrive(); event.preventDefault();" ontouchcancel="stopDrive();">
    LEFT
  </button>
  <button class="btn" onclick="stopDrive()">STOP</button>
  <button class="btn"
    onmousedown="drive(4)" onmouseup="stopDrive()" onmouseleave="stopDrive()"
    ontouchstart="drive(4); event.preventDefault();" ontouchend="stopDrive(); event.preventDefault();" ontouchcancel="stopDrive();">
    RIGHT
  </button>

  <div></div>
  <button class="btn"
    onmousedown="drive(2)" onmouseup="stopDrive()" onmouseleave="stopDrive()"
    ontouchstart="drive(2); event.preventDefault();" ontouchend="stopDrive(); event.preventDefault();" ontouchcancel="stopDrive();">
    BACK
  </button>
  <div></div>
</div>

<script>
var imageBusy = false;

function statusText(msg) {
  document.getElementById("status").innerText = msg;
}

function sendCmd(k, v) {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/cmd?k=" + encodeURIComponent(k) + "&v=" + encodeURIComponent(v) + "&t=" + Date.now(), true);
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        statusText("OK: " + k + " = " + v);
      } else {
        statusText("Command failed");
      }
    }
  };
  xhr.onerror = function() {
    statusText("Send failed");
  };
  xhr.send();
}

function drive(dir) {
  sendCmd("MoveCar", dir);
}

function stopDrive() {
  sendCmd("MoveCar", 0);
}

function refreshImage() {
  if (imageBusy) {
    return;
  }

  imageBusy = true;
  document.getElementById("cam").src = "/jpg?t=" + Date.now();
}

document.getElementById("cam").onload = function() {
  imageBusy = false;
};

document.getElementById("cam").onerror = function() {
  imageBusy = false;
};

setInterval(refreshImage, 400);
</script>
</body>
</html>
)rawliteral";

// ---------------- MOTOR LOGIC ----------------
void moveCar(int v) {
  currentDir = v;

  if (v == UP) {
    digitalWrite(RIGHT_IN1, HIGH);
    digitalWrite(RIGHT_IN2, LOW);
    digitalWrite(LEFT_IN3, HIGH);
    digitalWrite(LEFT_IN4, LOW);
  }
  else if (v == DOWN) {
    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, HIGH);
    digitalWrite(LEFT_IN3, LOW);
    digitalWrite(LEFT_IN4, HIGH);
  }
  else if (v == LEFT) {
    digitalWrite(RIGHT_IN1, HIGH);
    digitalWrite(RIGHT_IN2, LOW);
    digitalWrite(LEFT_IN3, LOW);
    digitalWrite(LEFT_IN4, HIGH);
  }
  else if (v == RIGHT) {
    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, HIGH);
    digitalWrite(LEFT_IN3, HIGH);
    digitalWrite(LEFT_IN4, LOW);
  }
  else {
    currentDir = STOP;
    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, LOW);
    digitalWrite(LEFT_IN3, LOW);
    digitalWrite(LEFT_IN4, LOW);
  }
}

// ---------------- CAMERA ----------------
void setupCamera() {
  camera_config_t config = {};

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
#else
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
#endif

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = psramFound() ? 2 : 1;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    while (true) {
      delay(1000);
    }
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != NULL) {
    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    sensor->set_quality(sensor, 12);
  }

  Serial.println("Camera init OK");
}

void handleJpg() {
  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");

  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);

  esp_camera_fb_return(fb);
}

void handleCommand() {
  if (!server.hasArg("k") || !server.hasArg("v")) {
    server.send(400, "text/plain", "Missing k or v");
    return;
  }

  String key = server.arg("k");
  int value = server.arg("v").toInt();

  Serial.print("Command: ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.println(value);

  if (key == "MoveCar") {
    if (value < STOP || value > RIGHT) {
      value = STOP;
    }
    moveCar(value);
  }
  else {
    server.send(400, "text/plain", "Unknown command");
    return;
  }

  server.send(200, "text/plain", "OK");
}

void setupPins() {
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);
  pinMode(LEFT_IN3, OUTPUT);
  pinMode(LEFT_IN4, OUTPUT);
  moveCar(STOP);
}

void setupWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(ssid, password);

  Serial.print("WiFi AP: ");
  Serial.println(ssid);
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Starting ESP32-CAM motor-only car...");

  setupWiFi();
  setupPins();
  setupCamera();

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.send(200, "text/html", htmlHomePage);
  });
  server.on("/jpg", HTTP_GET, handleJpg);
  server.on("/cmd", HTTP_GET, handleCommand);
  server.on("/test", HTTP_GET, []() {
    server.send(200, "text/plain", "ESP32-CAM motor-only car OK");
  });

  server.begin();
  Serial.println("Server started.");
}

void loop() {
  server.handleClient();
}
