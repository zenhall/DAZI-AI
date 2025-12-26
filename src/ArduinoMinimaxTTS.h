#ifndef ArduinoMinimaxTTS_h
#define ArduinoMinimaxTTS_h

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "Audio.h"

/**
 * @file ArduinoMinimaxTTS.h
 * @brief MiniMax Text-to-Speech (TTS) library header file
 *
 * This library provides HTTP REST API connection functionality with MiniMax TTS API,
 * supports long text speech synthesis and playback
 */

/**
 * @class ArduinoMinimaxTTS
 * @brief MiniMax Text-to-Speech class
 *
 * Provides complete integration with MiniMax TTS API, supports:
 * - HTTP REST API connection
 * - Long text speech synthesis
 * - Audio playback via Audio library
 * - Multiple voice and parameter configurations
 */
class ArduinoMinimaxTTS {
  public:
    /**
     * @brief Constructor
     * @param apiKey MiniMax API key (Bearer Token)
     * @param groupId MiniMax Group ID
     * @param audio Audio object pointer, used for playing synthesized audio
     */
    ArduinoMinimaxTTS(const char* apiKey, const char* groupId, Audio* audio);
    
    /**
     * @brief Destructor
     */
    ~ArduinoMinimaxTTS();

    /**
     * @brief Set API key
     * @param apiKey API key
     */
    void setApiKey(const char* apiKey);

    /**
     * @brief Set Group ID
     * @param groupId Group ID
     */
    void setGroupId(const char* groupId);

    /**
     * @brief Set voice ID
     * @param voiceId Voice ID, default is "male-qn-qingse"
     */
    void setVoiceId(const char* voiceId);

    /**
     * @brief Set speech speed
     * @param speed Speech speed, range [0.5, 2.0], default 1.0
     */
    void setSpeed(float speed);

    /**
     * @brief Set volume
     * @param vol Volume, range (0, 10], default 1.0
     */
    void setVolume(float vol);

    /**
     * @brief Set pitch
     * @param pitch Pitch, range [-12, 12], default 0
     */
    void setPitch(int pitch);

    /**
     * @brief Set emotion
     * @param emotion Emotion, options: "happy","sad","angry","fearful","disgusted","surprised","calm","fluent","whisper"
     */
    void setEmotion(const char* emotion);

    /**
     * @brief Set audio format
     * @param format Audio format, "mp3" or "pcm" or "flac", default "mp3"
     */
    void setAudioFormat(const char* format);

    /**
     * @brief Set sample rate
     * @param sampleRate Sample rate, options: 8000,16000,22050,24000,32000,44100, default 32000
     */
    void setSampleRate(int sampleRate);

    /**
     * @brief Set bitrate
     * @param bitrate Bitrate, options: 32000,64000,128000,256000, default 128000
     */
    void setBitrate(int bitrate);

    /**
     * @brief Set model
     * @param model Model name, default "speech-2.6-hd"
     */
    void setModel(const char* model);

    /**
     * @brief Synthesize text to speech and play
     * @param text Text to synthesize
     * @return Whether synthesis was successful
     */
    bool synthesizeAndPlay(const String& text);

  private:
    // API configuration
    const char* _apiKey;                           // API key
    const char* _groupId;                          // Group ID
    const char* _url;                              // API URL

    // TTS parameters
    const char* _model = "speech-01";              // Model
    const char* _voiceId = "male-qn-qingse";       // Voice ID
    float _speed = 1.0;                            // Speech speed
    float _volume = 1.0;                           // Volume
    int _pitch = 0;                                // Pitch
    const char* _emotion = nullptr;                // Emotion
    const char* _audioFormat = "mp3";              // Audio format
    int _sampleRate = 32000;                       // Sample rate
    int _bitrate = 128000;                         // Bitrate
    int _channel = 1;                              // Number of channels

    // Audio player
    Audio* _audio;                                 // Audio object pointer

    // Private helper methods
    bool synthesizeAndPlayFromURL(const String& text);  // Use URL mode (fastest, no decoding needed)
    bool saveAudioToFile(const String& text, const char* filepath);  // Stream save audio to file
    bool getAudioDataToPSRAM(const String& text, uint8_t** outBuffer, size_t* outSize);  // Get audio data to PSRAM
    uint8_t hexCharToByte(char high, char low);    // Hex character to byte
};

#endif