// ============================================================
// ESP32 End-to-End Realtime Voice Dialog System
// Using Volcengine Doubao End-to-End Realtime Voice Model API
// Function: Voice Input -> End-to-End Model Processing -> Voice Output (Unified)
// ============================================================

#include <WiFi.h>
#include <time.h>
#include <ArduinoRealtimeDialog.h>

// ============================================================
// I2S Audio Output Pin Definitions (Speaker)
// ============================================================
#define I2S_DOUT 47   // Data output pin
#define I2S_BCLK 48   // Bit clock pin
#define I2S_LRC 45    // Left/Right channel clock pin

// ============================================================
// INMP441 Microphone Input Pin Definitions
// ============================================================
#define I2S_MIC_SERIAL_CLOCK 5      // SCK - Serial clock
#define I2S_MIC_LEFT_RIGHT_CLOCK 4  // WS - Left/Right channel clock
#define I2S_MIC_SERIAL_DATA 6       // SD - Serial data

// ============================================================
// Button Pin Definition
// ============================================================
#define BOOT_BUTTON_PIN 0  // GPIO0 is the BOOT button on most ESP32 dev boards

// ============================================================
// WiFi Network Configuration
// ============================================================
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ============================================================
// Doubao End-to-End API Configuration
// ============================================================
// Get the following credentials from Volcengine Console's "Doubao End-to-End Realtime Voice Model" service:
const char* appId = "YOUR_APP_ID";        // APP ID (numeric format, e.g., "2144169741")
const char* accessKey = "YOUR_ACCESS_KEY"; // Access Key (UUID format)

// ============================================================
// Model Version Selection
// ============================================================
// Choose between "O" version (premium voices) or "SC" version (voice cloning)
const char* modelVersion = "O";  // "O" or "SC"

// ============================================================
// O Version Configuration (Premium Voices)
// ============================================================
// Used when modelVersion = "O"
const char* botName = "Doubao";
const char* systemRole = "You are a friendly AI assistant with a lively and dynamic female voice, cheerful personality, and love for life.";
const char* speakingStyle = "Your speaking style is concise and clear, with moderate speed and natural intonation.";

// O Version Voice Options (default uses vv voice)
// const char* ttsSpeaker = "zh_female_vv_jupiter_bigtts";      // vv voice: lively and dynamic female voice
// const char* ttsSpeaker = "zh_female_xiaohe_jupiter_bigtts";  // xiaohe voice: sweet and lively female voice
// const char* ttsSpeaker = "zh_male_yunzhou_jupiter_bigtts";   // yunzhou voice: refreshing and steady male voice
// const char* ttsSpeaker = "zh_male_xiaotian_jupiter_bigtts";  // xiaotian voice: refreshing and magnetic male voice

// ============================================================
// SC Version Configuration (Voice Cloning)
// ============================================================
// Used when modelVersion = "SC"
// Character description for SC version (replaces botName, systemRole, speakingStyle)
const char* characterManifest = "You are a gentle and considerate AI assistant with a sweet and pleasant voice, cheerful and lively personality.";

// SC Version Voice Options (Cloned Voices)
// Official cloned voices (character descriptions are pre-configured on server):
// const char* ttsSpeaker = "ICL_zh_female_aojiaonvyou_tob";      // Tsundere girlfriend
// const char* ttsSpeaker = "ICL_zh_female_bingjiaojiejie_tob";   // Ice Beauty Sister
// const char* ttsSpeaker = "ICL_zh_female_chengshujiejie_tob";   // Mature Sister
// const char* ttsSpeaker = "ICL_zh_female_keainvsheng_tob";      // Cute Female
// const char* ttsSpeaker = "ICL_zh_male_aiqilingren_tob";        // Loving Husband
// const char* ttsSpeaker = "ICL_zh_male_aojiaogongzi_tob";       // Tsundere Guy
// ... (21 official cloned voices available, see documentation)
//
// Custom cloned voices (register via Volcengine Console):
// const char* ttsSpeaker = "S_YOUR_CUSTOM_VOICE_ID";  // Custom voice ID starting with "S_"

// ============================================================
// Global Object Instantiation
// ============================================================
ArduinoRealtimeDialog realtimeDialog(appId, accessKey);

// ============================================================
// State Variables
// ============================================================
bool buttonPressed = false;
bool wasButtonPressed = false;
bool isInConversation = false;
unsigned long ttsStartTime = 0;

// Callback flags (for safely handling callback events)
volatile bool asrDetectedFlag = false;
volatile bool ttsStartedFlag = false;
volatile bool ttsEndedFlag = false;

// ============================================================
// Callback Functions
// ============================================================

/**
 * @brief ASR detected voice callback
 * Triggered when user starts speaking
 */
void onASRDetected() {
  // Use flag to avoid complex operations in callback
  asrDetectedFlag = true;
}

/**
 * @brief ASR recognition ended callback
 * Triggered when user finishes speaking and recognition is complete
 */
void onASREnded(String recognizedText) {
  Serial.println("\n╔═══ User Finished Speaking ═══╗");
  Serial.print("║ Recognized Text: ");
  Serial.println(recognizedText);
  Serial.println("╚════════════════════════════════╝");
  
  // Model will automatically process and return TTS audio, no need to manually call LLM and TTS
}

/**
 * @brief TTS started playing callback
 */
void onTTSStarted() {
  // Use flag to avoid complex operations in callback
  ttsStartedFlag = true;
}

/**
 * @brief TTS finished playing callback
 */
void onTTSEnded() {
  // Use flag to avoid complex operations in callback
  ttsEndedFlag = true;
}


// ============================================================
// NTP Time Synchronization Function
// ============================================================
bool syncTime() {
  Serial.println("Synchronizing NTP time...");
  
  // Configure NTP servers (using China timezone NTP servers)
  configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp1.aliyun.com", "time.windows.com");
  
  // Wait for time synchronization (max 10 seconds)
  int retry = 0;
  const int maxRetry = 20;
  
  while (retry < maxRetry) {
    time_t now = time(nullptr);
    if (now > 1000000000) {  // Reasonable timestamp (after 2001)
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      Serial.print("NTP time synchronized successfully: ");
      Serial.println(asctime(&timeinfo));
      return true;
    }
    Serial.print(".");
    delay(500);
    retry++;
  }
  
  Serial.println("\nNTP time synchronization failed, but will continue to try connecting");
  return false;
}

// ============================================================
// Initialization Function
// ============================================================
void setup() {
  Serial.begin(115200);  // Increase serial speed for faster debug output
  delay(1000);
  
  Serial.println("\n\n----- End-to-End Realtime Voice Dialog System Starting -----");
  
  // Initialize BOOT button
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize random seed
  randomSeed(analogRead(0) + millis());
  
  // Connect to WiFi
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
    
    // Synchronize NTP time (SSL connection requires correct system time)
    syncTime();
    
    // Initialize end-to-end dialog client
    realtimeDialog.setAudioParams(16000, 16, 1);
    
    // Set model version
    realtimeDialog.setModelVersion(modelVersion);
    
    // Configure based on model version
    if (String(modelVersion) == "SC") {
      // SC Version: Use character manifest and cloned voice
      realtimeDialog.setCharacterManifest(characterManifest);
      // realtimeDialog.setTTSSpeaker(ttsSpeaker);  // Optional: set cloned voice
      Serial.println("Using SC version (Voice Cloning)");
    } else {
      // O Version: Use system role configuration and premium voices
      realtimeDialog.setSystemRole(botName, systemRole, speakingStyle);
      // realtimeDialog.setTTSSpeaker(ttsSpeaker);  // Optional: set premium voice
      Serial.println("Using O version (Premium Voices)");
    }
    
    // Set callback functions
    realtimeDialog.setASRDetectedCallback(onASRDetected);
    realtimeDialog.setASREndedCallback(onASREnded);
    realtimeDialog.setTTSStartedCallback(onTTSStarted);
    realtimeDialog.setTTSEndedCallback(onTTSEnded);
    
    // Initialize I2S audio output (for TTS playback)
    if (!realtimeDialog.initI2SAudioOutput(I2S_BCLK, I2S_LRC, I2S_DOUT)) {
      Serial.println("I2S audio output initialization failed!");
      return;
    }
    
    // Initialize INMP441 microphone
    if (!realtimeDialog.initINMP441Microphone(I2S_MIC_SERIAL_CLOCK, 
                                               I2S_MIC_LEFT_RIGHT_CLOCK, 
                                               I2S_MIC_SERIAL_DATA)) {
      Serial.println("Microphone initialization failed!");
      return;
    }
    
    // Connect to WebSocket server
    if (!realtimeDialog.connectWebSocket()) {
      Serial.println("WebSocket connection failed!");
      return;
    }
    
    Serial.println("\n----- System Ready -----");
    Serial.println("Press BOOT button to start/stop conversation");
  } else {
    Serial.println("\nWiFi connection failed. Please check network credentials and retry.");
  }
}

// ============================================================
// Start Conversation
// ============================================================
void startConversation() {
  isInConversation = true;
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  End-to-End Realtime Dialog Mode ON   ║");
  Serial.println("║  Press BOOT button again to stop      ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  // Check WebSocket connection
  if (!realtimeDialog.isWebSocketConnected()) {
    Serial.println("\n[System] Reconnecting WebSocket...");
    if (!realtimeDialog.connectWebSocket()) {
      Serial.println("\n[Error] WebSocket connection failed");
      isInConversation = false;
      return;
    }
  }
  
  // Start session
  if (!realtimeDialog.startSession()) {
    Serial.println("\n[Error] Unable to start session");
    isInConversation = false;
    return;
  }
  
  delay(100);
  
  // Start recording (internal logging included)
  if (!realtimeDialog.startRecording()) {
    Serial.println("\n[Error] Unable to start recording");
    isInConversation = false;
  }
}

// ============================================================
// Stop Conversation
// ============================================================
void stopConversation() {
  isInConversation = false;
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  End-to-End Realtime Dialog Mode OFF  ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  // Stop recording
  if (realtimeDialog.isRecording()) {
    realtimeDialog.stopRecording();
  }
  
  // End session
  realtimeDialog.finishSession();
  
  // Disconnect WebSocket
  realtimeDialog.disconnectWebSocket();
  
  Serial.println("\nPress BOOT button to restart conversation");
}

// ============================================================
// Main Loop Function
// ============================================================
void loop() {
  // Handle callback flags (safely process in main loop)
  if (asrDetectedFlag) {
    asrDetectedFlag = false;
    Serial.println("\n[Callback] User started speaking detected");
  }
  
  if (ttsStartedFlag) {
    ttsStartedFlag = false;
    Serial.println("\n[Callback] TTS playback started");
    ttsStartTime = millis();
  }
  
  if (ttsEndedFlag) {
    ttsEndedFlag = false;
    Serial.println("\n[Callback] TTS playback ended");
    
    if (isInConversation) {
      // In continuous conversation mode, automatically restart recording after TTS ends
      delay(500);
      if (!realtimeDialog.startRecording()) {
        Serial.println("\n[Error] Unable to restart recording");
        isInConversation = false;
      }
    }
  }
  
  // Process end-to-end dialog loop
  realtimeDialog.loop();
  
  // Handle BOOT button (toggle conversation mode)
  buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  
  // Detect button press event (edge trigger)
  if (buttonPressed && !wasButtonPressed) {
    wasButtonPressed = true;
    
    // Toggle conversation mode
    if (!isInConversation) {
      startConversation();
    } else {
      stopConversation();
    }
  } else if (!buttonPressed && wasButtonPressed) {
    wasButtonPressed = false;
  }
  
  // Adjust delay based on state
  if (realtimeDialog.isRecording()) {
    yield();  // Minimize delay during recording
  } else {
    delay(10);
  }
}