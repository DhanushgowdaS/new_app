
#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TJpg_Decoder.h>

#include <ESP_Mail_Client.h>

#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// =====================================================
// 1. WIFI CONFIGURATION
// =====================================================

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// =====================================================
// 2. GMAIL CONFIGURATION
// =====================================================

#define SENDER_EMAIL "SENDER_GMAIL.COM"
#define SENDER_PASSWORD "YOUR_GOOGLE_APP_PASSWORD"
#define RECIPIENT_EMAIL "RECIPIENT_GMAIL.COM"

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

// =====================================================
// 3. OLED CONFIGURATION
// =====================================================

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#define OLED_SDA 13
#define OLED_SCL 14
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  OLED_WIDTH,
  OLED_HEIGHT,
  &Wire,
  -1
);

bool oledReady = false;

// =====================================================
// 4. AI THINKER ESP32-CAM PIN CONFIGURATION
// =====================================================

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

// =====================================================
// 5. CAMERA SETTINGS
// =====================================================

#define CAMERA_QUALITY 12

// VGA = 640 x 480
// QVGA = 320 x 240

#define CAMERA_RESOLUTION FRAMESIZE_VGA

// =====================================================
// 6. HTTP SERVER
// =====================================================

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

const char* STREAM_CONTENT_TYPE =
  "multipart/x-mixed-replace;boundary=frame";

const char* STREAM_BOUNDARY =
  "\r\n--frame\r\n";

const char* STREAM_PART =
  "Content-Type: image/jpeg\r\n"
  "Content-Length: %u\r\n\r\n";

// =====================================================
// 7. SYSTEM VARIABLES
// =====================================================

SemaphoreHandle_t cameraMutex;

volatile bool emailSending = false;

// Retry Wi-Fi connection periodically if the access point disconnects.
unsigned long lastWiFiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000;

portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;

char emailStatus[32] = "READY";

struct EmailJob {
  uint8_t* data;
  size_t size;
};

// =====================================================
// 8. UPDATE EMAIL STATUS
// =====================================================

void setEmailStatus(const char* status) {

  portENTER_CRITICAL(&statusMux);

  strncpy(
    emailStatus,
    status,
    sizeof(emailStatus) - 1
  );

  emailStatus[sizeof(emailStatus) - 1] = '\0';

  portEXIT_CRITICAL(&statusMux);
}

// =====================================================
// 9. OLED STATUS DISPLAY
// =====================================================

void showOLED(
  String line1,
  String line2,
  String line3,
  String line4
) {

  if (!oledReady) return;

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 2);
  display.println("ARDUINOTECH ROBOT");

  display.drawLine(0, 13, 127, 13, SSD1306_WHITE);

  display.setCursor(0, 19);
  display.println(line1);

  display.setCursor(0, 30);
  display.println(line2);

  display.setCursor(0, 41);
  display.println(line3);

  display.setCursor(0, 52);
  display.println(line4);

  display.display();
}

// =====================================================
// 10. OLED IMAGE CALLBACK
// Full-width crop with grayscale dithering
// =====================================================

bool oledJpegCallback(
  int16_t x,
  int16_t y,
  uint16_t w,
  uint16_t h,
  uint16_t* bitmap
) {

  if (!oledReady) return false;

  // VGA image at JPEG scale 4 = 160 x 120.
  // Centre crop to 128 x 64.

  const int OFFSET_X = 16;
  const int OFFSET_Y = 28;

  // 4x4 ordered dithering matrix
  const uint8_t bayer[4][4] = {
    {  0, 128,  32, 160 },
    {192,  64, 224,  96 },
    { 48, 176,  16, 144 },
    {240, 112, 208,  80 }
  };

  for (uint16_t j = 0; j < h; j++) {

    for (uint16_t i = 0; i < w; i++) {

      int px = x + i - OFFSET_X;
      int py = y + j - OFFSET_Y;

      if (
        px < 0 || px >= OLED_WIDTH ||
        py < 0 || py >= OLED_HEIGHT
      ) {
        continue;
      }

      uint16_t pixel = bitmap[j * w + i];

      uint8_t r = ((pixel >> 11) & 0x1F) * 255 / 31;
      uint8_t g = ((pixel >> 5) & 0x3F) * 255 / 63;
      uint8_t b = (pixel & 0x1F) * 255 / 31;

      uint8_t gray =
        (r * 30 + g * 59 + b * 11) / 100;

      uint8_t threshold = bayer[py % 4][px % 4];

      display.drawPixel(
        px,
        py,
        gray > threshold ?
        SSD1306_WHITE :
        SSD1306_BLACK
      );
    }
  }

  return true;
}

// =====================================================
// 11. CAMERA INITIALIZATION
// =====================================================

bool initCamera() {

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

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;

  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {

    config.frame_size = CAMERA_RESOLUTION;
    config.jpeg_quality = CAMERA_QUALITY;

    config.fb_count = 2;

    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    Serial.println("PSRAM DETECTED");

  } else {

    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;

    config.fb_count = 1;

    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    Serial.println("PSRAM NOT FOUND - USING QVGA");
  }

  esp_err_t error = esp_camera_init(&config);

  if (error != ESP_OK) {

    Serial.printf(
      "Camera initialization failed: 0x%x\n",
      error
    );

    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();

  if (sensor != NULL) {

    // 180-degree rotation
    sensor->set_vflip(sensor, 1);
    sensor->set_hmirror(sensor, 1);

    // Image adjustment
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 0);
    sensor->set_saturation(sensor, 0);

    sensor->set_whitebal(sensor, 1);
    sensor->set_awb_gain(sensor, 1);
  }

  Serial.println("CAMERA INITIALIZED");

  return true;
}

// =====================================================
// 12. SEND PHOTO THROUGH GMAIL
// =====================================================

bool sendPhotoEmail(
  uint8_t* photoData,
  size_t photoSize
) {

  if (photoData == NULL || photoSize == 0) {
    Serial.println("Invalid photo data");
    return false;
  }

  SMTPSession smtp;

  smtp.debug(1);

  Session_Config session;

  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;

  session.login.email = SENDER_EMAIL;
  session.login.password = SENDER_PASSWORD;

  // Gmail port 465: implicit TLS
  session.secure.startTLS = false;

  // Prototype testing only.
  // Configure trusted certificate verification
  // before production deployment.
  session.certificate.verify = false;

  SMTP_Message message;

  message.sender.name = "ArduinoTech Robot";
  message.sender.email = SENDER_EMAIL;

  message.subject =
    "ArduinoTech Robot - Captured Photo";

  message.addRecipient(
    "Robot Owner",
    RECIPIENT_EMAIL
  );

  message.text.content =
    "A photograph was captured using ArduinoTech Robot.";

  message.text.charSet = "utf-8";

  SMTP_Attachment attachment;

  attachment.descr.filename =
    "ArduinoTech_Capture.jpg";

  attachment.descr.mime = "image/jpeg";

  attachment.descr.transfer_encoding =
    Content_Transfer_Encoding::enc_base64;

  attachment.blob.data = photoData;
  attachment.blob.size = photoSize;

  message.addAttachment(attachment);

  Serial.println("Connecting to Gmail...");

  if (!smtp.connect(&session)) {

    Serial.println("SMTP CONNECTION FAILED");
    Serial.println(smtp.errorReason());

    smtp.closeSession();

    return false;
  }

  Serial.println("SMTP CONNECTED");
  Serial.println("Sending photo...");

  bool success = MailClient.sendMail(
    &smtp,
    &message,
    true
  );

  if (success) {

    Serial.println("PHOTO SENT SUCCESSFULLY");

  } else {

    Serial.println("EMAIL FAILED");
    Serial.println(smtp.errorReason());
  }

  smtp.closeSession();

  return success;
}

// =====================================================
// 13. BACKGROUND EMAIL TASK
// =====================================================

void emailTask(void* parameter) {

  EmailJob* job = (EmailJob*)parameter;

  Serial.println("EMAIL TASK STARTED");

  bool success = sendPhotoEmail(
    job->data,
    job->size
  );

  free(job->data);
  delete job;

  if (success) {

    setEmailStatus("PHOTO SENT");

    Serial.println("GMAIL: PHOTO SENT");

  } else {

    setEmailStatus("EMAIL FAILED");

    Serial.println("GMAIL: FAILED");
  }

  emailSending = false;

  vTaskDelete(NULL);
}

// =====================================================
// 14. PHOTO CAPTURE HANDLER
// =====================================================

static esp_err_t capture_handler(httpd_req_t* req) {

  if (emailSending) {

    httpd_resp_set_status(req, "409 Conflict");

    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr(
      req,
      "{\"success\":false,\"message\":\"Email already processing\"}"
    );

    return ESP_OK;
  }

  emailSending = true;

  if (
    cameraMutex == NULL ||
    xSemaphoreTake(
      cameraMutex,
      pdMS_TO_TICKS(5000)
    ) != pdTRUE
  ) {

    emailSending = false;

    httpd_resp_set_status(req, "503 Service Unavailable");

    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr(
      req,
      "{\"success\":false,\"message\":\"Camera busy\"}"
    );

    return ESP_OK;
  }

  camera_fb_t* fb = esp_camera_fb_get();

  if (fb == NULL) {

    xSemaphoreGive(cameraMutex);

    emailSending = false;

    httpd_resp_send_500(req);

    return ESP_OK;
  }

  size_t photoSize = fb->len;

  uint8_t* photoCopy =
    (uint8_t*)ps_malloc(photoSize);

  if (photoCopy == NULL) {
    photoCopy = (uint8_t*)malloc(photoSize);
  }

  if (photoCopy != NULL) {

    memcpy(
      photoCopy,
      fb->buf,
      photoSize
    );
  }

  esp_camera_fb_return(fb);

  xSemaphoreGive(cameraMutex);

  if (photoCopy == NULL) {

    emailSending = false;

    httpd_resp_set_status(req, "500 Internal Server Error");

    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr(
      req,
      "{\"success\":false,\"message\":\"Memory allocation failed\"}"
    );

    return ESP_OK;
  }

  Serial.printf(
    "PHOTO CAPTURED: %u bytes\n",
    (unsigned int)photoSize
  );

  EmailJob* job = new EmailJob;

  if (job == NULL) {

    free(photoCopy);

    emailSending = false;

    httpd_resp_send_500(req);

    return ESP_OK;
  }

  job->data = photoCopy;
  job->size = photoSize;

  setEmailStatus("SENDING");

  BaseType_t result = xTaskCreate(
    emailTask,
    "EmailTask",
    12288,
    job,
    1,
    NULL
  );

  if (result != pdPASS) {

    free(photoCopy);
    delete job;

    emailSending = false;

    setEmailStatus("TASK FAILED");

    httpd_resp_send_500(req);

    return ESP_OK;
  }

  httpd_resp_set_type(req, "application/json");

  httpd_resp_sendstr(
    req,
    "{\"success\":true,\"message\":\"Photo captured. Sending to Gmail...\"}"
  );

  return ESP_OK;
}

// =====================================================
// 15. EMAIL STATUS HANDLER
// =====================================================

static esp_err_t status_handler(httpd_req_t* req) {

  char currentStatus[32];

  portENTER_CRITICAL(&statusMux);

  strncpy(
    currentStatus,
    emailStatus,
    sizeof(currentStatus)
  );

  portEXIT_CRITICAL(&statusMux);

  char json[100];

  snprintf(
    json,
    sizeof(json),
    "{\"status\":\"%s\",\"sending\":%s}",
    currentStatus,
    emailSending ? "true" : "false"
  );

  httpd_resp_set_type(req, "application/json");

  httpd_resp_sendstr(req, json);

  return ESP_OK;
}

// =====================================================
// 16. MAIN WEB DASHBOARD
// =====================================================

static esp_err_t index_handler(httpd_req_t* req) {

  const char* html = R"rawliteral(

<!DOCTYPE html>

<html lang="en">

<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width, initial-scale=1">

<title>ArduinoTech Robot</title>

<style>

* {
  box-sizing: border-box;
}

body {

  margin: 0;

  padding: 20px;

  background: #111827;

  color: white;

  font-family: Arial, sans-serif;

  text-align: center;
}

h1 {

  color: #38bdf8;

  margin-top: 35px;

  font-size: 27px;
}

h3 {

  font-weight: normal;

  margin-top: 30px;
}

.camera {

  width: 100%;

  max-width: 700px;

  border: 4px solid #38bdf8;

  border-radius: 18px;

  background: black;

  margin-top: 10px;
}

.status {

  color: #22c55e;

  margin-top: 25px;

  font-size: 18px;
}

.capture {

  width: 90%;

  max-width: 400px;

  padding: 17px;

  margin-top: 30px;

  background: #0284c7;

  border: none;

  border-radius: 12px;

  color: white;

  font-size: 18px;

  font-weight: bold;

  cursor: pointer;
}

.capture:disabled {

  background: #64748b;

  cursor: wait;
}

.result {

  margin-top: 20px;

  font-size: 15px;

  color: #facc15;

  min-height: 25px;
}

.info {

  color: #94a3b8;

  font-size: 13px;

  margin-top: 30px;
}

</style>

</head>

<body>

<h1>ARDUINOTECH ROBOT</h1>

<h3>ESP32-CAM LIVE VIDEO</h3>

<img
  id="camera"
  class="camera"
  alt="Camera stream">

<div class="status" id="cameraStatus">

CAMERA STREAM ACTIVE

</div>

<button
  class="capture"
  id="captureButton"
  onclick="capturePhoto()">

  📸 CAPTURE PHOTO

</button>

<div class="result" id="result">

Ready to capture

</div>

<div class="info">

Gmail photo delivery enabled

</div>

<script>

const camera =
  document.getElementById("camera");

const button =
  document.getElementById("captureButton");

const result =
  document.getElementById("result");

const cameraStatus =
  document.getElementById("cameraStatus");

const host = window.location.hostname;

camera.src =
  "http://" + host + ":81/stream";

let polling = false;

camera.onerror = function() {

  cameraStatus.innerText =
    "CAMERA STREAM DISCONNECTED";

  cameraStatus.style.color = "#ef4444";
};

camera.onload = function() {

  cameraStatus.innerText =
    "CAMERA STREAM ACTIVE";

  cameraStatus.style.color = "#22c55e";
};

async function capturePhoto() {

  if (button.disabled) return;

  button.disabled = true;

  button.innerText = "CAPTURING PHOTO...";

  result.innerText =
    "Capturing photograph...";

  try {

    const response = await fetch("/capture", {
      method: "POST"
    });

    const data = await response.json();

    if (!response.ok || !data.success) {

      throw new Error(
        data.message || "Capture failed"
      );
    }

    result.innerText =
      "✓ Photo captured. Sending to Gmail...";

    button.innerText = "PHOTO CAPTURED";

    polling = true;

    checkEmailStatus();

  } catch(error) {

    result.innerText =
      "✗ " + error.message;

    button.disabled = false;

    button.innerText = "📸 CAPTURE PHOTO";
  }
}

async function checkEmailStatus() {

  if (!polling) return;

  try {

    const response = await fetch(
      "/status?t=" + Date.now(),
      { cache: "no-store" }
    );

    const data = await response.json();

    if (data.status === "PHOTO SENT") {

      result.innerText =
        "✓ PHOTO SUCCESSFULLY SENT TO GMAIL!";

      polling = false;

      button.disabled = false;

      button.innerText = "📸 CAPTURE PHOTO";

      return;
    }

    if (
      data.status === "EMAIL FAILED" ||
      data.status === "TASK FAILED"
    ) {

      result.innerText =
        "✗ " + data.status +
        ". Check Serial Monitor.";

      polling = false;

      button.disabled = false;

      button.innerText = "📸 CAPTURE PHOTO";

      return;
    }

    result.innerText =
      "Sending photo to Gmail...";

  } catch(error) {

    result.innerText =
      "Checking email status...";

  }

  setTimeout(checkEmailStatus, 2000);
}

</script>

</body>

</html>

)rawliteral";

  httpd_resp_set_type(req, "text/html");

  httpd_resp_set_hdr(
    req,
    "Cache-Control",
    "no-store"
  );

  return httpd_resp_send(
    req,
    html,
    HTTPD_RESP_USE_STRLEN
  );
}

// =====================================================
// 17. LIVE CAMERA STREAM HANDLER
// =====================================================

static esp_err_t stream_handler(httpd_req_t* req) {

  char part_buf[64];

  esp_err_t res = httpd_resp_set_type(
    req,
    STREAM_CONTENT_TYPE
  );

  if (res != ESP_OK) return res;

  httpd_resp_set_hdr(
    req,
    "Access-Control-Allow-Origin",
    "*"
  );

  httpd_resp_set_hdr(
    req,
    "Cache-Control",
    "no-store"
  );

  while (true) {

    if (
      cameraMutex == NULL ||
      xSemaphoreTake(
        cameraMutex,
        pdMS_TO_TICKS(5000)
      ) != pdTRUE
    ) {

      res = ESP_FAIL;

      break;
    }

    camera_fb_t* fb = esp_camera_fb_get();

    if (fb == NULL) {

      xSemaphoreGive(cameraMutex);

      res = ESP_FAIL;

      break;
    }

    res = httpd_resp_send_chunk(
      req,
      STREAM_BOUNDARY,
      strlen(STREAM_BOUNDARY)
    );

    if (res == ESP_OK) {

      size_t headerLength = snprintf(
        part_buf,
        sizeof(part_buf),
        STREAM_PART,
        fb->len
      );

      res = httpd_resp_send_chunk(
        req,
        part_buf,
        headerLength
      );
    }

    if (res == ESP_OK) {

      res = httpd_resp_send_chunk(
        req,
        (const char*)fb->buf,
        fb->len
      );
    }

    esp_camera_fb_return(fb);

    xSemaphoreGive(cameraMutex);

    if (res != ESP_OK) {
      break;
    }
  }

  return res;
}

// =====================================================
// 18. START WEB SERVERS
// =====================================================

void startCameraServer() {

  // Main dashboard server

  httpd_config_t config =
    HTTPD_DEFAULT_CONFIG();

  config.server_port = 80;
  config.ctrl_port = 32768;

  config.max_uri_handlers = 5;

  config.lru_purge_enable = true;

  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };

  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_POST,
    .handler = capture_handler,
    .user_ctx = NULL
  };

  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
  };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {

    httpd_register_uri_handler(
      camera_httpd,
      &index_uri
    );

    httpd_register_uri_handler(
      camera_httpd,
      &capture_uri
    );

    httpd_register_uri_handler(
      camera_httpd,
      &status_uri
    );

    Serial.println("DASHBOARD SERVER STARTED");
  }

  // Camera stream server

  httpd_config_t streamConfig =
    HTTPD_DEFAULT_CONFIG();

  streamConfig.server_port = 81;
  streamConfig.ctrl_port = 32769;

  streamConfig.max_uri_handlers = 2;

  streamConfig.lru_purge_enable = true;

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  if (
    httpd_start(
      &stream_httpd,
      &streamConfig
    ) == ESP_OK
  ) {

    httpd_register_uri_handler(
      stream_httpd,
      &stream_uri
    );

    Serial.println("CAMERA STREAM SERVER STARTED");
  }
}

// =====================================================
// 19. OLED LIVE CAMERA PREVIEW
// =====================================================

void showLiveOLED() {

  if (!oledReady) return;

  if (cameraMutex == NULL) return;

  if (
    xSemaphoreTake(
      cameraMutex,
      pdMS_TO_TICKS(100)
    ) != pdTRUE
  ) {
    return;
  }

  camera_fb_t* fb = esp_camera_fb_get();

  if (fb != NULL) {

    if (fb->format == PIXFORMAT_JPEG) {

      display.clearDisplay();

      TJpgDec.drawJpg(
        0,
        0,
        fb->buf,
        fb->len
      );

      display.display();
    }

    esp_camera_fb_return(fb);
  }

  xSemaphoreGive(cameraMutex);
}

// =====================================================
// 20. SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println("ARDUINOTECH ROBOT");
  Serial.println("==============================");

  // Create camera mutex

  cameraMutex = xSemaphoreCreateMutex();

  if (cameraMutex == NULL) {

    Serial.println("CAMERA MUTEX FAILED");

    while (true) {
      delay(1000);
    }
  }

  // OLED initialization

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  if (
    display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDRESS
    )
  ) {

    oledReady = true;

    showOLED(
      "System Starting",
      "Please wait...",
      "Camera: WAITING",
      "ROBOT: STARTING"
    );

    Serial.println("OLED READY");

  } else {

    Serial.println("OLED NOT FOUND");
  }

  // Startup screen duration

  delay(3000);

  // Camera initialization

  showOLED(
    "Camera Starting",
    "Initializing...",
    "Please wait",
    "ROBOT: STARTING"
  );

  if (!initCamera()) {

    showOLED(
      "Camera ERROR",
      "Check camera",
      "Initialization",
      "FAILED"
    );

    while (true) {
      delay(1000);
    }
  }

  // JPEG decoder for OLED

  TJpgDec.setJpgScale(4);

  TJpgDec.setCallback(
    oledJpegCallback
  );

  // WiFi initialization

  WiFi.mode(WIFI_STA);

  WiFi.setSleep(false);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  showOLED(
    "Camera: READY",
    "WiFi CONNECTING",
    "Please wait...",
    "ROBOT: STARTING"
  );

  Serial.print("Connecting to WiFi");

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    attempts < 40
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("WIFI CONNECTION FAILED");

    showOLED(
      "Camera: READY",
      "WiFi: FAILED",
      "Check password",
      "ROBOT: OFFLINE"
    );

    return;
  }

  Serial.println("WIFI CONNECTED");

  // Time synchronization for TLS
  // The SMTP certificate verification is
  // disabled in this prototype.

  configTime(
    0,
    0,
    "pool.ntp.org",
    "time.google.com"
  );

  IPAddress ip = WiFi.localIP();

  Serial.print("IP ADDRESS: ");
  Serial.println(ip);

  // Start HTTP servers

  startCameraServer();

  // OLED network status

  showOLED(
    "Camera: READY",
    "WiFi: CONNECTED",
    ip.toString(),
    "ROBOT: READY"
  );

  Serial.println("==============================");

  Serial.println("ARDUINOTECH ROBOT READY");

  Serial.print("DASHBOARD: http://");
  Serial.println(ip);

  Serial.print("STREAM: http://");
  Serial.print(ip);
  Serial.println(":81/stream");

  delay(3000);

  showOLED(
    "LIVE CAMERA",
    "Starting preview",
    "",
    ""
  );
}

// =====================================================
// 21. MAIN LOOP
// =====================================================

void loop() {

  if (WiFi.status() != WL_CONNECTED) {

    // Avoid repeatedly calling reconnect() on every loop iteration.
    if (millis() - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
      lastWiFiReconnectAttempt = millis();
      Serial.println("WiFi disconnected. Attempting to reconnect...");
      WiFi.reconnect();
      showOLED(
        "Camera: READY",
        "WiFi: RECONNECTING",
        "Please wait...",
        "ROBOT: OFFLINE"
      );
    }

    delay(100);
    return;
  }

  showLiveOLED();

  delay(120);
}
