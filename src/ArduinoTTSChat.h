#ifndef ArduinoTTSChat_h
#define ArduinoTTSChat_h

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include <mbedtls/base64.h>

/**
 * @file ArduinoTTSChat.h
 * @brief MiniMax TTS (Text-to-Speech) library header file
 *
 * This library provides WebSocket connection functionality with MiniMax TTS API,
 * supports real-time text-to-speech synthesis with streaming audio output
 */

// Speaker type selection
#ifndef SPEAKER_TYPE_DEFINED
#define SPEAKER_TYPE_DEFINED
enum SpeakerType {
  SPEAKER_TYPE_INTERNAL,   // Internal DAC (GPIO25/26)
  SPEAKER_TYPE_MAX98357    // MAX98357 I2S amplifier
};
#endif

/**
 * @class ArduinoTTSChat
 * @brief MiniMax text-to-speech class
 *
 * Provides complete integration with MiniMax TTS API, supports:
 * - WebSocket real-time connection
 * - Streaming audio playback
 * - Multiple voice options
 * - Audio parameter configuration
 */
class ArduinoTTSChat {
  public:
    /**
     * @brief Constructor
     * @param apiKey MiniMax API key (JWT token)
     */
    ArduinoTTSChat(const char* apiKey);

    /**
     * @brief Destructor
     */
    ~ArduinoTTSChat();

    /**
     * @brief Configure API settings
     * @param apiKey API key
     */
    void setApiKey(const char* apiKey);

    /**
     * @brief Set voice ID
     * @param voiceId Voice ID (e.g., "male-qn-qingse", "female-shaonv")
     */
    void setVoiceId(const char* voiceId);

    /**
     * @brief Set speech speed
     * @param speed Speed multiplier (0.5 - 2.0, default 1.0)
     */
    void setSpeed(float speed);

    /**
     * @brief Set volume
     * @param vol Volume multiplier (0.1 - 1.0, default 1.0)
     */
    void setVolume(float vol);

    /**
     * @brief Set pitch
     * @param pitch Pitch adjustment (-12 to 12, default 0)
     */
    void setPitch(int pitch);

    /**
     * @brief Set audio parameters
     * @param sampleRate Sample rate (default 32000Hz)
     * @param bitrate Bitrate (default 128000)
     */
    void setAudioParams(int sampleRate = 32000, int bitrate = 128000);

    /**
     * @brief Initialize MAX98357 I2S speaker
     * @param bclkPin Bit clock pin
     * @param lrclkPin LR clock (word select) pin
     * @param doutPin Data out pin
     * @return Whether initialization was successful
     */
    bool initMAX98357Speaker(int bclkPin, int lrclkPin, int doutPin);

    /**
     * @brief Initialize internal DAC speaker
     * @param dacPin DAC output pin (25 or 26 on ESP32)
     * @return Whether initialization was successful
     */
    bool initInternalDAC(int dacPin = 25);

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
     * @brief Start TTS task (send task_start)
     * @return Whether start was successful
     */
    bool startTask();

    /**
     * @brief Synthesize text to speech
     * @param text Text to synthesize
     * @return Whether synthesis started successfully
     */
    bool speak(const char* text);

    /**
     * @brief Check if currently playing audio
     * @return true if playing, false if not playing
     */
    bool isPlaying();

    /**
     * @brief Stop current playback
     */
    void stop();

    /**
     * @brief Main loop processing function
     *
     * Must be called in Arduino's loop() function,
     * used to handle WebSocket messages and audio playback
     */
    void loop();

    /**
     * @brief Completion callback function type
     */
    typedef void (*CompletionCallback)();

    /**
     * @brief Set completion callback function
     * @param callback Callback function pointer, called when TTS playback completes
     */
    void setCompletionCallback(CompletionCallback callback);

    /**
     * @brief Error callback function type
     * @param error Error message
     */
    typedef void (*ErrorCallback)(const char* error);

    /**
     * @brief Set error callback function
     * @param callback Callback function pointer
     */
    void setErrorCallback(ErrorCallback callback);

  private:
    // WebSocket configuration
    const char* _apiKey;                    // API key (JWT token)
    const char* _wsHost = "api.minimaxi.com";  // WebSocket host
    const int _wsPort = 443;                // WebSocket port
    const char* _wsPath = "/ws/v1/t2a_v2"; // WebSocket path
    const char* _model = "speech-2.6-hd";   // TTS model

    // Voice settings
    const char* _voiceId = "male-qn-qingse";  // Voice ID
    float _speed = 1.0;                     // Speech speed
    float _volume = 1.0;                    // Volume
    int _pitch = 0;                         // Pitch
    bool _englishNorm = false;              // English normalization

    // Audio parameters
    int _sampleRate = 16000;                // Sample rate (lower for smaller data)
    int _bitrate = 32000;                   // Bitrate (minimum 32k)
    const char* _format = "pcm";            // Audio format (pcm for direct playback)
    int _channels = 1;                      // Number of channels

    // Speaker configuration
    SpeakerType _speakerType = SPEAKER_TYPE_MAX98357;  // Speaker type
    I2SClass _I2S;                          // I2S object
    bool _speakerInitialized = false;       // Speaker initialized flag

    // WiFi client
    WiFiClientSecure _client;               // Secure WiFi client

    // Status flags (volatile for multi-task access)
    bool _wsConnected = false;              // WebSocket connection status
    bool _taskStarted = false;              // Task started flag
    volatile bool _isPlaying = false;       // Playing status
    volatile bool _shouldStop = false;      // Should stop flag
    volatile bool _receivingAudio = false;  // Receiving audio flag

    // Audio ring buffer - use PSRAM if available for larger buffer
    static const size_t AUDIO_BUFFER_SIZE = 524288;  // 512KB for long sentences
    uint8_t* _audioBuffer;                  // Audio ring buffer
    volatile size_t _audioWritePos = 0;     // Write position (producer)
    volatile size_t _audioReadPos = 0;      // Read position (consumer)
    volatile size_t _audioDataSize = 0;     // Current data size in buffer

    // WebSocket message reassembly buffer
    static const size_t MSG_BUFFER_SIZE = 65536;  // Max message size
    uint8_t* _msgBuffer = nullptr;          // Message reassembly buffer
    size_t _msgBufferPos = 0;               // Current position in message buffer
    bool _msgInProgress = false;            // Whether we're assembling a fragmented message

    // Statistics (volatile for multi-task access)
    unsigned long _playStartTime = 0;       // Playback start time
    volatile int _chunksReceived = 0;       // Chunks received count

    // FreeRTOS audio playback task
    TaskHandle_t _audioTaskHandle = nullptr;  // Audio playback task handle
    static void audioTaskWrapper(void* param);  // Static wrapper for task
    void audioTaskLoop();                     // Audio task main loop

    // Callback functions
    CompletionCallback _completionCallback = nullptr;  // Completion callback
    ErrorCallback _errorCallback = nullptr;            // Error callback

    // Private helper methods
    String generateWebSocketKey();          // Generate WebSocket key
    void handleWebSocketData();             // Handle WebSocket data
    void sendWebSocketFrame(uint8_t* data, size_t len, uint8_t opcode);  // Send WebSocket frame
    void sendTextFrame(const char* text);   // Send text WebSocket frame
    void sendTaskStart();                   // Send task_start message
    void sendTaskContinue(const char* text); // Send task_continue message
    void sendTaskFinish();                  // Send task_finish message
    void sendPong();                        // Send Pong response
    void parseJsonResponse(const char* json, size_t len);  // Parse JSON response
    void processAudioPlayback();            // Process audio playback
    size_t hexToBytes(const char* hex, size_t hexLen, uint8_t* output, size_t outputSize);  // Convert hex to bytes
    size_t readBytesWithTimeout(uint8_t* buffer, size_t len, unsigned long timeout_ms); // Reliable read helper
};

#endif
