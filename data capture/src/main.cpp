#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <string.h>
#include "img_converters.h"  // For fmt2jpg() function

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13
#define BUTTON_PIN        0

#define SD_CS_PIN         21

const char* ssid = "YOUR_WIFI_NETWORK_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

WebServer server(80);
camera_config_t config;

bool sdCardPresent = false;
bool wifiConnected = false;

bool burstInProgress = false;
int burstCurrent = 0;
int burstTotal = 0;

int currentQuality = 12;
framesize_t currentFrameSize = FRAMESIZE_VGA;
pixformat_t currentPixelFormat = PIXFORMAT_JPEG;
bool currentBigEndian = false;
int settingsMenuState = 0; // 0 = main menu, 1 = resolution, 2 = quality, 3 = color format, 4 = endianness


bool initCamera();
bool initSDCard();
bool initWiFi();
void setupWebServer();
void captureImage();
String getNextFilename(const char* extension = ".jpg");
void deleteAllImages();
void listImages();
void handleRoot();
void handleImage();
void handleCapture();
void handleDelete();
void handleListJSON();
void handleSetQuality();
void handleSetResolution();
void handleSetPixelFormat();
void handleSetEndianness();
void handleGetSettings();
void handleBurstCapture();
void handleBurstStatus();
void showSettingsMenu();
void showResolutionMenu();
void showColorFormatMenu();
void showEndiannessMenu();
void showMainMenu();

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("BOOT");
  Serial.flush();
  delay(400);
  
  Serial.println("\n\nXIAO Sense ESP32 Camera Capture");
  Serial.println("==================================");
  Serial.flush();
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  Serial.println("Starting initialization...");
  Serial.flush();
  
  if (!initCamera()) {
    Serial.println("Camera initialization failed!");
    Serial.println("Please check camera connections and power.");
    Serial.println("The device will continue but camera features will not work.");
    Serial.flush();
  } else {
    Serial.println("\nTesting camera capture...");
    Serial.flush();
    delay(500);
    camera_fb_t *test_fb = esp_camera_fb_get();
    if (test_fb) {
      Serial.printf("Camera test capture successful! Size: %u bytes\n", test_fb->len);
      esp_camera_fb_return(test_fb);
    } else {
      Serial.println("Camera test capture failed - check hardware connections");
    }
    Serial.flush();
  }
  
  if (!initSDCard()) {
    Serial.println("SD card initialization failed - images cannot be saved");
    Serial.println("Continuing without SD card...");
  } else {
    Serial.println("SD card initialized successfully!");
    sdCardPresent = true;
  }
  
  if (initWiFi()) {
    setupWebServer();
    wifiConnected = true;
    Serial.println("\nWeb server started!");
    Serial.print("Open your browser and go to: http://");
    Serial.println(WiFi.localIP());
    Serial.println("Or use: http://xiaocamera.local (if mDNS works)");
  } else {
    Serial.println("\nWiFi connection failed. Continuing without web server.");
    Serial.println("You can still capture images via button or serial commands.");
  }
  
  Serial.println("\nReady to capture images!");
  showMainMenu();
}

void showSettingsMenu() {
  Serial.println("\n=== Settings Menu ===");
  Serial.println("1 - Resolution");
  Serial.println("2 - JPEG Quality");
  Serial.println("3 - Color Format");
  Serial.println("4 - Endianness");
  Serial.println("c - Take a new photo and continue");
  Serial.print("Select option: ");
  Serial.flush();
}

void showResolutionMenu() {
  Serial.println("\n=== Resolution ===");
  Serial.println("0 - QQVGA (96x96 / 160x120)");
  Serial.println("1 - QCIF (176x144)");
  Serial.println("2 - QVGA (240x240 / 320x240)");
  Serial.println("3 - VGA (640x480)");
  Serial.println("4 - SVGA (800x600)");
  Serial.println("5 - XGA (1024x768)");
  Serial.println("6 - SXGA (1280x1024)");
  Serial.println("7 - UXGA (1600x1200)");
  Serial.print("Select resolution: ");
  Serial.flush();
}

void showColorFormatMenu() {
  Serial.println("\n=== Color Format ===");
  Serial.println("0 - RGB (JPEG)");
  Serial.println("1 - Grayscale");
  Serial.println("2 - RGB565");
  Serial.print("Select format: ");
  Serial.flush();
}

void showEndiannessMenu() {
  Serial.println("\n=== Endianness ===");
  Serial.println("1 - Little Endian");
  Serial.println("2 - Big Endian");
  Serial.print("Select endianness: ");
  Serial.flush();
}

void showMainMenu() {
  Serial.println("\n=== Serial Commands ===");
  Serial.println("c - Capture image");
  Serial.println("b - Burst capture (50 photos at 0.2s intervals)");
  Serial.println("s - Settings menu");
  Serial.println("l - List all images");
  Serial.println("d - Delete all images");
  Serial.println("h - Show help");
  if (wifiConnected) {
    Serial.printf("w - Web interface: http://%s\n", WiFi.localIP().toString().c_str());
  }
  Serial.flush();
}

void loop() {
  if (wifiConnected) {
    server.handleClient();
  }
  
  static unsigned long lastDebounceTime = 0;
  static bool lastButtonState = HIGH;
  bool currentButtonState = digitalRead(BUTTON_PIN);
  
  if (currentButtonState != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > 50) {
    if (currentButtonState == LOW && lastButtonState == HIGH) {
      captureImage();
      delay(500);
    }
  }
  lastButtonState = currentButtonState;
  
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;
    
    char command = input.charAt(0);
    
    // Handle settings menu navigation
    if (settingsMenuState > 0) {
      if (command == 'c' || command == 'C') {
        // Take a photo and continue in settings menu
        captureImage();
        Serial.println();
        showSettingsMenu();
        return;
      }
      
      if (settingsMenuState == 1) { // Resolution selection
        int choice = input.toInt();
        if (choice >= 0 && choice <= 7) {
          framesize_t newSize;
          switch(choice) {
            case 0: newSize = FRAMESIZE_QQVGA; break;
            case 1: newSize = FRAMESIZE_QCIF; break;
            case 2: newSize = FRAMESIZE_QVGA; break;
            case 3: newSize = FRAMESIZE_VGA; break;
            case 4: newSize = FRAMESIZE_SVGA; break;
            case 5: newSize = FRAMESIZE_XGA; break;
            case 6: newSize = FRAMESIZE_SXGA; break;
            case 7: newSize = FRAMESIZE_UXGA; break;
            default: newSize = currentFrameSize; break;
          }
          if (currentFrameSize != newSize) {
            currentFrameSize = newSize;
            Serial.println("\nResolution changed - reinitializing camera...");
            Serial.flush();
            esp_camera_deinit();
            delay(100);
            initCamera();
            Serial.println("Resolution updated successfully!");
            delay(200);
          } else {
            Serial.println("\nResolution unchanged.");
            delay(200);
          }
        } else {
          Serial.println("\nInvalid selection!");
          delay(200);
        }
        settingsMenuState = 0;
        Serial.println();
        showSettingsMenu();
        return;
      }
      
      if (settingsMenuState == 2) { // Quality input - requires Enter to confirm
        int quality = input.toInt();
        if (quality >= 0 && quality <= 63) {
          currentQuality = quality;
          Serial.printf("\nJPEG Quality set to %d (lower = higher quality)\n", quality);
          Serial.println("Note: Quality change will apply to next capture.");
          delay(200);
        } else {
          Serial.println("\nInvalid quality! Must be between 0 and 63.");
          delay(200);
        }
        settingsMenuState = 0;
        Serial.println();
        showSettingsMenu();
        return;
      }
      
      if (settingsMenuState == 3) { // Color format selection
        int choice = input.toInt();
        if (choice >= 0 && choice <= 2) {
          pixformat_t newFormat;
          switch(choice) {
            case 0: newFormat = PIXFORMAT_JPEG; break;
            case 1: newFormat = PIXFORMAT_GRAYSCALE; break;
            case 2: newFormat = PIXFORMAT_RGB565; break;
            default: newFormat = currentPixelFormat; break;
          }
          if (currentPixelFormat != newFormat) {
            currentPixelFormat = newFormat;
            Serial.println("\nColor format changed - reinitializing camera...");
            Serial.flush();
            esp_camera_deinit();
            delay(100);
            initCamera();
            Serial.println("Color format updated successfully!");
            delay(200);
          } else {
            Serial.println("\nColor format unchanged.");
            delay(200);
          }
        } else {
          Serial.println("\nInvalid selection!");
          delay(200);
        }
        settingsMenuState = 0;
        Serial.println();
        showSettingsMenu();
        return;
      }
      
      if (settingsMenuState == 4) { // Endianness selection
        int choice = input.toInt();
        if (choice == 1) {
          currentBigEndian = false;
          Serial.println("\nEndianness set to Little Endian");
          delay(200);
        } else if (choice == 2) {
          currentBigEndian = true;
          Serial.println("\nEndianness set to Big Endian");
          delay(200);
        } else {
          Serial.println("\nInvalid selection! Use 1 for Little Endian or 2 for Big Endian.");
          delay(200);
        }
        settingsMenuState = 0;
        Serial.println();
        showSettingsMenu();
        return;
      }
    }
    
    // Main command handling
    if (command == 'c' || command == 'C') {
      captureImage();
    } else if (command == 'b' || command == 'B') {
      // Burst capture: default 50 photos at 0.2 second intervals
      int count = 50;
      float interval = 0.2;
      
      Serial.printf("\n=== Starting Burst Capture ===\n");
      Serial.printf("Count: %d photos\n", count);
      Serial.printf("Interval: %.2f seconds\n", interval);
      Serial.flush();
      
      burstInProgress = true;
      burstCurrent = 0;
      burstTotal = count;
      
      unsigned long intervalMs = (unsigned long)(interval * 1000);
      
      for (int i = 0; i < count; i++) {
        burstCurrent = i + 1;
        Serial.printf("\nBurst capture %d/%d\n", i + 1, count);
        Serial.flush();
        
        captureImage();
        
        if (i < count - 1) {
          unsigned long startTime = millis();
          while (millis() - startTime < intervalMs) {
            if (wifiConnected) {
              server.handleClient();
            }
            delay(10);
          }
        }
      }
      
      burstInProgress = false;
      burstCurrent = 0;
      burstTotal = 0;
      
      Serial.println("\n=== Burst Capture Complete ===");
      Serial.flush();
    } else if (command == 'd' || command == 'D') {
      deleteAllImages();
    } else if (command == 'l' || command == 'L') {
      listImages();
    } else if (command == 's' || command == 'S') {
      settingsMenuState = 0;
      showSettingsMenu();
    } else if (command == 'h' || command == 'H') {
      showMainMenu();
    } else if (command == 'w' || command == 'W') {
      if (wifiConnected) {
        Serial.printf("\nWeb interface: http://%s\n", WiFi.localIP().toString().c_str());
        Serial.println("Or: http://xiaocamera.local");
      } else {
        Serial.println("WiFi not connected.");
      }
    } else if (settingsMenuState == 0 && (command == '1' || command == '2' || command == '3' || command == '4')) {
      // Handle menu selection when in main settings menu
      if (command == '1') {
        settingsMenuState = 1;
        showResolutionMenu();
      } else if (command == '2') {
        settingsMenuState = 2;
        Serial.print("\nEnter JPEG Quality (0-63, lower=higher quality), then press Enter: ");
        Serial.flush();
      } else if (command == '3') {
        settingsMenuState = 3;
        showColorFormatMenu();
      } else if (command == '4') {
        settingsMenuState = 4;
        showEndiannessMenu();
      }
    }
  }
  
  delay(10);
}

bool initCamera() {
  Serial.println("Initializing camera...");
  Serial.flush();
  
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
  // Use the selected pixel format
  config.pixel_format = currentPixelFormat;
  config.frame_size = currentFrameSize;
  config.jpeg_quality = currentQuality;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  
  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    if (config.pixel_format == PIXFORMAT_JPEG && currentQuality > 10) {
      config.jpeg_quality = 10;
    }
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
    if (config.pixel_format == PIXFORMAT_JPEG) {
      config.frame_size = FRAMESIZE_SVGA;
    }
  }
  
  Serial.println("Calling esp_camera_init()...");
  Serial.flush();
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    Serial.flush();
    return false;
  }
  
  Serial.println("Getting camera sensor...");
  Serial.flush();
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("Failed to get camera sensor!");
    Serial.flush();
    return false;
  }
  
  Serial.println("Configuring camera sensor settings...");
  Serial.flush();
  s->set_brightness(s, 0);
  s->set_contrast(s, 0);
  s->set_saturation(s, 0);
  s->set_special_effect(s, 0);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);
  s->set_exposure_ctrl(s, 1);
  s->set_aec2(s, 0);
  s->set_ae_level(s, 0);
  s->set_aec_value(s, 300);
  s->set_gain_ctrl(s, 1);
  s->set_agc_gain(s, 0);
  s->set_gainceiling(s, (gainceiling_t)0);
  s->set_bpc(s, 0);
  s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);
  s->set_lenc(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 0);
  s->set_dcw(s, 1);
  s->set_colorbar(s, 0);
  
  Serial.println("Camera initialized successfully!");
  delay(500);
  return true;
}

bool initSDCard() {
  Serial.println("Initializing SD card...");
  Serial.flush();
  
  unsigned long startTime = millis();
  bool success = false;
  
  for (int i = 0; i < 5; i++) {
    if (SD.begin(SD_CS_PIN)) {
      success = true;
      break;
    }
    Serial.print("SD init attempt ");
    Serial.print(i + 1);
    Serial.println(" failed, retrying...");
    delay(500);
  }
  
  if (!success) {
    Serial.println("SD.begin() failed after retries");
    return false;
  }
  
  delay(100);
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card detected");
    return false;
  }
  
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  
  uint64_t totalMB = SD.totalBytes() / (1024 * 1024);
  uint64_t freeMB = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
  Serial.printf("SD Card: %llu MB total, %llu MB free\n", totalMB, freeMB);
  
  Serial.print("Filesystem Format: ");
  File root = SD.open("/");
  if (root && root.isDirectory()) {
    Serial.println("FAT32");
    root.close();
  } else {
    Serial.println("UNKNOWN");
    if (root) root.close();
  }
  
  return true;
}

void captureImage() {
  Serial.println("\nCapturing image...");
  Serial.flush();
  
  // For high-resolution RGB565, add a small delay to let camera stabilize
  if (currentPixelFormat == PIXFORMAT_RGB565 && 
      (currentFrameSize == FRAMESIZE_SXGA || currentFrameSize == FRAMESIZE_UXGA)) {
    delay(100); // Extra stabilization for high-res RGB565
  }
  
  Serial.println("Getting frame buffer...");
  Serial.flush();
  
  // Capture in the selected format
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("ERROR: Camera capture failed!");
    Serial.flush();
    return;
  }
  
  Serial.printf("Captured image size: %u bytes, format: %d\n", fb->len, fb->format);
  Serial.flush();
  
  uint8_t* jpegData = NULL;
  size_t jpegLen = 0;
  bool needsFree = false;
  
  // Process based on format
  if (fb->format == PIXFORMAT_JPEG) {
    // Already JPEG, use as-is
    Serial.println("Image is already JPEG format");
    Serial.flush();
    jpegData = fb->buf;
    jpegLen = fb->len;
  } else if (fb->format == PIXFORMAT_GRAYSCALE) {
    // Convert grayscale to JPEG using fmt2jpg() - preserves grayscale appearance
    Serial.println("Converting grayscale to JPEG...");
    Serial.flush();
    
    // Map quality (0-63 camera quality to 0-100 fmt2jpg quality)
    int jpegQuality = map(currentQuality, 0, 63, 10, 100);
    if (jpegQuality < 10) jpegQuality = 10;
    if (jpegQuality > 100) jpegQuality = 100;
    
    // Convert grayscale to JPEG
    uint8_t *jpeg_buf = NULL;
    size_t jpeg_buf_len = 0;
    
    bool success = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_GRAYSCALE, jpegQuality, &jpeg_buf, &jpeg_buf_len);
    
    if (!success || !jpeg_buf) {
      Serial.println("ERROR: Grayscale to JPEG conversion failed!");
      Serial.flush();
      esp_camera_fb_return(fb);
      return;
    }
    
    jpegData = jpeg_buf;
    jpegLen = jpeg_buf_len;
    needsFree = true; // We need to free jpeg_buf later
    
    Serial.printf("Grayscale converted to JPEG: %u bytes\n", jpegLen);
    Serial.flush();
    
    // Return the original frame buffer
    esp_camera_fb_return(fb);
    fb = NULL; // Mark as returned so we don't return it again
    
  } else if (fb->format == PIXFORMAT_RGB565) {
    // Convert RGB565 to JPEG using fmt2jpg() - preserves RGB565 appearance
    Serial.println("Converting RGB565 to JPEG...");
    Serial.flush();
    
    // Handle endianness if needed
    uint8_t* rgb565Data = fb->buf;
    bool swappedData = false;
    if (currentBigEndian) {
      // Swap bytes for big endian
      Serial.println("Swapping bytes for big endian...");
      Serial.flush();
      rgb565Data = (uint8_t*)malloc(fb->len);
      if (rgb565Data) {
        // Process in chunks to avoid blocking for too long on large images
        size_t chunkSize = 65536; // Process 64KB at a time
        for (size_t i = 0; i < fb->len - 1; i += 2) {
          rgb565Data[i] = fb->buf[i + 1];
          rgb565Data[i + 1] = fb->buf[i];
          // Yield periodically for large images to prevent watchdog issues
          if (i > 0 && (i % chunkSize == 0)) {
            delay(1);
          }
        }
        swappedData = true;
        Serial.println("Byte swap complete");
        Serial.flush();
      } else {
        Serial.println("ERROR: Failed to allocate swap buffer!");
        Serial.flush();
        esp_camera_fb_return(fb);
        return;
      }
    }
    
    // Map quality (0-63 camera quality to 0-100 fmt2jpg quality)
    // For high-resolution images, use slightly lower quality to reduce processing time
    int jpegQuality = map(currentQuality, 0, 63, 10, 100);
    if (jpegQuality < 10) jpegQuality = 10;
    if (jpegQuality > 100) jpegQuality = 100;
    
    // For very high resolution (SXGA/UXGA), cap quality to reduce processing time
    if ((currentFrameSize == FRAMESIZE_SXGA || currentFrameSize == FRAMESIZE_UXGA) && jpegQuality > 80) {
      jpegQuality = 80;
      Serial.println("Reducing quality for high-resolution capture");
      Serial.flush();
    }
    
    Serial.printf("Encoding RGB565 to JPEG (quality: %d)...\n", jpegQuality);
    Serial.flush();
    
    // Convert RGB565 to JPEG
    uint8_t *jpeg_buf = NULL;
    size_t jpeg_buf_len = 0;
    
    bool success = fmt2jpg(rgb565Data, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, jpegQuality, &jpeg_buf, &jpeg_buf_len);
    
    if (swappedData) {
      free(rgb565Data);
    }
    
    if (!success || !jpeg_buf) {
      Serial.println("ERROR: RGB565 to JPEG conversion failed!");
      Serial.flush();
      esp_camera_fb_return(fb);
      return;
    }
    
    jpegData = jpeg_buf;
    jpegLen = jpeg_buf_len;
    needsFree = true; // We need to free jpeg_buf later
    
    Serial.printf("RGB565 converted to JPEG: %u bytes\n", jpegLen);
    Serial.flush();
    
    // Return the original frame buffer
    esp_camera_fb_return(fb);
    fb = NULL; // Mark as returned so we don't return it again
  } else {
    Serial.printf("ERROR: Unsupported format: %d\n", fb->format);
    Serial.flush();
    esp_camera_fb_return(fb);
    return;
  }
  
  // Save JPEG file
  String filename = getNextFilename(".jpg");
  Serial.printf("Saving as: %s\n", filename.c_str());
  Serial.flush();
  
  if (!sdCardPresent) {
    Serial.println("ERROR: SD card not available");
    Serial.flush();
    if (needsFree) free(jpegData);
    esp_camera_fb_return(fb);
    return;
  }
  
  Serial.println("Opening file for writing...");
  Serial.flush();
  
  File file = SD.open(filename.c_str(), FILE_WRITE);
  bool saved = false;
  if (file) {
    Serial.println("Writing data to SD card...");
    Serial.flush();
    size_t written = file.write(jpegData, jpegLen);
    file.close();
    saved = (written == jpegLen);
    Serial.printf("Written: %u bytes\n", written);
    Serial.flush();
  } else {
    Serial.println("ERROR: Failed to open file for writing");
    Serial.flush();
  }
  
  if (needsFree && jpegData != NULL) {
    free(jpegData);
  }
  
  if (fb != NULL) {
    esp_camera_fb_return(fb);
  }
  
  if (saved) {
    Serial.printf("SUCCESS: Image saved as %s!\n", filename.c_str());
  } else {
    Serial.println("ERROR: Failed to save image!");
  }
  Serial.flush();
}

String getNextFilename(const char* extension) {
  int fileNumber = 1;
  String filename;
  bool fileExists = true;
  
  while (fileExists) {
    filename = "/" + String(fileNumber) + extension;
    fileExists = SD.exists(filename.c_str());
    
    if (fileExists) {
      fileNumber++;
      if (fileNumber > 10000) {
        break;
      }
    }
  }
  
  return filename;
}

void deleteAllImages() {
  Serial.println("\nDeleting all images...");
  int deleted = 0;
  
  for (int i = 1; i <= 10000; i++) {
    String filename = "/" + String(i) + ".jpg";
    if (SD.exists(filename.c_str())) {
      SD.remove(filename.c_str());
      deleted++;
    } else if (deleted > 0) {
      break;
    }
  }
  
  Serial.printf("Deleted %d images\n", deleted);
}

void listImages() {
  Serial.println("\nListing all images:");
  int count = 0;
  
  for (int i = 1; i <= 10000; i++) {
    String filename = "/" + String(i) + ".jpg";
    if (SD.exists(filename.c_str())) {
      File file = SD.open(filename.c_str(), FILE_READ);
      size_t fileSize = 0;
      if (file) {
        fileSize = file.size();
        file.close();
      }
      Serial.printf("  %s (%u bytes)\n", filename.c_str(), fileSize);
      count++;
    } else if (count > 0) {
      break;
    }
  }
  
  if (count == 0) {
    Serial.println("  No images found");
  } else {
    Serial.printf("\nTotal: %d images\n", count);
  }
}

bool initWiFi() {
  Serial.println("\nConnecting to WiFi...");
  Serial.printf("SSID: %s\n", ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts % 10 == 0) {
      Serial.printf("\nStill connecting... (attempt %d/30)\n", attempts);
      WiFi.disconnect();
      delay(100);
      WiFi.begin(ssid, password);
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal strength (RSSI): %d dBm\n", WiFi.RSSI());
    
    if (!MDNS.begin("xiaocamera")) {
      Serial.println("mDNS responder failed to start");
    } else {
      Serial.println("mDNS responder started - try http://xiaocamera.local");
    }
    
    return true;
  } else {
    Serial.println("\nWiFi connection failed!");
    Serial.printf("WiFi status: %d\n", WiFi.status());
    Serial.println("Possible issues:");
    Serial.println("  - Wrong SSID or password");
    Serial.println("  - WiFi router not in range");
    Serial.println("  - Router only supports 5GHz (ESP32 only supports 2.4GHz)");
    return false;
  }
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/image", HTTP_GET, handleImage);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/list", HTTP_GET, handleListJSON);
  server.on("/setquality", HTTP_POST, handleSetQuality);
  server.on("/setresolution", HTTP_POST, handleSetResolution);
  server.on("/setpixelformat", HTTP_POST, handleSetPixelFormat);
  server.on("/setendianness", HTTP_POST, handleSetEndianness);
  server.on("/getsettings", HTTP_GET, handleGetSettings);
  // Backend burst endpoints (not exposed in web UI, but available for API use)
  server.on("/burstcapture", HTTP_POST, handleBurstCapture);
  server.on("/burststatus", HTTP_GET, handleBurstStatus);
  
  server.begin();
  Serial.println("HTTP server started");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>XIAO Camera Gallery</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }";
  html += "h1 { color: #333; }";
  html += ".controls { margin: 20px 0; }";
  html += "button { padding: 10px 20px; margin: 5px; font-size: 16px; cursor: pointer; }";
  html += ".capture { background: #4CAF50; color: white; border: none; border-radius: 5px; }";
  html += ".delete { background: #f44336; color: white; border: none; border-radius: 5px; }";
  html += ".refresh { background: #2196F3; color: white; border: none; border-radius: 5px; }";
  html += ".download-all { background: #FF9800; color: white; border: none; border-radius: 5px; }";
  html += ".download-all:disabled { background: #ccc; cursor: not-allowed; }";
  html += ".gallery { display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr)); gap: 15px; margin-top: 20px; }";
  html += ".image-card { background: white; padding: 10px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += ".image-card img { width: 100%; height: auto; border-radius: 5px; }";
  html += ".image-card a { display: block; margin-top: 5px; text-align: center; color: #2196F3; text-decoration: none; }";
  html += ".info { background: white; padding: 15px; border-radius: 8px; margin-bottom: 20px; }";
  html += ".settings { background: white; padding: 15px; border-radius: 8px; margin-bottom: 20px; }";
  html += ".settings h3 { margin-top: 0; }";
  html += ".settings div { margin-bottom: 15px; }";
  html += ".settings label { font-weight: bold; margin-right: 10px; }";
  html += ".settings select { padding: 5px; font-size: 14px; }";
  html += ".settings input[type='range'] { width: 200px; }";
  html += ".settings button { padding: 5px 10px; margin-left: 5px; font-size: 14px; }";
  html += ".latest-image { background: white; padding: 15px; border-radius: 8px; margin-bottom: 20px; }";
  html += ".latest-image img { max-width: 100%; max-height: 400px; border-radius: 5px; }";
  html += "</style></head><body>";
  html += "<h1>XIAO Camera Gallery</h1>";
  
  html += "<div class='latest-image' id='latestImage' style='background: white; padding: 15px; border-radius: 8px; margin-bottom: 20px; text-align: center; display: none;'>";
  html += "<h3 style='margin-top: 0;'>Latest Image</h3>";
  html += "<img id='latestImg' src='' alt='Latest image' style='max-width: 100%; max-height: 400px; border-radius: 5px;'>";
  html += "<p id='latestInfo' style='margin-top: 10px; color: #666;'></p>";
  html += "</div>";
  
  html += "<div class='settings'>";
  html += "<h3>Camera Settings</h3>";
  html += "<div>";
  html += "<label>Resolution: </label>";
  html += "<select id='resolutionSelect'>";
  html += "<option value='0'>96x96 (QQVGA 160x120)</option>";
  html += "<option value='1'>176x144 (QCIF)</option>";
  html += "<option value='2'>240x240 (QVGA 320x240)</option>";
  html += "<option value='3'>640x480 (VGA)</option>";
  html += "<option value='4'>800x600 (SVGA)</option>";
  html += "<option value='5'>1024x768 (XGA)</option>";
  html += "<option value='6'>1280x1024 (SXGA)</option>";
  html += "<option value='7'>1600x1200 (UXGA)</option>";
  html += "</select>";
  html += "<button onclick='changeResolution()'>Apply</button>";
  html += "</div>";
  html += "<div>";
  html += "<label>JPEG Quality (0-63, lower=higher quality): </label>";
  html += "<input type='range' id='qualitySlider' min='0' max='63' value='12'>";
  html += "<span id='qualityValue'>12</span>";
  html += "<button onclick='changeQuality()'>Apply</button>";
  html += "</div>";
  html += "<div>";
  html += "<label>Color Format: </label>";
  html += "<select id='pixelFormatSelect'>";
  html += "<option value='0'>RGB (JPEG)</option>";
  html += "<option value='1'>Grayscale</option>";
  html += "<option value='2'>RGB565</option>";
  html += "</select>";
  html += "<button onclick='changePixelFormat()'>Apply</button>";
  html += "</div>";
  html += "<div>";
  html += "<label>Endianness: </label>";
  html += "<select id='endiannessSelect'>";
  html += "<option value='0'>Little Endian</option>";
  html += "<option value='1'>Big Endian</option>";
  html += "</select>";
  html += "<button onclick='changeEndianness()'>Apply</button>";
  html += "<p style='font-size: 12px; color: #666; margin-top: 5px; margin-left: 0;'>";
  html += "Only applies to RGB565 format. Use Little Endian for ESP32/MicroPython. ";
  html += "Use Big Endian if your processing software requires it (e.g., some ML frameworks).";
  html += "</p>";
  html += "<p style='font-size: 12px; color: #666; margin-top: 5px; margin-left: 0;'>";
  html += "<strong>Note:</strong> Burst capture (50 photos at 0.2 second intervals) is available via serial monitor using the <code>b</code> command. ";
  html += "Burst photos are saved to SD card and use the current camera settings. ";
  html += "Refreshing this page will show all pictures taken via serial monitor (including burst captures) in the gallery.";
  html += "</p>";
  html += "</div>";
  html += "</div>";
  
  html += "<div class='info'>";
  html += "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>";
  html += "<p><strong>Storage:</strong> ";
  if (sdCardPresent) {
    uint64_t totalMB = SD.totalBytes() / (1024 * 1024);
    uint64_t usedMB = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
    html += "SD Card (" + String(usedMB) + " MB / " + String(totalMB) + " MB used)";
  } else {
    html += "SD Card not available";
  }
  html += "</p></div>";
  
  html += "<div class='controls'>";
  html += "<button class='capture' onclick='captureImage()'>Capture New Image</button>";
  html += "<button class='download-all' id='downloadAllBtn' onclick='downloadAllImages()'>Download All Images</button>";
  html += "<button class='refresh' onclick='location.reload()'>Refresh</button>";
  html += "<button class='delete' onclick='deleteAll()'>Delete All Images</button>";
  html += "</div>";
  html += "<div id='downloadStatus' style='margin: 10px 0; color: #666;'></div>";
  
  html += "<div class='gallery' id='gallery'>";
  html += "<p>Loading images...</p>";
  html += "</div>";
  
  html += "<script>";
  html += "let allImages = [];";
  html += "const gallery = document.getElementById('gallery');";
  html += "const downloadAllBtn = document.getElementById('downloadAllBtn');";
  html += "const latestImage = document.getElementById('latestImage');";
  html += "const latestImg = document.getElementById('latestImg');";
  html += "const latestInfo = document.getElementById('latestInfo');";
  html += "function loadImages() {";
  html += "  fetch('/list')";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      allImages = data.images;";
  html += "      if (data.images.length === 0) {";
  html += "        gallery.innerHTML = '<p>No images found. Click Capture to take your first photo!</p>';";
  html += "        downloadAllBtn.disabled = true;";
  html += "        latestImage.style.display = 'none';";
  html += "        return;";
  html += "      }";
  html += "      downloadAllBtn.disabled = false;";
  html += "      gallery.innerHTML = '';";
  html += "      const latest = data.images[data.images.length - 1];";
  html += "      latestImg.src = '/image?n=' + latest.number;";
  html += "      latestInfo.textContent = latest.filename + ' (' + (latest.size / 1024).toFixed(1) + ' KB)';";
  html += "      latestImage.style.display = 'block';";
  html += "      data.images.forEach(img => {";
  html += "        const card = document.createElement('div');";
  html += "        card.className = 'image-card';";
  html += "        card.innerHTML = '<img src=\"/image?n=' + img.number + '\" alt=\"' + img.filename + '\">' +";
  html += "                         '<a href=\"/image?n=' + img.number + '\" download=\"' + img.filename + '\">Download ' + img.filename + '</a>';";
  html += "        gallery.appendChild(card);";
  html += "      });";
  html += "    })";
  html += "    .catch(err => console.error('Error loading images:', err));";
  html += "}";
  html += "const status = document.getElementById('downloadStatus');";
  html += "function downloadAllImages() {";
  html += "  if (allImages.length === 0) {";
  html += "    alert('No images to download!');";
  html += "    return;";
  html += "  }";
  html += "  downloadAllBtn.disabled = true;";
  html += "  status.innerHTML = 'Downloading ' + allImages.length + ' images...';";
  html += "  let downloaded = 0;";
  html += "  allImages.forEach((img, index) => {";
  html += "    setTimeout(() => {";
  html += "      const link = document.createElement('a');";
  html += "      link.href = '/image?n=' + img.number;";
  html += "      link.download = img.filename;";
  html += "      link.style.display = 'none';";
  html += "      document.body.appendChild(link);";
  html += "      link.click();";
  html += "      document.body.removeChild(link);";
  html += "      downloaded++;";
  html += "      status.innerHTML = 'Downloaded ' + downloaded + ' / ' + allImages.length + ' images...';";
  html += "      if (downloaded === allImages.length) {";
  html += "        status.innerHTML = 'All ' + allImages.length + ' images downloaded successfully!';";
  html += "        downloadAllBtn.disabled = false;";
  html += "        setTimeout(() => status.innerHTML = '', 5000);";
  html += "      }";
  html += "    }, index * 300);";
  html += "  });";
  html += "}";
  html += "function captureImage() {";
  html += "  status.innerHTML = 'Capturing image...';";
  html += "  fetch('/capture')";
  html += "    .then(() => {";
  html += "      status.innerHTML = 'Image captured! Reloading...';";
  html += "      setTimeout(() => location.reload(), 2000);";
  html += "    })";
  html += "    .catch(() => {";
  html += "      status.innerHTML = 'Capture failed. Please try again.';";
  html += "    });";
  html += "}";
  html += "function deleteAll() {";
  html += "  if (confirm('Are you sure you want to delete ALL images?')) {";
  html += "    fetch('/delete')";
  html += "      .then(() => setTimeout(() => location.reload(), 1000));";
  html += "  }";
  html += "}";
  html += "function changeQuality() {";
  html += "  const value = parseInt(qualitySlider.value);";
  html += "  fetch('/setquality', {";
  html += "    method: 'POST',";
  html += "    headers: {'Content-Type': 'text/plain'},";
  html += "    body: value.toString()";
  html += "  })";
  html += "  .then(response => response.json())";
  html += "  .then(data => {";
  html += "    if (data.status === 'ok') {";
  html += "      qualityValue.textContent = data.quality;";
  html += "      alert('Quality set to ' + data.quality);";
  html += "    }";
  html += "  })";
  html += "  .catch(err => console.error('Error setting quality:', err));";
  html += "}";
  html += "function changeResolution() {";
  html += "  const select = document.getElementById('resolutionSelect');";
  html += "  const value = select.value;";
  html += "  fetch('/setresolution', {";
  html += "    method: 'POST',";
  html += "    headers: {'Content-Type': 'text/plain'},";
  html += "    body: value";
  html += "  })";
  html += "  .then(response => response.json())";
  html += "  .then(data => {";
  html += "    if (data.status === 'ok') {";
  html += "      alert('Resolution changed. Next capture will use this setting.');";
  html += "    }";
  html += "  })";
  html += "  .catch(err => console.error('Error setting resolution:', err));";
  html += "}";
  html += "function changePixelFormat() {";
  html += "  const select = document.getElementById('pixelFormatSelect');";
  html += "  const value = select.value;";
  html += "  fetch('/setpixelformat', {";
  html += "    method: 'POST',";
  html += "    headers: {'Content-Type': 'text/plain'},";
  html += "    body: value";
  html += "  })";
  html += "  .then(response => response.json())";
  html += "  .then(data => {";
  html += "    if (data.status === 'ok') {";
  html += "      alert('Pixel format changed. Next capture will use this setting.');";
  html += "    }";
  html += "  })";
  html += "  .catch(err => console.error('Error setting pixel format:', err));";
  html += "}";
  html += "function changeEndianness() {";
  html += "  const select = document.getElementById('endiannessSelect');";
  html += "  const value = select.value;";
  html += "  fetch('/setendianness', {";
  html += "    method: 'POST',";
  html += "    headers: {'Content-Type': 'text/plain'},";
  html += "    body: value";
  html += "  })";
  html += "  .then(response => response.json())";
  html += "  .then(data => {";
  html += "    if (data.status === 'ok') {";
  html += "      alert('Endianness changed. Next capture will use this setting.');";
  html += "    }";
  html += "  })";
  html += "  .catch(err => console.error('Error setting endianness:', err));";
  html += "}";
  html += "const qualitySlider = document.getElementById('qualitySlider');";
  html += "const qualityValue = document.getElementById('qualityValue');";
  html += "qualitySlider.addEventListener('input', function() {";
  html += "  qualityValue.textContent = this.value;";
  html += "});";
  html += "fetch('/getsettings')";
  html += "  .then(response => response.json())";
  html += "  .then(data => {";
  html += "    document.getElementById('resolutionSelect').value = data.resolution;";
  html += "    qualitySlider.value = data.quality;";
  html += "    qualityValue.textContent = data.quality;";
  html += "    document.getElementById('pixelFormatSelect').value = data.pixelFormat;";
  html += "    document.getElementById('endiannessSelect').value = data.endianness;";
  html += "  });";
  html += "loadImages();";
  html += "</script>";
  
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleBurstCapture() {
  if (!sdCardPresent) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"SD card required for burst capture\"}");
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
    return;
  }
  
  String body = server.arg("plain");
  
  // Parse JSON manually (simple parsing)
  int count = 50;
  float interval = 0.2;
  
  // Extract count
  int countIndex = body.indexOf("\"count\":");
  if (countIndex >= 0) {
    int countStart = body.indexOf(':', countIndex) + 1;
    int countEnd = body.indexOf(',', countStart);
    if (countEnd < 0) countEnd = body.indexOf('}', countStart);
    if (countEnd > countStart) {
      count = body.substring(countStart, countEnd).toInt();
    }
  }
  
  // Extract interval
  int intervalIndex = body.indexOf("\"interval\":");
  if (intervalIndex >= 0) {
    int intervalStart = body.indexOf(':', intervalIndex) + 1;
    int intervalEnd = body.indexOf('}', intervalStart);
    if (intervalEnd > intervalStart) {
      interval = body.substring(intervalStart, intervalEnd).toFloat();
    }
  }
  
  // Validate
  if (count < 1 || count > 200) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Count must be between 1 and 200\"}");
    return;
  }
  if (interval < 0.1 || interval > 5.0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Interval must be between 0.1 and 5.0 seconds\"}");
    return;
  }
  
  // Send response immediately
  server.send(200, "application/json", "{\"status\":\"ok\",\"count\":" + String(count) + ",\"interval\":" + String(interval) + "}");
  
  // Small delay to ensure response is sent
  delay(10);
  
  // Set burst status
  burstInProgress = true;
  burstCurrent = 0;
  burstTotal = count;
  
  // Perform burst capture
  Serial.printf("\n=== Starting Burst Capture ===\n");
  Serial.printf("Count: %d photos\n", count);
  Serial.printf("Interval: %.2f seconds\n", interval);
  Serial.flush();
  
  unsigned long intervalMs = (unsigned long)(interval * 1000);
  
  for (int i = 0; i < count; i++) {
    burstCurrent = i + 1;
    Serial.printf("\nBurst capture %d/%d\n", i + 1, count);
    Serial.flush();
    
    captureImage();
    
    // Handle web server during delay to prevent timeout
    if (i < count - 1) { // Don't delay after last capture
      unsigned long startTime = millis();
      while (millis() - startTime < intervalMs) {
        if (wifiConnected) {
          server.handleClient();
        }
        delay(10);
      }
    }
  }
  
  burstInProgress = false;
  burstCurrent = 0;
  burstTotal = 0;
  
  Serial.println("\n=== Burst Capture Complete ===");
  Serial.flush();
}

void handleBurstStatus() {
  String json = "{";
  json += "\"inProgress\":" + String(burstInProgress ? "true" : "false") + ",";
  json += "\"current\":" + String(burstCurrent) + ",";
  json += "\"total\":" + String(burstTotal);
  json += "}";
  server.send(200, "application/json", json);
}

void handleImage() {
  if (!server.hasArg("n")) {
    server.send(400, "text/plain", "Missing image number parameter");
    return;
  }
  
  int imageNum = server.arg("n").toInt();
  String filename = "/" + String(imageNum) + ".jpg";
  
  if (!sdCardPresent || !SD.exists(filename.c_str())) {
    server.send(404, "text/plain", "Image not found");
    return;
  }
  
  File file = SD.open(filename.c_str(), FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "Failed to open image");
    return;
  }
  
  server.streamFile(file, "image/jpeg");
  file.close();
}

void handleCapture() {
  // Send response immediately, then capture in background
  server.send(200, "text/plain", "Capturing image...");
  // Small delay to ensure response is sent before starting capture
  delay(10);
  captureImage();
}

void handleDelete() {
  deleteAllImages();
  server.send(200, "text/plain", "All images deleted");
}

void handleListJSON() {
  String json = "{\"images\":[";
  bool first = true;
  int count = 0;
  
  if (!sdCardPresent) {
    server.send(200, "application/json", "{\"images\":[]}");
    return;
  }
  
  for (int i = 1; i <= 10000; i++) {
    String filename = "/" + String(i) + ".jpg";
    if (SD.exists(filename.c_str())) {
      File file = SD.open(filename.c_str(), FILE_READ);
      size_t fileSize = 0;
      if (file) {
        fileSize = file.size();
        file.close();
      }
      String foundFilename = String(i) + ".jpg";
      if (!first) json += ",";
      json += "{\"number\":" + String(i) + ",\"filename\":\"" + foundFilename + "\",\"size\":" + String(fileSize) + "}";
      first = false;
      count++;
    } else if (count > 0) {
      break;
    }
  }
  
  json += "]}";
  server.send(200, "application/json", json);
}

void handleSetQuality() {
  if (server.hasArg("plain")) {
    int newQuality = server.arg("plain").toInt();
    if (newQuality >= 0 && newQuality <= 63) {
      currentQuality = newQuality;
      // Quality can be changed without full reinit - just update the sensor
      sensor_t *s = esp_camera_sensor_get();
      if (s) {
        // Quality is set during init, but we can note it for next capture
        // For immediate effect, we'd need to reinit, but that's slow
        // So we'll just update the variable and it will apply on next init
      }
      String json = "{\"status\":\"ok\",\"quality\":" + String(currentQuality) + "}";
      server.send(200, "application/json", json);
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid quality\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameter\"}");
  }
}

void handleSetResolution() {
  if (server.hasArg("plain")) {
    int res = server.arg("plain").toInt();
    framesize_t newSize;
    
    switch(res) {
      case 0: newSize = FRAMESIZE_QQVGA; break;
      case 1: newSize = FRAMESIZE_QCIF; break;
      case 2: newSize = FRAMESIZE_QVGA; break;
      case 3: newSize = FRAMESIZE_VGA; break;
      case 4: newSize = FRAMESIZE_SVGA; break;
      case 5: newSize = FRAMESIZE_XGA; break;
      case 6: newSize = FRAMESIZE_SXGA; break;
      case 7: newSize = FRAMESIZE_UXGA; break;
      default: 
        server.send(400, "application/json", "{\"status\":\"error\"}");
        return;
    }
    
    // Only reinit if resolution actually changed
    if (currentFrameSize != newSize) {
      currentFrameSize = newSize;
      Serial.println("Resolution changed - reinitializing camera...");
      Serial.flush();
      esp_camera_deinit();
      delay(100); // Brief delay before reinit
      initCamera();
    }
    String json = "{\"status\":\"ok\",\"resolution\":" + String(res) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(400, "application/json", "{\"status\":\"error\"}");
  }
}

void handleSetPixelFormat() {
  if (server.hasArg("plain")) {
    int format = server.arg("plain").toInt();
    pixformat_t newFormat;
    
    switch(format) {
      case 0: newFormat = PIXFORMAT_JPEG; break;
      case 1: newFormat = PIXFORMAT_GRAYSCALE; break;
      case 2: newFormat = PIXFORMAT_RGB565; break;
      default: 
        server.send(400, "application/json", "{\"status\":\"error\"}");
        return;
    }
    
    // Only reinit if format actually changed
    if (currentPixelFormat != newFormat) {
      currentPixelFormat = newFormat;
      Serial.println("Pixel format changed - reinitializing camera...");
      Serial.flush();
      esp_camera_deinit();
      delay(100); // Brief delay before reinit
      initCamera();
    }
    String json = "{\"status\":\"ok\",\"pixelFormat\":" + String(format) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(400, "application/json", "{\"status\":\"error\"}");
  }
}

void handleSetEndianness() {
  if (server.hasArg("plain")) {
    int endian = server.arg("plain").toInt();
    
    if (endian == 0 || endian == 1) {
      currentBigEndian = (endian == 1);
      String json = "{\"status\":\"ok\",\"endianness\":" + String(endian) + "}";
      server.send(200, "application/json", json);
    } else {
      server.send(400, "application/json", "{\"status\":\"error\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\"}");
  }
}

void handleGetSettings() {
  int resValue = 0;
  if (currentFrameSize == FRAMESIZE_QQVGA) resValue = 0;
  else if (currentFrameSize == FRAMESIZE_QCIF) resValue = 1;
  else if (currentFrameSize == FRAMESIZE_QVGA) resValue = 2;
  else if (currentFrameSize == FRAMESIZE_VGA) resValue = 3;
  else if (currentFrameSize == FRAMESIZE_SVGA) resValue = 4;
  else if (currentFrameSize == FRAMESIZE_XGA) resValue = 5;
  else if (currentFrameSize == FRAMESIZE_SXGA) resValue = 6;
  else if (currentFrameSize == FRAMESIZE_UXGA) resValue = 7;
  
  int pixelFormatValue = 0;
  if (currentPixelFormat == PIXFORMAT_JPEG) pixelFormatValue = 0;
  else if (currentPixelFormat == PIXFORMAT_GRAYSCALE) pixelFormatValue = 1;
  else if (currentPixelFormat == PIXFORMAT_RGB565) pixelFormatValue = 2;
  
  int endiannessValue = currentBigEndian ? 1 : 0;
  
  String json = "{\"quality\":" + String(currentQuality) + 
                ",\"resolution\":" + String(resValue) + 
                ",\"pixelFormat\":" + String(pixelFormatValue) + 
                ",\"endianness\":" + String(endiannessValue) + "}";
  server.send(200, "application/json", json);
}
