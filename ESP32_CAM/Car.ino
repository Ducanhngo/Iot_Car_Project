#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

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
#define RIGHT_IN1 4
#define RIGHT_IN2 13
#define LEFT_IN3  14
#define LEFT_IN4  15

// ---------------- PAN/TILT SERVO PINS ----------------
// IO2 = pan
// U0R / GPIO3 = tilt
// Disconnect tilt signal while uploading code.
#define PAN_SERVO_PIN   2
#define TILT_SERVO_PIN  3

#define SERVO_MIN_US 500
#define SERVO_MAX_US 2400

Servo panServo;
Servo tiltServo;

int panAngle = 90;
int tiltAngle = 90;

// ---------------- CAR DIRECTION ----------------
#define STOP  0
#define UP    1
#define DOWN  2
#define LEFT  3
#define RIGHT 4

const char* ssid = "MyWiFiCar";
const char* password = "12345678";

WebServer server(80);

int currentDir = STOP;
bool carPowerOn = true;

// ---------------- CAMERA / DETECTION DATA ----------------
bool cameraReady = false;
String cameraMode = "UNKNOWN";

bool motionDetected = false;
int motionScore = 0;
unsigned long lastDetectionMs = 0;

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
  max-width: 1000px;
  margin: 0 auto 12px auto;
  background: #111;
  border: 2px solid #222;
  border-radius: 12px;
  padding: 6px;
  min-height: 40px;
}

.video-box.alarm {
  border: 5px solid red;
}

.camera-wrap {
  position: relative;
  display: inline-block;
  width: 100%;
  background: #000;
  border-radius: 8px;
  overflow: hidden;
}

.camera-wrap img {
  display: block;
  width: 100%;
  max-height: 68vh;
  object-fit: contain;
  background: #000;
}

#overlayCanvas {
  position: absolute;
  left: 0;
  top: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
}

.detect-box {
  max-width: 1000px;
  margin: 10px auto;
  padding: 10px;
  background: #e8ffe8;
  border: 2px solid #008000;
  border-radius: 14px;
  font-weight: bold;
  font-size: 18px;
}

.detect-box.alarm {
  background: #ffe8e8;
  border: 2px solid red;
  color: red;
}

.info-row {
  max-width: 1000px;
  margin: 10px auto;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
}

.status,
.fps-box {
  min-height: 34px;
  padding: 8px;
  background: #fff;
  border: 1px solid #777;
  border-radius: 12px;
  font-weight: bold;
}

.control-box {
  max-width: 1000px;
  margin: 10px auto;
  padding: 12px;
  background: #fff;
  border: 1px solid #777;
  border-radius: 14px;
}

.slider-row {
  margin-top: 12px;
  font-weight: bold;
}

.slider-row input {
  width: 70%;
  max-width: 500px;
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

.power-btn {
  width: 160px;
  height: 60px;
  color: lime;
}

.center-btn {
  width: 120px;
  height: 55px;
  color: cyan;
}

.small-note {
  font-size: 13px;
  color: #555;
  margin-top: 8px;
}

@media (max-width: 600px) {
  .info-row {
    grid-template-columns: 1fr;
  }

  .slider-row input {
    width: 90%;
  }
}
</style>
</head>

<body>
<h2>ESP32-CAM Car — Faster Detection + Pan/Tilt</h2>

<div class="video-box" id="videoBox">
  <div class="camera-wrap">
    <img id="cam" src="/jpg?t=0" alt="Camera">
    <canvas id="overlayCanvas"></canvas>
  </div>
</div>

<div class="detect-box" id="detectBox">
  Detection: starting...
</div>

<div class="info-row">
  <div class="status" id="status">Ready</div>
  <div class="fps-box" id="fpsBox">FPS: starting...</div>
</div>

<div class="small-note" id="modeText">
  Loading ESP32-CAM status...
</div>

<div class="control-box">
  <button class="btn power-btn" id="powerBtn" onclick="togglePower()">POWER ON</button>

  <div class="slider-row">
    Pan:
    <input type="range" min="0" max="180" value="90" id="panSlider"
      oninput="sliderServo('Pan', this.value)">
    <span id="panVal">90</span>°
  </div>

  <div class="slider-row">
    Tilt:
    <input type="range" min="0" max="180" value="90" id="tiltSlider"
      oninput="sliderServo('Tilt', this.value)">
    <span id="tiltVal">90</span>°
  </div>

  <div style="margin-top:12px;">
    <button class="btn center-btn" onclick="centerPanTilt()">CENTER</button>
  </div>
</div>

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

<canvas id="detectCanvas" width="96" height="72" style="display:none;"></canvas>

<script>
// Faster FPS settings.
// If Wi-Fi becomes unstable, change refreshDelay to 120.
var refreshDelay = 95;
var minRefreshDelay = 80;
var maxRefreshDelay = 300;

var imageBusy = false;
var refreshTimer = null;

var frameCounter = 0;
var fpsFrameCount = 0;
var fpsLastTime = Date.now();
var stableFpsValue = 0;

var lastImageStartTime = 0;
var imageTimeoutMs = 1800;

var successFrameCount = 0;
var errorFrameCount = 0;

var powerOn = true;
window.currentMoving = false;

var lastServoSendTime = 0;
var servoSendDelay = 120;

// Detection settings
var smallW = 96;
var smallH = 72;

var pixelDiffThreshold = 34;
var changedPixelTrigger = 560;
var minBoxArea = 90;
var maxChangedRatio = 0.55;

var previousGray = null;
var lastSentDetection = -1;
var lastSendTime = 0;

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

function updatePowerButton() {
  var btn = document.getElementById("powerBtn");

  if (powerOn) {
    btn.innerText = "POWER ON";
    btn.style.background = "#111";
    btn.style.color = "lime";
  } else {
    btn.innerText = "POWER OFF";
    btn.style.background = "#555";
    btn.style.color = "red";
  }
}

function togglePower() {
  powerOn = !powerOn;
  sendCmd("Power", powerOn ? 1 : 0);
  updatePowerButton();

  if (!powerOn) {
    stopDrive();
  }
}

function sliderServo(k, v) {
  if (k == "Pan") {
    document.getElementById("panVal").innerText = v;
  }

  if (k == "Tilt") {
    document.getElementById("tiltVal").innerText = v;
  }

  var now = Date.now();

  if (now - lastServoSendTime < servoSendDelay) {
    return;
  }

  lastServoSendTime = now;
  sendCmd(k, v);
}

function centerPanTilt() {
  document.getElementById("panSlider").value = 90;
  document.getElementById("tiltSlider").value = 90;
  document.getElementById("panVal").innerText = 90;
  document.getElementById("tiltVal").innerText = 90;

  sendCmd("Center", 1);
}

function drive(dir) {
  if (!powerOn) {
    statusText("Software power is OFF");
    return;
  }

  window.currentMoving = true;
  previousGray = null;
  clearRoundedBox();
  updateDetectionUI(false, 0);
  sendDetectionToESP32(false, 0);
  sendCmd("MoveCar", dir);
}

function stopDrive() {
  window.currentMoving = false;
  previousGray = null;
  clearRoundedBox();
  sendCmd("MoveCar", 0);
}

function updateStatusFromESP32() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/data?t=" + Date.now(), true);

  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4 && xhr.status == 200) {
      try {
        var data = JSON.parse(xhr.responseText);

        powerOn = data.power;
        updatePowerButton();

        document.getElementById("panSlider").value = data.pan;
        document.getElementById("tiltSlider").value = data.tilt;
        document.getElementById("panVal").innerText = data.pan;
        document.getElementById("tiltVal").innerText = data.tilt;

        document.getElementById("modeText").innerText =
          "Camera: " + data.cameraMode +
          " | Ready: " + data.cameraReady +
          " | PSRAM: " + data.psram +
          " | Power: " + data.power +
          " | Pan: " + data.pan +
          " | Tilt: " + data.tilt +
          " | Motion: " + data.motion +
          " | Score: " + data.score +
          " | Delay: " + refreshDelay + " ms";
      } catch (e) {
        console.log(e);
      }
    }
  };

  xhr.send();
}

function refreshImage() {
  var now = Date.now();

  if (imageBusy && now - lastImageStartTime > imageTimeoutMs) {
    imageBusy = false;
    errorFrameCount++;
    successFrameCount = 0;

    refreshDelay = Math.min(maxRefreshDelay, refreshDelay + 35);
    statusText("Camera slow. Stabilizing...");
  }

  if (imageBusy) {
    scheduleNextFrame();
    return;
  }

  imageBusy = true;
  lastImageStartTime = Date.now();

  var img = document.getElementById("cam");
  img.src = "/jpg?t=" + lastImageStartTime;
}

function scheduleNextFrame() {
  if (refreshTimer !== null) {
    clearTimeout(refreshTimer);
  }

  refreshTimer = setTimeout(refreshImage, refreshDelay);
}

function updateFPS() {
  fpsFrameCount++;

  var now = Date.now();
  var elapsed = now - fpsLastTime;

  if (elapsed >= 1000) {
    var fps = fpsFrameCount * 1000 / elapsed;

    if (stableFpsValue === 0) {
      stableFpsValue = fps;
    } else {
      stableFpsValue = stableFpsValue * 0.75 + fps * 0.25;
    }

    document.getElementById("fpsBox").innerText =
      "Real FPS: " + stableFpsValue.toFixed(1) +
      " | delay: " + refreshDelay + " ms";

    fpsFrameCount = 0;
    fpsLastTime = now;
  }
}

function sendDetectionToESP32(detected, score) {
  var v = detected ? 1 : 0;
  var now = Date.now();

  if (v === lastSentDetection && now - lastSendTime < 1000) {
    return;
  }

  lastSentDetection = v;
  lastSendTime = now;

  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/detect?v=" + v + "&score=" + score + "&t=" + Date.now(), true);
  xhr.send();
}

function updateDetectionUI(detected, score) {
  var box = document.getElementById("detectBox");
  var videoBox = document.getElementById("videoBox");

  if (detected) {
    box.classList.add("alarm");
    videoBox.classList.add("alarm");
    box.innerText = "Detection: OBJECT / MOTION CHANGE DETECTED | score = " + score;
  } else {
    box.classList.remove("alarm");
    videoBox.classList.remove("alarm");

    if (window.currentMoving) {
      box.innerText = "Detection: paused while car is moving";
    } else {
      box.innerText = "Detection: no object change | score = " + score;
    }
  }
}

function drawRoundedBox(x, y, w, h, score) {
  var img = document.getElementById("cam");
  var overlay = document.getElementById("overlayCanvas");
  var ctx = overlay.getContext("2d");

  overlay.width = img.clientWidth;
  overlay.height = img.clientHeight;

  ctx.clearRect(0, 0, overlay.width, overlay.height);

  var scaleX = overlay.width / smallW;
  var scaleY = overlay.height / smallH;

  var bx = x * scaleX;
  var by = y * scaleY;
  var bw = w * scaleX;
  var bh = h * scaleY;

  // Bigger rounded detection box
  var radius = 26;

  ctx.lineWidth = 6;
  ctx.strokeStyle = "red";
  ctx.fillStyle = "rgba(255, 0, 0, 0.18)";

  ctx.beginPath();
  ctx.moveTo(bx + radius, by);
  ctx.lineTo(bx + bw - radius, by);
  ctx.quadraticCurveTo(bx + bw, by, bx + bw, by + radius);
  ctx.lineTo(bx + bw, by + bh - radius);
  ctx.quadraticCurveTo(bx + bw, by + bh, bx + bw - radius, by + bh);
  ctx.lineTo(bx + radius, by + bh);
  ctx.quadraticCurveTo(bx, by + bh, bx, by + bh - radius);
  ctx.lineTo(bx, by + radius);
  ctx.quadraticCurveTo(bx, by, bx + radius, by);
  ctx.closePath();

  ctx.fill();
  ctx.stroke();

  ctx.font = "bold 20px Arial";
  ctx.fillStyle = "red";
  ctx.fillText("Object Change: " + score, bx + 10, Math.max(by - 10, 24));
}

function clearRoundedBox() {
  var img = document.getElementById("cam");
  var overlay = document.getElementById("overlayCanvas");
  var ctx = overlay.getContext("2d");

  overlay.width = img.clientWidth;
  overlay.height = img.clientHeight;

  ctx.clearRect(0, 0, overlay.width, overlay.height);
}

function analyzeCameraFrame() {
  if (window.currentMoving === true) {
    previousGray = null;
    updateDetectionUI(false, 0);
    clearRoundedBox();
    sendDetectionToESP32(false, 0);
    return;
  }

  var img = document.getElementById("cam");
  var canvas = document.getElementById("detectCanvas");
  var ctx = canvas.getContext("2d", { willReadFrequently: true });

  try {
    ctx.drawImage(img, 0, 0, smallW, smallH);

    var frame = ctx.getImageData(0, 0, smallW, smallH);
    var data = frame.data;

    var gray = new Uint8Array(smallW * smallH);

    for (var i = 0, j = 0; i < data.length; i += 4, j++) {
      gray[j] = (data[i] * 0.30 + data[i + 1] * 0.59 + data[i + 2] * 0.11);
    }

    if (previousGray === null) {
      previousGray = gray;
      updateDetectionUI(false, 0);
      sendDetectionToESP32(false, 0);
      clearRoundedBox();
      return;
    }

    var changedPixels = 0;

    var minX = smallW;
    var minY = smallH;
    var maxX = 0;
    var maxY = 0;

    for (var y = 0; y < smallH; y++) {
      for (var x = 0; x < smallW; x++) {
        var p = y * smallW + x;
        var diff = Math.abs(gray[p] - previousGray[p]);

        if (diff > pixelDiffThreshold) {
          changedPixels++;

          if (x < minX) minX = x;
          if (y < minY) minY = y;
          if (x > maxX) maxX = x;
          if (y > maxY) maxY = y;
        }
      }
    }

    previousGray = gray;

    var changedRatio = changedPixels / (smallW * smallH);

    var boxW = maxX - minX;
    var boxH = maxY - minY;
    var boxArea = boxW * boxH;

    var detected =
      changedPixels > changedPixelTrigger &&
      boxArea > minBoxArea &&
      changedRatio < maxChangedRatio;

    updateDetectionUI(detected, changedPixels);
    sendDetectionToESP32(detected, changedPixels);

    if (detected) {
      // Bigger padding around detected object
      var padding = 12;

      minX = Math.max(0, minX - padding);
      minY = Math.max(0, minY - padding);
      maxX = Math.min(smallW - 1, maxX + padding);
      maxY = Math.min(smallH - 1, maxY + padding);

      boxW = maxX - minX;
      boxH = maxY - minY;

      drawRoundedBox(minX, minY, boxW, boxH, changedPixels);
    } else {
      clearRoundedBox();
    }

  } catch (e) {
    console.log("Detection error:", e);
  }
}

document.getElementById("cam").onload = function() {
  imageBusy = false;

  frameCounter++;
  successFrameCount++;
  errorFrameCount = 0;

  updateFPS();

  if (successFrameCount >= 20) {
    refreshDelay = Math.max(minRefreshDelay, refreshDelay - 10);
    successFrameCount = 0;
  }

  // Detection every 15 frames to keep FPS higher.
  if (frameCounter % 15 == 0) {
    analyzeCameraFrame();
  }

  scheduleNextFrame();
};

document.getElementById("cam").onerror = function() {
  imageBusy = false;
  successFrameCount = 0;
  errorFrameCount++;

  refreshDelay = Math.min(maxRefreshDelay, refreshDelay + 40);

  document.getElementById("fpsBox").innerText =
    "Real FPS: image error | delay: " + refreshDelay + " ms";

  scheduleNextFrame();
};

updatePowerButton();
updateStatusFromESP32();
setInterval(updateStatusFromESP32, 3000);
setTimeout(refreshImage, 500);
</script>
</body>
</html>
)rawliteral";

// ---------------- SERVO LOGIC ----------------
int clampAngle(int angle) {
  if (angle < 0) return 0;
  if (angle > 180) return 180;
  return angle;
}

void setPan(int angle) {
  panAngle = clampAngle(angle);
  panServo.write(panAngle);
}

void setTilt(int angle) {
  tiltAngle = clampAngle(angle);
  tiltServo.write(tiltAngle);
}

void setupServos() {
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  panServo.setPeriodHertz(50);
  tiltServo.setPeriodHertz(50);

  panServo.attach(PAN_SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
  tiltServo.attach(TILT_SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);

  setPan(90);
  setTilt(90);

  Serial.println("Pan/Tilt servos ready.");
}

// ---------------- MOTOR LOGIC ----------------
void moveCar(int v) {
  if (!carPowerOn) {
    v = STOP;
  }

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

// ---------------- CAMERA SETUP ----------------
void setupCamera() {
  camera_config_t config = {};

  bool hasPsram = psramFound();

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

  // Faster QVGA mode.
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 14;
  config.fb_count = hasPsram ? 2 : 1;

  if (hasPsram) {
    cameraMode = "QVGA 320x240 fast FPS, quality 14, PSRAM";
  } else {
    cameraMode = "QVGA 320x240 fast FPS, quality 14, no PSRAM";
  }

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    cameraReady = false;
    cameraMode = "Camera init failed";
    return;
  }

  sensor_t *sensor = esp_camera_sensor_get();

  if (sensor != NULL) {
    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    sensor->set_quality(sensor, 14);

    sensor->set_vflip(sensor, 1);
    sensor->set_hmirror(sensor, 0);

    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 1);
    sensor->set_saturation(sensor, 0);

    sensor->set_whitebal(sensor, 1);
    sensor->set_awb_gain(sensor, 1);
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_gain_ctrl(sensor, 1);
  }

  cameraReady = true;

  Serial.print("Camera init OK: ");
  Serial.println(cameraMode);
}

// ---------------- CAMERA IMAGE HANDLER ----------------
void handleJpg() {
  if (!cameraReady) {
    server.send(503, "text/plain", "Camera not ready");
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");

  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);

  esp_camera_fb_return(fb);
}

// ---------------- COMMAND HANDLER ----------------
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
  else if (key == "Power") {
    carPowerOn = value == 1;

    if (!carPowerOn) {
      moveCar(STOP);
    }
  }
  else if (key == "Pan") {
    setPan(value);
  }
  else if (key == "Tilt") {
    setTilt(value);
  }
  else if (key == "Center") {
    setPan(90);
    setTilt(90);
  }
  else {
    server.send(400, "text/plain", "Unknown command");
    return;
  }

  server.send(200, "text/plain", "OK");
}

// ---------------- DETECTION HANDLER ----------------
void handleDetection() {
  if (!server.hasArg("v")) {
    server.send(400, "text/plain", "Missing v");
    return;
  }

  int v = server.arg("v").toInt();
  motionDetected = (v == 1);

  if (server.hasArg("score")) {
    motionScore = server.arg("score").toInt();
  }

  lastDetectionMs = millis();

  Serial.print("Detection: ");
  Serial.print(motionDetected ? "OBJECT / MOTION CHANGE DETECTED" : "no object change");
  Serial.print(" | score = ");
  Serial.println(motionScore);

  server.send(200, "text/plain", "OK");
}

// ---------------- DATA JSON HANDLER ----------------
void handleData() {
  String json = "{";
  json += "\"cameraReady\":";
  json += (cameraReady ? "true" : "false");
  json += ",";
  json += "\"cameraMode\":\"";
  json += cameraMode;
  json += "\",";
  json += "\"psram\":";
  json += (psramFound() ? "true" : "false");
  json += ",";
  json += "\"direction\":";
  json += String(currentDir);
  json += ",";
  json += "\"power\":";
  json += (carPowerOn ? "true" : "false");
  json += ",";
  json += "\"pan\":";
  json += String(panAngle);
  json += ",";
  json += "\"tilt\":";
  json += String(tiltAngle);
  json += ",";
  json += "\"motion\":";
  json += (motionDetected ? "true" : "false");
  json += ",";
  json += "\"score\":";
  json += String(motionScore);
  json += ",";
  json += "\"last_ms\":";
  json += String(lastDetectionMs);
  json += "}";

  server.send(200, "application/json", json);
}

// ---------------- PIN SETUP ----------------
void setupPins() {
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);
  pinMode(LEFT_IN3, OUTPUT);
  pinMode(LEFT_IN4, OUTPUT);

  moveCar(STOP);
}

// ---------------- WIFI SETUP ----------------
void setupWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Channel 6, visible SSID, max 1 connected client.
  bool ok = WiFi.softAP(ssid, password, 6, 0, 1);

  Serial.print("softAP status: ");
  Serial.println(ok ? "OK" : "FAILED");

  Serial.print("WiFi AP: ");
  Serial.println(ssid);

  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Starting ESP32-CAM faster object detection pan/tilt car...");

  setupPins();
  setupWiFi();

  setupCamera();
  setupServos();

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.send(200, "text/html", htmlHomePage);
  });

  server.on("/jpg", HTTP_GET, handleJpg);
  server.on("/cmd", HTTP_GET, handleCommand);
  server.on("/detect", HTTP_GET, handleDetection);
  server.on("/data", HTTP_GET, handleData);

  server.on("/test", HTTP_GET, []() {
    String msg = "ESP32-CAM web server OK\n";
    msg += "Camera ready: ";
    msg += cameraReady ? "YES\n" : "NO\n";
    msg += "Camera mode: ";
    msg += cameraMode;
    msg += "\n";
    msg += "PSRAM: ";
    msg += psramFound() ? "YES\n" : "NO\n";
    msg += "Power: ";
    msg += carPowerOn ? "ON\n" : "OFF\n";
    msg += "Pan angle: ";
    msg += String(panAngle);
    msg += "\n";
    msg += "Tilt angle: ";
    msg += String(tiltAngle);
    msg += "\n";
    msg += "Motion: ";
    msg += motionDetected ? "YES\n" : "NO\n";
    msg += "Score: ";
    msg += String(motionScore);
    msg += "\n";
    msg += "Open /jpg to test camera image directly\n";

    server.send(200, "text/plain", msg);
  });

  server.begin();

  Serial.println("Server started.");
}

// ---------------- LOOP ----------------
void loop() {
  server.handleClient();
  delay(1);
}
