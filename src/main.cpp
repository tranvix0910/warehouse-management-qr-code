#include "esp_camera.h"
#include <WiFi.h>
#include <quirc.h>
#include <WebServer.h>
#include "esp_http_server.h"

// WiFi credentials - Connect to existing network
const char* ssid = "Huhu";
const char* password = "hahahahaa";

// Camera pins for ESP32-S3-EYE
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

// LED pin
#define LED_PIN 2

// Streaming server
WebServer server(80);
httpd_handle_t stream_httpd = NULL;
volatile bool isStreaming = false;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char* INDEX_HTML =
"<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>ESP32-CAM</title></head>"
"<body style='font-family:Arial;text-align:center'>"
"<h2>ESP32-CAM Stream</h2>"
"<img id='stream' style='width:100%;max-width:640px;border:1px solid #ccc'/>"
"<script>document.getElementById('stream').src='http://'+location.hostname+':81/stream';</script>"
"</body></html>";

static esp_err_t stream_handler(httpd_req_t *req) {
  isStreaming = true;
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t jpg_buf_len = 0;
  uint8_t *jpg_buf = NULL;
  char part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) { isStreaming = false; return res; }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 60, &jpg_buf, &jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          res = ESP_FAIL;
        }
      } else {
        jpg_buf = fb->buf;
        jpg_buf_len = fb->len;
      }
    }

    if (res == ESP_OK) {
      int hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, (unsigned int)jpg_buf_len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }

    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      jpg_buf = NULL;
    } else if (jpg_buf) {
      free(jpg_buf);
      jpg_buf = NULL;
    }

    if (res != ESP_OK) {
      break;
    }

    vTaskDelay(1);
  }
  isStreaming = false;
  return res;
}

static void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

// QR code detection variables
struct quirc *q = NULL;
struct quirc_code code;
struct quirc_data data;
quirc_decode_error_t err;

// Statistics for QR detection
int totalQRDetected = 0;
int totalQRDecoded = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32-CAM QR Scanner (Optimized) ===");
  
  // Setup LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Disable brownout detector
  esp_sleep_enable_timer_wakeup(1);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  
  // WiFi setup
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    while (true) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
    return;
  }
  
  Serial.println("\nWiFi connected successfully!");
  Serial.println("IP address: " + WiFi.localIP().toString());
  Serial.println("SSID: " + String(WiFi.SSID()));
  
  // Camera configuration
  camera_config_t config;
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
  config.pixel_format = PIXFORMAT_GRAYSCALE; // keep grayscale for QR; stream converts to JPEG
  config.frame_size = FRAMESIZE_QVGA; // 320x240 to enlarge QR in frame
  config.jpeg_quality = 12;
  config.fb_count = 2;                // double buffering to reduce blocking
  config.grab_mode = CAMERA_GRAB_LATEST; // drop old frames, get latest
  
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  Serial.println("Camera initialized successfully!");
  
  // Tune camera sensor for better QR detection
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);       // -2 to 2
    s->set_contrast(s, 2);         // -2 to 2
    s->set_saturation(s, 0);       // -2 to 2
    s->set_sharpness(s, 2);        // -2 to 2
    s->set_denoise(s, 0);          // 0/1 (off for sharper edges)
    s->set_gain_ctrl(s, 1);        // Auto gain
    s->set_exposure_ctrl(s, 1);    // Auto exposure
    s->set_ae_level(s, 0);         // -2 to 2
    s->set_whitebal(s, 1);         // Auto white balance
    s->set_awb_gain(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_lenc(s, 1);             // lens correction
  }

  // Initialize quirc for QR code detection
  q = quirc_new();
  if (!q) {
    Serial.println("Failed to create quirc object");
    return;
  }
  
  if (quirc_resize(q, 320, 240) < 0) {
    Serial.println("Failed to resize quirc");
    return;
  }
  
  Serial.println("QR Code Scanner Ready!");
  Serial.println("Scanning every 3 seconds...");

  // HTTP server for index and redirect to stream
  server.on("/", [](){ server.send(200, "text/html", INDEX_HTML); });
  server.on("/stream", [](){
    String url = String("http://") + WiFi.localIP().toString() + ":81/stream";
    server.sendHeader("Location", url, true);
    server.send(302, "text/plain", "");
  });
  server.begin();
  startCameraServer();
}

void loop() {
  // If streaming, pause QR scanning to avoid contention
  if (isStreaming) {
    server.handleClient();
    delay(10);
    return;
  }
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }
  
  // Debug camera info every 10 seconds
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 10000) {
    Serial.printf("Camera: %dx%d, format: %d, len: %d\n", 
                 fb->width, fb->height, fb->format, fb->len);
    lastDebug = millis();
  }
  
  // Copy raw grayscale into quirc buffer (fixed size 320x240)
  int qw = 0, qh = 0;
  uint8_t *image = quirc_begin(q, &qw, &qh);
  if (!image) {
    Serial.println("Failed to get image buffer");
    esp_camera_fb_return(fb);
    return;
  }
  size_t copy_len = (size_t)qw * (size_t)qh; // expect 320*240
  memcpy(image, fb->buf, copy_len);
  
  // Process the image
  quirc_end(q);
  
  // Check for QR codes
  int num_codes = quirc_count(q);
  if (num_codes > 0) {
    totalQRDetected++;
    Serial.printf("Found %d QR code(s) - Total detected: %d\n", num_codes, totalQRDetected);
    
    bool qrDecoded = false;
    for (int i = 0; i < num_codes; i++) {
      quirc_extract(q, i, &code);
      
      // Print QR code info for debugging
      Serial.printf("QR Code %d: size=%dx%d, corners=(%d,%d),(%d,%d),(%d,%d),(%d,%d)\n", 
                   i+1, code.size, code.size,
                   code.corners[0].x, code.corners[0].y,
                   code.corners[1].x, code.corners[1].y,
                   code.corners[2].x, code.corners[2].y,
                   code.corners[3].x, code.corners[3].y);
      
      // Check if QR code size is reasonable
      if (code.size < 21) {
        Serial.printf("⚠️  QR code too small (size=%d), may be hard to decode\n", code.size);
      } else if (code.size > 177) {
        Serial.printf("⚠️  QR code too large (size=%d), may be corrupted\n", code.size);
      } else {
        Serial.printf("✅ QR code size looks good (size=%d)\n", code.size);
      }
      
      // Single decode attempt per code (reliability over mutations)
      err = quirc_decode(&code, &data);
      if (err == QUIRC_SUCCESS) {
        totalQRDecoded++;
        Serial.printf("✅ QR Code: %s (Success rate: %d/%d = %.1f%%)\n", 
                     data.payload, totalQRDecoded, totalQRDetected, 
                     (float)totalQRDecoded/totalQRDetected*100);
        qrDecoded = true;
        break;
      } else {
        // minimize spam: only print decode errors occasionally
        static unsigned long lastErr = 0;
        if (millis() - lastErr > 2000) {
          Serial.printf("❌ Decode failed: %s\n", quirc_strerror(err));
          lastErr = millis();
        }
      }
      
      if (qrDecoded) break;
    }

    // Second pass: adaptive threshold for dotted/low-contrast QR
    if (!qrDecoded) {
      int qw2 = 0, qh2 = 0;
      uint8_t *th_img = quirc_begin(q, &qw2, &qh2);
      if (th_img && qw2 > 0 && qh2 > 0) {
        size_t n = (size_t)qw2 * (size_t)qh2;
        // Compute fast approximate mean (stride 4)
        uint32_t sum = 0; size_t cnt = 0;
        for (size_t k = 0; k < n; k += 4) { sum += ((uint8_t*)fb->buf)[k]; cnt++; }
        uint8_t thr = cnt ? (uint8_t)(sum / cnt) : 128;
        // Slightly raise threshold to suppress gray dots
        if (thr < 110) thr = 110; if (thr > 160) thr = 160;
        // Write thresholded image to quirc buffer
        for (size_t k = 0; k < n; k++) {
          uint8_t px = ((uint8_t*)fb->buf)[k];
          th_img[k] = (px > thr) ? 255 : 0;
        }
        quirc_end(q);

        int num2 = quirc_count(q);
        if (num2 > 0) {
          for (int i = 0; i < num2; i++) {
            quirc_extract(q, i, &code);
            if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
              totalQRDecoded++;
              Serial.printf("✅ (TH) QR: %s (Success: %d/%d)\n", (char*)data.payload, totalQRDecoded, totalQRDetected);
              // Blink confirmation
              for (int b = 0; b < 2; b++) { digitalWrite(LED_PIN, HIGH); delay(100); digitalWrite(LED_PIN, LOW); delay(100); }
              qrDecoded = true;
              break;
            }
          }
        }
      } else {
        quirc_end(q);
      }
    }
    
    // Blink LED 2 times when QR is detected
    if (num_codes > 0) {
      for (int blink = 0; blink < 2; blink++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
      }
    }
  } else {
    static unsigned long lastNo = 0;
    if (millis() - lastNo > 2000) {
      Serial.println("No QR code found");
      lastNo = millis();
    }
  }
  
  // Return camera frame buffer
  esp_camera_fb_return(fb);
  
  server.handleClient();
  
  // Check WiFi status
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    digitalWrite(LED_PIN, HIGH); // LED on when disconnected
  } else {
    digitalWrite(LED_PIN, LOW);  // LED off when connected
  }
  
  delay(100); // faster scan interval
}