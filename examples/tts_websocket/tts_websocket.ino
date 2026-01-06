/*
 * ============================================================================
 * ESP32 MiniMax TTS WebSocket Streaming Example
 * ============================================================================
 * Features: Real-time text-to-speech using WebSocket streaming
 * - Lower latency compared to HTTP REST API
 * - Streaming audio playback as chunks arrive
 * - Interactive text input via Serial
 *
 * Hardware Requirements:
 * - ESP32 development board
 * - MAX98357 I2S amplifier or similar I2S speaker
 *
 * Based on Python reference: tts_websocket_interactive.py
 * ============================================================================
 */

#include <WiFi.h>
#include <ArduinoTTSChat.h>

// ============================================================================
// Hardware Pin Definitions
// ============================================================================

// I2S audio output pins (for MAX98357 amplifier)
#define I2S_BCLK 48   // Bit clock pin
#define I2S_LRC  45   // LR clock (word select) pin
#define I2S_DOUT 47   // Data output pin

// ============================================================================
// Network and API Configuration
// ============================================================================

// WiFi settings - CHANGE THESE
const char* ssid     = "";
const char* password = "";

// MiniMax API key (JWT token) - CHANGE THIS
// Get your API key from: https://www.minimaxi.com/
const char* minimax_apiKey = "";

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

// TTS WebSocket client
ArduinoTTSChat ttsChat(minimax_apiKey);

// Serial input buffer
String inputBuffer = "";

// ============================================================================
// Callback Functions
// ============================================================================

void onTTSComplete() {
  Serial.println("\n[TTS] Playback completed");
  Serial.println("\nEnter text to synthesize (or 'quit' to exit):");
  Serial.print("> ");
}

void onTTSError(const char* error) {
  Serial.printf("\n[TTS Error] %s\n", error);
  Serial.println("\nEnter text to synthesize (or 'quit' to exit):");
  Serial.print("> ");
}

// ============================================================================
// Setup Function
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n========================================");
  Serial.println("  MiniMax TTS WebSocket Streaming Demo");
  Serial.println("========================================");

  // Initialize random seed
  randomSeed(analogRead(0) + millis());

  // ========== WiFi Connection ==========
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
    Serial.println("Please check credentials and restart.");
    while (1) delay(1000);
  }

  Serial.println(" Connected!");
  Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  // ========== Configure TTS (MUST be done BEFORE speaker init) ==========
  ttsChat.setVoiceId(tts_voice_id);
  ttsChat.setSpeed(tts_speed);
  ttsChat.setVolume(tts_volume);
  ttsChat.setPitch(tts_pitch);
  ttsChat.setAudioParams(tts_sample_rate, tts_bitrate);  // Set sample rate first!

  // ========== Initialize Speaker ==========
  Serial.println("\nInitializing speaker...");
  if (!ttsChat.initMAX98357Speaker(I2S_BCLK, I2S_LRC, I2S_DOUT)) {
    Serial.println("Speaker initialization failed!");
    while (1) delay(1000);
  }

  // Set callbacks
  ttsChat.setCompletionCallback(onTTSComplete);
  ttsChat.setErrorCallback(onTTSError);

  Serial.printf("TTS Config: voice=%s, speed=%.1f, sample_rate=%d\n",
                tts_voice_id, tts_speed, tts_sample_rate);

  // ========== Connect to MiniMax WebSocket ==========
  Serial.println("\nConnecting to MiniMax TTS WebSocket...");
  if (!ttsChat.connectWebSocket()) {
    Serial.println("WebSocket connection failed!");
    Serial.println("Please check API key and try again.");
    while (1) delay(1000);
  }

  Serial.println("\n========================================");
  Serial.println("  Ready for TTS!");
  Serial.println("========================================");
  Serial.println("\nEnter text to synthesize (or 'quit' to exit):");
  Serial.print("> ");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  // Process TTS (handles WebSocket messages and audio playback)
  ttsChat.loop();

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
          ttsChat.disconnectWebSocket();
          Serial.println("Goodbye!");
          while (1) delay(1000);
        }

        if (ttsChat.isPlaying()) {
          Serial.println("\nStill playing, please wait...");
        } else {
          Serial.printf("\nSynthesizing: %s\n", inputBuffer.c_str());

          if (!ttsChat.speak(inputBuffer.c_str())) {
            Serial.println("Failed to start TTS synthesis");
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
