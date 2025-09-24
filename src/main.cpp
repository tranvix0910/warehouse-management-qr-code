#include "esp_camera.h"
#include <WiFi.h>
#include <quirc.h>

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
  config.fb_count = 2; // double buffer for smoother streaming
  config.grab_mode = CAMERA_GRAB_LATEST; // reduce blocking
  
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
}

void loop() {
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
  
  // Ensure quirc buffer matches frame size and copy raw grayscale
  int qw = 0, qh = 0;
  if (fb->width != 0 && fb->height != 0) {
    if (quirc_resize(q, fb->width, fb->height) < 0) {
      Serial.println("Failed to resize quirc to frame size");
      esp_camera_fb_return(fb);
      return;
    }
  }
  uint8_t *image = quirc_begin(q, &qw, &qh);
  if (!image) {
    Serial.println("Failed to get image buffer");
    esp_camera_fb_return(fb);
    return;
  }
  size_t copy_len = (size_t)qw * (size_t)qh;
  if (fb->len < copy_len) copy_len = fb->len; // safety
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
        Serial.printf("❌ Decode failed: %s\n", quirc_strerror(err));
      }
      
      if (qrDecoded) break;
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
    Serial.println("No QR code found");
  }
  
  // Return camera frame buffer
  esp_camera_fb_return(fb);
  
  // Check WiFi status
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    digitalWrite(LED_PIN, HIGH); // LED on when disconnected
  } else {
    digitalWrite(LED_PIN, LOW);  // LED off when connected
  }
  
  delay(1000); // Scan every 1 second
}