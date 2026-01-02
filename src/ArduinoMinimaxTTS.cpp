/**
 * @file ArduinoMinimaxTTS.cpp
 * @brief MiniMax Text-to-Speech (TTS) class implementation
 * @details Implements long text speech synthesis and playback via HTTP REST API
 */

#include "ArduinoMinimaxTTS.h"

/**
 * @brief Constructor - Initialize MiniMax TTS client
 * @param apiKey MiniMax API key (Bearer Token)
 * @param groupId MiniMax Group ID
 * @param audio Audio object pointer
 */
ArduinoMinimaxTTS::ArduinoMinimaxTTS(const char* apiKey, const char* groupId, Audio* audio) {
  _apiKey = apiKey;
  _groupId = groupId;
  _audio = audio;
  _url = "https://api.minimaxi.com/v1/t2a_v2";
}

/**
 * @brief Destructor
 */
ArduinoMinimaxTTS::~ArduinoMinimaxTTS() {
  // Cleanup resources
}

/**
 * @brief Set API key
 * @param apiKey API key
 */
void ArduinoMinimaxTTS::setApiKey(const char* apiKey) {
  _apiKey = apiKey;
}

/**
 * @brief Set Group ID
 * @param groupId Group ID
 */
void ArduinoMinimaxTTS::setGroupId(const char* groupId) {
  _groupId = groupId;
}

/**
 * @brief Set voice ID
 * @param voiceId Voice ID
 */
void ArduinoMinimaxTTS::setVoiceId(const char* voiceId) {
  _voiceId = voiceId;
}

/**
 * @brief Set speech speed
 * @param speed Speech speed, range [0.5, 2.0]
 */
void ArduinoMinimaxTTS::setSpeed(float speed) {
  if (speed >= 0.5 && speed <= 2.0) {
    _speed = speed;
  }
}

/**
 * @brief Set volume
 * @param vol Volume, range (0, 10]
 */
void ArduinoMinimaxTTS::setVolume(float vol) {
  if (vol > 0 && vol <= 10.0) {
    _volume = vol;
  }
}

/**
 * @brief Set pitch
 * @param pitch Pitch, range [-12, 12]
 */
void ArduinoMinimaxTTS::setPitch(int pitch) {
  if (pitch >= -12 && pitch <= 12) {
    _pitch = pitch;
  }
}

/**
 * @brief Set emotion
 * @param emotion Emotion
 */
void ArduinoMinimaxTTS::setEmotion(const char* emotion) {
  _emotion = emotion;
}

/**
 * @brief Set audio format
 * @param format Audio format
 */
void ArduinoMinimaxTTS::setAudioFormat(const char* format) {
  _audioFormat = format;
}

/**
 * @brief Set sample rate
 * @param sampleRate Sample rate
 */
void ArduinoMinimaxTTS::setSampleRate(int sampleRate) {
  _sampleRate = sampleRate;
}

/**
 * @brief Set bitrate
 * @param bitrate Bitrate
 */
void ArduinoMinimaxTTS::setBitrate(int bitrate) {
  _bitrate = bitrate;
}

/**
 * @brief Set model
 * @param model Model name
 */
void ArduinoMinimaxTTS::setModel(const char* model) {
  _model = model;
}

/**
 * @brief Save audio data directly to file
 * @param text Text to synthesize
 * @param filepath File path to save
 * @return Success status
 */
bool ArduinoMinimaxTTS::saveAudioToFile(const String& text, const char* filepath) {
  HTTPClient http;
  http.setTimeout(30000);  // 30 second timeout
  
  // Add GroupId parameter
  String urlWithParams = String(_url) + "?GroupId=" + _groupId;
  http.begin(urlWithParams);
  http.addHeader("Content-Type", "application/json");
  
  String token_key = String("Bearer ") + _apiKey;
  http.addHeader("Authorization", token_key);

  // Create JSON document
  DynamicJsonDocument doc(1024);
  doc["model"] = _model;
  doc["text"] = text;
  doc["stream"] = false;  // Non-streaming
  
  // voice_setting object
  JsonObject voice_setting = doc.createNestedObject("voice_setting");
  voice_setting["voice_id"] = _voiceId;
  voice_setting["speed"] = _speed;
  voice_setting["vol"] = _volume;
  voice_setting["pitch"] = _pitch;
  if (_emotion != nullptr && strlen(_emotion) > 0) {
    voice_setting["emotion"] = _emotion;
  }
  
  // audio_setting object
  JsonObject audio_setting = doc.createNestedObject("audio_setting");
  audio_setting["sample_rate"] = _sampleRate;
  audio_setting["bitrate"] = _bitrate;
  audio_setting["format"] = _audioFormat;
  audio_setting["channel"] = _channel;

  // Serialize JSON
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("[MiniMax TTS] Sending request:");
  Serial.println(jsonString);

  // Send POST request
  int httpResponseCode = http.POST(jsonString);

  if (httpResponseCode != 200) {
    Serial.println("[MiniMax TTS] HTTP request failed");
    Serial.printf("[MiniMax TTS] Response code: %d\n", httpResponseCode);
    if (http.connected()) {
      String errorResponse = http.getString();
      Serial.println("[MiniMax TTS] Error response:");
      Serial.println(errorResponse);
    }
    http.end();
    return false;
  }

  // Get response stream
  WiFiClient* stream = http.getStreamPtr();
  
  // Open file for writing
  File file = SPIFFS.open(filepath, FILE_WRITE);
  if (!file) {
    Serial.println("[MiniMax TTS] Cannot create file");
    http.end();
    return false;
  }

  Serial.println("[MiniMax TTS] Starting to receive and parse response...");
  
  // Read response until finding "audio":"
  String buffer = "";
  bool foundAudio = false;
  int bytesRead = 0;
  
  while (http.connected() && bytesRead < 10000) {  // Only read first 10KB to find audio field
    if (stream->available()) {
      char c = stream->read();
      buffer += c;
      bytesRead++;
      
      // Find "audio":"
      int pos = buffer.indexOf("\"audio\":\"");
      if (pos >= 0) {
        foundAudio = true;
        // Skip already read part, locate to hex data start
        buffer = buffer.substring(pos + 9);  // 9 = length of "audio":"
        break;
      }
      
      // Keep buffer size reasonable
      if (buffer.length() > 1000) {
        buffer = buffer.substring(buffer.length() - 500);
      }
    }
  }

  if (!foundAudio) {
    Serial.println("[MiniMax TTS] Audio field not found");
    file.close();
    http.end();
    return false;
  }

  Serial.println("[MiniMax TTS] Found audio data, starting decode and write to file...");
  
  // Stream read hex data and decode to file
  size_t totalDecoded = 0;
  uint8_t hexPair[2];
  int hexIndex = 0;
  
  // Process remaining data in buffer
  for (size_t i = 0; i < buffer.length(); i++) {
    char c = buffer[i];
    if (c == '"') break;  // Encountered closing quote
    
    if (isxdigit(c)) {
      hexPair[hexIndex++] = c;
      if (hexIndex == 2) {
        uint8_t byte = hexCharToByte(hexPair[0], hexPair[1]);
        file.write(byte);
        totalDecoded++;
        hexIndex = 0;
      }
    }
  }
  
  // Continue reading from stream
  while (http.connected() || stream->available()) {
    if (stream->available()) {
      char c = stream->read();
      
      if (c == '"') break;  // Encountered closing quote
      
      if (isxdigit(c)) {
        hexPair[hexIndex++] = c;
        if (hexIndex == 2) {
          uint8_t byte = hexCharToByte(hexPair[0], hexPair[1]);
          file.write(byte);
          totalDecoded++;
          hexIndex = 0;
          
          // Print progress every 1KB
          if (totalDecoded % 1024 == 0) {
            Serial.printf("[MiniMax TTS] Decoded: %d bytes\r", totalDecoded);
          }
        }
      }
    }
  }
  
  file.close();
  http.end();
  
  Serial.printf("\n[MiniMax TTS] Decode complete, total: %d bytes\n", totalDecoded);
  
  return totalDecoded > 0;
}

/**
 * @brief Convert hex character to byte
 */
uint8_t ArduinoMinimaxTTS::hexCharToByte(char high, char low) {
  uint8_t high_val = (high >= '0' && high <= '9') ? (high - '0') :
                     (high >= 'a' && high <= 'f') ? (high - 'a' + 10) :
                     (high >= 'A' && high <= 'F') ? (high - 'A' + 10) : 0;
  
  uint8_t low_val = (low >= '0' && low <= '9') ? (low - '0') :
                    (low >= 'a' && low <= 'f') ? (low - 'a' + 10) :
                    (low >= 'A' && low <= 'F') ? (low - 'A' + 10) : 0;
  
  return (high_val << 4) | low_val;
}

/**
 * @brief Get audio data using PSRAM (faster, requires PSRAM)
 * @param text Text to synthesize
 * @param outBuffer Output buffer pointer
 * @param outSize Output size
 * @return Success status
 */
bool ArduinoMinimaxTTS::getAudioDataToPSRAM(const String& text, uint8_t** outBuffer, size_t* outSize) {
  HTTPClient http;
  http.setTimeout(30000);
  
  String urlWithParams = String(_url) + "?GroupId=" + _groupId;
  http.begin(urlWithParams);
  http.addHeader("Content-Type", "application/json");
  
  String token_key = String("Bearer ") + _apiKey;
  http.addHeader("Authorization", token_key);

  // Create request JSON
  DynamicJsonDocument doc(1024);
  doc["model"] = _model;
  doc["text"] = text;
  doc["stream"] = false;
  
  JsonObject voice_setting = doc.createNestedObject("voice_setting");
  voice_setting["voice_id"] = _voiceId;
  voice_setting["speed"] = _speed;
  voice_setting["vol"] = _volume;
  voice_setting["pitch"] = _pitch;
  if (_emotion != nullptr && strlen(_emotion) > 0) {
    voice_setting["emotion"] = _emotion;
  }
  
  JsonObject audio_setting = doc.createNestedObject("audio_setting");
  audio_setting["sample_rate"] = _sampleRate;
  audio_setting["bitrate"] = _bitrate;
  audio_setting["format"] = _audioFormat;
  audio_setting["channel"] = _channel;

  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("[MiniMax TTS] Sending request (PSRAM mode)");

  int httpResponseCode = http.POST(jsonString);

  if (httpResponseCode != 200) {
    Serial.printf("[MiniMax TTS] HTTP request failed: %d\n", httpResponseCode);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  
  // Allocate buffer in PSRAM (estimated max 500KB)
  size_t maxSize = 500 * 1024;
  uint8_t* psramBuffer = (uint8_t*)ps_malloc(maxSize);
  if (!psramBuffer) {
    Serial.println("[MiniMax TTS] PSRAM allocation failed");
    http.end();
    return false;
  }

  Serial.println("[MiniMax TTS] Starting to receive data to PSRAM...");
  
  // Read response until finding "audio":"
  String buffer = "";
  bool foundAudio = false;
  int bytesRead = 0;
  
  while (http.connected() && bytesRead < 10000) {
    if (stream->available()) {
      char c = stream->read();
      buffer += c;
      bytesRead++;
      
      int pos = buffer.indexOf("\"audio\":\"");
      if (pos >= 0) {
        foundAudio = true;
        buffer = buffer.substring(pos + 9);
        break;
      }
      
      if (buffer.length() > 1000) {
        buffer = buffer.substring(buffer.length() - 500);
      }
    }
  }

  if (!foundAudio) {
    Serial.println("[MiniMax TTS] Audio field not found");
    free(psramBuffer);
    http.end();
    return false;
  }

  Serial.println("[MiniMax TTS] Found audio data, decoding to PSRAM...");
  
  size_t totalDecoded = 0;
  uint8_t hexPair[2];
  int hexIndex = 0;
  
  // Process data in buffer
  for (size_t i = 0; i < buffer.length() && totalDecoded < maxSize; i++) {
    char c = buffer[i];
    if (c == '"') break;
    
    if (isxdigit(c)) {
      hexPair[hexIndex++] = c;
      if (hexIndex == 2) {
        psramBuffer[totalDecoded++] = hexCharToByte(hexPair[0], hexPair[1]);
        hexIndex = 0;
      }
    }
  }
  
  // Continue reading from stream
  while ((http.connected() || stream->available()) && totalDecoded < maxSize) {
    if (stream->available()) {
      char c = stream->read();
      
      if (c == '"') break;
      
      if (isxdigit(c)) {
        hexPair[hexIndex++] = c;
        if (hexIndex == 2) {
          psramBuffer[totalDecoded++] = hexCharToByte(hexPair[0], hexPair[1]);
          hexIndex = 0;
          
          if (totalDecoded % 10240 == 0) {
            Serial.printf("[MiniMax TTS] Decoded: %d KB\r", totalDecoded / 1024);
          }
        }
      }
    }
  }
  
  http.end();
  
  Serial.printf("\n[MiniMax TTS] Decode complete: %d bytes\n", totalDecoded);
  
  *outBuffer = psramBuffer;
  *outSize = totalDecoded;
  
  return totalDecoded > 0;
}

/**
 * @brief Synthesize and play using URL mode (fastest, no hex decoding needed)
 * @param text Text to synthesize
 * @return Success status
 */
bool ArduinoMinimaxTTS::synthesizeAndPlayFromURL(const String& text) {
  HTTPClient http;
  http.setTimeout(30000);
  
  String urlWithParams = String(_url) + "?GroupId=" + _groupId;
  http.begin(urlWithParams);
  http.addHeader("Content-Type", "application/json");
  
  String token_key = String("Bearer ") + _apiKey;
  http.addHeader("Authorization", token_key);

  // Create request JSON - Key: set output_format to "url"
  DynamicJsonDocument doc(1024);
  doc["model"] = _model;
  doc["text"] = text;
  doc["stream"] = false;
  doc["output_format"] = "url";  // Key parameter: return URL instead of hex data
  
  JsonObject voice_setting = doc.createNestedObject("voice_setting");
  voice_setting["voice_id"] = _voiceId;
  voice_setting["speed"] = _speed;
  voice_setting["vol"] = _volume;
  voice_setting["pitch"] = _pitch;
  if (_emotion != nullptr && strlen(_emotion) > 0) {
    voice_setting["emotion"] = _emotion;
  }
  
  JsonObject audio_setting = doc.createNestedObject("audio_setting");
  audio_setting["sample_rate"] = _sampleRate;
  audio_setting["bitrate"] = _bitrate;
  audio_setting["format"] = _audioFormat;
  audio_setting["channel"] = _channel;

  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("[MiniMax TTS] Sending request (URL mode - no decoding needed)");

  int httpResponseCode = http.POST(jsonString);

  if (httpResponseCode != 200) {
    Serial.printf("[MiniMax TTS] HTTP request failed: %d\n", httpResponseCode);
    if (http.connected()) {
      String errorResponse = http.getString();
      Serial.println("[MiniMax TTS] Error response:");
      Serial.println(errorResponse);
    }
    http.end();
    return false;
  }

  // Parse JSON response to get URL
  String response = http.getString();
  http.end();
  
  Serial.println("[MiniMax TTS] Response received, parsing URL...");
  
  DynamicJsonDocument responseDoc(2048);
  DeserializationError error = deserializeJson(responseDoc, response);
  
  if (error) {
    Serial.print("[MiniMax TTS] JSON parse failed: ");
    Serial.println(error.c_str());
    return false;
  }
  
  // Check response status
  int statusCode = responseDoc["base_resp"]["status_code"];
  if (statusCode != 0) {
    Serial.printf("[MiniMax TTS] API error: %d - %s\n",
                  statusCode,
                  responseDoc["base_resp"]["status_msg"].as<const char*>());
    return false;
  }
  
  // Get audio URL
  const char* audioUrl = responseDoc["data"]["audio"];
  if (!audioUrl || strlen(audioUrl) == 0) {
    Serial.println("[MiniMax TTS] Audio URL not found");
    return false;
  }
  
  Serial.printf("[MiniMax TTS] Audio URL: %s\n", audioUrl);
  Serial.println("[MiniMax TTS] Playing audio directly from URL...");
  
  // Play directly from URL using Audio library
  bool success = _audio->connecttohost(audioUrl);
  
  if (success) {
    Serial.println("[MiniMax TTS] Audio playback started successfully");
    return true;
  } else {
    Serial.println("[MiniMax TTS] Audio playback start failed");
    return false;
  }
}

/**
 * @brief Synthesize text to speech and play (intelligently select optimal method)
 * @param text Text to synthesize
 * @return Synthesis success status
 */
bool ArduinoMinimaxTTS::synthesizeAndPlay(const String& text) {
  if (!_audio) {
    Serial.println("[MiniMax TTS] Audio object not set");
    return false;
  }

  if (text.length() == 0) {
    Serial.println("[MiniMax TTS] Text is empty");
    return false;
  }

  Serial.println("[MiniMax TTS] Starting speech synthesis...");
  Serial.printf("[MiniMax TTS] Text: %s\n", text.c_str());

  // Prefer URL mode (fastest, no decoding needed)
  Serial.println("[MiniMax TTS] Trying URL mode (fastest, no hex decoding needed)");
  if (synthesizeAndPlayFromURL(text)) {
    return true;
  }
  
  // URL mode failed, use fallback
  Serial.println("[MiniMax TTS] URL mode failed, switching to fallback");
  
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[MiniMax TTS] SPIFFS initialization failed");
    return false;
  }

  const char* tempFile = "/tts_temp.mp3";
  
  // Check for PSRAM and try to use it
  if (psramFound()) {
    Serial.printf("[MiniMax TTS] PSRAM detected, available: %d bytes\n", ESP.getFreePsram());
    
    uint8_t* audioBuffer = nullptr;
    size_t audioSize = 0;
    
    if (getAudioDataToPSRAM(text, &audioBuffer, &audioSize)) {
      // Write from PSRAM to file
      File file = SPIFFS.open(tempFile, FILE_WRITE);
      if (file) {
        size_t written = file.write(audioBuffer, audioSize);
        file.close();
        free(audioBuffer);

        if (written == audioSize) {
          Serial.printf("[MiniMax TTS] Audio saved to: %s\n", tempFile);
          
          // Play file using Audio library
          Serial.println("[MiniMax TTS] Starting audio playback...");
          bool success = _audio->connecttoFS(SPIFFS, tempFile);
          
          if (success) {
            Serial.println("[MiniMax TTS] Audio playback started successfully");
            return true;
          } else {
            Serial.println("[MiniMax TTS] Audio playback start failed");
            SPIFFS.remove(tempFile);
            return false;
          }
        } else {
          free(audioBuffer);
        }
      } else {
        free(audioBuffer);
      }
    }
    
    Serial.println("[MiniMax TTS] PSRAM method failed, switching to streaming method");
  }
  
  // Use streaming method (when PSRAM not available or PSRAM method failed)
  if (!saveAudioToFile(text, tempFile)) {
    Serial.println("[MiniMax TTS] Failed to save audio file");
    return false;
  }

  Serial.printf("[MiniMax TTS] Audio saved to: %s\n", tempFile);

  // Play file using Audio library
  Serial.println("[MiniMax TTS] Starting audio playback...");
  bool success = _audio->connecttoFS(SPIFFS, tempFile);
  
  if (success) {
    Serial.println("[MiniMax TTS] Audio playback started successfully");
    return true;
  } else {
    Serial.println("[MiniMax TTS] Audio playback start failed");
    SPIFFS.remove(tempFile);
    return false;
  }
}