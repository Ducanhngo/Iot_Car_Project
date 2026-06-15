#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// ============================================================
// ESP32-CAM CAR: HIGHER FPS WEB STREAM + LEFT/RIGHT MOTOR SPEED
// Board: AI Thinker ESP32-CAM
//
// Web page:
//   http://10.76.104.1/
//
// Camera stream:
//   http://10.76.104.1:81/stream
//
// Motor speed:
//   Controlled by PWM on existing L298N IN pins.
//   No extra ENA/ENB GPIO required.
// ============================================================

// ---------------- AI THINKER ESP32-CAM PIN MAP ----------------
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5

#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// ---------------- L298N MOTOR PINS ----------------
// Current wiring from your original code.
#define RIGHT_IN1 4
#define RIGHT_IN2 13
#define LEFT_IN3 14
#define LEFT_IN4 15

// ---------------- PAN/TILT SERVO PINS ----------------
// GPIO3 is UART RX. Disconnect tilt servo signal while uploading code.
#define PAN_SERVO_PIN 2
#define TILT_SERVO_PIN 3

#define SERVO_MIN_US 500
#define SERVO_MAX_US 2400

// ---------------- CAR DIRECTION ----------------
#define STOP 0
#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4

// ---------------- CAMERA FPS SETTINGS ----------------
// QVGA 320x240 is a good balance.
// For maximum FPS, change CAMERA_FRAME_SIZE to FRAMESIZE_QQVGA and quality to 24.
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#define CAMERA_JPEG_QUALITY 20 // Higher number = lower quality = faster
#define STREAM_DELAY_MS 10     // Lower = higher stream rate, more Wi-Fi load

// ---------------- MOTOR PWM SETTINGS ----------------
#define MOTOR_PWM_FREQ 1200
#define MOTOR_PWM_RESOLUTION 8
#define MOTOR_MAX_DUTY 255

// Use LEDC channels away from camera channel 0.
#define RIGHT_IN1_CH 4
#define RIGHT_IN2_CH 5
#define LEFT_IN3_CH 6
#define LEFT_IN4_CH 7

// Default speed. If your left motor is faster, lower leftMotorSpeed from page.
int leftMotorSpeed = 200;
int rightMotorSpeed = 200;

// Minimum active speed helps weak motors start moving.
// Set to 0 if your car moves smoothly at very low speed.
int minMotorDuty = 80;

const char *ssid = "MyWiFiCar";
const char *password = "12345678";

WebServer server(80);
WiFiServer streamServer(81);

Servo panServo;
Servo tiltServo;

int panAngle = 90;
int tiltAngle = 90;

int currentDir = STOP;
bool carPowerOn = true;

// ---------------- CAMERA / DETECTION DATA ----------------
bool cameraReady = false;
String cameraMode = "UNKNOWN";

bool motionDetected = false;
int motionScore = 0;
unsigned long lastDetectionMs = 0;

// ---------------- HTML PAGE ----------------
const char *htmlHomePage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">

<style>
* {
  box-sizing: border-box;
  touch-action: manipulation;
}

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
  font-size: 22px;
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
  font-size: 17px;
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

.motor-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
  max-width: 1000px;
  margin: 10px auto;
}

.motor-card {
  padding: 12px;
  background: #fff;
  border: 1px solid #777;
  border-radius: 14px;
  font-weight: bold;
}

.motor-card input {
  width: 90%;
}

.trim-row {
  display: flex;
  gap: 8px;
  justify-content: center;
  flex-wrap: wrap;
  margin-top: 8px;
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

.small-btn {
  width: auto;
  height: 44px;
  min-width: 82px;
  padding: 0 12px;
  font-size: 14px;
  color: cyan;
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

.stop-btn {
  color: #ff3333;
}

.small-note {
  font-size: 13px;
  color: #555;
  margin-top: 8px;
}

@media (max-width: 700px) {
  .info-row,
  .motor-grid {
    grid-template-columns: 1fr;
  }

  .slider-row input {
    width: 90%;
  }
}
</style>
</head>

<body>
<h2>ESP32-CAM Car — MJPEG FPS + Motor Speed Balance</h2>

<div class="video-box" id="videoBox">
  <div class="camera-wrap">
    <img id="cam" crossorigin="anonymous" src="" alt="Camera Stream">
    <canvas id="overlayCanvas"></canvas>
  </div>
</div>

<div class="detect-box" id="detectBox">
  Detection: starting...
</div>

<div class="info-row">
  <div class="status" id="status">Ready</div>
  <div class="fps-box" id="fpsBox">Stream: MJPEG mode</div>
</div>

<div class="small-note" id="modeText">
  Loading ESP32-CAM status...
</div>

<div class="motor-grid">
  <div class="motor-card">
    Left Motor Speed:
    <span id="leftSpeedVal">200</span> / 255
    <br>
    <input type="range" min="0" max="255" value="200" id="leftSpeedSlider"
      oninput="setMotorSpeed('LeftSpeed', this.value)">
    <div class="trim-row">
      <button class="btn small-btn" onclick="trimMotor('left', -5)">Left -5</button>
      <button class="btn small-btn" onclick="trimMotor('left', 5)">Left +5</button>
    </div>
  </div>

  <div class="motor-card">
    Right Motor Speed:
    <span id="rightSpeedVal">200</span> / 255
    <br>
    <input type="range" min="0" max="255" value="200" id="rightSpeedSlider"
      oninput="setMotorSpeed('RightSpeed', this.value)">
    <div class="trim-row">
      <button class="btn small-btn" onclick="trimMotor('right', -5)">Right -5</button>
      <button class="btn small-btn" onclick="trimMotor('right', 5)">Right +5</button>
    </div>
  </div>
</div>

<div class="control-box">
  <button class="btn power-btn" id="powerBtn" onclick="togglePower()">POWER ON</button>
  <button class="btn small-btn" onclick="toggleDetection()">Detection: <span id="detectToggleText">ON</span></button>
  <button class="btn small-btn" onclick="reloadStream()">Reload Cam</button>

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

  <button class="btn stop-btn" onclick="stopDrive()">STOP</button>

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

<div class="small-note">
  Tip: If the car curves right while moving forward, the left motor is probably faster. Lower Left Motor Speed or raise Right Motor Speed.
</div>

<canvas id="detectCanvas" width="64" height="48" style="display:none;"></canvas>

<script>
var powerOn = true;
window.currentMoving = false;

var lastServoSendTime = 0;
var servoSendDelay = 120;

var lastMotorSpeedSendTime = 0;
var motorSpeedSendDelay = 90;

var detectionEnabled = true;
var detectionInterval = 280;

// Detection settings: smaller canvas = faster browser performance.
var smallW = 64;
var smallH = 48;

var pixelDiffThreshold = 34;
var changedPixelTrigger = 180;
var minBoxArea = 40;
var maxChangedRatio = 0.55;

var previousGray = null;
var lastSentDetection = -1;
var lastSendTime = 0;

function statusText(msg) {
  document.getElementById("status").innerText = msg;
}

function reloadStream() {
  var img = document.getElementById("cam");
  img.src = "http://" + location.hostname + ":81/stream?t=" + Date.now();
  statusText("Reloading MJPEG stream...");
}

function sendCmd(k, v) {
  fetch("/cmd?k=" + encodeURIComponent(k) + "&v=" + encodeURIComponent(v) + "&t=" + Date.now(), {
    cache: "no-store"
  })
    .then(function(response) {
      if (response.ok) {
        statusText("OK: " + k + " = " + v);
      } else {
        statusText("Command failed: " + k);
      }
    })
    .catch(function() {
      statusText("Send failed: " + k);
    });
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

function setMotorSpeed(k, v) {
  v = Math.max(0, Math.min(255, Number(v)));

  if (k == "LeftSpeed") {
    document.getElementById("leftSpeedVal").innerText = v;
    document.getElementById("leftSpeedSlider").value = v;
  }

  if (k == "RightSpeed") {
    document.getElementById("rightSpeedVal").innerText = v;
    document.getElementById("rightSpeedSlider").value = v;
  }

  var now = Date.now();

  if (now - lastMotorSpeedSendTime < motorSpeedSendDelay) {
    return;
  }

  lastMotorSpeedSendTime = now;
  sendCmd(k, v);
}

function trimMotor(side, delta) {
  if (side == "left") {
    var s = document.getElementById("leftSpeedSlider");
    var v = Math.max(0, Math.min(255, Number(s.value) + delta));
    setMotorSpeed("LeftSpeed", v);
  } else {
    var s2 = document.getElementById("rightSpeedSlider");
    var v2 = Math.max(0, Math.min(255, Number(s2.value) + delta));
    setMotorSpeed("RightSpeed", v2);
  }
}

function sliderServo(k, v) {
  v = Math.max(0, Math.min(180, Number(v)));

  if (k == "Pan") {
    document.getElementById("panVal").innerText = v;
    document.getElementById("panSlider").value = v;
  }

  if (k == "Tilt") {
    document.getElementById("tiltVal").innerText = v;
    document.getElementById("tiltSlider").value = v;
  }

  var now = Date.now();

  if (now - lastServoSendTime < servoSendDelay) {
    return;
  }

  lastServoSendTime = now;
  sendCmd(k, v);
}

function centerPanTilt() {
  sliderServo("Pan", 90);
  sliderServo("Tilt", 90);
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

function toggleDetection() {
  detectionEnabled = !detectionEnabled;
  document.getElementById("detectToggleText").innerText = detectionEnabled ? "ON" : "OFF";
  previousGray = null;
  clearRoundedBox();
  updateDetectionUI(false, 0);
}

function updateStatusFromESP32() {
  fetch("/data?t=" + Date.now(), { cache: "no-store" })
    .then(function(response) { return response.json(); })
    .then(function(data) {
      powerOn = data.power;
      updatePowerButton();

      document.getElementById("panSlider").value = data.pan;
      document.getElementById("tiltSlider").value = data.tilt;
      document.getElementById("panVal").innerText = data.pan;
      document.getElementById("tiltVal").innerText = data.tilt;

      document.getElementById("leftSpeedSlider").value = data.leftSpeed;
      document.getElementById("rightSpeedSlider").value = data.rightSpeed;
      document.getElementById("leftSpeedVal").innerText = data.leftSpeed;
      document.getElementById("rightSpeedVal").innerText = data.rightSpeed;

      document.getElementById("modeText").innerText =
        "Camera: " + data.cameraMode +
        " | Ready: " + data.cameraReady +
        " | PSRAM: " + data.psram +
        " | Power: " + data.power +
        " | Direction: " + data.direction +
        " | Left speed: " + data.leftSpeed +
        " | Right speed: " + data.rightSpeed +
        " | Motion: " + data.motion +
        " | Score: " + data.score;
    })
    .catch(function(e) {
      console.log(e);
    });
}

function sendDetectionToESP32(detected, score) {
  var v = detected ? 1 : 0;
  var now = Date.now();

  if (v === lastSentDetection && now - lastSendTime < 1000) {
    return;
  }

  lastSentDetection = v;
  lastSendTime = now;

  fetch("/detect?v=" + v + "&score=" + score + "&t=" + Date.now(), {
    cache: "no-store"
  }).catch(function() {});
}

function updateDetectionUI(detected, score) {
  var box = document.getElementById("detectBox");
  var videoBox = document.getElementById("videoBox");

  if (!detectionEnabled) {
    box.classList.remove("alarm");
    videoBox.classList.remove("alarm");
    box.innerText = "Detection: OFF";
    return;
  }

  if (detected) {
    box.classList.add("alarm");
    videoBox.classList.add("alarm");
    box.innerText = "Detection: MOTION CHANGE DETECTED | score = " + score;
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

  var radius = 20;

  ctx.lineWidth = 5;
  ctx.strokeStyle = "red";
  ctx.fillStyle = "rgba(255, 0, 0, 0.16)";

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

  ctx.font = "bold 18px Arial";
  ctx.fillStyle = "red";
  ctx.fillText("Motion: " + score, bx + 10, Math.max(by - 10, 22));
}

function clearRoundedBox() {
  var img = document.getElementById("cam");
  var overlay = document.getElementById("overlayCanvas");
  var ctx = overlay.getContext("2d");

  overlay.width = img.clientWidth || 1;
  overlay.height = img.clientHeight || 1;

  ctx.clearRect(0, 0, overlay.width, overlay.height);
}

function analyzeCameraFrame() {
  if (!detectionEnabled) {
    clearRoundedBox();
    return;
  }

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

  if (!img.naturalWidth || !img.naturalHeight) {
    return;
  }

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
      var padding = 6;

      minX = Math.max(0, minX - padding);
      minY = Math.max(0, minY - padding);
      maxX = Math.min(smallW - 1, maxX + padding);
      maxY = Math.min(smallH - 1, maxY + padding);

      drawRoundedBox(minX, minY, maxX - minX, maxY - minY, changedPixels);
    } else {
      clearRoundedBox();
    }
  } catch (e) {
    console.log("Detection error:", e);
    updateDetectionUI(false, 0);
  }
}

// Keyboard control.
document.addEventListener("keydown", function(e) {
  if (e.repeat) return;

  var k = e.key.toLowerCase();

  if (k === "w") drive(1);
  else if (k === "s") drive(2);
  else if (k === "a") drive(3);
  else if (k === "d") drive(4);
  else if (k === " " || k === "x") stopDrive();
  else if (k === "j") sliderServo("Pan", Number(document.getElementById("panSlider").value) - 5);
  else if (k === "l") sliderServo("Pan", Number(document.getElementById("panSlider").value) + 5);
  else if (k === "i") sliderServo("Tilt", Number(document.getElementById("tiltSlider").value) + 5);
  else if (k === "k") sliderServo("Tilt", Number(document.getElementById("tiltSlider").value) - 5);
  else if (k === "c") centerPanTilt();
});

document.addEventListener("keyup", function(e) {
  var k = e.key.toLowerCase();

  if (k === "w" || k === "a" || k === "s" || k === "d") {
    stopDrive();
  }
});

document.getElementById("cam").onload = function() {
  statusText("MJPEG stream loaded");
  document.getElementById("fpsBox").innerText = "Stream: MJPEG on port 81";
};

document.getElementById("cam").onerror = function() {
  statusText("Camera stream error. Press Reload Cam.");
  document.getElementById("fpsBox").innerText = "Stream error";
};

updatePowerButton();
reloadStream();
updateStatusFromESP32();
setInterval(updateStatusFromESP32, 3000);
setInterval(analyzeCameraFrame, detectionInterval);
</script>
</body>
</html>
)rawliteral";

// ---------------- UTILITY ----------------
int clampInt(int value, int low, int high)
{
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

int speedToDuty(int speedValue)
{
  speedValue = clampInt(speedValue, 0, MOTOR_MAX_DUTY);

  if (speedValue == 0)
  {
    return 0;
  }

  if (speedValue < minMotorDuty)
  {
    return minMotorDuty;
  }

  return speedValue;
}

// ---------------- PWM COMPATIBILITY ----------------
void motorPwmAttach(int pin, int channel)
{
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(pin, MOTOR_PWM_FREQ, MOTOR_PWM_RESOLUTION, channel);
#else
  ledcSetup(channel, MOTOR_PWM_FREQ, MOTOR_PWM_RESOLUTION);
  ledcAttachPin(pin, channel);
#endif
}

void motorPwmWrite(int pin, int channel, int duty)
{
  duty = clampInt(duty, 0, MOTOR_MAX_DUTY);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
#else
  ledcWrite(channel, duty);
#endif
}

void setupMotorPwm()
{
  motorPwmAttach(RIGHT_IN1, RIGHT_IN1_CH);
  motorPwmAttach(RIGHT_IN2, RIGHT_IN2_CH);
  motorPwmAttach(LEFT_IN3, LEFT_IN3_CH);
  motorPwmAttach(LEFT_IN4, LEFT_IN4_CH);

  motorPwmWrite(RIGHT_IN1, RIGHT_IN1_CH, 0);
  motorPwmWrite(RIGHT_IN2, RIGHT_IN2_CH, 0);
  motorPwmWrite(LEFT_IN3, LEFT_IN3_CH, 0);
  motorPwmWrite(LEFT_IN4, LEFT_IN4_CH, 0);
}

// ---------------- SERVO LOGIC ----------------
int clampAngle(int angle)
{
  return clampInt(angle, 0, 180);
}

void setPan(int angle)
{
  panAngle = clampAngle(angle);
  panServo.write(panAngle);
}

void setTilt(int angle)
{
  tiltAngle = clampAngle(angle);
  tiltServo.write(tiltAngle);
}

void setupServos()
{
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
void stopMotors()
{
  motorPwmWrite(RIGHT_IN1, RIGHT_IN1_CH, 0);
  motorPwmWrite(RIGHT_IN2, RIGHT_IN2_CH, 0);
  motorPwmWrite(LEFT_IN3, LEFT_IN3_CH, 0);
  motorPwmWrite(LEFT_IN4, LEFT_IN4_CH, 0);
}

void setRightForward(int duty)
{
  motorPwmWrite(RIGHT_IN1, RIGHT_IN1_CH, duty);
  motorPwmWrite(RIGHT_IN2, RIGHT_IN2_CH, 0);
}

void setRightBackward(int duty)
{
  motorPwmWrite(RIGHT_IN1, RIGHT_IN1_CH, 0);
  motorPwmWrite(RIGHT_IN2, RIGHT_IN2_CH, duty);
}

void setLeftForward(int duty)
{
  motorPwmWrite(LEFT_IN3, LEFT_IN3_CH, duty);
  motorPwmWrite(LEFT_IN4, LEFT_IN4_CH, 0);
}

void setLeftBackward(int duty)
{
  motorPwmWrite(LEFT_IN3, LEFT_IN3_CH, 0);
  motorPwmWrite(LEFT_IN4, LEFT_IN4_CH, duty);
}

void moveCar(int v)
{
  if (!carPowerOn)
  {
    v = STOP;
  }

  if (v < STOP || v > RIGHT)
  {
    v = STOP;
  }

  currentDir = v;

  int leftDuty = speedToDuty(leftMotorSpeed);
  int rightDuty = speedToDuty(rightMotorSpeed);

  if (v == UP)
  {
    setRightForward(rightDuty);
    setLeftForward(leftDuty);
  }
  else if (v == DOWN)
  {
    setRightBackward(rightDuty);
    setLeftBackward(leftDuty);
  }
  else if (v == LEFT)
  {
    setRightForward(rightDuty);
    setLeftBackward(leftDuty);
  }
  else if (v == RIGHT)
  {
    setRightBackward(rightDuty);
    setLeftForward(leftDuty);
  }
  else
  {
    currentDir = STOP;
    stopMotors();
  }
}

void updateLeftSpeed(int value)
{
  leftMotorSpeed = clampInt(value, 0, MOTOR_MAX_DUTY);

  if (currentDir != STOP)
  {
    moveCar(currentDir);
  }
}

void updateRightSpeed(int value)
{
  rightMotorSpeed = clampInt(value, 0, MOTOR_MAX_DUTY);

  if (currentDir != STOP)
  {
    moveCar(currentDir);
  }
}

// ---------------- CAMERA SETUP ----------------
void setupCamera()
{
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

  config.frame_size = CAMERA_FRAME_SIZE;
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.fb_count = hasPsram ? 2 : 1;

#if defined(CAMERA_GRAB_LATEST)
  config.grab_mode = CAMERA_GRAB_LATEST;
#endif

#if defined(CAMERA_FB_IN_PSRAM)
  if (hasPsram)
  {
    config.fb_location = CAMERA_FB_IN_PSRAM;
  }
#endif

  if (CAMERA_FRAME_SIZE == FRAMESIZE_QQVGA)
  {
    cameraMode = "QQVGA 160x120 MJPEG, quality ";
  }
  else if (CAMERA_FRAME_SIZE == FRAMESIZE_QVGA)
  {
    cameraMode = "QVGA 320x240 MJPEG, quality ";
  }
  else
  {
    cameraMode = "MJPEG, quality ";
  }

  cameraMode += String(CAMERA_JPEG_QUALITY);
  cameraMode += hasPsram ? ", PSRAM" : ", no PSRAM";

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    cameraReady = false;
    cameraMode = "Camera init failed";
    return;
  }

  sensor_t *sensor = esp_camera_sensor_get();

  if (sensor != NULL)
  {
    sensor->set_framesize(sensor, CAMERA_FRAME_SIZE);
    sensor->set_quality(sensor, CAMERA_JPEG_QUALITY);

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
void handleJpg()
{
  if (!cameraReady)
  {
    server.send(503, "text/plain", "Camera not ready");
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb)
  {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");

  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);

  esp_camera_fb_return(fb);
}

// ---------------- MJPEG STREAM HANDLER ON PORT 81 ----------------
void streamTask(void *param)
{
  streamServer.begin();

  while (true)
  {
    WiFiClient client = streamServer.available();

    if (!client)
    {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    client.setNoDelay(true);

    // Read request headers.
    unsigned long headerStart = millis();
    String currentLine = "";

    while (client.connected() && millis() - headerStart < 1200)
    {
      if (!client.available())
      {
        vTaskDelay(1 / portTICK_PERIOD_MS);
        continue;
      }

      char c = client.read();

      if (c == '\n')
      {
        if (currentLine.length() <= 1)
        {
          break;
        }
        currentLine = "";
      }
      else if (c != '\r')
      {
        currentLine += c;
      }
    }

    client.print("HTTP/1.1 200 OK\r\n");
    client.print("Content-Type: multipart/x-mixed-replace; boundary=frame\r\n");
    client.print("Access-Control-Allow-Origin: *\r\n");
    client.print("Cache-Control: no-cache, no-store, must-revalidate\r\n");
    client.print("Pragma: no-cache\r\n");
    client.print("Expires: 0\r\n");
    client.print("Connection: close\r\n\r\n");

    while (client.connected())
    {
      if (!cameraReady)
      {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        continue;
      }

      camera_fb_t *fb = esp_camera_fb_get();

      if (!fb)
      {
        vTaskDelay(30 / portTICK_PERIOD_MS);
        continue;
      }

      client.print("--frame\r\n");
      client.print("Content-Type: image/jpeg\r\n");
      client.print("Content-Length: ");
      client.print(fb->len);
      client.print("\r\n\r\n");

      size_t written = client.write(fb->buf, fb->len);
      client.print("\r\n");

      esp_camera_fb_return(fb);

      if (written == 0)
      {
        break;
      }

      vTaskDelay(STREAM_DELAY_MS / portTICK_PERIOD_MS);
    }

    client.stop();
  }
}

// ---------------- COMMAND HANDLER ----------------
void handleCommand()
{
  if (!server.hasArg("k") || !server.hasArg("v"))
  {
    server.send(400, "text/plain", "Missing k or v");
    return;
  }

  String key = server.arg("k");
  int value = server.arg("v").toInt();

  if (key == "MoveCar")
  {
    moveCar(value);
  }
  else if (key == "Power")
  {
    carPowerOn = value == 1;

    if (!carPowerOn)
    {
      moveCar(STOP);
    }
  }
  else if (key == "Pan")
  {
    setPan(value);
  }
  else if (key == "Tilt")
  {
    setTilt(value);
  }
  else if (key == "Center")
  {
    setPan(90);
    setTilt(90);
  }
  else if (key == "LeftSpeed")
  {
    updateLeftSpeed(value);
  }
  else if (key == "RightSpeed")
  {
    updateRightSpeed(value);
  }
  else if (key == "MinDuty")
  {
    minMotorDuty = clampInt(value, 0, MOTOR_MAX_DUTY);
  }
  else
  {
    server.send(400, "text/plain", "Unknown command");
    return;
  }

  server.send(200, "text/plain", "OK");
}

// ---------------- DETECTION HANDLER ----------------
void handleDetection()
{
  if (!server.hasArg("v"))
  {
    server.send(400, "text/plain", "Missing v");
    return;
  }

  int v = server.arg("v").toInt();
  motionDetected = (v == 1);

  if (server.hasArg("score"))
  {
    motionScore = server.arg("score").toInt();
  }

  lastDetectionMs = millis();

  server.send(200, "text/plain", "OK");
}

// ---------------- DATA JSON HANDLER ----------------
void handleData()
{
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
  json += "\"leftSpeed\":";
  json += String(leftMotorSpeed);
  json += ",";
  json += "\"rightSpeed\":";
  json += String(rightMotorSpeed);
  json += ",";
  json += "\"minDuty\":";
  json += String(minMotorDuty);
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
void setupPins()
{
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);
  pinMode(LEFT_IN3, OUTPUT);
  pinMode(LEFT_IN4, OUTPUT);

  setupMotorPwm();
  stopMotors();
}

// ---------------- WIFI SETUP ----------------
void setupWiFi()
{
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Try channel 1, 6, or 11 if your area has Wi-Fi interference.
  bool ok = WiFi.softAP(ssid, password, 6, 0, 1);

  Serial.print("softAP status: ");
  Serial.println(ok ? "OK" : "FAILED");

  Serial.print("WiFi AP: ");
  Serial.println(ssid);

  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
}

// ---------------- SETUP ----------------
void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Starting ESP32-CAM MJPEG motor-balance car...");

  setupPins();
  setupWiFi();

  setupCamera();
  setupServos();

  server.on("/", HTTP_GET, []()
            {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.send(200, "text/html", htmlHomePage); });

  server.on("/jpg", HTTP_GET, handleJpg);
  server.on("/cmd", HTTP_GET, handleCommand);
  server.on("/detect", HTTP_GET, handleDetection);
  server.on("/data", HTTP_GET, handleData);

  server.on("/test", HTTP_GET, []()
            {
    String msg = "ESP32-CAM web server OK\n";
    msg += "Open page: http://";
    msg += WiFi.softAPIP().toString();
    msg += "/\n";
    msg += "MJPEG stream: http://";
    msg += WiFi.softAPIP().toString();
    msg += ":81/stream\n";
    msg += "Camera ready: ";
    msg += cameraReady ? "YES\n" : "NO\n";
    msg += "Camera mode: ";
    msg += cameraMode;
    msg += "\n";
    msg += "PSRAM: ";
    msg += psramFound() ? "YES\n" : "NO\n";
    msg += "Power: ";
    msg += carPowerOn ? "ON\n" : "OFF\n";
    msg += "Left speed: ";
    msg += String(leftMotorSpeed);
    msg += "\n";
    msg += "Right speed: ";
    msg += String(rightMotorSpeed);
    msg += "\n";
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

    server.send(200, "text/plain", msg); });

  server.begin();
  Serial.println("HTTP control server started on port 80.");

  xTaskCreatePinnedToCore(
      streamTask,
      "streamTask",
      8192,
      NULL,
      1,
      NULL,
      0);

  Serial.println("MJPEG stream server started on port 81.");
}

// ---------------- LOOP ----------------
void loop()
{
  server.handleClient();
  delay(1);
}