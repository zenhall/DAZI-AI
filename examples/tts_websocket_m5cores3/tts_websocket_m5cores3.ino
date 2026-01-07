/*
 * ============================================================================
 * M5CoreS3 MiniMax TTS WebSocket Streaming Example
 * ============================================================================
 * Features: Real-time text-to-speech using WebSocket streaming on M5CoreS3
 * - Lower latency compared to HTTP REST API
 * - Streaming audio playback as chunks arrive
 * - Interactive text input via Serial
 * - Uses M5CoreS3 built-in speaker (AW88298 codec)
 *
 * Hardware Requirements:
 * - M5CoreS3 development board
 *
 * Required Libraries:
 * - M5CoreS3: https://github.com/m5stack/M5CoreS3
 * - M5GFX: https://github.com/m5stack/M5GFX
 * - M5Unified: https://github.com/m5stack/M5Unified
 * - ArduinoTTSChat (this library)
 *
 * Based on tts_websocket.ino for ESP32+MAX98357
 * ============================================================================
 */

#include <M5CoreS3.h>
#include <WiFi.h>
#include <ArduinoTTSChat.h>

// ============================================================================
// Network and API Configuration
// ============================================================================

// WiFi settings - CHANGE THESE
const char* ssid     = "2nd-curv";
const char* password = "xbotpark";

// MiniMax API key (JWT token) - CHANGE THIS
// Get your API key from: https://www.minimaxi.com/
const char* minimax_apiKey = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJHcm91cE5hbWUiOiLmtbfonrrnlKjmiLdfNDU3Mjk3NjYxNDY0MTUwMDE4IiwiVXNlck5hbWUiOiLmtbfonrrnlKjmiLdfNDU3Mjk3NjYxNDY0MTUwMDE4IiwiQWNjb3VudCI6IiIsIlN1YmplY3RJRCI6IjIwMDA4Mjk4MTIxMTcwODIzOTkiLCJQaG9uZSI6IjE4ODUzODU1NTcwIiwiR3JvdXBJRCI6IjIwMDA4Mjk4MTIxMDg2OTM3OTEiLCJQYWdlTmFtZSI6IiIsIk1haWwiOiIiLCJDcmVhdGVUaW1lIjoiMjAyNS0xMi0xNiAyMToxMToxMCIsIlRva2VuVHlwZSI6MSwiaXNzIjoibWluaW1heCJ9.i--NHawJ1efq6hsFmXlQIiE5KMTB8nGv_Al2WbaGiFQo-_3rTlIVpxVhDgB0IXvRDPDW42ZGw7L30od8M_e5qoT7f6Tih0XGM9IBvFmf0dTZthxh3ZCJrtjmLarb9nBvETmP99rqQnfIIjMK1VcPuJPhNXe8glYT86V89sjmX858nUgbT8ftZcLoRc85x-2LxZhYDoT7lAurlLym92-YisL73HKtuiR9gkmYPCCN-1SgPVYhEnnFZ6gA--HvttOVKZPS0ACCUTq4o_-oHrA46G8qP298AYOPW3zMv4EEialjl3ThvOiBxaw2FAmmxf-FSgzJSmtOOwh_aW_6I2Gsqg";

// ============================================================================
// TTS Configuration
// ============================================================================

// Voice settings
const char* tts_voice_id = "male-qn-qingse";  // Voice ID
// Available voices:
// - male-qn-qingse: Young male voice
// - male-qn-daxuesheng: College student male voice
// - female-tianmei: Sweet female voice
// - female-tianmei-jingpin: Sweet female premium voice
// - presenter_male: Male presenter
// - presenter_female: Female presenter

const float tts_speed = 1.0;        // Speech speed [0.5-2.0]
const float tts_volume = 1.0;       // Volume [0.1-1.0]
const int tts_pitch = 0;            // Pitch [-12 to 12]
const int tts_sample_rate = 16000;  // Sample rate (lower = smaller data)
const int tts_bitrate = 32000;      // Bitrate (minimum)

// ============================================================================
// Global Objects
// ============================================================================

// TTS WebSocket client - created in setup() after M5CoreS3 init
ArduinoTTSChat* ttsChat = nullptr;

// Serial input buffer
String inputBuffer = "";

// Speaker volume (0-255)
int speakerVolume = 200;

// ============================================================================
// Audio Playback Callback for M5CoreS3
// ============================================================================

// Double buffer for M5CoreS3 playRaw (stores pointer, doesn't copy data)
static int16_t* audioBufferA = nullptr;
static int16_t* audioBufferB = nullptr;
static const size_t AUDIO_CHUNK_SIZE = 2048;  // samples (4KB of 16-bit audio)
static bool useBufferA = true;

/**
 * @brief Initialize audio buffers for M5CoreS3
 */
void initAudioBuffers() {
  if (psramFound()) {
    audioBufferA = (int16_t*)ps_malloc(AUDIO_CHUNK_SIZE * sizeof(int16_t));
    audioBufferB = (int16_t*)ps_malloc(AUDIO_CHUNK_SIZE * sizeof(int16_t));
    Serial.println("Using PSRAM for audio buffers");
  } else {
    audioBufferA = new int16_t[AUDIO_CHUNK_SIZE];
    audioBufferB = new int16_t[AUDIO_CHUNK_SIZE];
    Serial.println("Using heap for audio buffers");
  }
}

/**
 * @brief Callback function to play audio via M5CoreS3 speaker
 * @param data Audio data (16-bit signed PCM)
 * @param samples Number of samples
 * @param sampleRate Sample rate in Hz
 * @return true if playback started successfully
 */
bool playAudioCallback(const int16_t* data, size_t samples, uint32_t sampleRate) {
  // Wait if speaker queue is full (channel 0 has 2 slots)
  int waitCount = 0;
  while (CoreS3.Speaker.isPlaying(0) >= 2 && waitCount < 100) {
    delay(1);
    waitCount++;
  }

  if (waitCount >= 100) {
    return false;  // Timeout, retry later
  }

  // Select buffer (double buffering)
  int16_t* buffer = useBufferA ? audioBufferA : audioBufferB;
  useBufferA = !useBufferA;

  // Copy data to persistent buffer (playRaw stores pointer)
  size_t copySize = min(samples, AUDIO_CHUNK_SIZE);
  memcpy(buffer, data, copySize * sizeof(int16_t));

  // Play via M5CoreS3 speaker
  // playRaw: data, length, sampleRate, stereo, repeat, channel, stopCurrent
  return CoreS3.Speaker.playRaw(buffer, copySize, sampleRate, false, 1, 0, false);
}

// ============================================================================
// Callback Functions
// ============================================================================

void onTTSComplete() {
  Serial.println("\n[TTS] Playback completed");
  CoreS3.Display.fillRect(0, 200, 320, 40, TFT_BLACK);
  CoreS3.Display.setTextColor(TFT_GREEN);
  CoreS3.Display.drawString("Ready", 160, 210);
  Serial.println("\nEnter text to synthesize (or 'quit' to exit):");
  Serial.print("> ");
}

void onTTSError(const char* error) {
  Serial.printf("\n[TTS Error] %s\n", error);
  CoreS3.Display.fillRect(0, 200, 320, 40, TFT_BLACK);
  CoreS3.Display.setTextColor(TFT_RED);
  CoreS3.Display.drawString("Error!", 160, 210);
  Serial.println("\nEnter text to synthesize (or 'quit' to exit):");
  Serial.print("> ");
}

// ============================================================================
// Display Functions
// ============================================================================

void displayInit() {
  CoreS3.Display.setRotation(1);
  CoreS3.Display.fillScreen(TFT_BLACK);
  CoreS3.Display.setTextDatum(MC_DATUM);
  CoreS3.Display.setFont(&fonts::FreeSansBold12pt7b);
  CoreS3.Display.setTextColor(TFT_WHITE);
  CoreS3.Display.drawString("MiniMax TTS", 160, 30);
  CoreS3.Display.setFont(&fonts::FreeSans9pt7b);
  CoreS3.Display.drawString("M5CoreS3 WebSocket Demo", 160, 60);
}

void displayStatus(const char* status) {
  CoreS3.Display.fillRect(0, 200, 320, 40, TFT_BLACK);
  CoreS3.Display.setTextColor(TFT_YELLOW);
  CoreS3.Display.drawString(status, 160, 210);
}

void displayVolume() {
  CoreS3.Display.fillRect(0, 160, 320, 30, TFT_BLACK);
  CoreS3.Display.setTextColor(TFT_CYAN);
  char volStr[32];
  snprintf(volStr, sizeof(volStr), "Volume: %d", speakerVolume);
  CoreS3.Display.drawString(volStr, 160, 175);
}

// ============================================================================
// Setup Function
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n========================================");
  Serial.println("  MiniMax TTS WebSocket - M5CoreS3");
  Serial.println("========================================");

  // ========== Initialize M5CoreS3 ==========
  auto cfg = M5.config();
  CoreS3.begin(cfg);

  // Initialize display
  displayInit();
  displayStatus("Initializing...");

  // ========== Configure M5CoreS3 Speaker ==========
  CoreS3.Speaker.setVolume(speakerVolume);
  CoreS3.Speaker.begin();
  displayVolume();

  // Initialize audio buffers for streaming
  initAudioBuffers();

  if (!CoreS3.Speaker.isEnabled()) {
    Serial.println("Speaker not available!");
    displayStatus("Speaker Error!");
    while (1) delay(1000);
  }

  // Play startup tone
  CoreS3.Speaker.tone(1000, 100);
  delay(150);
  CoreS3.Speaker.tone(1500, 100);
  delay(150);

  // Initialize random seed
  randomSeed(analogRead(0) + millis());

  // ========== WiFi Connection ==========
  displayStatus("Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 30) {
    Serial.print(".");
    delay(500);
    attempt++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    displayStatus("WiFi Failed!");
    while (1) delay(1000);
  }

  Serial.println(" Connected!");
  Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  displayStatus("WiFi Connected");

  // ========== Create TTS object (after M5CoreS3 init for proper PSRAM access) ==========
  ttsChat = new ArduinoTTSChat(minimax_apiKey);

  // ========== Configure TTS (MUST be done BEFORE speaker init) ==========
  ttsChat->setVoiceId(tts_voice_id);
  ttsChat->setSpeed(tts_speed);
  ttsChat->setVolume(tts_volume);
  ttsChat->setPitch(tts_pitch);
  ttsChat->setAudioParams(tts_sample_rate, tts_bitrate);

  // ========== Initialize M5CoreS3 Speaker Mode ==========
  Serial.println("\nInitializing M5CoreS3 speaker mode...");
  displayStatus("Init Speaker...");

  if (!ttsChat->initM5CoreS3Speaker()) {
    Serial.println("TTS speaker initialization failed!");
    displayStatus("TTS Init Failed!");
    while (1) delay(1000);
  }

  // Set the audio playback callback
  ttsChat->setAudioPlayCallback(playAudioCallback);

  // Set other callbacks
  ttsChat->setCompletionCallback(onTTSComplete);
  ttsChat->setErrorCallback(onTTSError);

  Serial.printf("TTS Config: voice=%s, speed=%.1f, sample_rate=%d\n",
                tts_voice_id, tts_speed, tts_sample_rate);

  // ========== Connect to MiniMax WebSocket ==========
  Serial.println("\nConnecting to MiniMax TTS WebSocket...");
  displayStatus("Connecting TTS...");

  if (!ttsChat->connectWebSocket()) {
    Serial.println("WebSocket connection failed!");
    displayStatus("WS Connect Failed!");
    while (1) delay(1000);
  }

  Serial.println("\n========================================");
  Serial.println("  Ready for TTS!");
  Serial.println("========================================");
  Serial.println("\nControls:");
  Serial.println("- Touch screen: Speak 'Hello M5CoreS3'");
  Serial.println("- Serial: Type text and press Enter");
  Serial.println("\nEnter text to synthesize (or 'quit' to exit):");
  Serial.print("> ");

  displayStatus("Ready");
  CoreS3.Display.setTextColor(TFT_GREEN);
  CoreS3.Display.fillRect(0, 200, 320, 40, TFT_BLACK);
  CoreS3.Display.drawString("Ready - Touch to speak", 160, 210);
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  // Update M5CoreS3
  CoreS3.update();

  // Process TTS (handles WebSocket messages and audio playback)
  ttsChat->loop();

  // Check for touch input
  if (CoreS3.Touch.getCount() > 0) {
    auto detail = CoreS3.Touch.getDetail(0);
    if (detail.wasPressed() && !ttsChat->isPlaying()) {
      Serial.println("\nTouch detected - Speaking demo text...");
      displayStatus("Speaking...");
      ttsChat->speak("Hello, I am M5CoreS3, nice to meet you!");
    }
  }

  // Check for Serial input
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        // Process the input
        inputBuffer.trim();

        if (inputBuffer.equalsIgnoreCase("quit") ||
            inputBuffer.equalsIgnoreCase("exit") ||
            inputBuffer.equalsIgnoreCase("q")) {
          Serial.println("\nExiting...");
          ttsChat->disconnectWebSocket();
          displayStatus("Disconnected");
          Serial.println("Goodbye!");
          while (1) delay(1000);
        }

        // Volume control commands
        if (inputBuffer.startsWith("vol ") || inputBuffer.startsWith("VOL ")) {
          int vol = inputBuffer.substring(4).toInt();
          if (vol >= 0 && vol <= 255) {
            speakerVolume = vol;
            CoreS3.Speaker.setVolume(speakerVolume);
            displayVolume();
            Serial.printf("Volume set to %d\n", speakerVolume);
          }
          inputBuffer = "";
          Serial.print("> ");
          continue;
        }

        if (ttsChat->isPlaying()) {
          Serial.println("\nStill playing, please wait...");
        } else {
          Serial.printf("\nSynthesizing: %s\n", inputBuffer.c_str());
          displayStatus("Speaking...");

          // Display text on screen
          CoreS3.Display.fillRect(0, 90, 320, 60, TFT_BLACK);
          CoreS3.Display.setTextColor(TFT_WHITE);
          CoreS3.Display.drawString(inputBuffer.substring(0, 30).c_str(), 160, 110);

          if (!ttsChat->speak(inputBuffer.c_str())) {
            Serial.println("Failed to start TTS synthesis");
            displayStatus("TTS Failed!");
            Serial.println("\nEnter text to synthesize:");
            Serial.print("> ");
          }
        }

        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
      Serial.print(c);  // Echo character
    }
  }

  // Small delay to prevent CPU hogging
  delay(1);
}
