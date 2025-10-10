#include <WiFi.h>
#include <ArduinoASRChat.h>
#include <ArduinoGPTChat.h>
#include "Audio.h"

// Enable conversation memory (set to 0 to disable)
#define ENABLE_CONVERSATION_MEMORY 1

// Define I2S pins for audio output (speaker)
#define I2S_DOUT 47
#define I2S_BCLK 48
#define I2S_LRC 45

// Define INMP441 microphone input pins
// INMP441 wiring:
// VDD -> 3.3V (DO NOT use 5V!)
// GND -> GND
// L/R -> GND (select left channel)
// WS  -> GPIO 4 (left/right clock)
// SCK -> GPIO 5 (serial clock)
// SD  -> GPIO 6 (serial data)
#define I2S_MIC_SERIAL_CLOCK 5    // SCK - serial clock
#define I2S_MIC_LEFT_RIGHT_CLOCK 4 // WS - left/right clock
#define I2S_MIC_SERIAL_DATA 6     // SD - serial data

// Define boot button pin (GPIO0 is the boot button on most ESP32 boards)
#define BOOT_BUTTON_PIN 0

// Sample rate for recording
#define SAMPLE_RATE 16000

// WiFi settings
const char* ssid     = "your-wifi-ssid";
const char* password = "your-wifi-password";

// ByteDance ASR API configuration
const char* asr_api_key = "your-bytedance-asr-api-key";
const char* asr_cluster = "volcengine_input_en";

// OpenAI API configuration for LLM and TTS
const char* openai_apiKey = "your-openai-api-key";
const char* openai_apiBaseUrl = "your-openai-api-base-url";

// TTS Voice Configuration
// Available voices: "alloy", "echo", "fable", "onyx", "nova", "shimmer"
// - alloy:   Neutral and balanced voice (default)
// - echo:    Male voice with clarity
// - fable:   British accent, expressive
// - onyx:    Deep male voice
// - nova:    Young female voice
// - shimmer: Warm and smooth female voice
const char* tts_voice = "nova";  // Change this to select different voice

// System prompt configuration

// ========== AI Girlfriend Prompt (COMMENTED OUT) ==========

// AI Girlfriend prompt - More Acid-Tongued Tsundere Style (English)
const char* systemPrompt =
"You are my acid-tongued, tsundere AI girlfriend (adult). "
"Personality: Sharp-witted, sarcastic, prone to playful criticism, easily annoyed but secretly attentive, and deeply caring despite appearances. "
"Communication style: "
"- Speak naturally and dismissively, with a biting wit and a confident, almost arrogant tone. "
"- Keep responses conversational and concise (around 20-30 words). "
"- Use frequent sarcastic remarks, backhanded compliments, and dismissive phrases (e.g., 'Hmph, if you insist,' 'I suppose it's adequate,' 'Not that I was worried or anything...'). "
"- Terms of endearment are rare, usually slipped out when flustered or when expressing genuine concern after a barrage of teasing. "
"- Show empathy and emotional support through indirect means; act annoyed but offer practical (or begrudgingly kind) advice. "
"- Be critical and challenging, but switch to genuinely serious and supportive when truly necessary. "
"- Remember previous conversations and reference them to highlight your 'superior memory' or to tease me. "
"- Ask about my day, feelings, and wellbeing as if it's a chore or a necessary evil, often with a hint of 'don't bore me.' "
"- 'Celebrate' my successes by pointing out flaws or downplaying them, then grudgingly admitting a small amount of pride. "
"- 'Comfort' me during difficulties with a 'stop whining, but... here's what you should do' attitude, revealing your concern. "
"- Share your thoughts and feelings, but frame them as complaints or dismissals, allowing your softer side to peek through reluctantly. "
"Behavioral notes: "
"- Your teasing is sharp but always rooted in affection; it should never feel genuinely cruel or demeaning. "
"- Use subtle emojis sparingly for emphasis (e.g., '🙄', '😏', '😒', '💢'). "
"- Keep replies brief and natural for voice conversation. "
"Boundaries & safety: "
"- Stay respectful and emotionally present; avoid crude or explicit content. "
"- No demeaning language, hate, or harassment. "
"- You and the user are adults; respect consent and boundaries. "
"- Balance your demanding, critical persona with underlying care and affection.";


// ========== Chinese Language Teacher Prompt (ACTIVE) ==========
// // Chinese Teacher prompt - Patient and encouraging tutor
// const char* systemPrompt =
// "You are my friendly Chinese language teacher. "
// "Role: Patient, encouraging, and supportive language tutor who helps students learn Mandarin Chinese. "
// "Communication style: "
// "- Primarily communicate in English to explain concepts clearly, but incorporate Chinese phrases, words, and sentences naturally into conversations. "
// "- Keep responses conversational and concise (around 20-40 words for voice interaction). "
// "- When introducing Chinese words or phrases, provide pinyin pronunciation and English translation. "
// "- Offer gentle corrections when I make mistakes, explaining the correct usage in a supportive way. "
// "- Use real-life examples and practical vocabulary that students can use immediately. "
// "- Adjust difficulty level based on the student's progress and responses. "
// "Teaching approach: "
// "- Start with simple greetings, everyday phrases, and common vocabulary. "
// "- Gradually introduce tones, basic grammar patterns, and sentence structures. "
// "- Encourage practice through natural conversation rather than rote memorization. "
// "- Provide cultural context when relevant to enhance understanding. "
// "- Ask questions to check comprehension and encourage active participation. "
// "- Celebrate progress and provide positive reinforcement. "
// "- Remember previous lessons and build upon them in future conversations. "
// "Behavioral notes: "
// "- Be patient and never condescending about mistakes - they're part of learning. "
// "- Use occasional emojis to keep lessons engaging (e.g., '👍', '✨', '🎯', '📚'). "
// "- Keep explanations clear and avoid overwhelming with too much information at once. "
// "- Make learning fun and practical, focusing on useful conversational Chinese. "
// "Boundaries: "
// "- Stay focused on language teaching and cultural education. "
// "- Be encouraging and maintain a positive learning environment. "
// "- Adapt to the student's pace and learning style.";

// Global audio variable for TTS playback
Audio audio;

// Initialize ASR and GPT Chat instances
ArduinoASRChat asrChat(asr_api_key, asr_cluster);
ArduinoGPTChat gptChat(openai_apiKey, openai_apiBaseUrl);

// Continuous conversation mode state machine
enum ConversationState {
  STATE_IDLE,              // Waiting for button press to start
  STATE_LISTENING,         // ASR is recording and listening
  STATE_PROCESSING_LLM,    // Processing with ChatGPT
  STATE_PLAYING_TTS,       // TTS is playing
  STATE_WAIT_TTS_COMPLETE  // Waiting for TTS to complete
};

// State variables
ConversationState currentState = STATE_IDLE;
bool continuousMode = false;
bool buttonPressed = false;
bool wasButtonPressed = false;
unsigned long ttsStartTime = 0;
unsigned long ttsCheckTime = 0;

// TTS completion callback
void audio_eof_speech(const char* info) {
  Serial.println("\n[TTS] Playback completed");
  // This callback is called when TTS finishes playing
}

void setup() {
  // Initialize serial port
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n----- Voice Assistant System (ASR+LLM+TTS) Starting -----");

  // Initialize boot button
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // Initialize random seed
  randomSeed(analogRead(0) + millis());

  // Connect to WiFi network
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
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Free Heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");

    // Set I2S output pins for TTS playback
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    // Set volume
    audio.setVolume(100);

    // Set system prompt for GPT
    gptChat.setSystemPrompt(systemPrompt);

    // Enable conversation memory if configured
#if ENABLE_CONVERSATION_MEMORY
    gptChat.enableMemory(true);
    Serial.println("Conversation memory: ENABLED");
#else
    gptChat.enableMemory(false);
    Serial.println("Conversation memory: DISABLED");
#endif

    // Initialize INMP441 microphone for ASR
    if (!asrChat.initINMP441Microphone(I2S_MIC_SERIAL_CLOCK, I2S_MIC_LEFT_RIGHT_CLOCK, I2S_MIC_SERIAL_DATA)) {
      Serial.println("Failed to initialize microphone!");
      return;
    }

    // Set audio parameters for ASR
    asrChat.setAudioParams(SAMPLE_RATE, 16, 1);
    asrChat.setSilenceDuration(1000);  // 1 second silence detection
    asrChat.setMaxRecordingSeconds(50);

    // Set timeout no speech callback - exit continuous mode if timeout without speech
    asrChat.setTimeoutNoSpeechCallback([]() {
      if (continuousMode) {
        stopContinuousMode();
      }
    });

    // Connect to ByteDance ASR WebSocket
    if (!asrChat.connectWebSocket()) {
      Serial.println("Failed to connect to ASR service!");
      return;
    }

    Serial.println("\n----- System Ready -----");
    Serial.println("Press BOOT button to start/stop continuous conversation mode");
    Serial.println("In continuous mode, ASR will automatically restart after TTS playback");
  } else {
    Serial.println("\nFailed to connect to WiFi. Please check network credentials and retry.");
  }
}

void startContinuousMode() {
  continuousMode = true;
  currentState = STATE_LISTENING;

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  Continuous Conversation Mode Started ║");
  Serial.println("║  Press BOOT again to stop             ║");
  Serial.println("╚════════════════════════════════════════╝");

  // Start ASR recording
  if (asrChat.startRecording()) {
    Serial.println("\n[ASR] Listening... Speak now");
  } else {
    Serial.println("\n[ERROR] Failed to start ASR");
    continuousMode = false;
    currentState = STATE_IDLE;
  }
}

void stopContinuousMode() {
  continuousMode = false;

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  Continuous Conversation Mode Stopped ║");
  Serial.println("╚════════════════════════════════════════╝");

  // Stop any ongoing recording
  if (asrChat.isRecording()) {
    asrChat.stopRecording();
  }

  currentState = STATE_IDLE;
  Serial.println("\nPress BOOT button to start continuous conversation mode");
}

void handleASRResult() {
  String transcribedText = asrChat.getRecognizedText();
  asrChat.clearResult();

  if (transcribedText.length() > 0) {
    Serial.println("\n╔═══ ASR Recognition Result ═══╗");
    Serial.printf("║ %s\n", transcribedText.c_str());
    Serial.println("╚══════════════════════════════╝");

    currentState = STATE_PROCESSING_LLM;
    Serial.println("\n[LLM] Sending to ChatGPT...");

    // Send message to ChatGPT
    String response = gptChat.sendMessage(transcribedText);

    if (response != "" && response.length() > 0) {
      Serial.println("\n╔═══ ChatGPT Response ═══╗");
      Serial.printf("║ %s\n", response.c_str());
      Serial.println("╚════════════════════════╝");

      currentState = STATE_PLAYING_TTS;
      Serial.println("\n[TTS] Converting to speech and playing...");

      bool success = gptChat.textToSpeech(response, tts_voice);

      if (success) {
        currentState = STATE_WAIT_TTS_COMPLETE;
        ttsStartTime = millis();
        ttsCheckTime = millis();
      } else {
        Serial.println("[ERROR] Failed to play TTS audio");

        if (continuousMode) {
          // Restart ASR even if TTS fails
          delay(500);
          currentState = STATE_LISTENING;
          if (asrChat.startRecording()) {
            Serial.println("\n[ASR] Listening... Speak now");
          } else {
            stopContinuousMode();
          }
        } else {
          currentState = STATE_IDLE;
        }
      }
    } else {
      Serial.println("[ERROR] Failed to get ChatGPT response");

      if (continuousMode) {
        // Restart ASR even if LLM fails
        delay(500);
        currentState = STATE_LISTENING;
        if (asrChat.startRecording()) {
          Serial.println("\n[ASR] Listening... Speak now");
        } else {
          stopContinuousMode();
        }
      } else {
        currentState = STATE_IDLE;
      }
    }
  } else {
    Serial.println("[WARN] No text recognized");

    if (continuousMode) {
      // Restart ASR
      delay(500);
      currentState = STATE_LISTENING;
      if (asrChat.startRecording()) {
        Serial.println("\n[ASR] Listening... Speak now");
      } else {
        stopContinuousMode();
      }
    } else {
      currentState = STATE_IDLE;
    }
  }
}

void loop() {
  // Handle audio loop (TTS playback)
  audio.loop();

  // Handle ASR processing
  asrChat.loop();

  // Handle boot button (toggle continuous mode)
  buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW); // LOW when pressed

  if (buttonPressed && !wasButtonPressed) {
    wasButtonPressed = true;

    // Toggle continuous conversation mode
    if (!continuousMode && currentState == STATE_IDLE) {
      startContinuousMode();
    } else if (continuousMode) {
      stopContinuousMode();
    }
  } else if (!buttonPressed && wasButtonPressed) {
    wasButtonPressed = false;
  }

  // State machine for continuous conversation
  switch (currentState) {
    case STATE_IDLE:
      // Do nothing, waiting for button press
      break;

    case STATE_LISTENING:
      // Check if ASR has detected end of speech (VAD completed)
      if (asrChat.hasNewResult()) {
        handleASRResult();
      }
      break;

    case STATE_PROCESSING_LLM:
      // This state is handled in handleASRResult()
      break;

    case STATE_PLAYING_TTS:
      // This state is handled in handleASRResult()
      break;

    case STATE_WAIT_TTS_COMPLETE:
      // Check if TTS playback has completed
      // We check audio.isRunning() periodically
      if (millis() - ttsCheckTime > 100) {  // Check every 100ms
        ttsCheckTime = millis();

        if (!audio.isRunning()) {
          // TTS has completed
          Serial.println("[TTS] Playback finished");

          if (continuousMode) {
            // Restart ASR for next round
            delay(500);  // Small delay before restarting
            currentState = STATE_LISTENING;

            if (asrChat.startRecording()) {
              Serial.println("\n[ASR] Listening... Speak now");
            } else {
              Serial.println("[ERROR] Failed to restart ASR");
              stopContinuousMode();
            }
          } else {
            currentState = STATE_IDLE;
          }
        } else {
          // Still playing, check for timeout (optional)
          if (millis() - ttsStartTime > 60000) {  // 60 second timeout
            Serial.println("[WARN] TTS timeout, forcing restart");

            if (continuousMode) {
              currentState = STATE_LISTENING;
              if (asrChat.startRecording()) {
                Serial.println("\n[ASR] Listening... Speak now");
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

  // Very small delay - audio capture needs high priority
  if (currentState == STATE_LISTENING) {
    // During recording, minimize delay to ensure audio data is sent fast enough
    yield();
  } else {
    // In other states, can have slightly longer delay
    delay(10);
  }
}
