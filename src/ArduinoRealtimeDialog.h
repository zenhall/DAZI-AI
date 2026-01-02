#ifndef ArduinoRealtimeDialog_h
#define ArduinoRealtimeDialog_h

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include <mbedtls/base64.h>
#include "I2SAudioPlayer.h"

/**
 * @file ArduinoRealtimeDialog.h
 * @brief Doubao End-to-End Realtime Voice LLM API Client
 *
 * Supports real-time voice-to-voice conversation without separating ASR, LLM, TTS steps
 */

// Microphone type (if not defined)
#ifndef MICROPHONE_TYPE_DEFINED
#define MICROPHONE_TYPE_DEFINED
enum MicrophoneType {
  MIC_TYPE_PDM,      // PDM microphone
  MIC_TYPE_INMP441   // INMP441 I2S MEMS microphone
};
#endif

// Client event IDs
#define EVENT_START_CONNECTION 1
#define EVENT_FINISH_CONNECTION 2
#define EVENT_START_SESSION 100
#define EVENT_FINISH_SESSION 102
#define EVENT_TASK_REQUEST 200
#define EVENT_SAY_HELLO 300
#define EVENT_CHAT_TTS_TEXT 500
#define EVENT_CHAT_TEXT_QUERY 501
#define EVENT_CHAT_RAG_TEXT 502

// Server event IDs
#define EVENT_CONNECTION_STARTED 50
#define EVENT_CONNECTION_FAILED 51
#define EVENT_CONNECTION_FINISHED 52
#define EVENT_SESSION_STARTED 150
#define EVENT_SESSION_FINISHED 152
#define EVENT_SESSION_FAILED 153
#define EVENT_USAGE_RESPONSE 154
#define EVENT_TTS_SENTENCE_START 350
#define EVENT_TTS_SENTENCE_END 351
#define EVENT_TTS_RESPONSE 352
#define EVENT_TTS_ENDED 359
#define EVENT_ASR_INFO 450
#define EVENT_ASR_RESPONSE 451
#define EVENT_ASR_ENDED 459
#define EVENT_CHAT_RESPONSE 550
#define EVENT_CHAT_TEXT_QUERY_CONFIRMED 553
#define EVENT_CHAT_ENDED 559

// Message types
#define MSG_TYPE_CLIENT_FULL 0b0001
#define MSG_TYPE_CLIENT_AUDIO 0b0010
#define MSG_TYPE_SERVER_FULL 0b1001
#define MSG_TYPE_SERVER_ACK 0b1011
#define MSG_TYPE_SERVER_ERROR 0b1111

// Message flags
#define MSG_FLAG_NO_SEQUENCE 0b0000
#define MSG_FLAG_WITH_EVENT 0b0100

// Serialization methods
#define SERIAL_RAW 0b0000
#define SERIAL_JSON 0b0001

// Compression methods
#define COMPRESS_NONE 0b0000
#define COMPRESS_GZIP 0b0001

/**
 * @class ArduinoRealtimeDialog
 * @brief End-to-End Realtime Voice Dialog Class
 */
class ArduinoRealtimeDialog {
  public:
    /**
     * @brief Constructor
     * @param appId APP ID (obtained from Volcengine console, numeric format)
     * @param accessKey Access Key (obtained from Volcengine console, UUID format)
     */
    ArduinoRealtimeDialog(const char* appId, const char* accessKey);

    /**
     * @brief Allocate audio buffer memory
     */
    bool allocateBuffers();

    /**
     * @brief Set audio parameters
     */
    void setAudioParams(int sampleRate = 16000, int bitsPerSample = 16, int channels = 1);

    /**
     * @brief Set model version ("O" or "SC")
     * @param version Model version, "O" for O version (supports premium voices), "SC" for SC version (supports cloned voices)
     */
    void setModelVersion(const char* version);

    /**
     * @brief Set TTS voice
     * @param speaker Voice name
     *   - O version: zh_female_vv_jupiter_bigtts, zh_female_xiaohe_jupiter_bigtts,
     *            zh_male_yunzhou_jupiter_bigtts, zh_male_xiaotian_jupiter_bigtts
     *   - SC version: Official cloned voices starting with ICL_ or custom cloned voices starting with S_
     */
    void setTTSSpeaker(const char* speaker);

    /**
     * @brief Set system role (O version only)
     * @param botName Bot name
     * @param systemRole System role description
     * @param speakingStyle Speaking style
     */
    void setSystemRole(const char* botName, const char* systemRole, const char* speakingStyle);

    /**
     * @brief Set character manifest (SC version only)
     * @param manifest Character description text for SC version role-playing
     */
    void setCharacterManifest(const char* manifest);

    /**
     * @brief Initialize INMP441 microphone
     */
    bool initINMP441Microphone(int i2sSckPin, int i2sWsPin, int i2sSdPin);

    /**
     * @brief Initialize I2S audio output (for TTS playback)
     * @param bclk Bit clock pin
     * @param lrc Left/right channel clock pin
     * @param dout Data output pin
     */
    bool initI2SAudioOutput(int bclk, int lrc, int dout);

    /**
     * @brief Connect to WebSocket server
     */
    bool connectWebSocket();

    /**
     * @brief Disconnect WebSocket connection
     */
    void disconnectWebSocket();

    /**
     * @brief Check if WebSocket is connected
     */
    bool isWebSocketConnected();

    /**
     * @brief Start session
     */
    bool startSession();

    /**
     * @brief Finish session
     */
    void finishSession();

    /**
     * @brief Start recording and send audio
     */
    bool startRecording();

    /**
     * @brief Stop recording
     */
    void stopRecording();

    /**
     * @brief Check if currently recording
     */
    bool isRecording();

    /**
     * @brief Main loop processing function
     */
    void loop();

    /**
     * @brief Check if TTS is playing
     */
    bool isPlayingTTS();

    /**
     * @brief Get recognized text
     */
    String getRecognizedText();

    /**
     * @brief Clear recognized text
     */
    void clearRecognizedText();

    /**
     * @brief ASR speech detected callback
     */
    typedef void (*ASRDetectedCallback)();
    void setASRDetectedCallback(ASRDetectedCallback callback);

    /**
     * @brief ASR ended callback
     */
    typedef void (*ASREndedCallback)(String text);
    void setASREndedCallback(ASREndedCallback callback);

    /**
     * @brief TTS started callback
     */
    typedef void (*TTSStartedCallback)();
    void setTTSStartedCallback(TTSStartedCallback callback);

    /**
     * @brief TTS ended callback
     */
    typedef void (*TTSEndedCallback)();
    void setTTSEndedCallback(TTSEndedCallback callback);

  private:
    // WebSocket configuration
    const char* _appId;
    const char* _accessKey;
    const char* _wsHost = "openspeech.bytedance.com";
    const int _wsPort = 443;
    const char* _wsPath = "/api/v3/realtime/dialogue";

    // Audio parameters
    int _sampleRate = 16000; // Sampling rate
    int _bitsPerSample = 16; // Bits per sample
    int _channels = 1; // Number of channels
    int _samplesPerRead = 800; // Samples per read
    int _sendBatchSize = 3200; // Send batch size
    const char* _ttsSpeaker = "zh_female_vv_jupiter_bigtts"; // TTS speaker

    // Model version configuration
    String _modelVersion = "O";  // Default to O version

    // System role configuration (O version)
    String _botName = "Doubao"; // Bot name
    String _systemRole = ""; // System role
    String _speakingStyle = ""; // Speaking style

    // Character manifest configuration (SC version)
    String _characterManifest = ""; // Character manifest

    // Microphone configuration
    MicrophoneType _micType = MIC_TYPE_INMP441; // Microphone type
    I2SClass _I2S; // I2S interface

    // I2S audio player
    I2SAudioPlayer _i2sPlayer; // I2S audio player

    // WiFi client
    WiFiClientSecure _client; // WiFi client

    // Status flags
    bool _wsConnected = false; // WebSocket connected
    bool _sessionStarted = false; // Session started
    bool _isRecording = false; // Recording
    bool _isPlayingTTS = false; // Playing TTS
    bool _userSpeaking = false; // User speaking

    // Session ID
    String _sessionId; // Session ID
    String _dialogId; // Dialog ID

    // Recognized text
    String _recognizedText = ""; // Recognized text
    String _lastASRText = ""; // Last ASR text

    // Audio buffer
    int16_t* _sendBuffer; // Send buffer
    int _sendBufferPos = 0; // Send buffer position

    // TTS audio buffer (for receiving and playing PCM data, preferably allocated 1MB from PSRAM)
    uint8_t* _ttsBuffer = nullptr; // TTS buffer
    size_t _ttsBufferSize = 0; // TTS buffer size
    size_t _ttsBufferPos = 0; // TTS buffer position

    // Callback functions
    ASRDetectedCallback _asrDetectedCallback = nullptr; // ASR detected callback
    ASREndedCallback _asrEndedCallback = nullptr; // ASR ended callback
    TTSStartedCallback _ttsStartedCallback = nullptr; // TTS started callback
    TTSEndedCallback _ttsEndedCallback = nullptr; // TTS ended callback

    // Private methods
    String generateWebSocketKey(); // Generate WebSocket key
    String generateSessionId(); // Generate session ID
    void handleWebSocketData(); // Handle WebSocket data
    void sendWebSocketFrame(uint8_t* data, size_t len, uint8_t opcode); // Send WebSocket frame
    
    // Protocol related
    void sendStartConnection(); // Send start connection
    void sendFinishConnection(); // Send finish connection
    void sendStartSession(); // Send start session
    void sendFinishSession(); // Send finish session
    void sendAudioChunk(uint8_t* data, size_t len); // Send audio chunk
    void sendPong(); // Send pong
    
    // Parse response
    void parseResponse(uint8_t* data, size_t len); // Parse response
    void handleServerEvent(int eventId, JsonObject& payload); // Handle server event
    
    // Audio processing
    void processAudioSending(); // Process audio sending
    void processTTSAudio(uint8_t* data, size_t len); // Process TTS audio
};

#endif