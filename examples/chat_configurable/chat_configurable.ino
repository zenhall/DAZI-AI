/*
 * ============================================================================
 * ESP32 Configurable Voice Assistant System
 * ============================================================================
 * Features: Choose between Free or Pro TTS via serial JSON config
 * - Free version (subscription="free"): Uses OpenAI TTS
 * - Pro version (subscription="pro"): Uses MiniMax TTS
 *
 * Configuration Method:
 * 1. After power-on, send JSON config via serial before pressing BOOT button
 * 2. After config is complete, press BOOT button to start voice assistant
 *
 * Free version config example:
 * {"wifi_ssid":"YourWiFi","wifi_password":"YourPassword","subscription":"free","asr_api_key":"your-asr-key","asr_cluster":"volcengine_input_en","openai_apiKey":"your-openai-key","openai_apiBaseUrl":"https://api.openai.com","system_prompt":"You are a helpful assistant."}
 *
 * Pro version config example:
 * {"wifi_ssid":"YourWiFi","wifi_password":"YourPassword","subscription":"pro","asr_api_key":"your-asr-key","asr_cluster":"volcengine_input_en","openai_apiKey":"your-openai-key","openai_apiBaseUrl":"https://api.openai.com","system_prompt":"You are a helpful assistant.","minimax_apiKey":"your-minimax-key","minimax_groupId":"your-group-id","tts_voice_id":"female-tianmei"}
 *
 * Hardware Requirements:
 * - ESP32 development board
 * - INMP441 MEMS microphone module
 * - I2S audio output device (speaker/amplifier)
 * ============================================================================
 */

#include <WiFi.h>
#include <ArduinoASRChat.h>
#include <ArduinoGPTChat.h>
#include <ArduinoMinimaxTTS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "Audio.h"

// ============================================================================
// Hardware Pin Definitions
// ============================================================================

// I2S audio output pins (for TTS speech playback)
#define I2S_DOUT 47  // Data output pin
#define I2S_BCLK 48  // Bit clock pin
#define I2S_LRC 45   // Left/Right channel clock pin

// INMP441 microphone input pin definitions
#define I2S_MIC_SERIAL_CLOCK 5      // SCK - Serial clock
#define I2S_MIC_LEFT_RIGHT_CLOCK 4  // WS - Left/Right channel clock
#define I2S_MIC_SERIAL_DATA 6       // SD - Serial data

// BOOT button pin
#define BOOT_BUTTON_PIN 0

// Recording sample rate
#define SAMPLE_RATE 16000

// ============================================================================
// Configuration Variables
// ============================================================================

// WiFi configuration
String wifi_ssid = "";
String wifi_password = "";

// Subscription type: "free" or "pro"
String subscription = "free";

// ASR configuration
String asr_api_key = "";
String asr_cluster = "volcengine_input_en";

// OpenAI configuration
String openai_apiKey = "";
String openai_apiBaseUrl = "";

// System prompt
String system_prompt = "You are a helpful AI assistant.";

// MiniMax TTS configuration (Pro version only)
String minimax_apiKey = "";
String minimax_groupId = "";
String tts_voice_id = "female-tianmei";
float tts_speed = 1.0;
float tts_volume = 1.0;
String tts_model = "speech-2.6-hd";
String tts_audio_format = "mp3";
int tts_sample_rate = 32000;
int tts_bitrate = 128000;

// Configuration status
bool configReceived = false;
bool systemInitialized = false;

// ============================================================================
// Global Object Instances
// ============================================================================

Audio audio;
ArduinoASRChat* asrChat = nullptr;
ArduinoGPTChat* gptChat = nullptr;
ArduinoMinimaxTTS* minimaxTTS = nullptr;
Preferences preferences;

// ============================================================================
// State Machine Definition
// ============================================================================

enum ConversationState {
  STATE_WAITING_CONFIG,    // Waiting for config state
  STATE_IDLE,              // Idle state
  STATE_LISTENING,         // Listening state
  STATE_PROCESSING_LLM,    // Processing state
  STATE_PLAYING_TTS,       // Playing state
  STATE_WAIT_TTS_COMPLETE  // Waiting for playback completion state
};

// ============================================================================
// State Variables
// ============================================================================

ConversationState currentState = STATE_WAITING_CONFIG;
bool continuousMode = false;
bool buttonPressed = false;
bool wasButtonPressed = false;
unsigned long ttsStartTime = 0;
unsigned long ttsCheckTime = 0;

// ============================================================================
// Flash Storage Functions
// ============================================================================

/**
 * Save configuration to Flash
 */
bool saveConfigToFlash() {
  preferences.begin("voice_config", false);
  
  preferences.putString("wifi_ssid", wifi_ssid);
  preferences.putString("wifi_pass", wifi_password);
  preferences.putString("subscription", subscription);
  preferences.putString("asr_key", asr_api_key);
  preferences.putString("asr_cluster", asr_cluster);
  preferences.putString("openai_key", openai_apiKey);
  preferences.putString("openai_url", openai_apiBaseUrl);
  preferences.putString("sys_prompt", system_prompt);
  
  if (subscription == "pro") {
    preferences.putString("minimax_key", minimax_apiKey);
    preferences.putString("minimax_gid", minimax_groupId);
    preferences.putString("tts_voice", tts_voice_id);
    preferences.putFloat("tts_speed", tts_speed);
    preferences.putFloat("tts_volume", tts_volume);
    preferences.putString("tts_model", tts_model);
    preferences.putString("tts_format", tts_audio_format);
    preferences.putInt("tts_sample", tts_sample_rate);
    preferences.putInt("tts_bitrate", tts_bitrate);
  }
  
  preferences.putBool("configured", true);
  preferences.end();
  
  Serial.println("[Flash] Config saved to Flash");
  return true;
}

/**
 * Load configuration from Flash
 */
bool loadConfigFromFlash() {
  preferences.begin("voice_config", true);
  
  if (!preferences.getBool("configured", false)) {
    preferences.end();
    Serial.println("[Flash] No config found in Flash");
    return false;
  }
  
  wifi_ssid = preferences.getString("wifi_ssid", "");
  wifi_password = preferences.getString("wifi_pass", "");
  subscription = preferences.getString("subscription", "free");
  asr_api_key = preferences.getString("asr_key", "");
  asr_cluster = preferences.getString("asr_cluster", "volcengine_input_en");
  openai_apiKey = preferences.getString("openai_key", "");
  openai_apiBaseUrl = preferences.getString("openai_url", "");
  system_prompt = preferences.getString("sys_prompt", "You are a helpful AI assistant.");
  
  if (subscription == "pro") {
    minimax_apiKey = preferences.getString("minimax_key", "");
    minimax_groupId = preferences.getString("minimax_gid", "");
    tts_voice_id = preferences.getString("tts_voice", "female-tianmei");
    tts_speed = preferences.getFloat("tts_speed", 1.0);
    tts_volume = preferences.getFloat("tts_volume", 1.0);
    tts_model = preferences.getString("tts_model", "speech-2.6-hd");
    tts_audio_format = preferences.getString("tts_format", "mp3");
    tts_sample_rate = preferences.getInt("tts_sample", 32000);
    tts_bitrate = preferences.getInt("tts_bitrate", 128000);
  }
  
  preferences.end();
  
  Serial.println("[Flash] Config loaded from Flash");
  Serial.println("Subscription: " + subscription);
  Serial.println("WiFi SSID: " + wifi_ssid);
  
  return true;
}

/**
 * Clear configuration in Flash
 */
void clearFlashConfig() {
  preferences.begin("voice_config", false);
  preferences.clear();
  preferences.end();
  Serial.println("[Flash] Flash config cleared");
}

// ============================================================================
// Configuration Reception Functions
// ============================================================================

/**
 * Receive JSON configuration from serial port
 * Uses buffering to handle fragmented serial data
 */
bool receiveConfig() {
  static String jsonBuffer = "";
  static unsigned long lastReceiveTime = 0;
  static bool receiving = false;
  
  // Check if we should start looking for data
  if (!receiving && Serial.available() > 0) {
    // Peek at first character to see if it's a '{'
    char first = Serial.peek();
    if (first != '{') {
      // Clear any junk before the JSON starts
      while (Serial.available() > 0 && Serial.peek() != '{') {
        Serial.read();
      }
    }
  }
  
  // Read all available data
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    // Start receiving when we see opening brace
    if (c == '{' && !receiving) {
      jsonBuffer = "{";
      receiving = true;
      lastReceiveTime = millis();
      continue;
    }
    
    // Only accumulate if we're receiving
    if (receiving) {
      jsonBuffer += c;
      lastReceiveTime = millis();
      
      // If we receive closing brace, try to parse
      if (c == '}') {
        // Wait longer for any remaining data in buffer
        delay(200);
        
        // Read any remaining data until newline or no more data
        int noDataCount = 0;
        while (noDataCount < 10) {  // Wait for up to 10 cycles of no data
          if (Serial.available() > 0) {
            char extra = Serial.read();
            if (extra == '\n' || extra == '\r') {
              break; // Stop at newline
            }
            jsonBuffer += extra;
            noDataCount = 0;  // Reset counter when data received
            delay(2);
          } else {
            delay(5);
            noDataCount++;
          }
        }
        
        jsonBuffer.trim();
        
        // Check if we have a complete JSON
        if (jsonBuffer.startsWith("{") && jsonBuffer.endsWith("}")) {
          // Parse JSON with larger buffer
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, jsonBuffer);
          
          if (error) {
            Serial.print("[Error] JSON parse failed: ");
            Serial.println(error.c_str());
            jsonBuffer = "";
            receiving = false;
            return false;
          }
          
          // Clear buffer
          jsonBuffer = "";
          receiving = false;
          
          // Extract configuration
          if (doc.containsKey("wifi_ssid")) {
            wifi_ssid = doc["wifi_ssid"].as<String>();
          }
          if (doc.containsKey("wifi_password")) {
            wifi_password = doc["wifi_password"].as<String>();
          }
          if (doc.containsKey("subscription")) {
            subscription = doc["subscription"].as<String>();
          }
          if (doc.containsKey("asr_api_key")) {
            asr_api_key = doc["asr_api_key"].as<String>();
          }
          if (doc.containsKey("asr_cluster")) {
            asr_cluster = doc["asr_cluster"].as<String>();
          }
          if (doc.containsKey("openai_apiKey")) {
            openai_apiKey = doc["openai_apiKey"].as<String>();
          }
          if (doc.containsKey("openai_apiBaseUrl")) {
            openai_apiBaseUrl = doc["openai_apiBaseUrl"].as<String>();
          }
          if (doc.containsKey("system_prompt")) {
            system_prompt = doc["system_prompt"].as<String>();
          }
          
          // Pro version additional configuration
          if (subscription == "pro") {
            if (doc.containsKey("minimax_apiKey")) {
              minimax_apiKey = doc["minimax_apiKey"].as<String>();
            }
            if (doc.containsKey("minimax_groupId")) {
              minimax_groupId = doc["minimax_groupId"].as<String>();
            }
            if (doc.containsKey("tts_voice_id")) {
              tts_voice_id = doc["tts_voice_id"].as<String>();
            }
            if (doc.containsKey("tts_speed")) {
              tts_speed = doc["tts_speed"].as<float>();
            }
            if (doc.containsKey("tts_volume")) {
              tts_volume = doc["tts_volume"].as<float>();
            }
            if (doc.containsKey("tts_model")) {
              tts_model = doc["tts_model"].as<String>();
            }
            if (doc.containsKey("tts_audio_format")) {
              tts_audio_format = doc["tts_audio_format"].as<String>();
            }
            if (doc.containsKey("tts_sample_rate")) {
              tts_sample_rate = doc["tts_sample_rate"].as<int>();
            }
            if (doc.containsKey("tts_bitrate")) {
              tts_bitrate = doc["tts_bitrate"].as<int>();
            }
          }
          
          // Validate required configuration
          if (wifi_ssid.length() == 0 || wifi_password.length() == 0 ||
              asr_api_key.length() == 0 || openai_apiKey.length() == 0) {
            Serial.println("Error: Missing required config");
            return false;
          }
          
          if (subscription == "pro" && (minimax_apiKey.length() == 0 || minimax_groupId.length() == 0)) {
            Serial.println("Error: Pro version missing MiniMax config");
            return false;
          }
          
          Serial.println("\n[Config] Configuration received successfully!");
          Serial.println("[Config] Mode: " + subscription);
          
          return true;
        }
      }
    }
  }
  
  // Timeout check - if no data received for 3 seconds, clear buffer
  if (receiving && jsonBuffer.length() > 0 && (millis() - lastReceiveTime > 3000)) {
    Serial.println("[Warning] Config receive timeout, buffer cleared");
    jsonBuffer = "";
    receiving = false;
  }
  
  return false;
}

// ============================================================================
// System Initialization Function
// ============================================================================

bool initializeSystem() {
  Serial.println("\n----- System Initialization -----");
  
  // ========== WiFi Connection ==========
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  Serial.println("Connecting to WiFi...");
  
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    Serial.print('.');
    delay(1000);
    attempt++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    return false;
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  
  // ========== Audio Output Initialization ==========
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(20);
  Serial.println("Audio player initialized");
  
  // ========== ASR Initialization ==========
  asrChat = new ArduinoASRChat(asr_api_key.c_str(), asr_cluster.c_str());
  
  if (!asrChat->initINMP441Microphone(I2S_MIC_SERIAL_CLOCK, I2S_MIC_LEFT_RIGHT_CLOCK, I2S_MIC_SERIAL_DATA)) {
    Serial.println("Microphone init failed!");
    return false;
  }
  
  asrChat->setAudioParams(SAMPLE_RATE, 16, 1);
  asrChat->setSilenceDuration(1000);
  asrChat->setMaxRecordingSeconds(50);
  
  asrChat->setTimeoutNoSpeechCallback([]() {
    if (continuousMode) {
      stopContinuousMode();
    }
  });
  
  if (!asrChat->connectWebSocket()) {
    Serial.println("ASR service connection failed!");
    return false;
  }
  
  Serial.println("ASR initialized");
  
  // ========== GPT Chat Initialization ==========
  gptChat = new ArduinoGPTChat(openai_apiKey.c_str(), openai_apiBaseUrl.c_str());
  gptChat->setSystemPrompt(system_prompt.c_str());
  gptChat->enableMemory(true);
  Serial.println("GPT Chat initialized");
  Serial.println("Conversation Memory: Enabled");
  
  // ========== TTS Initialization (Based on subscription type) ==========
  if (subscription == "pro") {
    // Pro version: Use MiniMax TTS
    minimaxTTS = new ArduinoMinimaxTTS(minimax_apiKey.c_str(), minimax_groupId.c_str(), &audio);
    minimaxTTS->setVoiceId(tts_voice_id.c_str());
    minimaxTTS->setSpeed(tts_speed);
    minimaxTTS->setVolume(tts_volume);
    minimaxTTS->setModel(tts_model.c_str());
    minimaxTTS->setAudioFormat(tts_audio_format.c_str());
    minimaxTTS->setSampleRate(tts_sample_rate);
    minimaxTTS->setBitrate(tts_bitrate);
    
    Serial.println("TTS Mode: MiniMax (Pro)");
    Serial.printf("Config: Voice=%s, Format=%s, SampleRate=%d\n",
                  tts_voice_id.c_str(), tts_audio_format.c_str(), tts_sample_rate);
  } else {
    // Free version: Use OpenAI TTS
    Serial.println("TTS Mode: OpenAI (Free)");
  }
  
  Serial.println("\n----- System Ready -----");
  Serial.println("Press BOOT button to start/stop conversation");
  
  return true;
}

// ============================================================================
// Continuous Conversation Mode Control Functions
// ============================================================================

void startContinuousMode() {
  continuousMode = true;
  currentState = STATE_LISTENING;
  
  Serial.println("\n========================================");
  Serial.println("  Continuous Conversation Mode Started");
  Serial.println("  Press BOOT again to stop");
  Serial.println("========================================");
  
  if (asrChat->startRecording()) {
    Serial.println("\n[ASR] Listening... Please speak");
  } else {
    Serial.println("\n[Error] ASR startup failed");
    continuousMode = false;
    currentState = STATE_IDLE;
  }
}

void stopContinuousMode() {
  continuousMode = false;
  
  Serial.println("\n========================================");
  Serial.println("  Continuous Conversation Mode Stopped");
  Serial.println("========================================");
  
  if (asrChat->isRecording()) {
    asrChat->stopRecording();
  }
  
  currentState = STATE_IDLE;
  Serial.println("\nPress BOOT button to start conversation");
}

// ============================================================================
// ASR Result Processing Function
// ============================================================================

void handleASRResult() {
  String transcribedText = asrChat->getRecognizedText();
  asrChat->clearResult();
  
  if (transcribedText.length() > 0) {
    // ========== Display Recognition Result ==========
    Serial.println("\n=== ASR Recognition Result ===");
    Serial.printf("%s\n", transcribedText.c_str());
    Serial.println("==============================");
    
    // ========== Send to ChatGPT ==========
    currentState = STATE_PROCESSING_LLM;
    Serial.println("\n[LLM] Sending to ChatGPT...");
    
    String response = gptChat->sendMessage(transcribedText);
    
    if (response != "" && response.length() > 0) {
      // ========== Display ChatGPT Response ==========
      Serial.println("\n=== ChatGPT Response ===");
      Serial.printf("%s\n", response.c_str());
      Serial.println("========================");
      
      // ========== Convert to Speech and Play ==========
      currentState = STATE_PLAYING_TTS;
      bool success = false;
      
      if (subscription == "pro") {
        // Pro: Use MiniMax TTS
        Serial.println("\n[MiniMax TTS] Converting to speech...");
        success = minimaxTTS->synthesizeAndPlay(response);
      } else {
        // Free: Use OpenAI TTS
        Serial.println("\n[OpenAI TTS] Converting to speech...");
        success = gptChat->textToSpeech(response);
      }
      
      if (success) {
        currentState = STATE_WAIT_TTS_COMPLETE;
        ttsStartTime = millis();
        ttsCheckTime = millis();
      } else {
        Serial.println("[Error] TTS playback failed");
        
        if (continuousMode) {
          delay(500);
          currentState = STATE_LISTENING;
          if (asrChat->startRecording()) {
            Serial.println("\n[ASR] Listening... Please speak");
          } else {
            stopContinuousMode();
          }
        } else {
          currentState = STATE_IDLE;
        }
      }
    } else {
      Serial.println("[Error] Failed to get ChatGPT response");
      
      if (continuousMode) {
        delay(500);
        currentState = STATE_LISTENING;
        if (asrChat->startRecording()) {
          Serial.println("\n[ASR] Listening... Please speak");
        } else {
          stopContinuousMode();
        }
      } else {
        currentState = STATE_IDLE;
      }
    }
  } else {
    Serial.println("[Warning] No text recognized");
    
    if (continuousMode) {
      delay(500);
      currentState = STATE_LISTENING;
      if (asrChat->startRecording()) {
        Serial.println("\n[ASR] Listening... Please speak");
      } else {
        stopContinuousMode();
      }
    } else {
      currentState = STATE_IDLE;
    }
  }
}

// ============================================================================
// Initialization Function
// ============================================================================

void setup() {
  // Increase serial RX buffer to handle long JSON strings
  Serial.setRxBufferSize(2048);  // Increase from default 256 to 2048 bytes
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n====================================================");
  Serial.println("   ESP32 Configurable Voice Assistant System");
  Serial.println("====================================================");
  
  // Initialize BOOT button
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize random seed
  randomSeed(analogRead(0) + millis());
  
  // Try to load config from Flash
  Serial.println("\n[Startup] Checking Flash for config...");
  if (loadConfigFromFlash()) {
    configReceived = true;
    Serial.println("[Startup] Using config from Flash");
    Serial.println("\nTip: Send new JSON config via serial before pressing BOOT to update");
  } else {
    Serial.println("[Startup] No config in Flash, waiting for serial config...");
    Serial.println("\nFree version config example:");
    Serial.println("{\"wifi_ssid\":\"YourWiFi\",\"wifi_password\":\"YourPassword\",\"subscription\":\"free\",\"asr_api_key\":\"your-key\",\"asr_cluster\":\"volcengine_input_en\",\"openai_apiKey\":\"your-key\",\"openai_apiBaseUrl\":\"https://api.openai.com\",\"system_prompt\":\"You are a helpful assistant.\"}");
    Serial.println("\nPro version config example:");
    Serial.println("{\"wifi_ssid\":\"YourWiFi\",\"wifi_password\":\"YourPassword\",\"subscription\":\"pro\",\"asr_api_key\":\"your-key\",\"asr_cluster\":\"volcengine_input_en\",\"openai_apiKey\":\"your-key\",\"openai_apiBaseUrl\":\"https://api.openai.com\",\"system_prompt\":\"You are a helpful assistant.\",\"minimax_apiKey\":\"your-key\",\"minimax_groupId\":\"your-id\",\"tts_voice_id\":\"female-tianmei\"}");
  }
  
  Serial.println("\nPress BOOT button to start system...\n");
}

// ============================================================================
// Main Loop Function
// ============================================================================

void loop() {
  // ========== Waiting for Config State ==========
  if (currentState == STATE_WAITING_CONFIG) {
    // Check for new serial config
    if (receiveConfig()) {
      configReceived = true;
      Serial.println("\nNew config received!");
      Serial.println("Press BOOT to test and save new config...");
    }
    
    // Detect BOOT button, initialize system and start directly
    buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    if (buttonPressed && !wasButtonPressed && configReceived && !systemInitialized) {
      wasButtonPressed = true;
      
      Serial.println("\n[Startup] Initializing system...");
      if (initializeSystem()) {
        systemInitialized = true;
        
        // Init successful, save config to Flash
        saveConfigToFlash();
        
        // Start conversation mode directly
        delay(1000);
        startContinuousMode();
      } else {
        Serial.println("\n[Error] System init failed, check config");
        Serial.println("Config not saved to Flash, please fix and retry");
        currentState = STATE_WAITING_CONFIG;
        configReceived = false;
      }
    } else if (!buttonPressed && wasButtonPressed) {
      wasButtonPressed = false;
    }
    
    delay(100);
    return;
  }
  
  // ========== Process Audio Loop ==========
  audio.loop();
  
  // ========== Process ASR Loop ==========
  if (asrChat != nullptr) {
    asrChat->loop();
  }
  
  // ========== Process BOOT Button (Stop only) ==========
  buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  
  if (buttonPressed && !wasButtonPressed) {
    wasButtonPressed = true;
    
    // Only allow stopping continuous mode
    if (continuousMode) {
      stopContinuousMode();
    }
  } else if (!buttonPressed && wasButtonPressed) {
    wasButtonPressed = false;
  }
  
  // ========== State Machine Processing ==========
  switch (currentState) {
    case STATE_IDLE:
      break;
      
    case STATE_LISTENING:
      if (asrChat->hasNewResult()) {
        handleASRResult();
      }
      break;
      
    case STATE_PROCESSING_LLM:
      break;
      
    case STATE_PLAYING_TTS:
      break;
      
    case STATE_WAIT_TTS_COMPLETE:
      if (millis() - ttsCheckTime > 100) {
        ttsCheckTime = millis();
        
        if (!audio.isRunning()) {
          Serial.println("[TTS] Playback completed");
          
          if (continuousMode) {
            delay(500);
            currentState = STATE_LISTENING;
            
            if (asrChat->startRecording()) {
              Serial.println("\n[ASR] Listening... Please speak");
            } else {
              Serial.println("[Error] ASR restart failed");
              stopContinuousMode();
            }
          } else {
            currentState = STATE_IDLE;
          }
        } else {
          // Check timeout
          if (millis() - ttsStartTime > 60000) {
            Serial.println("[Warning] TTS timeout, forcing restart");
            
            if (continuousMode) {
              currentState = STATE_LISTENING;
              if (asrChat->startRecording()) {
                Serial.println("\n[ASR] Listening... Please speak");
              } else {
                stopContinuousMode();
              }
            } else {
              currentState = STATE_IDLE;
            }
          }
        }
      }
      break;
      
    case STATE_WAITING_CONFIG:
      // Already handled above
      break;
  }
  
  // ========== Loop Delay Control ==========
  if (currentState == STATE_LISTENING) {
    yield();
  } else {
    delay(10);
  }
}