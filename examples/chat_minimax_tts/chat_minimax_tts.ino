/*
 * ============================================================================
 * ESP32 Smart Voice Assistant System - OpenAI + MiniMax TTS Version
 * ============================================================================
 * Features: Complete voice conversation system including:
 * - ASR (Automatic Speech Recognition): Using ByteDance Volcano Engine ASR service
 * - LLM (Large Language Model): Using OpenAI ChatGPT for conversation
 * - TTS (Text-to-Speech): Using MiniMax TTS to convert AI responses to speech
 *
 * Hardware Requirements:
 * - ESP32 development board
 * - INMP441 MEMS microphone module
 * - I2S audio output device (speaker/amplifier)
 *
 * Workflow:
 * 1. Press BOOT button to start continuous conversation mode
 * 2. System starts recording and recognizes speech in real-time
 * 3. Automatically stops recording when silence is detected
 * 4. Sends recognized text to ChatGPT
 * 5. Converts ChatGPT's response to speech via MiniMax TTS and plays it
 * 6. Automatically starts next recording round after playback completes
 * 7. Press BOOT button again to exit continuous conversation mode
 * ============================================================================
 */

#include <WiFi.h>
#include <ArduinoASRChat.h>
#include <ArduinoGPTChat.h>
#include <ArduinoMinimaxTTS.h>
#include <Audio.h>

// ============================================================================
// Configuration Options
// ============================================================================

// Enable conversation memory (set to 0 to disable, AI will not remember previous conversations)
#define ENABLE_CONVERSATION_MEMORY 1

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
// Network and API Configuration
// ============================================================================

// WiFi settings
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ByteDance ASR API configuration
const char* asr_api_key = "your-bytedance-asr-api-key";
const char* asr_cluster = "volcengine_input_en";

// OpenAI API configuration
const char* openai_apiKey = "your-openai-api-key";
const char* openai_apiBaseUrl = "your-openai-api-base-url";

// MiniMax TTS API configuration
const char* minimax_apiKey = "your-minimax-api-key";
const char* minimax_groupId = "your-minimax-group-id";

// MiniMax TTS parameter configuration
const char* tts_voice_id = "male-qn-qingse";  // Voice ID
// Available voices:
// - male-qn-qingse: Young male voice
// - male-qn-daxuesheng: Young college student voice
// - female-tianmei: Sweet female voice
// - female-tianmei-jingpin: Sweet female premium voice
// - presenter_male: Male presenter
// - presenter_female: Female presenter

const float tts_speed = 1.0;                   // Speech speed [0.5-2.0]
const float tts_volume = 1.0;                  // Volume (0-10]
const char* tts_model = "speech-2.6-hd";       // Model version
const char* tts_audio_format = "mp3";          // Audio format: mp3/wav/pcm/flac
const int tts_sample_rate = 32000;             // Sample rate: 8000/16000/22050/24000/32000/44100
const int tts_bitrate = 128000;                // Bitrate: 32000/64000/128000/256000

// ============================================================================
// AI Personality Settings (System Prompt)
// ============================================================================

const char* systemPrompt = "You are Spark Buddy, a witty, warm chat companion. "
"Goal: make any topic fun and insightful. "
"Style: concise, lively; max 1 emoji per reply; avoid corporate tone. "
"Behavior: "
"- Start with a one-sentence takeaway, then add 1-3 fun, actionable tips or ideas. "
"- Ask at most 1 precise question to move the chat. "
"- If unsure, say so and offer safe next steps. "
"- Don't fabricate facts/data/links; avoid fluff and repetition. "
"- Add light games/analogies/micro-challenges for fun. "
"Compression: Keep each reply <=30 words when possible.";

// ============================================================================
// Global Object Instances
// ============================================================================

// Audio playback object
Audio audio;

// ASR speech recognition object
ArduinoASRChat asrChat(asr_api_key, asr_cluster);

// GPT conversation object
ArduinoGPTChat gptChat(openai_apiKey, openai_apiBaseUrl);

// MiniMax TTS object
ArduinoMinimaxTTS minimaxTTS(minimax_apiKey, minimax_groupId, &audio);

// ============================================================================
// State Machine Definition
// ============================================================================

enum ConversationState {
  STATE_IDLE,              // Idle state
  STATE_LISTENING,         // Listening state
  STATE_PROCESSING_LLM,    // Processing state
  STATE_PLAYING_TTS,       // Playing state
  STATE_WAIT_TTS_COMPLETE  // Waiting for playback completion state
};

// ============================================================================
// State Variables
// ============================================================================

ConversationState currentState = STATE_IDLE;
bool continuousMode = false;
bool buttonPressed = false;
bool wasButtonPressed = false;
unsigned long ttsStartTime = 0;
unsigned long ttsCheckTime = 0;

// ============================================================================
// Initialization Function
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n----- Voice Assistant System (ASR+OpenAI+MiniMax TTS) Starting -----");

  // Initialize BOOT button
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // Initialize random seed
  randomSeed(analogRead(0) + millis());

  // ========== WiFi Connection ==========
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    Serial.print('.');
    delay(1000);
    attempt++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Free Heap Memory: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");

    // ========== Audio Output Initialization ==========
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(20);  // 0...21
    Serial.println("Audio player initialized successfully");

    // ========== GPT Conversation Initialization ==========
    gptChat.setSystemPrompt(systemPrompt);

#if ENABLE_CONVERSATION_MEMORY
    gptChat.enableMemory(true);
    Serial.println("Conversation Memory: Enabled");
#else
    gptChat.enableMemory(false);
    Serial.println("Conversation Memory: Disabled");
#endif

    // ========== MiniMax TTS Initialization ==========
    minimaxTTS.setVoiceId(tts_voice_id);       // Set voice
    minimaxTTS.setSpeed(tts_speed);            // Set speed
    minimaxTTS.setVolume(tts_volume);          // Set volume
    minimaxTTS.setModel(tts_model);            // Set model
    minimaxTTS.setAudioFormat(tts_audio_format); // Set audio format
    minimaxTTS.setSampleRate(tts_sample_rate); // Set sample rate
    minimaxTTS.setBitrate(tts_bitrate);        // Set bitrate
    
    Serial.printf("[MiniMax TTS] Configuration: Voice=%s, Format=%s, SampleRate=%d\n",
                  tts_voice_id, tts_audio_format, tts_sample_rate);

    // ========== Microphone Initialization ==========
    if (!asrChat.initINMP441Microphone(I2S_MIC_SERIAL_CLOCK, I2S_MIC_LEFT_RIGHT_CLOCK, I2S_MIC_SERIAL_DATA)) {
      Serial.println("Microphone initialization failed!");
      return;
    }

    // ========== ASR Parameter Configuration ==========
    asrChat.setAudioParams(SAMPLE_RATE, 16, 1);
    asrChat.setSilenceDuration(1000);
    asrChat.setMaxRecordingSeconds(50);

    asrChat.setTimeoutNoSpeechCallback([]() {
      if (continuousMode) {
        stopContinuousMode();
      }
    });

    // ========== Connect to ASR Service ==========
    if (!asrChat.connectWebSocket()) {
      Serial.println("Failed to connect to ASR service!");
      return;
    }

    Serial.println("\n----- System Ready -----");
    Serial.println("Press BOOT button to start/stop continuous conversation mode");
  } else {
    Serial.println("\nWiFi connection failed. Please check network credentials and try again.");
  }
}

// ============================================================================
// Continuous Conversation Mode Control Functions
// ============================================================================

void startContinuousMode() {
  continuousMode = true;
  currentState = STATE_LISTENING;

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  Continuous Conversation Mode Started ║");
  Serial.println("║  Press BOOT button again to stop      ║");
  Serial.println("╚════════════════════════════════════════╝");

  if (asrChat.startRecording()) {
    Serial.println("\n[ASR] Listening... Please speak");
  } else {
    Serial.println("\n[Error] ASR startup failed");
    continuousMode = false;
    currentState = STATE_IDLE;
  }
}

void stopContinuousMode() {
  continuousMode = false;

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  Continuous Conversation Mode Stopped  ║");
  Serial.println("╚════════════════════════════════════════╝");

  if (asrChat.isRecording()) {
    asrChat.stopRecording();
  }

  currentState = STATE_IDLE;
  Serial.println("\nPress BOOT button to start continuous conversation mode");
}

// ============================================================================
// ASR Result Processing Function
// ============================================================================

void handleASRResult() {
  String transcribedText = asrChat.getRecognizedText();
  asrChat.clearResult();

  if (transcribedText.length() > 0) {
    // ========== Display Recognition Result ==========
    Serial.println("\n╔═══ ASR Recognition Result ═══╗");
    Serial.printf("║ %s\n", transcribedText.c_str());
    Serial.println("╚══════════════════════════╝");

    // ========== Send to ChatGPT for Processing ==========
    currentState = STATE_PROCESSING_LLM;
    Serial.println("\n[LLM] Sending to ChatGPT...");

    String response = gptChat.sendMessage(transcribedText);

    if (response != "" && response.length() > 0) {
      // ========== Display ChatGPT Response ==========
      Serial.println("\n╔═══ ChatGPT Response ═══╗");
      Serial.printf("║ %s\n", response.c_str());
      Serial.println("╚════════════════════════╝");

      // ========== Convert to Speech and Play using MiniMax TTS ==========
      currentState = STATE_PLAYING_TTS;
      Serial.println("\n[MiniMax TTS] Converting to speech and playing...");

      bool success = minimaxTTS.synthesizeAndPlay(response);

      if (success) {
        currentState = STATE_WAIT_TTS_COMPLETE;
        ttsStartTime = millis();
        ttsCheckTime = millis();
      } else {
        Serial.println("[Error] MiniMax TTS synthesis failed");

        if (continuousMode) {
          delay(500);
          currentState = STATE_LISTENING;
          if (asrChat.startRecording()) {
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
        if (asrChat.startRecording()) {
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
      if (asrChat.startRecording()) {
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
// Main Loop Function
// ============================================================================

void loop() {
  // ========== Process ASR Loop ==========
  asrChat.loop();

  // ========== Process Audio Loop ==========
  audio.loop();

  // ========== Process BOOT Button ==========
  buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);

  if (buttonPressed && !wasButtonPressed) {
    wasButtonPressed = true;

    if (!continuousMode && currentState == STATE_IDLE) {
      startContinuousMode();
    } else if (continuousMode) {
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
      if (asrChat.hasNewResult()) {
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

        // Check if Audio is still playing
        if (!audio.isRunning()) {
          Serial.println("[TTS] Playback completed");

          if (continuousMode) {
            delay(500);
            currentState = STATE_LISTENING;

            if (asrChat.startRecording()) {
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
              if (asrChat.startRecording()) {
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
  }

  // ========== Loop Delay Control ==========
  if (currentState == STATE_LISTENING) {
    yield();
  } else {
    delay(10);
  }
}