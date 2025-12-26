#ifndef ArduinoGPTChat_h
#define ArduinoGPTChat_h

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Audio.h"
#include "FS.h"
#include "SD.h"
#include "ESP_I2S.h"
#include <vector>

class ArduinoGPTChat {
  public:
    ArduinoGPTChat(const char* apiKey = nullptr, const char* apiBaseUrl = nullptr);
    void setApiConfig(const char* apiKey = nullptr, const char* apiBaseUrl = nullptr);
    void setSystemPrompt(const char* systemPrompt);
    void enableMemory(bool enable);
    void clearMemory();
    String sendMessage(String message);
    bool textToSpeech(String text);
    String speechToText(const char* audioFilePath);
    String speechToTextFromBuffer(uint8_t* audioBuffer, size_t bufferSize);
    String sendImageMessage(const char* imageFilePath, String question);

    // Recording control functions
    void initializeRecording(int micClkPin, int micWsPin, int micDataPin, int sampleRate = 8000,
                             i2s_mode_t mode = I2S_MODE_STD, i2s_data_bit_width_t bitWidth = I2S_DATA_BIT_WIDTH_16BIT,
                             i2s_slot_mode_t slotMode = I2S_SLOT_MODE_MONO, i2s_std_slot_mask_t slotMask = I2S_STD_SLOT_LEFT);
    bool startRecording();
    void continueRecording();
    String stopRecordingAndProcess();
    bool isRecording();
    size_t getRecordedSampleCount();
    
  private:
    void base64_encode(const uint8_t* input, size_t length, char* output);
    size_t base64_encode_length(size_t input_length);
    const char* _apiKey;
    String _apiBaseUrl;
    String _apiUrl;
    String _ttsApiUrl;
    String _sttApiUrl;
    String _systemPrompt;
    String _buildPayload(String message);
    String _processResponse(String response);
    String _buildTTSPayload(String text);
    String _buildMultipartForm(const char* audioFilePath, String boundary);
    void _updateApiUrls();

    // Conversation memory
    bool _memoryEnabled = false;
    std::vector<std::pair<String, String>> _conversationHistory;  // pair<user_msg, assistant_msg>
    const int _maxHistoryPairs = 5;  // Maximum conversation pairs to keep

    // WAV file handling
    uint8_t* createWAVBuffer(int16_t* samples, size_t numSamples);
    size_t calculateWAVSize(size_t numSamples);

    // Recording variables
    I2SClass _recordingI2S;
    std::vector<int16_t> _audioBuffer;
    int _sampleRate;
    int _micClkPin, _micWsPin, _micDataPin;
    const int _bufferSize = 512;
    bool _isRecording = false;

    // I2S configuration parameters
    i2s_mode_t _i2sMode;
    i2s_data_bit_width_t _i2sBitWidth;
    i2s_slot_mode_t _i2sSlotMode;
    i2s_std_slot_mask_t _i2sSlotMask;
};

#endif
