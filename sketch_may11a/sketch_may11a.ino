#include <WiFi.h>
#include "esp_camera.h"

// AI Thinker pin definitions
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

const char* ssid     = "";
const char* password = "";

WiFiServer server(80);

volatile bool motionFlag = false;

#define MD_WIDTH  80
#define MD_HEIGHT 60
#define MD_PIXELS (MD_WIDTH * MD_HEIGHT)

uint8_t  refFrame[MD_PIXELS];
bool     refFrameReady    = false;
int      motionThreshold  = 20;   // per-pixel difference to count as changed
float    motionSensitivity = 0.05; // fraction of pixels that must change (5%)

unsigned long lastMotionCheck = 0;
const unsigned long MOTION_CHECK_MS = 500; //5 sec

//downsacled
bool captureDownscaled(uint8_t* buf) {
  // Temporarily switch to smaller frame for motion check
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;

  s->set_framesize(s, FRAMESIZE_QQVGA); // 160x120
  delay(100); // let sensor settle

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    s->set_framesize(s, FRAMESIZE_VGA);
    return false;
  }

  // Simple 2x2 box downsample from 160x120 → 80x60
  // fb->buf is JPEG so we need a grayscale capture here
  // Switch pixel format temporarily for motion frames
  // Instead: use the JPEG luminance approximation trick —
  // just sample every other pixel row/col from the raw buffer
  // For simplicity, use the JPEG data length as a proxy motion metric
  // (larger = more detail = movement). This avoids needing raw pixel access.
  // Store the length as a single comparable value.
  memcpy(buf, fb->buf, fb->len < MD_PIXELS ? fb->len : MD_PIXELS);

  esp_camera_fb_return(fb);
  s->set_framesize(s, FRAMESIZE_VGA);
  delay(50);
  return true;
}
// Captures two small grayscale frames and compares them
void checkMotion() {
  // Switch to grayscale + small frame just for this check
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;

  s->set_framesize(s, FRAMESIZE_QQVGA);   // 160x120
  s->set_pixformat(s, PIXFORMAT_GRAYSCALE);
  delay(150); // sensor needs time to switch modes

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    // Restore settings
    s->set_pixformat(s, PIXFORMAT_JPEG);
    s->set_framesize(s, FRAMESIZE_VGA);
    return;
  }

  if (!refFrameReady) {
    // First frame — just store as reference, no comparison yet
    // Downsample 160x120 → 80x60 with 2x2 box average
    for (int y = 0; y < MD_HEIGHT; y++) {
      for (int x = 0; x < MD_WIDTH; x++) {
        int sx = x * 2, sy = y * 2;
        refFrame[y * MD_WIDTH + x] = (
          fb->buf[sy * 160 + sx] +
          fb->buf[sy * 160 + sx + 1] +
          fb->buf[(sy+1) * 160 + sx] +
          fb->buf[(sy+1) * 160 + sx + 1]
        ) / 4;
      }
    }
    refFrameReady = true;
    esp_camera_fb_return(fb);
    s->set_pixformat(s, PIXFORMAT_JPEG);
    s->set_framesize(s, FRAMESIZE_VGA);
    return;
  }

  // Compare current frame to reference
  int changedPixels = 0;
  for (int y = 0; y < MD_HEIGHT; y++) {
    for (int x = 0; x < MD_WIDTH; x++) {
      int sx = x * 2, sy = y * 2;
      uint8_t curr = (
        fb->buf[sy * 160 + sx] +
        fb->buf[sy * 160 + sx + 1] +
        fb->buf[(sy+1) * 160 + sx] +
        fb->buf[(sy+1) * 160 + sx + 1]
      ) / 4;

      if (abs((int)curr - (int)refFrame[y * MD_WIDTH + x]) > motionThreshold) {
        changedPixels++;
      }

      // Update reference frame with slow blend (keeps up with lighting changes)
      // alpha=0.2 means reference drifts slowly toward current
      refFrame[y * MD_WIDTH + x] = (uint8_t)(
        0.8f * refFrame[y * MD_WIDTH + x] + 0.2f * curr
      );
    }
  }

  esp_camera_fb_return(fb);
  s->set_pixformat(s, PIXFORMAT_JPEG);
  s->set_framesize(s, FRAMESIZE_VGA);
  delay(50); // let sensor settle back to JPEG/VGA

  float changedFraction = (float)changedPixels / MD_PIXELS;
  if (changedFraction > motionSensitivity) {
    motionFlag = true;
    Serial.printf("Motion detected: %.1f%% pixels changed\n", changedFraction * 100);
  }
}

void handleClient(WiFiClient client) {
  String request = "";
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
    }
  }

  if (request.indexOf("GET /capture") >= 0) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      client.println("HTTP/1.1 500 Internal Server Error\r\n\r\n");
      client.stop();
      return;
    }

    client.printf(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: image/jpeg\r\n"
      "Content-Length: %u\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "\r\n",
      fb->len
    );
    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);

  } else if (request.indexOf(("GET /motion")>=0){
    // Lightweight flag poll — Pi calls this at 10Hz
    // Returns current flag state and clears it
    String json = motionFlag ? "{\"motion\":true}" : "{\"motion\":false}";
    motionFlag = false; // clear after read
    client.printf(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: %u\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "\r\n"
      "%s",
      json.length(), json.c_str()
    );
  }else if (request.indexOf("GET /status") >= 0) {
    // Optional: useful for debugging from browser
    String json = "{\"motion_flag\":" + String(motionFlag ? "true" : "false") +
                  ",\"threshold\":" + String(motionThreshold) +
                  ",\"sensitivity\":" + String(motionSensitivity) +
                  ",\"ref_ready\":" + String(refFrameReady ? "true" : "false") + "}";
    client.printf(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: %u\r\n"
      "\r\n"
      "%s",
      json.length(), json.c_str()
    );

  } else {
    client.println("HTTP/1.1 404 Not Found\r\n\r\n");
  }

  client.stop();
}


  client.stop();
}
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nConnected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  camera_config_t camera_config = {
    .pin_pwdn       = PWDN_GPIO_NUM,
    .pin_reset      = RESET_GPIO_NUM,
    .pin_xclk       = XCLK_GPIO_NUM,
    .pin_sccb_sda   = SIOD_GPIO_NUM,
    .pin_sccb_scl   = SIOC_GPIO_NUM,
    .pin_d7         = Y9_GPIO_NUM,
    .pin_d6         = Y8_GPIO_NUM,
    .pin_d5         = Y7_GPIO_NUM,
    .pin_d4         = Y6_GPIO_NUM,
    .pin_d3         = Y5_GPIO_NUM,
    .pin_d2         = Y4_GPIO_NUM,
    .pin_d1         = Y3_GPIO_NUM,
    .pin_d0         = Y2_GPIO_NUM,
    .pin_vsync      = VSYNC_GPIO_NUM,
    .pin_href       = HREF_GPIO_NUM,
    .pin_pclk       = PCLK_GPIO_NUM,
    .xclk_freq_hz   = 20000000,
    .ledc_timer     = LEDC_TIMER_0,
    .ledc_channel   = LEDC_CHANNEL_0,
    .pixel_format   = PIXFORMAT_JPEG,
    .frame_size     = FRAMESIZE_VGA,
    .jpeg_quality   = 10,
    .fb_count       = 1,
    .grab_mode      = CAMERA_GRAB_WHEN_EMPTY
  };

  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while (true) delay(1000);
  }
  Serial.println("Camera ready");

  server.begin();
  Serial.println("Server started");
}

void loop() {
 // Run motion detection on a timer
  unsigned long now = millis();
  if (now - lastMotionCheck >= MOTION_CHECK_MS) {
    checkMotion();
    lastMotionCheck = now;
  }

  // Handle incoming HTTP requests
  WiFiClient client = server.available();
  if (client) {
    handleClient(client);
  }
}