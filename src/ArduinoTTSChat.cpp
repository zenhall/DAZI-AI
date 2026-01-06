/**
 * @file ArduinoTTSChat.cpp
 * @brief MiniMax TTS (Text-to-Speech) class implementation
 * @details Implements streaming text-to-speech via WebSocket protocol
 */

#include "ArduinoTTSChat.h"

/**
 * @brief Constructor - Initialize TTS client
 * @param apiKey MiniMax API key (JWT token)
 */
ArduinoTTSChat::ArduinoTTSChat(const char* apiKey) {
  _apiKey = apiKey;

  // Allocate audio buffer - prefer PSRAM for larger buffer
  if (psramFound()) {
    _audioBuffer = (uint8_t*)ps_malloc(AUDIO_BUFFER_SIZE);
    Serial.printf("Using PSRAM for audio buffer (%d bytes)\n", AUDIO_BUFFER_SIZE);
  } else {
    _audioBuffer = new uint8_t[AUDIO_BUFFER_SIZE];
    Serial.printf("Using heap for audio buffer (%d bytes)\n", AUDIO_BUFFER_SIZE);
  }

  _msgBuffer = new uint8_t[MSG_BUFFER_SIZE];
}

/**
 * @brief Destructor - Clean up resources
 */
ArduinoTTSChat::~ArduinoTTSChat() {
  // Delete audio task if running
  if (_audioTaskHandle != nullptr) {
    vTaskDelete(_audioTaskHandle);
    _audioTaskHandle = nullptr;
  }
  if (_audioBuffer != nullptr) {
    delete[] _audioBuffer;
    _audioBuffer = nullptr;
  }
  if (_msgBuffer != nullptr) {
    delete[] _msgBuffer;
    _msgBuffer = nullptr;
  }
}

/**
 * @brief Set API key
 * @param apiKey API key (JWT token)
 */
void ArduinoTTSChat::setApiKey(const char* apiKey) {
  _apiKey = apiKey;
}

/**
 * @brief Set voice ID
 * @param voiceId Voice ID
 */
void ArduinoTTSChat::setVoiceId(const char* voiceId) {
  _voiceId = voiceId;
}

/**
 * @brief Set speech speed
 * @param speed Speed multiplier (0.5 - 2.0)
 */
void ArduinoTTSChat::setSpeed(float speed) {
  if (speed >= 0.5 && speed <= 2.0) {
    _speed = speed;
  }
}

/**
 * @brief Set volume
 * @param vol Volume multiplier (0.1 - 1.0)
 */
void ArduinoTTSChat::setVolume(float vol) {
  if (vol >= 0.1 && vol <= 1.0) {
    _volume = vol;
  }
}

/**
 * @brief Set pitch
 * @param pitch Pitch adjustment (-12 to 12)
 */
void ArduinoTTSChat::setPitch(int pitch) {
  if (pitch >= -12 && pitch <= 12) {
    _pitch = pitch;
  }
}

/**
 * @brief Set audio parameters
 * @param sampleRate Sample rate
 * @param bitrate Bitrate
 */
void ArduinoTTSChat::setAudioParams(int sampleRate, int bitrate) {
  _sampleRate = sampleRate;
  _bitrate = bitrate;
}

/**
 * @brief Initialize MAX98357 I2S speaker
 * @param bclkPin Bit clock pin
 * @param lrclkPin LR clock pin
 * @param doutPin Data out pin
 * @return true if initialization successful
 */
bool ArduinoTTSChat::initMAX98357Speaker(int bclkPin, int lrclkPin, int doutPin) {
  _speakerType = SPEAKER_TYPE_MAX98357;
  _I2S.setPins(bclkPin, lrclkPin, doutPin, -1);

  // Initialize I2S standard mode for output with configured sample rate
  if (!_I2S.begin(I2S_MODE_STD, _sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("MAX98357 I2S initialization failed!");
    return false;
  }

  Serial.printf("MAX98357 speaker initialized at %d Hz\n", _sampleRate);
  _speakerInitialized = true;

  // Create audio playback task on core 0 (WebSocket runs on core 1)
  xTaskCreatePinnedToCore(
    audioTaskWrapper,      // Task function
    "AudioTask",           // Task name
    4096,                  // Stack size
    this,                  // Parameter (this pointer)
    1,                     // Priority (low)
    &_audioTaskHandle,     // Task handle
    0                      // Core 0
  );
  Serial.println("Audio playback task created on core 0");

  return true;
}

/**
 * @brief Initialize internal DAC speaker
 * @param dacPin DAC output pin
 * @return true if initialization successful
 */
bool ArduinoTTSChat::initInternalDAC(int dacPin) {
  _speakerType = SPEAKER_TYPE_INTERNAL;

  // ESP32 internal DAC uses GPIO25 or GPIO26
  if (!_I2S.begin(I2S_MODE_PDM_TX, _sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Internal DAC initialization failed!");
    return false;
  }

  Serial.println("Internal DAC initialized");
  _speakerInitialized = true;
  return true;
}

/**
 * @brief Generate WebSocket handshake key
 * @return Base64 encoded random key string
 */
String ArduinoTTSChat::generateWebSocketKey() {
  uint8_t random_bytes[16];
  for (int i = 0; i < 16; i++) {
    random_bytes[i] = random(0, 256);
  }

  size_t output_len;
  unsigned char output[32];
  mbedtls_base64_encode(output, sizeof(output), &output_len, random_bytes, 16);

  return String((char*)output);
}

/**
 * @brief Connect to MiniMax TTS WebSocket server
 * @return true if connection successful
 */
bool ArduinoTTSChat::connectWebSocket() {
  Serial.println("Connecting to MiniMax TTS WebSocket...");

  // Skip SSL certificate verification (for testing)
  _client.setInsecure();

  // Connect to TTS server
  if (!_client.connect(_wsHost, _wsPort)) {
    Serial.println("SSL connection failed");
    return false;
  }

  // Disable Nagle algorithm for low latency
  _client.setNoDelay(true);

  // Generate WebSocket key and send handshake request
  String ws_key = generateWebSocketKey();
  String request = String("GET ") + _wsPath + " HTTP/1.1\r\n";
  request += String("Host: ") + _wsHost + "\r\n";
  request += "Upgrade: websocket\r\n";
  request += "Connection: Upgrade\r\n";
  request += "Sec-WebSocket-Key: " + ws_key + "\r\n";
  request += "Sec-WebSocket-Version: 13\r\n";
  request += String("Authorization: Bearer ") + _apiKey + "\r\n";
  request += "\r\n";

  _client.print(request);

  // Wait for server response (max 5 seconds)
  unsigned long timeout = millis();
  while (_client.connected() && !_client.available()) {
    if (millis() - timeout > 5000) {
      Serial.println("Response timeout");
      _client.stop();
      return false;
    }
    delay(10);
  }

  // Read HTTP response headers
  String response = "";
  bool headers_complete = false;
  while (_client.available() && !headers_complete) {
    String line = _client.readStringUntil('\n');
    response += line + "\n";
    if (line == "\r" || line.length() == 0) {
      headers_complete = true;
    }
  }

  // Check if handshake succeeded
  if (response.indexOf("101") >= 0 && response.indexOf("Switching Protocols") >= 0) {
    Serial.println("WebSocket connected");
    _wsConnected = true;
    _taskStarted = false;

    // Wait for connected_success message
    delay(100);
    if (_client.available()) {
      handleWebSocketData();
    }

    return true;
  } else {
    Serial.println("WebSocket handshake failed");
    Serial.println(response);
    _client.stop();
    return false;
  }
}

/**
 * @brief Disconnect WebSocket connection
 */
void ArduinoTTSChat::disconnectWebSocket() {
  if (_wsConnected) {
    sendTaskFinish();
    delay(100);
    _client.stop();
    _wsConnected = false;
    _taskStarted = false;
    Serial.println("WebSocket disconnected");
  }
}

/**
 * @brief Check if WebSocket is connected
 * @return true if connected
 */
bool ArduinoTTSChat::isWebSocketConnected() {
  return _wsConnected && _client.connected();
}

/**
 * @brief Start TTS task
 * @return true if task started successfully
 */
bool ArduinoTTSChat::startTask() {
  if (!_wsConnected) {
    Serial.println("WebSocket not connected!");
    return false;
  }

  sendTaskStart();

  // Wait for task_started response
  unsigned long timeout = millis();
  while (!_taskStarted && millis() - timeout < 3000) {
    if (_client.available()) {
      handleWebSocketData();
    }
    delay(10);
  }

  if (!_taskStarted) {
    Serial.println("Task start timeout");
    return false;
  }

  Serial.println("TTS task started");
  return true;
}

/**
 * @brief Synthesize text to speech
 * @param text Text to synthesize
 * @return true if synthesis started successfully
 */
bool ArduinoTTSChat::speak(const char* text) {
  if (!_wsConnected) {
    Serial.println("WebSocket not connected!");
    return false;
  }

  if (!_taskStarted) {
    if (!startTask()) {
      return false;
    }
  }

  if (_isPlaying) {
    Serial.println("Already playing, please wait");
    return false;
  }

  Serial.printf("Synthesizing: %s\n", text);

  // Reset ring buffer state
  _isPlaying = true;
  _shouldStop = false;
  _receivingAudio = false;
  _audioWritePos = 0;
  _audioReadPos = 0;
  _audioDataSize = 0;
  _chunksReceived = 0;
  _playStartTime = millis();

  // Send task_continue with text
  sendTaskContinue(text);

  return true;
}

/**
 * @brief Check if currently playing
 * @return true if playing
 */
bool ArduinoTTSChat::isPlaying() {
  return _isPlaying;
}

/**
 * @brief Stop current playback
 */
void ArduinoTTSChat::stop() {
  _shouldStop = true;
  _isPlaying = false;
  _receivingAudio = false;
  _audioWritePos = 0;
  _audioReadPos = 0;
  _audioDataSize = 0;
}

/**
 * @brief Main loop processing function
 */
void ArduinoTTSChat::loop() {
  // Check connection status
  if (_wsConnected && !_client.connected()) {
    Serial.println("Connection lost");
    _wsConnected = false;
    _isPlaying = false;
    _taskStarted = false;
  }

  if (!_wsConnected) {
    return;
  }

  // Process incoming WebSocket data only
  // Audio playback is handled by separate FreeRTOS task
  while (_client.available()) {
    handleWebSocketData();
  }
}

/**
 * @brief Set completion callback
 * @param callback Callback function pointer
 */
void ArduinoTTSChat::setCompletionCallback(CompletionCallback callback) {
  _completionCallback = callback;
}

/**
 * @brief Set error callback
 * @param callback Callback function pointer
 */
void ArduinoTTSChat::setErrorCallback(ErrorCallback callback) {
  _errorCallback = callback;
}

/**
 * @brief Send task_start message
 */
void ArduinoTTSChat::sendTaskStart() {
  StaticJsonDocument<512> doc;
  doc["event"] = "task_start";
  doc["model"] = _model;

  JsonObject voice = doc.createNestedObject("voice_setting");
  voice["voice_id"] = _voiceId;
  voice["speed"] = _speed;
  voice["vol"] = _volume;
  voice["pitch"] = _pitch;
  voice["english_normalization"] = _englishNorm;

  JsonObject audio = doc.createNestedObject("audio_setting");
  audio["sample_rate"] = _sampleRate;
  audio["bitrate"] = _bitrate;
  audio["format"] = _format;
  audio["channel"] = _channels;

  String json;
  serializeJson(doc, json);

  Serial.println("Sending task_start:");
  Serial.println(json);

  sendTextFrame(json.c_str());
}

/**
 * @brief Send task_continue message with text
 * @param text Text to synthesize
 */
void ArduinoTTSChat::sendTaskContinue(const char* text) {
  StaticJsonDocument<1024> doc;
  doc["event"] = "task_continue";
  doc["text"] = text;

  String json;
  serializeJson(doc, json);

  Serial.println("Sending task_continue");

  sendTextFrame(json.c_str());
}

/**
 * @brief Send task_finish message
 */
void ArduinoTTSChat::sendTaskFinish() {
  StaticJsonDocument<64> doc;
  doc["event"] = "task_finish";

  String json;
  serializeJson(doc, json);

  sendTextFrame(json.c_str());
  Serial.println("Task finish sent");
}

/**
 * @brief Send text WebSocket frame
 * @param text Text to send
 */
void ArduinoTTSChat::sendTextFrame(const char* text) {
  size_t len = strlen(text);
  sendWebSocketFrame((uint8_t*)text, len, 0x01);  // 0x01 = text frame
}

/**
 * @brief Send WebSocket frame
 * @param data Data to send
 * @param len Data length
 * @param opcode WebSocket opcode
 */
void ArduinoTTSChat::sendWebSocketFrame(uint8_t* data, size_t len, uint8_t opcode) {
  if (!_wsConnected || !_client.connected()) return;

  // Build WebSocket frame header
  uint8_t header[14];
  int header_len = 2;

  header[0] = 0x80 | opcode;  // FIN=1 + opcode
  header[1] = 0x80;           // MASK=1

  // Length encoding
  if (len < 126) {
    header[1] |= len;
  } else if (len < 65536) {
    header[1] |= 126;
    header[2] = (len >> 8) & 0xFF;
    header[3] = len & 0xFF;
    header_len = 4;
  } else {
    header[1] |= 127;
    for (int i = 0; i < 8; i++) {
      header[2 + i] = (len >> (56 - i * 8)) & 0xFF;
    }
    header_len = 10;
  }

  // Generate random mask key
  uint8_t mask_key[4];
  for (int i = 0; i < 4; i++) {
    mask_key[i] = random(0, 256);
  }
  memcpy(header + header_len, mask_key, 4);
  header_len += 4;

  // Send frame header
  _client.write(header, header_len);

  // Mask data and send
  uint8_t* masked_data = new uint8_t[len];
  for (size_t i = 0; i < len; i++) {
    masked_data[i] = data[i] ^ mask_key[i % 4];
  }
  _client.write(masked_data, len);
  delete[] masked_data;
}

/**
 * @brief Send Pong response
 */
void ArduinoTTSChat::sendPong() {
  uint8_t pong_data[1] = {0};
  sendWebSocketFrame(pong_data, 0, 0x0A);  // 0x0A = Pong frame
}

/**
 * @brief Read exact number of bytes with timeout
 * @param buffer Buffer to read into
 * @param len Number of bytes to read
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes actually read
 */
size_t ArduinoTTSChat::readBytesWithTimeout(uint8_t* buffer, size_t len, unsigned long timeout_ms) {
  size_t total_read = 0;
  unsigned long start = millis();

  while (total_read < len && (millis() - start) < timeout_ms) {
    if (_client.available()) {
      size_t to_read = min(len - total_read, (size_t)_client.available());
      size_t read_now = _client.readBytes(buffer + total_read, to_read);
      total_read += read_now;
      start = millis();  // Reset timeout on successful read
    } else {
      delay(1);
    }
  }

  return total_read;
}

/**
 * @brief Handle received WebSocket data
 */
void ArduinoTTSChat::handleWebSocketData() {
  // Read WebSocket frame header (2 bytes)
  uint8_t header[2];
  if (readBytesWithTimeout(header, 2, 1000) != 2) {
    return;
  }

  bool fin = header[0] & 0x80;              // FIN flag - 1 means final frame
  uint8_t opcode = header[0] & 0x0F;        // Opcode
  bool masked = header[1] & 0x80;
  uint64_t payload_len = header[1] & 0x7F;

  // Handle extended length
  if (payload_len == 126) {
    uint8_t len_bytes[2];
    if (readBytesWithTimeout(len_bytes, 2, 1000) != 2) return;
    payload_len = (len_bytes[0] << 8) | len_bytes[1];
  } else if (payload_len == 127) {
    uint8_t len_bytes[8];
    if (readBytesWithTimeout(len_bytes, 8, 1000) != 8) return;
    payload_len = 0;
    for (int i = 0; i < 8; i++) {
      payload_len = (payload_len << 8) | len_bytes[i];
    }
  }

  // Read mask key (if present)
  uint8_t mask_key[4] = {0};
  if (masked) {
    if (readBytesWithTimeout(mask_key, 4, 1000) != 4) return;
  }

  // Read payload data
  if (payload_len > 0 && payload_len < 200000) {
    uint8_t* payload = (uint8_t*)malloc(payload_len + 1);
    if (payload == nullptr) {
      Serial.println("Failed to allocate payload buffer");
      return;
    }

    size_t bytes_read = readBytesWithTimeout(payload, payload_len, 10000);

    if (bytes_read == payload_len) {
      // Unmask
      if (masked) {
        for (size_t i = 0; i < payload_len; i++) {
          payload[i] ^= mask_key[i % 4];
        }
      }

      // Handle message fragmentation
      // opcode 0x00 = continuation frame
      // opcode 0x01 = text frame (start of new message)
      // opcode 0x02 = binary frame
      // FIN=1 means this is the final fragment

      if (opcode == 0x01 || opcode == 0x02) {
        // Start of a new message
        if (fin) {
          // Complete message in one frame
          payload[payload_len] = '\0';
          parseJsonResponse((char*)payload, payload_len);
        } else {
          // First fragment of multi-frame message
          _msgInProgress = true;
          _msgBufferPos = 0;
          if (payload_len < MSG_BUFFER_SIZE) {
            memcpy(_msgBuffer, payload, payload_len);
            _msgBufferPos = payload_len;
          }
        }
      } else if (opcode == 0x00) {
        // Continuation frame
        if (_msgInProgress && _msgBuffer != nullptr) {
          if (_msgBufferPos + payload_len < MSG_BUFFER_SIZE) {
            memcpy(_msgBuffer + _msgBufferPos, payload, payload_len);
            _msgBufferPos += payload_len;
          }

          if (fin) {
            // Final fragment - process complete message
            _msgBuffer[_msgBufferPos] = '\0';
            parseJsonResponse((char*)_msgBuffer, _msgBufferPos);
            _msgInProgress = false;
            _msgBufferPos = 0;
          }
        }
      } else if (opcode == 0x08) {  // Close
        Serial.println("Server closed connection");
        _wsConnected = false;
        _client.stop();
      } else if (opcode == 0x09) {  // Ping
        sendPong();
      }
    } else {
      Serial.printf("Incomplete read: got %d of %d bytes\n", (int)bytes_read, (int)payload_len);
    }

    free(payload);
  } else if (payload_len >= 200000) {
    Serial.printf("Payload too large: %d bytes\n", (int)payload_len);
  }
}

/**
 * @brief Parse JSON response from server
 * @param json JSON string
 * @param len String length
 */
void ArduinoTTSChat::parseJsonResponse(const char* json, size_t len) {
  // Use dynamic JSON document - audio responses can be very large (hex encoded)
  // Allocate based on input length + overhead
  size_t docSize = len + 1024;
  if (docSize < 4096) docSize = 4096;
  if (docSize > 200000) docSize = 200000;  // Cap at 200KB

  DynamicJsonDocument doc(docSize);
  DeserializationError error = deserializeJson(doc, json, len);

  if (error) {
    // Only log if it's not just a small/empty frame
    if (len > 10) {
      Serial.printf("JSON parse error: %s (len=%d)\n", error.c_str(), (int)len);
    }
    return;
  }

  // Check event type
  const char* event = doc["event"];
  if (event == nullptr) {
    // Check for connected_success (initial connection)
    if (doc.containsKey("event")) {
      event = doc["event"].as<const char*>();
    }
  }

  if (event != nullptr) {
    if (strcmp(event, "connected_success") == 0) {
      Serial.println("Connected to MiniMax TTS server");
    } else if (strcmp(event, "task_started") == 0) {
      Serial.println("Task started");
      _taskStarted = true;
    } else if (strcmp(event, "task_finished") == 0) {
      Serial.println("Task finished");
    } else if (strcmp(event, "error") == 0) {
      const char* errMsg = doc["message"] | "Unknown error";
      Serial.printf("Error: %s\n", errMsg);
      if (_errorCallback != nullptr) {
        _errorCallback(errMsg);
      }
    }
  }

  // Check for audio data
  if (doc.containsKey("data") && doc["data"].containsKey("audio")) {
    const char* audioHex = doc["data"]["audio"];
    if (audioHex != nullptr && strlen(audioHex) > 0) {
      _chunksReceived++;
      _receivingAudio = true;

      if (_chunksReceived == 1) {
        unsigned long delay_ms = millis() - _playStartTime;
        Serial.printf("First audio chunk received (delay: %lums)\n", delay_ms);
      }

      // Convert hex to bytes
      size_t hexLen = strlen(audioHex);
      size_t bytesNeeded = hexLen / 2;

      // Check if we have enough space in ring buffer
      size_t freeSpace = AUDIO_BUFFER_SIZE - _audioDataSize;

      if (bytesNeeded <= freeSpace) {
        // Write to ring buffer (may wrap around)
        for (size_t i = 0; i < hexLen; i += 2) {
          uint8_t high = 0, low = 0;
          char c = audioHex[i];
          if (c >= '0' && c <= '9') high = c - '0';
          else if (c >= 'a' && c <= 'f') high = c - 'a' + 10;
          else if (c >= 'A' && c <= 'F') high = c - 'A' + 10;

          c = audioHex[i + 1];
          if (c >= '0' && c <= '9') low = c - '0';
          else if (c >= 'a' && c <= 'f') low = c - 'a' + 10;
          else if (c >= 'A' && c <= 'F') low = c - 'A' + 10;

          _audioBuffer[_audioWritePos] = (high << 4) | low;
          _audioWritePos = (_audioWritePos + 1) % AUDIO_BUFFER_SIZE;
        }
        _audioDataSize += bytesNeeded;
      } else {
        Serial.printf("Buffer full: need %d, free %d\n", (int)bytesNeeded, (int)freeSpace);
      }
    }
  }

  // Check if final
  if (doc.containsKey("is_final") && doc["is_final"].as<bool>()) {
    Serial.printf("Audio synthesis completed: %d chunks received\n", _chunksReceived);
    _receivingAudio = false;
  }
}

/**
 * @brief Convert hex string to bytes
 * @param hex Hex string
 * @param hexLen Hex string length
 * @param output Output buffer
 * @param outputSize Output buffer size
 * @return Number of bytes converted
 */
size_t ArduinoTTSChat::hexToBytes(const char* hex, size_t hexLen, uint8_t* output, size_t outputSize) {
  size_t byteCount = 0;
  for (size_t i = 0; i < hexLen && byteCount < outputSize; i += 2) {
    uint8_t high = 0, low = 0;

    char c = hex[i];
    if (c >= '0' && c <= '9') high = c - '0';
    else if (c >= 'a' && c <= 'f') high = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') high = c - 'A' + 10;

    c = hex[i + 1];
    if (c >= '0' && c <= '9') low = c - '0';
    else if (c >= 'a' && c <= 'f') low = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') low = c - 'A' + 10;

    output[byteCount++] = (high << 4) | low;
  }
  return byteCount;
}

/**
 * @brief Process audio playback from ring buffer
 */
void ArduinoTTSChat::processAudioPlayback() {
  // Play available audio data from ring buffer
  while (_audioDataSize > 0) {
    // Calculate contiguous bytes available from read position
    size_t contiguous = AUDIO_BUFFER_SIZE - _audioReadPos;
    size_t dataSize = _audioDataSize;  // Copy volatile to local
    size_t toRead = min(dataSize, contiguous);
    toRead = min(toRead, (size_t)4096);  // Max 4KB per write
    toRead = (toRead / 2) * 2;  // Align to 16-bit boundary

    if (toRead > 0) {
      size_t written = _I2S.write(_audioBuffer + _audioReadPos, toRead);
      if (written == 0) {
        // I2S buffer full, try again next loop
        break;
      }
      _audioReadPos = (_audioReadPos + written) % AUDIO_BUFFER_SIZE;
      _audioDataSize -= written;
    } else {
      break;
    }
  }

  // Check if playback is complete
  if (!_receivingAudio && _audioDataSize == 0 && _chunksReceived > 0) {
    Serial.println("Playback complete");
    _isPlaying = false;
    _audioWritePos = 0;
    _audioReadPos = 0;
    _audioDataSize = 0;
    _chunksReceived = 0;

    // Need to start a new task for next synthesis
    _taskStarted = false;

    if (_completionCallback != nullptr) {
      _completionCallback();
    }
  }
}

/**
 * @brief Static wrapper for FreeRTOS task
 * @param param Pointer to ArduinoTTSChat instance
 */
void ArduinoTTSChat::audioTaskWrapper(void* param) {
  ArduinoTTSChat* instance = static_cast<ArduinoTTSChat*>(param);
  instance->audioTaskLoop();
}

/**
 * @brief Audio playback task main loop (runs on separate core)
 */
void ArduinoTTSChat::audioTaskLoop() {
  while (true) {
    if (_isPlaying && _speakerInitialized) {
      processAudioPlayback();
    }
    // Small delay to prevent starving other tasks
    vTaskDelay(1);  // 1 tick = ~1ms
  }
}
