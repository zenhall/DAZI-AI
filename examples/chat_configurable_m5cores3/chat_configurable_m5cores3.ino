/*
 * ============================================================================
 * M5CoreS3 Configurable Voice Assistant System
 * ============================================================================
 * Features: Voice assistant using MiniMax TTS (Pro version only)
 * - Uses M5CoreS3 built-in microphone and speaker
 * - Touch screen for interaction
 * - LCD display for status
 *
 * Configuration Method:
 * 1. After power-on, send JSON config via serial before touching screen
 * 2. After config is complete, touch screen to start voice assistant
 *
 * Config example:
 * {"wifi_ssid":"YourWiFi","wifi_password":"YourPassword","asr_api_key":"your-asr-key","asr_cluster":"volcengine_input_en","openai_apiKey":"your-openai-key","openai_apiBaseUrl":"https://api.openai.com","system_prompt":"You are a helpful assistant.","minimax_apiKey":"your-minimax-key","tts_voice_id":"female-tianmei"}
 *
 * Hardware Requirements:
 * - M5CoreS3 development board
 *
 * Based on chat_configurable.ino for ESP32+MAX98357+INMP441
 * ============================================================================
 */

#include <M5CoreS3.h>
#include <WiFi.h>
#include <ArduinoASRChat.h>
#include <ArduinoGPTChat.h>
#include <ArduinoTTSChat.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ============================================================================
// M5CoreS3 Audio Configuration
// ============================================================================

// Recording sample rate
#define SAMPLE_RATE 16000

// Audio buffer for TTS playback (double buffering)
static int16_t* audioBufferA = nullptr;
static int16_t* audioBufferB = nullptr;
static const size_t AUDIO_CHUNK_SIZE = 2048;
static bool useBufferA = true;

// Microphone recording buffer
static int16_t* micBuffer = nullptr;
static const size_t MIC_BUFFER_SIZE = 320;  // ~20ms at 16kHz

// Speaker volume (0-255)
int speakerVolume = 255;

// ============================================================================
// Configuration Variables
// ============================================================================

// WiFi configuration
String wifi_ssid = "";
String wifi_password = "";

// ASR configuration
String asr_api_key = "";
String asr_cluster = "volcengine_input_en";

// OpenAI configuration
String openai_apiKey = "";
String openai_apiBaseUrl = "";

// System prompt
String system_prompt = "You are a helpful AI assistant.";

// MiniMax TTS configuration
String minimax_apiKey = "";
String tts_voice_id = "female-tianmei";
float tts_speed = 1.0;
float tts_volume = 1.0;
int tts_sample_rate = 16000;
int tts_bitrate = 32000;

// Configuration status
bool configReceived = false;
bool systemInitialized = false;

// ============================================================================
// Global Object Instances
// ============================================================================

ArduinoASRChat* asrChat = nullptr;
ArduinoGPTChat* gptChat = nullptr;
ArduinoTTSChat* ttsChat = nullptr;
Preferences preferences;

// TTS completion flag
volatile bool ttsCompleted = false;

// ============================================================================
// State Machine Definition
// ============================================================================

enum ConversationState {
  STATE_WAITING_CONFIG,
  STATE_IDLE,
  STATE_LISTENING,
  STATE_PROCESSING_LLM,
  STATE_PLAYING_TTS,
  STATE_WAIT_TTS_COMPLETE
};

// ============================================================================
// State Variables
// ============================================================================

ConversationState currentState = STATE_WAITING_CONFIG;
bool continuousMode = false;
unsigned long ttsStartTime = 0;
unsigned long ttsCheckTime = 0;
bool touchDetected = false;
bool wasTouched = false;

// ============================================================================
// Audio Buffer Initialization
// ============================================================================

void initAudioBuffers() {
  if (psramFound()) {
    audioBufferA = (int16_t*)ps_malloc(AUDIO_CHUNK_SIZE * sizeof(int16_t));
    audioBufferB = (int16_t*)ps_malloc(AUDIO_CHUNK_SIZE * sizeof(int16_t));
    micBuffer = (int16_t*)ps_malloc(MIC_BUFFER_SIZE * sizeof(int16_t));
    Serial.println("Using PSRAM for audio buffers");
  } else {
    audioBufferA = new int16_t[AUDIO_CHUNK_SIZE];
    audioBufferB = new int16_t[AUDIO_CHUNK_SIZE];
    micBuffer = new int16_t[MIC_BUFFER_SIZE];
    Serial.println("Using heap for audio buffers");
  }
}

// ============================================================================
// Audio Playback Callback for M5CoreS3
// ============================================================================

bool playAudioCallback(const int16_t* data, size_t samples, uint32_t sampleRate) {
  int waitCount = 0;
  while (CoreS3.Speaker.isPlaying(0) >= 2 && waitCount < 100) {
    delay(1);
    waitCount++;
  }

  if (waitCount >= 100) {
    return false;
  }

  int16_t* buffer = useBufferA ? audioBufferA : audioBufferB;
  useBufferA = !useBufferA;

  size_t copySize = min(samples, AUDIO_CHUNK_SIZE);
  memcpy(buffer, data, copySize * sizeof(int16_t));

  return CoreS3.Speaker.playRaw(buffer, copySize, sampleRate, false, 1, 0, false);
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
  CoreS3.Display.drawString("DAZI-M5 AI ASSISTANT", 160, 20);
  CoreS3.Display.setFont(&fonts::FreeSans9pt7b);
}

void displayStatus(const char* status, uint16_t color = TFT_YELLOW) {
  CoreS3.Display.fillRect(0, 200, 320, 40, TFT_BLACK);
  CoreS3.Display.setTextColor(color);
  CoreS3.Display.drawString(status, 160, 210);
}

// Display user speech text (multi-line area)
void displayUserText(const String& text) {
  // Clear user area (y=40 to y=85)
  CoreS3.Display.fillRect(0, 40, 320, 45, TFT_BLACK);
  CoreS3.Display.setTextColor(TFT_CYAN);
  CoreS3.Display.setTextDatum(TL_DATUM);  // Top-left align
  CoreS3.Display.drawString("You:", 5, 42);
  CoreS3.Display.setTextColor(TFT_WHITE);

  const int maxCharsPerLine = 38;
  const int maxLines = 2;
  const int lineHeight = 18;
  int startY = 42;
  int startX = 45;  // After "You:" label

  String remaining = text;
  int lineCount = 0;

  while (remaining.length() > 0 && lineCount < maxLines) {
    String line;
    int lineMaxChars = (lineCount == 0) ? maxCharsPerLine - 5 : maxCharsPerLine;  // First line shorter

    if (remaining.length() <= lineMaxChars) {
      line = remaining;
      remaining = "";
    } else {
      int cutPos = lineMaxChars;
      int lastSpace = remaining.lastIndexOf(' ', lineMaxChars);
      if (lastSpace > 10) cutPos = lastSpace;
      line = remaining.substring(0, cutPos);
      remaining = remaining.substring(cutPos);
      remaining.trim();
    }

    if (lineCount == maxLines - 1 && remaining.length() > 0) {
      if (line.length() > lineMaxChars - 3) line = line.substring(0, lineMaxChars - 3);
      line += "...";
    }

    int x = (lineCount == 0) ? startX : 5;
    CoreS3.Display.drawString(line.c_str(), x, startY + lineCount * lineHeight);
    lineCount++;
  }
  CoreS3.Display.setTextDatum(MC_DATUM);  // Reset to center
}

// Display AI response text (multi-line area)
void displayAIText(const String& text) {
  // Clear AI area (y=90 to y=195)
  CoreS3.Display.fillRect(0, 90, 320, 105, TFT_BLACK);
  CoreS3.Display.setTextColor(TFT_CYAN);
  CoreS3.Display.setTextDatum(TL_DATUM);  // Top-left align
  CoreS3.Display.drawString("AI:", 5, 92);
  CoreS3.Display.setTextColor(TFT_WHITE);

  const int maxCharsPerLine = 38;
  const int maxLines = 5;
  const int lineHeight = 18;
  int startY = 92;
  int startX = 30;  // After "AI:" label

  String remaining = text;
  int lineCount = 0;

  while (remaining.length() > 0 && lineCount < maxLines) {
    String line;
    int lineMaxChars = (lineCount == 0) ? maxCharsPerLine - 3 : maxCharsPerLine;  // First line shorter

    if (remaining.length() <= lineMaxChars) {
      line = remaining;
      remaining = "";
    } else {
      int cutPos = lineMaxChars;
      int lastSpace = remaining.lastIndexOf(' ', lineMaxChars);
      if (lastSpace > 10) cutPos = lastSpace;
      line = remaining.substring(0, cutPos);
      remaining = remaining.substring(cutPos);
      remaining.trim();
    }

    if (lineCount == maxLines - 1 && remaining.length() > 0) {
      if (line.length() > lineMaxChars - 3) line = line.substring(0, lineMaxChars - 3);
      line += "...";
    }

    int x = (lineCount == 0) ? startX : 5;
    CoreS3.Display.drawString(line.c_str(), x, startY + lineCount * lineHeight);
    lineCount++;
  }
  CoreS3.Display.setTextDatum(MC_DATUM);  // Reset to center
}

void displayVolume() {
  CoreS3.Display.fillRect(0, 160, 320, 30, TFT_BLACK);
  CoreS3.Display.setTextColor(TFT_CYAN);
  char volStr[32];
  snprintf(volStr, sizeof(volStr), "Volume: %d", speakerVolume);
  CoreS3.Display.drawString(volStr, 160, 175);
}

// ============================================================================
// Microphone/Speaker Switching
// ============================================================================

void switchToMicrophone() {
  CoreS3.Speaker.end();
  CoreS3.Mic.begin();
  Serial.println("[Audio] Switched to microphone mode");
}

void switchToSpeaker() {
  CoreS3.Mic.end();
  CoreS3.Speaker.begin();
  CoreS3.Speaker.setVolume(speakerVolume);
  Serial.println("[Audio] Switched to speaker mode");
}

// ============================================================================
// Flash Storage Functions
// ============================================================================

bool saveConfigToFlash() {
  preferences.begin("voice_config", false);

  preferences.putString("wifi_ssid", wifi_ssid);
  preferences.putString("wifi_pass", wifi_password);
  preferences.putString("asr_key", asr_api_key);
  preferences.putString("asr_cluster", asr_cluster);
  preferences.putString("openai_key", openai_apiKey);
  preferences.putString("openai_url", openai_apiBaseUrl);
  preferences.putString("sys_prompt", system_prompt);
  preferences.putString("minimax_key", minimax_apiKey);
  preferences.putString("tts_voice", tts_voice_id);
  preferences.putFloat("tts_speed", tts_speed);
  preferences.putFloat("tts_volume", tts_volume);
  preferences.putInt("tts_sample", tts_sample_rate);
  preferences.putInt("tts_bitrate", tts_bitrate);
  preferences.putBool("configured", true);
  preferences.end();

  Serial.println("[Flash] Config saved to Flash");
  return true;
}

bool loadConfigFromFlash() {
  preferences.begin("voice_config", true);

  if (!preferences.getBool("configured", false)) {
    preferences.end();
    Serial.println("[Flash] No config found in Flash");
    return false;
  }

  wifi_ssid = preferences.getString("wifi_ssid", "");
  wifi_password = preferences.getString("wifi_pass", "");
  asr_api_key = preferences.getString("asr_key", "");
  asr_cluster = preferences.getString("asr_cluster", "volcengine_input_en");
  openai_apiKey = preferences.getString("openai_key", "");
  openai_apiBaseUrl = preferences.getString("openai_url", "");
  system_prompt = preferences.getString("sys_prompt", "You are a helpful AI assistant.");
  minimax_apiKey = preferences.getString("minimax_key", "");
  tts_voice_id = preferences.getString("tts_voice", "female-tianmei");
  tts_speed = preferences.getFloat("tts_speed", 1.0);
  tts_volume = preferences.getFloat("tts_volume", 1.0);
  tts_sample_rate = preferences.getInt("tts_sample", 16000);
  tts_bitrate = preferences.getInt("tts_bitrate", 32000);

  preferences.end();

  Serial.println("[Flash] Config loaded from Flash");
  Serial.println("WiFi SSID: " + wifi_ssid);

  return true;
}

void clearFlashConfig() {
  preferences.begin("voice_config", false);
  preferences.clear();
  preferences.end();
  Serial.println("[Flash] Flash config cleared");
}

// ============================================================================
// Configuration Reception Function
// ============================================================================

bool receiveConfig() {
  static String jsonBuffer = "";
  static unsigned long lastReceiveTime = 0;
  static bool receiving = false;

  if (!receiving && Serial.available() > 0) {
    char first = Serial.peek();
    if (first != '{') {
      while (Serial.available() > 0 && Serial.peek() != '{') {
        Serial.read();
      }
    }
  }

  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '{' && !receiving) {
      jsonBuffer = "{";
      receiving = true;
      lastReceiveTime = millis();
      continue;
    }

    if (receiving) {
      jsonBuffer += c;
      lastReceiveTime = millis();

      if (c == '}') {
        delay(200);

        int noDataCount = 0;
        while (noDataCount < 10) {
          if (Serial.available() > 0) {
            char extra = Serial.read();
            if (extra == '\n' || extra == '\r') {
              break;
            }
            jsonBuffer += extra;
            noDataCount = 0;
            delay(2);
          } else {
            delay(5);
            noDataCount++;
          }
        }

        jsonBuffer.trim();

        if (jsonBuffer.startsWith("{") && jsonBuffer.endsWith("}")) {
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, jsonBuffer);

          if (error) {
            Serial.print("[Error] JSON parse failed: ");
            Serial.println(error.c_str());
            jsonBuffer = "";
            receiving = false;
            return false;
          }

          jsonBuffer = "";
          receiving = false;

          if (doc.containsKey("wifi_ssid")) wifi_ssid = doc["wifi_ssid"].as<String>();
          if (doc.containsKey("wifi_password")) wifi_password = doc["wifi_password"].as<String>();
          if (doc.containsKey("asr_api_key")) asr_api_key = doc["asr_api_key"].as<String>();
          if (doc.containsKey("asr_cluster")) asr_cluster = doc["asr_cluster"].as<String>();
          if (doc.containsKey("openai_apiKey")) openai_apiKey = doc["openai_apiKey"].as<String>();
          if (doc.containsKey("openai_apiBaseUrl")) openai_apiBaseUrl = doc["openai_apiBaseUrl"].as<String>();
          if (doc.containsKey("system_prompt")) system_prompt = doc["system_prompt"].as<String>();
          if (doc.containsKey("minimax_apiKey")) minimax_apiKey = doc["minimax_apiKey"].as<String>();
          if (doc.containsKey("tts_voice_id")) tts_voice_id = doc["tts_voice_id"].as<String>();
          if (doc.containsKey("tts_speed")) tts_speed = doc["tts_speed"].as<float>();
          if (doc.containsKey("tts_volume")) tts_volume = doc["tts_volume"].as<float>();
          if (doc.containsKey("tts_sample_rate")) tts_sample_rate = doc["tts_sample_rate"].as<int>();
          if (doc.containsKey("tts_bitrate")) tts_bitrate = doc["tts_bitrate"].as<int>();

          if (wifi_ssid.length() == 0 || wifi_password.length() == 0 ||
              asr_api_key.length() == 0 || openai_apiKey.length() == 0 ||
              minimax_apiKey.length() == 0) {
            Serial.println("Error: Missing required config");
            return false;
          }

          Serial.println("\n[Config] Configuration received successfully!");
          return true;
        }
      }
    }
  }

  if (receiving && jsonBuffer.length() > 0 && (millis() - lastReceiveTime > 3000)) {
    Serial.println("[Warning] Config receive timeout, buffer cleared");
    jsonBuffer = "";
    receiving = false;
  }

  return false;
}

// ============================================================================
// TTS Callbacks
// ============================================================================

void onTTSComplete() {
  Serial.println("[TTS] Playback completed");
  ttsCompleted = true;
}

void onTTSError(const char* error) {
  Serial.printf("[TTS Error] %s\n", error);
  ttsCompleted = true;
}

// ============================================================================
// System Initialization
// ============================================================================

bool initializeSystem() {
  Serial.println("\n----- System Initialization -----");
  displayStatus("Connecting WiFi...");

  // WiFi Connection
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
    displayStatus("WiFi Failed!", TFT_RED);
    return false;
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  displayStatus("WiFi Connected", TFT_GREEN);

  // ASR Initialization
  displayStatus("Init ASR...");
  asrChat = new ArduinoASRChat(asr_api_key.c_str(), asr_cluster.c_str());

  if (!asrChat->initM5CoreS3Microphone()) {
    Serial.println("Microphone init failed!");
    displayStatus("Mic Init Failed!", TFT_RED);
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
    displayStatus("ASR Connect Failed!", TFT_RED);
    return false;
  }

  Serial.println("ASR initialized");

  // GPT Chat Initialization
  displayStatus("Init GPT...");
  gptChat = new ArduinoGPTChat(openai_apiKey.c_str(), openai_apiBaseUrl.c_str());
  gptChat->setSystemPrompt(system_prompt.c_str());
  gptChat->enableMemory(true);
  Serial.println("GPT Chat initialized");

  // TTS Initialization
  displayStatus("Init TTS...");
  ttsChat = new ArduinoTTSChat(minimax_apiKey.c_str());

  ttsChat->setVoiceId(tts_voice_id.c_str());
  ttsChat->setSpeed(tts_speed);
  ttsChat->setVolume(tts_volume);
  ttsChat->setAudioParams(tts_sample_rate, tts_bitrate);

  if (!ttsChat->initM5CoreS3Speaker()) {
    Serial.println("TTS speaker init failed!");
    displayStatus("TTS Init Failed!", TFT_RED);
    return false;
  }

  ttsChat->setAudioPlayCallback(playAudioCallback);
  ttsChat->setCompletionCallback(onTTSComplete);
  ttsChat->setErrorCallback(onTTSError);

  if (!ttsChat->connectWebSocket()) {
    Serial.println("TTS WebSocket connection failed!");
    displayStatus("TTS Connect Failed!", TFT_RED);
    return false;
  }

  Serial.printf("TTS: Voice=%s, Speed=%.1f, SampleRate=%d\n",
                tts_voice_id.c_str(), tts_speed, tts_sample_rate);

  Serial.println("\n----- System Ready -----");
  displayStatus("Ready - Touch to start", TFT_GREEN);
  displayVolume();

  return true;
}

// ============================================================================
// Continuous Conversation Mode
// ============================================================================

void startContinuousMode() {
  continuousMode = true;
  currentState = STATE_LISTENING;

  Serial.println("\n========================================");
  Serial.println("  Continuous Conversation Mode Started");
  Serial.println("  Touch screen again to stop");
  Serial.println("========================================");

  // Switch to microphone mode
  switchToMicrophone();

  if (asrChat->startRecording()) {
    Serial.println("\n[ASR] Listening... Please speak");
    displayStatus("Listening...", TFT_GREEN);
  } else {
    Serial.println("\n[Error] ASR startup failed");
    displayStatus("ASR Error!", TFT_RED);
    continuousMode = false;
    currentState = STATE_IDLE;
    switchToSpeaker();
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

  switchToSpeaker();
  currentState = STATE_IDLE;
  displayStatus("Ready - Touch to start", TFT_GREEN);
  Serial.println("\nTouch screen to start conversation");
}

// ============================================================================
// ASR Result Processing
// ============================================================================

void handleASRResult() {
  String transcribedText = asrChat->getRecognizedText();
  asrChat->clearResult();

  if (transcribedText.length() > 0) {
    Serial.println("\n=== ASR Recognition Result ===");
    Serial.printf("%s\n", transcribedText.c_str());
    Serial.println("==============================");

    displayUserText(transcribedText);

    // Switch to speaker before LLM processing (speaker will be used for TTS)
    switchToSpeaker();

    currentState = STATE_PROCESSING_LLM;
    displayStatus("Thinking...", TFT_YELLOW);
    Serial.println("\n[LLM] Sending to ChatGPT...");

    String response = gptChat->sendMessage(transcribedText);

    if (response != "" && response.length() > 0) {
      Serial.println("\n=== ChatGPT Response ===");
      Serial.printf("%s\n", response.c_str());
      Serial.println("========================");

      displayAIText(response);

      currentState = STATE_PLAYING_TTS;
      displayStatus("Speaking...", TFT_CYAN);
      Serial.println("\n[TTS] Converting to speech...");

      ttsCompleted = false;
      bool success = ttsChat->speak(response.c_str());

      if (success) {
        currentState = STATE_WAIT_TTS_COMPLETE;
        ttsStartTime = millis();
        ttsCheckTime = millis();
      } else {
        Serial.println("[Error] TTS playback failed");
        handleTTSFailure();
      }
    } else {
      Serial.println("[Error] Failed to get ChatGPT response");
      displayStatus("LLM Error!", TFT_RED);
      handleTTSFailure();
    }
  } else {
    Serial.println("[Warning] No text recognized");
    handleTTSFailure();
  }
}

void handleTTSFailure() {
  if (continuousMode) {
    delay(500);
    currentState = STATE_LISTENING;
    switchToMicrophone();
    if (asrChat->startRecording()) {
      Serial.println("\n[ASR] Listening... Please speak");
      displayStatus("Listening...", TFT_GREEN);
    } else {
      stopContinuousMode();
    }
  } else {
    currentState = STATE_IDLE;
    displayStatus("Ready - Touch to start", TFT_GREEN);
  }
}

// ============================================================================
// Setup Function
// ============================================================================

void setup() {
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n====================================================");
  Serial.println("   M5CoreS3 Voice Assistant System");
  Serial.println("====================================================");

  // Initialize M5CoreS3
  auto cfg = M5.config();
  CoreS3.begin(cfg);

  // Initialize display
  displayInit();
  displayStatus("Initializing...");

  // Initialize speaker (default mode)
  CoreS3.Speaker.setVolume(speakerVolume);
  CoreS3.Speaker.begin();
  displayVolume();

  // Initialize audio buffers
  initAudioBuffers();

  if (!CoreS3.Speaker.isEnabled()) {
    Serial.println("Speaker not available!");
    displayStatus("Speaker Error!", TFT_RED);
    while (1) delay(1000);
  }

  // Play startup tone
  CoreS3.Speaker.tone(1000, 100);
  delay(150);
  CoreS3.Speaker.tone(1500, 100);
  delay(150);

  // Initialize random seed
  randomSeed(analogRead(0) + millis());

  // Try to load config from Flash
  Serial.println("\n[Startup] Checking Flash for config...");
  if (loadConfigFromFlash()) {
    configReceived = true;
    Serial.println("[Startup] Using config from Flash");
    displayStatus("Config loaded", TFT_GREEN);
    Serial.println("\nTip: Send new JSON config via serial before touching to update");
  } else {
    Serial.println("[Startup] No config in Flash, waiting for serial config...");
    displayStatus("Waiting for config...");
    Serial.println("\nConfig example:");
    Serial.println("{\"wifi_ssid\":\"YourWiFi\",\"wifi_password\":\"YourPassword\",\"asr_api_key\":\"your-key\",\"asr_cluster\":\"volcengine_input_en\",\"openai_apiKey\":\"your-key\",\"openai_apiBaseUrl\":\"https://api.openai.com\",\"system_prompt\":\"You are a helpful assistant.\",\"minimax_apiKey\":\"your-key\",\"tts_voice_id\":\"female-tianmei\"}");
  }

  Serial.println("\nTouch screen to start system...\n");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  // Update M5CoreS3
  CoreS3.update();

  // Waiting for Config State
  if (currentState == STATE_WAITING_CONFIG) {
    if (receiveConfig()) {
      configReceived = true;
      Serial.println("\nNew config received!");
      displayStatus("Touch to start", TFT_GREEN);
      Serial.println("Touch screen to test and save new config...");
    }

    // Touch detection for starting
    if (CoreS3.Touch.getCount() > 0) {
      auto detail = CoreS3.Touch.getDetail(0);
      if (detail.wasPressed() && configReceived && !systemInitialized) {
        Serial.println("\n[Startup] Initializing system...");
        displayStatus("Starting...");

        if (initializeSystem()) {
          systemInitialized = true;
          saveConfigToFlash();
          delay(1000);
          startContinuousMode();
        } else {
          Serial.println("\n[Error] System init failed, check config");
          displayStatus("Init Failed!", TFT_RED);
          currentState = STATE_WAITING_CONFIG;
          configReceived = false;
        }
      }
    }

    delay(100);
    return;
  }

  // Process TTS loop
  if (ttsChat != nullptr) {
    ttsChat->loop();
  }

  // Process ASR loop and feed microphone data
  if (asrChat != nullptr) {
    asrChat->loop();

    // Feed microphone data if recording in M5CoreS3 mode
    if (asrChat->isRecording() && CoreS3.Mic.isEnabled()) {
      if (CoreS3.Mic.record(micBuffer, MIC_BUFFER_SIZE, SAMPLE_RATE)) {
        asrChat->feedAudioData(micBuffer, MIC_BUFFER_SIZE);
      }
    }
  }

  // Touch detection for stopping
  if (CoreS3.Touch.getCount() > 0) {
    auto detail = CoreS3.Touch.getDetail(0);
    if (detail.wasPressed() && !wasTouched) {
      wasTouched = true;
      if (continuousMode) {
        stopContinuousMode();
      }
    }
  } else {
    wasTouched = false;
  }

  // State Machine Processing
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

        bool playbackComplete = ttsCompleted || !ttsChat->isPlaying();

        if (playbackComplete) {
          Serial.println("[TTS] Playback completed");

          if (continuousMode) {
            delay(500);
            currentState = STATE_LISTENING;
            switchToMicrophone();

            if (asrChat->startRecording()) {
              Serial.println("\n[ASR] Listening... Please speak");
              displayStatus("Listening...", TFT_GREEN);
            } else {
              Serial.println("[Error] ASR restart failed");
              stopContinuousMode();
            }
          } else {
            currentState = STATE_IDLE;
            displayStatus("Ready - Touch to start", TFT_GREEN);
          }
        } else {
          if (millis() - ttsStartTime > 60000) {
            Serial.println("[Warning] TTS timeout, forcing restart");
            ttsChat->stop();

            if (continuousMode) {
              currentState = STATE_LISTENING;
              switchToMicrophone();
              if (asrChat->startRecording()) {
                Serial.println("\n[ASR] Listening... Please speak");
                displayStatus("Listening...", TFT_GREEN);
              } else {
                stopContinuousMode();
              }
            } else {
              currentState = STATE_IDLE;
              displayStatus("Ready - Touch to start", TFT_GREEN);
            }
          }
        }
      }
      break;

    case STATE_WAITING_CONFIG:
      break;
  }

  // Loop delay
  if (currentState == STATE_LISTENING ||
      currentState == STATE_PLAYING_TTS ||
      currentState == STATE_WAIT_TTS_COMPLETE) {
    delay(1);
  } else {
    delay(10);
  }
}
