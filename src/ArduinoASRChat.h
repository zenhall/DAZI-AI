#ifndef ArduinoASRChat_h
#define ArduinoASRChat_h

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

/**
 * @file ArduinoASRChat.h
 * @brief ByteDance ASR (Automatic Speech Recognition) chat library header file
 *
 * This library provides WebSocket connection functionality with ByteDance OpenSpeech API,
 * supports real-time speech recognition, supports PDM and INMP441 microphones
 */

// Microphone type selection (if not defined)
#ifndef MICROPHONE_TYPE_DEFINED
#define MICROPHONE_TYPE_DEFINED
enum MicrophoneType {
  MIC_TYPE_PDM,      // PDM microphone (e.g., ESP32-S3 onboard microphone)
  MIC_TYPE_INMP441   // INMP441 I2S MEMS microphone
};
#endif

// ByteDance ASR protocol message types
#define CLIENT_FULL_REQUEST 0b0001      // Client full request (including handshake info)
#define CLIENT_AUDIO_ONLY_REQUEST 0b0010 // Client audio-only request
#define SERVER_FULL_RESPONSE 0b1001     // Server full response
#define SERVER_ACK 0b1011               // Server acknowledgment response
#define SERVER_ERROR_RESPONSE 0b1111    // Server error response

// Sequence flags
#define NO_SEQUENCE 0b0000             // No sequence
#define NEG_SEQUENCE 0b0010            // Negative sequence

/**
 * @class ArduinoASRChat
 * @brief ByteDance speech recognition chat class
 *
 * Provides complete integration with ByteDance OpenSpeech API, supports:
 * - WebSocket real-time connection
 * - Multiple microphone type support
 * - Real-time speech recognition
 * - Silence detection
 * - Timeout control
 * - Callback mechanism
 */
class ArduinoASRChat {
  public:
    /**
     * @brief Constructor
     * @param apiKey API key
     * @param cluster Cluster name, default is "volcengine_input_en"
     */
    ArduinoASRChat(const char* apiKey, const char* cluster = "volcengine_input_en");

    /**
     * @brief Configure API settings
     * @param apiKey API key
     * @param cluster Cluster name, optional
     */
    void setApiConfig(const char* apiKey, const char* cluster = nullptr);

    /**
     * @brief Set microphone type
     * @param micType Microphone type
     */
    void setMicrophoneType(MicrophoneType micType);

    /**
     * @brief Set audio parameters
     * @param sampleRate Sample rate, default 16000Hz
     * @param bitsPerSample Bit depth, default 16 bits
     * @param channels Number of channels, default 1 (mono)
     */
    void setAudioParams(int sampleRate = 16000, int bitsPerSample = 16, int channels = 1);

    /**
     * @brief Set silence duration
     * @param duration Silence duration (milliseconds)
     */
    void setSilenceDuration(unsigned long duration);

    /**
     * @brief Set maximum recording duration
     * @param seconds Maximum recording seconds
     */
    void setMaxRecordingSeconds(int seconds);

    /**
     * @brief Initialize PDM microphone
     * @param pdmClkPin PDM clock pin
     * @param pdmDataPin PDM data pin
     * @return Whether initialization was successful
     */
    bool initPDMMicrophone(int pdmClkPin, int pdmDataPin);

    /**
     * @brief Initialize INMP441 I2S microphone
     * @param i2sSckPin I2S clock pin
     * @param i2sWsPin I2S word select pin
     * @param i2sSdPin I2S data pin
     * @return Whether initialization was successful
     */
    bool initINMP441Microphone(int i2sSckPin, int i2sWsPin, int i2sSdPin);

    /**
     * @brief Connect to WebSocket server
     * @return Whether connection was successful
     */
    bool connectWebSocket();

    /**
     * @brief Disconnect WebSocket connection
     */
    void disconnectWebSocket();

    /**
     * @brief Check if WebSocket is connected
     * @return true if connected, false if not connected
     */
    bool isWebSocketConnected();

    /**
     * @brief Start recording
     * @return Whether start was successful
     */
    bool startRecording();

    /**
     * @brief Stop recording
     */
    void stopRecording();

    /**
     * @brief Check if currently recording
     * @return true if recording, false if not recording
     */
    bool isRecording();

    /**
     * @brief Main loop processing function
     *
     * Must be called in Arduino's loop() function,
     * used to handle WebSocket messages and audio sending
     */
    void loop();

    /**
     * @brief Get recognized text
     * @return Recognized text content
     */
    String getRecognizedText();

    /**
     * @brief Check if there is a new recognition result
     * @return true if new result available, false if no new result
     */
    bool hasNewResult();

    /**
     * @brief Clear recognition result
     */
    void clearResult();

    /**
     * @brief Result callback function type
     * @param text Recognized text
     */
    typedef void (*ResultCallback)(String text);

    /**
     * @brief Set result callback function
     * @param callback Callback function pointer
     */
    void setResultCallback(ResultCallback callback);

    /**
     * @brief Timeout no speech callback function type
     */
    typedef void (*TimeoutNoSpeechCallback)();

    /**
     * @brief Set timeout no speech callback function
     * @param callback Callback function pointer
     */
    void setTimeoutNoSpeechCallback(TimeoutNoSpeechCallback callback);

  private:
    // WebSocket configuration
    const char* _apiKey;                    // API key
    const char* _cluster;                   // Cluster name
    const char* _wsHost = "openspeech.bytedance.com";  // WebSocket host
    const int _wsPort = 443;                // WebSocket port
    const char* _wsPath = "/api/v2/asr";   // WebSocket path

    // Audio parameters
    int _sampleRate = 16000;                // Sample rate
    int _bitsPerSample = 16;                // Bit depth
    int _channels = 1;                      // Number of channels
    int _samplesPerRead = 800;              // Samples per read (50ms data)
    int _sendBatchSize = 3200;              // Send batch size (200ms data)
    unsigned long _silenceDuration = 1000;  // Silence detection duration (milliseconds)
    int _maxSeconds = 50;                  // Maximum recording duration

    // Microphone configuration
    MicrophoneType _micType = MIC_TYPE_INMP441;  // Microphone type
    I2SClass _I2S;                              // I2S object

    // WiFi client
    WiFiClientSecure _client;                  // Secure WiFi client

    // Status flags
    bool _wsConnected = false;                 // WebSocket connection status
    bool _isRecording = false;                 // Recording status
    bool _shouldStop = false;                  // Should stop flag
    bool _hasSpeech = false;                   // Speech detected flag
    bool _hasNewResult = false;                // Has new result flag
    bool _endMarkerSent = false;               // Track if end marker has been sent

    // Recording state
    String _lastResultText = "";               // Previous result text
    String _recognizedText = "";               // Recognized text
    unsigned long _recordingStartTime = 0;    // Recording start time
    unsigned long _lastSpeechTime = 0;         // Last speech time
    int _sameResultCount = 0;                  // Same result count
    unsigned long _lastDotTime = 0;            // Last dot time

    // Audio buffer
    int16_t* _sendBuffer;                     // Send buffer
    int _sendBufferPos = 0;                    // Send buffer position

    // Callback functions
    ResultCallback _resultCallback = nullptr;           // Result callback function
    TimeoutNoSpeechCallback _timeoutNoSpeechCallback = nullptr;  // Timeout no speech callback function

    // Private helper methods
    String generateWebSocketKey();            // Generate WebSocket key
    void handleWebSocketData();                // Handle WebSocket data
    void sendWebSocketFrame(uint8_t* data, size_t len, uint8_t opcode);  // Send WebSocket frame
    void sendFullRequest();                   // Send full request
    void sendAudioChunk(uint8_t* data, size_t len);  // Send audio chunk
    void sendEndMarker();                      // Send end marker
    void sendPong();                           // Send Pong response
    void parseResponse(uint8_t* data, size_t len);   // Parse response
    void processAudioSending();                // Process audio sending
    void checkRecordingTimeout();             // Check recording timeout
    void checkSilence();                       // Check silence
};

#endif
