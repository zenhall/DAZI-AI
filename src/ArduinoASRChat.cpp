/**
 * @file ArduinoASRChat.cpp
 * @brief Real-time Automatic Speech Recognition (ASR) class implementation - using ByteDance ASR API
 * @details Implements streaming speech recognition via WebSocket protocol, supports VAD (Voice Activity Detection)
 */

#include "ArduinoASRChat.h"

/**
 * @brief Constructor - Initialize ASR client
 * @param apiKey ByteDance ASR API key
 * @param cluster ASR service cluster name (default: "volcengine_input_en")
 */
ArduinoASRChat::ArduinoASRChat(const char* apiKey, const char* cluster) {
  _apiKey = apiKey;
  _cluster = cluster;

  // Allocate audio send buffer (3200 bytes = 200ms of 16kHz 16bit mono audio)
  _sendBuffer = new int16_t[_sendBatchSize / 2];
}

/**
 * @brief Set API configuration
 * @param apiKey API key (optional)
 * @param cluster Service cluster (optional)
 */
void ArduinoASRChat::setApiConfig(const char* apiKey, const char* cluster) {
  if (apiKey != nullptr) {
    _apiKey = apiKey;
  }
  if (cluster != nullptr) {
    _cluster = cluster;
  }
}

/**
 * @brief Set microphone type
 * @param micType Microphone type (MIC_TYPE_PDM or MIC_TYPE_INMP441)
 */
void ArduinoASRChat::setMicrophoneType(MicrophoneType micType) {
  _micType = micType;
}

/**
 * @brief Set audio parameters
 * @param sampleRate Sample rate (default: 16000 Hz)
 * @param bitsPerSample Bit depth (default: 16 bit)
 * @param channels Number of channels (default: 1 mono)
 */
void ArduinoASRChat::setAudioParams(int sampleRate, int bitsPerSample, int channels) {
  _sampleRate = sampleRate;
  _bitsPerSample = bitsPerSample;
  _channels = channels;
}

/**
 * @brief Set silence detection duration
 * @param duration Silence duration in milliseconds, recording stops automatically after this time
 */
void ArduinoASRChat::setSilenceDuration(unsigned long duration) {
  _silenceDuration = duration;
}

/**
 * @brief Set maximum recording duration
 * @param seconds Maximum recording seconds (default: 50 seconds)
 */
void ArduinoASRChat::setMaxRecordingSeconds(int seconds) {
  _maxSeconds = seconds;
}

/**
 * @brief Initialize PDM microphone (e.g., ESP32-S3 onboard microphone)
 * @param pdmClkPin PDM clock pin
 * @param pdmDataPin PDM data pin
 * @return true if initialization successful, false if failed
 */
bool ArduinoASRChat::initPDMMicrophone(int pdmClkPin, int pdmDataPin) {
  _micType = MIC_TYPE_PDM;
  _I2S.setPinsPdmRx(pdmClkPin, pdmDataPin);

  // Initialize I2S PDM receive mode
  if (!_I2S.begin(I2S_MODE_PDM_RX, _sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("PDM I2S initialization failed!");
    return false;
  }

  Serial.println("PDM microphone initialized");

  // Wait for hardware to stabilize and clear buffer
  delay(500);
  for (int i = 0; i < 2000; i++) {
    _I2S.read();
  }

  return true;
}

/**
 * @brief Initialize INMP441 I2S MEMS microphone
 * @param i2sSckPin I2S serial clock pin (SCK)
 * @param i2sWsPin I2S word select pin (WS/LRCLK)
 * @param i2sSdPin I2S serial data pin (SD)
 * @return true if initialization successful, false if failed
 */
bool ArduinoASRChat::initINMP441Microphone(int i2sSckPin, int i2sWsPin, int i2sSdPin) {
  _micType = MIC_TYPE_INMP441;
  _I2S.setPins(i2sSckPin, i2sWsPin, -1, i2sSdPin);

  // Initialize I2S standard mode, left channel
  if (!_I2S.begin(I2S_MODE_STD, _sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("INMP441 I2S initialization failed!");
    return false;
  }

  Serial.println("INMP441 microphone initialized");

  // Wait for hardware to stabilize and clear buffer
  delay(500);
  for (int i = 0; i < 2000; i++) {
    _I2S.read();
  }

  return true;
}

/**
 * @brief Generate WebSocket handshake key
 * @return Base64 encoded random key string
 * @details Used for WebSocket protocol handshake, generates 16 random bytes and Base64 encodes them
 */
String ArduinoASRChat::generateWebSocketKey() {
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
 * @brief Connect to ByteDance ASR WebSocket server
 * @return true if connection successful, false if failed
 * @details Performs WebSocket handshake protocol, establishes SSL encrypted connection
 */
bool ArduinoASRChat::connectWebSocket() {
  Serial.println("Connecting WebSocket...");

  // Skip SSL certificate verification (for testing, production should verify certificates)
  _client.setInsecure();

  // Connect to ASR server (HTTPS port 443)
  if (!_client.connect(_wsHost, _wsPort)) {
    Serial.println("SSL connection failed");
    return false;
  }

  // Disable Nagle algorithm, ensure data is sent immediately (reduce latency)
  _client.setNoDelay(true);

  // Generate WebSocket key and send handshake request
  String ws_key = generateWebSocketKey();
  String request = String("GET ") + _wsPath + " HTTP/1.1\r\n";
  request += String("Host: ") + _wsHost + "\r\n";
  request += "Upgrade: websocket\r\n";
  request += "Connection: Upgrade\r\n";
  request += "Sec-WebSocket-Key: " + ws_key + "\r\n";
  request += "Sec-WebSocket-Version: 13\r\n";
  request += String("x-api-key: ") + _apiKey + "\r\n";  // ByteDance API key
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

  // Check if handshake succeeded (HTTP 101 Switching Protocols)
  if (response.indexOf("101") >= 0 && response.indexOf("Switching Protocols") >= 0) {
    Serial.println("WebSocket connected");
    _wsConnected = true;
    _endMarkerSent = false;  // Reset end marker flag
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
void ArduinoASRChat::disconnectWebSocket() {
  if (_wsConnected) {
    _client.stop();
    _wsConnected = false;
    Serial.println("WebSocket disconnected");
  }
}

/**
 * @brief Check if WebSocket is connected
 * @return true if connected, false if not connected
 */
bool ArduinoASRChat::isWebSocketConnected() {
  return _wsConnected && _client.connected();
}

/**
 * @brief Start recording and real-time recognition
 * @return true if started successfully, false if failed
 * @details Initialize recording state, send session configuration to ASR server
 */
bool ArduinoASRChat::startRecording() {
  // If end marker was sent, need to reconnect WebSocket (start new session)
  if (_endMarkerSent) {
    Serial.println("Reconnecting WebSocket for new session...");
    disconnectWebSocket();
    delay(100);
    if (!connectWebSocket()) {
      Serial.println("Failed to reconnect WebSocket!");
      return false;
    }
    _endMarkerSent = false;
  }

  if (!_wsConnected) {
    Serial.println("WebSocket not connected!");
    return false;
  }

  if (_isRecording) {
    Serial.println("Already recording!");
    return false;
  }

  Serial.println("\n========================================");
  Serial.println("Recording started...");
  Serial.println("========================================");

  // Reset all recording state variables
  _isRecording = true;
  _shouldStop = false;
  _hasSpeech = false;           // Whether speech was detected
  _hasNewResult = false;        // Whether there is a new recognition result
  _lastResultText = "";         // Previous recognition result
  _recognizedText = "";         // Final recognized text
  _lastSpeechTime = 0;          // Last time speech was detected
  _recordingStartTime = millis(); // Recording start time
  _sendBufferPos = 0;           // Send buffer position
  _sameResultCount = 0;         // Same result count (for stability detection)
  _lastDotTime = millis();      // Last time progress dot was printed

  // Send new session request, start new recognition session
  sendFullRequest();
  delay(50);  // Wait for server confirmation

  return true;
}

/**
 * @brief Stop recording and get final recognition result
 * @details Send remaining audio data, send end marker, trigger callback function
 */
void ArduinoASRChat::stopRecording() {
  if (!_isRecording) {
    return;
  }

  // Send remaining audio data in buffer
  if (_sendBufferPos > 0) {
    sendAudioChunk((uint8_t*)_sendBuffer, _sendBufferPos * 2);
    _sendBufferPos = 0;
  }

  Serial.println("\n========================================");
  Serial.println("Recording stopped");
  Serial.print("Final result: ");
  Serial.println(_lastResultText);
  Serial.println("========================================\n");

  _isRecording = false;
  _shouldStop = true;
  _recognizedText = _lastResultText;  // Save final recognition result
  _hasNewResult = true;               // Mark new result available

  sendEndMarker();                    // Send end marker to server
  _endMarkerSent = true;              // Mark end marker as sent

  // If callback is set, trigger callback
  if (_resultCallback != nullptr && _recognizedText.length() > 0) {
    _resultCallback(_recognizedText);
  }
}

/**
 * @brief Check if currently recording
 * @return true if recording, false if not recording
 */
bool ArduinoASRChat::isRecording() {
  return _isRecording;
}

/**
 * @brief Main loop processing function - must be called in Arduino loop()
 * @details Handle audio sending, receive recognition results, check timeout and silence
 */
void ArduinoASRChat::loop() {
  // Check connection status - mark as disconnected if connection lost
  if (_wsConnected && !_client.connected()) {
    Serial.println("Connection lost");
    _wsConnected = false;
    _isRecording = false;
  }

  // If not connected, return directly
  if (!_wsConnected) {
    return;
  }

  // Handle audio sending during recording
  if (_isRecording && !_shouldStop) {
    processAudioSending();      // Read microphone data and send
    checkRecordingTimeout();    // Check if max recording duration exceeded
    checkSilence();             // Check if silence detected
  }

  // Process received data
  if (_client.available()) {
    if (_isRecording) {
      // Only process one message during recording to avoid blocking too long
      handleWebSocketData();
    } else {
      // Process all pending responses after recording ends
      while (_client.available()) {
        handleWebSocketData();
        delay(10);
      }
    }
  }
}

/**
 * @brief Process audio data sending
 * @details Read audio samples from I2S microphone, buffer and batch send to server
 *          Print a progress dot every second, avoid I2S buffer overflow
 */
void ArduinoASRChat::processAudioSending() {
  // Print a progress dot every second
  if (millis() - _lastDotTime > 1000) {
    Serial.print(".");
    _lastDotTime = millis();
  }

  // Tight loop to read audio samples, keep in sync with I2S data rate
  // Must read fast enough to avoid buffer overflow and send data timely
  for (int i = 0; i < _samplesPerRead; i++) {
    if (!_I2S.available()) {
      break;  // No more data available
    }

    int sample = _I2S.read();

    // Filter invalid data (0, -1, 1 are usually noise or initialization values)
    if (sample != 0 && sample != -1 && sample != 1) {
      _sendBuffer[_sendBufferPos++] = (int16_t)sample;

      // Buffer full, send batch immediately
      if (_sendBufferPos >= _sendBatchSize / 2) {
        sendAudioChunk((uint8_t*)_sendBuffer, _sendBufferPos * 2);
        _sendBufferPos = 0;
      }
    }
  }

  yield();  // Yield CPU to other tasks
}

/**
 * @brief Check recording timeout
 * @details Check if max recording duration exceeded, stop recording if timeout
 *          If no speech detected and timeout callback is set, trigger callback
 */
void ArduinoASRChat::checkRecordingTimeout() {
  // Check max duration
  if (millis() - _recordingStartTime > _maxSeconds * 1000) {
    Serial.println("\nMax duration reached");

    // If no speech detected and callback is set, trigger timeout callback
    if (!_hasSpeech && _timeoutNoSpeechCallback != nullptr) {
      Serial.println("No speech detected during recording, exiting continuous mode");
      stopRecording();
      _timeoutNoSpeechCallback();
    } else {
      Serial.println("Stopping recording");
      stopRecording();
    }
  }
}

/**
 * @brief Check silence status
 * @details If speech was detected and silence duration exceeded, auto stop recording
 *          This is the core function of VAD (Voice Activity Detection)
 */
void ArduinoASRChat::checkSilence() {
  // Check silence - if speech detected and silence duration exceeded
  if (_hasSpeech && _lastSpeechTime > 0) {
    unsigned long silence = millis() - _lastSpeechTime;
    if (silence >= _silenceDuration) {
      Serial.printf("\nSilence detected (%.1fs), stopping\n", silence / 1000.0);
      stopRecording();
    }
  }
}

/**
 * @brief Get recognized text
 * @return Final recognition result string
 */
String ArduinoASRChat::getRecognizedText() {
  return _recognizedText;
}

/**
 * @brief Check if there is a new recognition result
 * @return true if new result available, false if no new result
 */
bool ArduinoASRChat::hasNewResult() {
  return _hasNewResult;
}

/**
 * @brief Clear new result flag
 * @details Should be called after reading result to avoid duplicate processing
 */
void ArduinoASRChat::clearResult() {
  _hasNewResult = false;
}

/**
 * @brief Set recognition result callback function
 * @param callback Callback function pointer, called when new result is available
 */
void ArduinoASRChat::setResultCallback(ResultCallback callback) {
  _resultCallback = callback;
}

/**
 * @brief Set timeout no speech callback function
 * @param callback Callback function pointer, called when recording timeout and no speech detected
 */
void ArduinoASRChat::setTimeoutNoSpeechCallback(TimeoutNoSpeechCallback callback) {
  _timeoutNoSpeechCallback = callback;
}

/**
 * @brief Send full session request (including configuration)
 * @details Build JSON configuration including audio parameters, workflow, etc., send to ASR server
 *          This is the first message of each recognition session
 */
void ArduinoASRChat::sendFullRequest() {
  // Generate unique session ID (timestamp + random number)
  String reqid = String(millis()) + "_" + String(random(10000, 99999));

  // Use MAC address as stable user ID
  String uid = String(ESP.getEfuseMac(), HEX);

  // Build JSON configuration
  StaticJsonDocument<512> doc;
  doc["app"]["cluster"] = _cluster;                    // Service cluster
  doc["user"]["uid"] = uid;                            // User ID
  doc["request"]["reqid"] = reqid;                     // Request ID
  doc["request"]["nbest"] = 1;                         // Number of best results to return
  doc["request"]["workflow"] = "audio_in,resample,partition,vad,fe,decode,itn,nlu_punctuate";  // Processing workflow
  doc["request"]["result_type"] = "full";              // Result type
  doc["request"]["sequence"] = 1;                      // Sequence number
  doc["audio"]["format"] = "raw";                      // Audio format
  doc["audio"]["rate"] = _sampleRate;                  // Sample rate
  doc["audio"]["bits"] = _bitsPerSample;               // Bit depth
  doc["audio"]["channel"] = _channels;                 // Number of channels
  doc["audio"]["codec"] = "raw";                       // Codec

  String json_str;
  serializeJson(doc, json_str);

  Serial.print("Request ID: ");
  Serial.println(reqid);
  Serial.println("Sending config:");
  Serial.println(json_str);

  // ByteDance ASR protocol header (4 bytes)
  uint8_t header[4] = {0x11, (CLIENT_FULL_REQUEST << 4) | NO_SEQUENCE, 0x10, 0x00};
  uint32_t payload_len = json_str.length();
  // Length field (4 bytes, big-endian)
  uint8_t len_bytes[4];
  len_bytes[0] = (payload_len >> 24) & 0xFF;
  len_bytes[1] = (payload_len >> 16) & 0xFF;
  len_bytes[2] = (payload_len >> 8) & 0xFF;
  len_bytes[3] = payload_len & 0xFF;

  // Assemble complete request (protocol header + length + JSON data)
  uint8_t* full_request = new uint8_t[8 + payload_len];
  memcpy(full_request, header, 4);
  memcpy(full_request + 4, len_bytes, 4);
  memcpy(full_request + 8, json_str.c_str(), payload_len);

  sendWebSocketFrame(full_request, 8 + payload_len, 0x02);  // 0x02 = binary frame
  delete[] full_request;
}

/**
 * @brief Send audio data chunk
 * @param data Audio data pointer
 * @param len Data length (bytes)
 * @details Encapsulate audio data in ByteDance ASR protocol format and send via WebSocket
 */
void ArduinoASRChat::sendAudioChunk(uint8_t* data, size_t len) {
  // Protocol header (audio-only request)
  uint8_t header[4] = {0x11, (CLIENT_AUDIO_ONLY_REQUEST << 4) | NO_SEQUENCE, 0x10, 0x00};
  // Length field (big-endian)
  uint8_t len_bytes[4];
  len_bytes[0] = (len >> 24) & 0xFF;
  len_bytes[1] = (len >> 16) & 0xFF;
  len_bytes[2] = (len >> 8) & 0xFF;
  len_bytes[3] = len & 0xFF;

  // Assemble audio request (protocol header + length + audio data)
  uint8_t* audio_request = new uint8_t[8 + len];
  memcpy(audio_request, header, 4);
  memcpy(audio_request + 4, len_bytes, 4);
  memcpy(audio_request + 8, data, len);

  sendWebSocketFrame(audio_request, 8 + len, 0x02);
  delete[] audio_request;
}

/**
 * @brief Send end marker
 * @details Notify server that audio stream has ended, trigger final recognition result
 *          Use negative sequence number (NEG_SEQUENCE) to identify end
 */
void ArduinoASRChat::sendEndMarker() {
  uint8_t header[4] = {0x11, (CLIENT_AUDIO_ONLY_REQUEST << 4) | NEG_SEQUENCE, 0x10, 0x00};
  uint8_t len_bytes[4] = {0x00, 0x00, 0x00, 0x00};  // Length is 0
  uint8_t end_request[8];
  memcpy(end_request, header, 4);
  memcpy(end_request + 4, len_bytes, 4);

  sendWebSocketFrame(end_request, 8, 0x02);
  Serial.println("End marker sent");
}

/**
 * @brief Send Pong response
 * @details Respond to server's Ping message, keep connection alive
 */
void ArduinoASRChat::sendPong() {
  uint8_t pong_data[1] = {0};
  sendWebSocketFrame(pong_data, 0, 0x0A);  // 0x0A = Pong frame
}

/**
 * @brief Send WebSocket frame
 * @param data Data to send
 * @param len Data length
 * @param opcode WebSocket opcode (0x01=text, 0x02=binary, 0x08=close, 0x09=Ping, 0x0A=Pong)
 * @details Encapsulate data frame according to WebSocket protocol, including frame header, mask, data
 */
void ArduinoASRChat::sendWebSocketFrame(uint8_t* data, size_t len, uint8_t opcode) {
  if (!_wsConnected || !_client.connected()) return;

  // Build WebSocket frame header
  uint8_t header[10];
  int header_len = 2;

  header[0] = 0x80 | opcode;  // FIN=1 + opcode
  header[1] = 0x80;           // MASK=1

  // Length encoding (use different format based on data length)
  if (len < 126) {
    header[1] |= len;  // 7-bit length
  } else if (len < 65536) {
    header[1] |= 126;  // Use 16-bit extended length
    header[2] = (len >> 8) & 0xFF;
    header[3] = len & 0xFF;
    header_len = 4;
  } else {
    header[1] |= 127;  // Use 64-bit extended length
    for (int i = 0; i < 8; i++) {
      header[2 + i] = (len >> (56 - i * 8)) & 0xFF;
    }
    header_len = 10;
  }

  // Generate random mask key (client to server must be masked)
  uint8_t mask_key[4];
  for (int i = 0; i < 4; i++) {
    mask_key[i] = random(0, 256);
  }
  memcpy(header + header_len, mask_key, 4);
  header_len += 4;

  // Send frame header
  _client.write(header, header_len);

  // Mask data and send
  for (size_t i = 0; i < len; i++) {
    data[i] ^= mask_key[i % 4];
  }
  _client.write(data, len);
}

/**
 * @brief Handle received WebSocket data
 * @details Parse WebSocket frame, handle different types of messages (text/binary/control frames)
 */
void ArduinoASRChat::handleWebSocketData() {
  // Read WebSocket frame header (2 bytes)
  uint8_t header[2];
  if (_client.readBytes(header, 2) != 2) {
    return;
  }

  bool fin = header[0] & 0x80;           // FIN flag
  uint8_t opcode = header[0] & 0x0F;     // Opcode
  bool masked = header[1] & 0x80;        // MASK flag
  uint64_t payload_len = header[1] & 0x7F;  // Payload length
  
  // Handle extended length
  if (payload_len == 126) {
    uint8_t len_bytes[2];
    _client.readBytes(len_bytes, 2);
    payload_len = (len_bytes[0] << 8) | len_bytes[1];
  } else if (payload_len == 127) {
    uint8_t len_bytes[8];
    _client.readBytes(len_bytes, 8);
    payload_len = 0;
    for (int i = 0; i < 8; i++) {
      payload_len = (payload_len << 8) | len_bytes[i];
    }
  }

  // Read mask key (if present)
  uint8_t mask_key[4] = {0};
  if (masked) {
    _client.readBytes(mask_key, 4);
  }

  // Read payload data
  if (payload_len > 0 && payload_len < 100000) {  // Limit maximum length to prevent memory overflow
    uint8_t* payload = new uint8_t[payload_len];
    size_t bytes_read = _client.readBytes(payload, payload_len);

    if (bytes_read == payload_len) {
      // Unmask
      if (masked) {
        for (size_t i = 0; i < payload_len; i++) {
          payload[i] ^= mask_key[i % 4];
        }
      }

      // Handle different opcodes
      if (opcode == 0x01 || opcode == 0x02) {  // Text or binary data
        parseResponse(payload, payload_len);
      } else if (opcode == 0x08) {  // Close connection
        Serial.println("Server closed connection");
        _wsConnected = false;
        _client.stop();
      } else if (opcode == 0x09) {  // Ping
        sendPong();
      }
    }

    delete[] payload;
  }
}

/**
 * @brief Parse ASR server response
 * @param data Response data
 * @param len Data length
 * @details Parse ByteDance ASR protocol format response, extract recognition results
 *          Implement VAD and result stability detection
 */
void ArduinoASRChat::parseResponse(uint8_t* data, size_t len) {
  if (len < 4) return;

  // Parse ByteDance ASR protocol header
  uint8_t msg_type = data[1] >> 4;      // Message type
  uint8_t header_size = data[0] & 0x0f; // Header size (in 4-byte units)

  if (len < header_size * 4) return;

  // Skip protocol header, get JSON payload
  uint8_t* payload = data + header_size * 4;
  size_t payload_len = len - header_size * 4;

  // Skip additional header bytes based on message type
  if (msg_type == SERVER_FULL_RESPONSE && payload_len > 4) {
    payload += 4;
    payload_len -= 4;
  } else if (msg_type == SERVER_ACK && payload_len >= 8) {
    payload += 8;
    payload_len -= 8;
  } else if (msg_type == SERVER_ERROR_RESPONSE && payload_len >= 8) {
    payload += 8;
    payload_len -= 8;
  }

  // Parse JSON response
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, payload, payload_len);

  if (error) {
    return;
  }

  // Check error code
  if (doc.containsKey("code")) {
    int code = doc["code"];
    if (code != 1000 && code != 1013) {
      // Ignore 1000 (success) and 1013 (silence detection)
      Serial.print("\nError: ");
      serializeJson(doc, Serial);
      Serial.println();
    }
  }

  // Extract recognition results
  if (doc.containsKey("result")) {
    JsonVariant result = doc["result"];
    String current_text = "";

    if (result.is<JsonArray>() && result.size() > 0) {
      if (result[0].containsKey("text")) {
        current_text = result[0]["text"].as<String>();
      }
    }

    if (current_text.length() > 0 && current_text != " ") {
      if (!_hasSpeech) {
        _hasSpeech = true;  // Mark speech detected
        Serial.println("\nSpeech detected...");
      }

      // Update last speech detection time (for silence detection)
      _lastSpeechTime = millis();

      // Result stability detection
      if (current_text == _lastResultText) {
        _sameResultCount++;  // Increment same result count
        if (_sameResultCount <= 3) {
          Serial.printf("Recognizing: %s\n", current_text.c_str());
        } else if (_sameResultCount == 4) {
          Serial.printf("Result stable: %s\n", current_text.c_str());
        }

        // Result stable (10 consecutive same results), automatically stop recording
        if (_sameResultCount >= 10 && _isRecording && !_shouldStop) {
          Serial.println("\nResult stable, stopping recording");
          stopRecording();
        }
      } else {
        // Result changed, reset count
        _sameResultCount = 1;
        _lastResultText = current_text;
        Serial.printf("Recognizing: %s\n", current_text.c_str());
      }
    }
  }
}
