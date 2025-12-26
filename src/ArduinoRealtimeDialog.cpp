/**
 * @file ArduinoRealtimeDialog.cpp
 * @brief Doubao End-to-End Realtime Voice LLM API Implementation
 */

#include "ArduinoRealtimeDialog.h"

/**
 * @brief Constructor
 */
ArduinoRealtimeDialog::ArduinoRealtimeDialog(const char* appId, const char* accessKey) {
  _appId = appId;
  _accessKey = accessKey;
  
  // Delay memory allocation until after WebSocket connection
  // This leaves enough heap memory for SSL handshake
  _sendBuffer = nullptr;
  _ttsBuffer = nullptr;
}

/**
 * @brief Allocate audio buffer memory
 */
bool ArduinoRealtimeDialog::allocateBuffers() {
  if (_sendBuffer != nullptr && _ttsBuffer != nullptr) {
    return true;
  }
  
  // Allocate audio send buffer (small, 3.2KB)
  if (_sendBuffer == nullptr) {
    _sendBuffer = (int16_t*)malloc(_sendBatchSize);
    if (_sendBuffer == nullptr) {
      Serial.println("[Error] Send buffer allocation failed!");
      return false;
    }
  }
  
  // Allocate TTS buffer (prefer PSRAM, try from large to small)
  if (_ttsBuffer == nullptr) {
    // Print memory status
    Serial.printf("[Memory] Heap available: %d bytes\n", ESP.getFreeHeap());
    if (psramFound()) {
      Serial.printf("[Memory] PSRAM available: %d bytes\n", ESP.getFreePsram());
    } else {
      Serial.println("[Warning] PSRAM not detected");
    }
    
    // First try to allocate 1MB from PSRAM (about 20 seconds of audio)
    if (psramFound()) {
      _ttsBufferSize = 1024 * 1024;  // 1MB
      _ttsBuffer = (uint8_t*)ps_malloc(_ttsBufferSize);
      if (_ttsBuffer != nullptr) {
        Serial.printf("[Success] TTS buffer allocated: %d KB (PSRAM)\n", _ttsBufferSize / 1024);
      } else {
        Serial.printf("[Failed] PSRAM 1MB allocation failed\n");
      }
    }
    
    // PSRAM allocation failed, try smaller buffer from heap
    if (_ttsBuffer == nullptr) {
      Serial.println("[Trying] Allocating 512KB from heap...");
      _ttsBufferSize = 512 * 1024;
      _ttsBuffer = (uint8_t*)malloc(_ttsBufferSize);
      if (_ttsBuffer != nullptr) {
        Serial.printf("[Success] TTS buffer allocated: %d KB (heap)\n", _ttsBufferSize / 1024);
      }
    }
    
    if (_ttsBuffer == nullptr) {
      Serial.println("[Trying] Allocating 256KB from heap...");
      _ttsBufferSize = 256 * 1024;
      _ttsBuffer = (uint8_t*)malloc(_ttsBufferSize);
      if (_ttsBuffer != nullptr) {
        Serial.printf("[Success] TTS buffer allocated: %d KB (heap)\n", _ttsBufferSize / 1024);
      }
    }
    
    if (_ttsBuffer == nullptr) {
      Serial.println("[Trying] Allocating 128KB from heap...");
      _ttsBufferSize = 128 * 1024;
      _ttsBuffer = (uint8_t*)malloc(_ttsBufferSize);
      if (_ttsBuffer != nullptr) {
        Serial.printf("[Success] TTS buffer allocated: %d KB (heap)\n", _ttsBufferSize / 1024);
      }
    }
    
    if (_ttsBuffer == nullptr) {
      Serial.println("[Trying] Allocating 64KB from heap...");
      _ttsBufferSize = 64 * 1024;
      _ttsBuffer = (uint8_t*)malloc(_ttsBufferSize);
      if (_ttsBuffer != nullptr) {
        Serial.printf("[Success] TTS buffer allocated: %d KB (heap)\n", _ttsBufferSize / 1024);
      }
    }
    
    if (_ttsBuffer == nullptr) {
      Serial.println("[Error] TTS buffer allocation failed! All attempts failed");
      Serial.printf("[Memory] Current heap available: %d bytes\n", ESP.getFreeHeap());
      free(_sendBuffer);
      _sendBuffer = nullptr;
      return false;
    }
  }
  
  return true;
}

/**
 * @brief Set audio parameters
 */
void ArduinoRealtimeDialog::setAudioParams(int sampleRate, int bitsPerSample, int channels) {
  _sampleRate = sampleRate;
  _bitsPerSample = bitsPerSample;
  _channels = channels;
}

/**
 * @brief Set model version
 */
void ArduinoRealtimeDialog::setModelVersion(const char* version) {
  _modelVersion = String(version);
}

/**
 * @brief Set TTS speaker voice
 */
void ArduinoRealtimeDialog::setTTSSpeaker(const char* speaker) {
  _ttsSpeaker = speaker;
}

/**
 * @brief Set system role (O version only)
 */
void ArduinoRealtimeDialog::setSystemRole(const char* botName, const char* systemRole, const char* speakingStyle) {
  if (botName != nullptr) _botName = String(botName);
  if (systemRole != nullptr) _systemRole = String(systemRole);
  if (speakingStyle != nullptr) _speakingStyle = String(speakingStyle);
}

/**
 * @brief Set character manifest (SC version only)
 */
void ArduinoRealtimeDialog::setCharacterManifest(const char* manifest) {
  if (manifest != nullptr) _characterManifest = String(manifest);
}

/**
 * @brief Initialize INMP441 microphone
 */
bool ArduinoRealtimeDialog::initINMP441Microphone(int i2sSckPin, int i2sWsPin, int i2sSdPin) {
  _micType = MIC_TYPE_INMP441;
  _I2S.setPins(i2sSckPin, i2sWsPin, -1, i2sSdPin);
  
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
 * @brief Initialize I2S audio output
 */
bool ArduinoRealtimeDialog::initI2SAudioOutput(int bclk, int lrc, int dout) {
  return _i2sPlayer.init(bclk, lrc, dout, 24000);
}

/**
 * @brief Generate WebSocket key
 */
String ArduinoRealtimeDialog::generateWebSocketKey() {
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
 * @brief Generate session ID
 */
String ArduinoRealtimeDialog::generateSessionId() {
  char uuid[37];
  sprintf(uuid, "%08x-%04x-%04x-%04x-%012x",
          random(0x100000000), random(0x10000), random(0x10000),
          random(0x10000), random(0x1000000000000));
  return String(uuid);
}

/**
 * @brief Connect to WebSocket server
 */
bool ArduinoRealtimeDialog::connectWebSocket() {
  Serial.println("Connecting to WebSocket server...");
  
  // Configure SSL client
  _client.setInsecure();
  _client.setTimeout(15000);
  
  // Establish SSL connection
  if (!_client.connect(_wsHost, _wsPort)) {
    Serial.println("SSL connection failed!");
    return false;
  }
  
  Serial.println("SSL connection successful");
  _client.setNoDelay(true);
  
  // After SSL connection succeeds, allocate audio buffers
  if (!allocateBuffers()) {
    Serial.println("Memory allocation failed!");
    _client.stop();
    return false;
  }
  
  // Generate WebSocket key and send handshake request (using full 4-parameter authentication)
  String ws_key = generateWebSocketKey();
  String request = String("GET ") + _wsPath + " HTTP/1.1\r\n";
  request += String("Host: ") + _wsHost + "\r\n";
  request += "Upgrade: websocket\r\n";
  request += "Connection: Upgrade\r\n";
  request += "Sec-WebSocket-Key: " + ws_key + "\r\n";
  request += "Sec-WebSocket-Version: 13\r\n";
  request += String("X-Api-App-ID: ") + _appId + "\r\n";
  request += String("X-Api-Access-Key: ") + _accessKey + "\r\n";
  request += "X-Api-Resource-Id: volc.speech.dialog\r\n";
  request += "X-Api-App-Key: PlgvMymc7f3tQnJ6\r\n";
  request += "\r\n";
   Serial.println("Sending WebSocket handshake request...");
  _client.print(request);
  
  // Wait for server response
  unsigned long timeout = millis();
  while (_client.connected() && !_client.available()) {
    if (millis() - timeout > 10000) {  // Increased to 10 second timeout
      Serial.println("Server response timeout");
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
    Serial.println("WebSocket connection successful");
    _wsConnected = true;
    
    // Send StartConnection event
    sendStartConnection();
    delay(100);
    
    // Wait for and handle StartConnection response
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
void ArduinoRealtimeDialog::disconnectWebSocket() {
  if (_wsConnected) {
    if (_sessionStarted) {
      finishSession();
      delay(100);
    }
    sendFinishConnection();
    delay(100);
    _client.stop();
    _wsConnected = false;
    Serial.println("WebSocket disconnected");
  }
}

/**
 * @brief Check if WebSocket is connected
 */
bool ArduinoRealtimeDialog::isWebSocketConnected() {
  return _wsConnected && _client.connected();
}

/**
 * @brief Start session
 */
bool ArduinoRealtimeDialog::startSession() {
  if (!_wsConnected) {
    Serial.println("WebSocket not connected!");
    return false;
  }
  
  if (_sessionStarted) {
    Serial.println("Session already started!");
    return false;
  }
  
  // Generate new session ID
  _sessionId = generateSessionId();
  
  Serial.println("Starting session: " + _sessionId);
  
  // Send StartSession event
  sendStartSession();
  delay(100);
  
  // Wait for and handle StartSession response
  if (_client.available()) {
    handleWebSocketData();
  }
  
  _sessionStarted = true;
  return true;
}

/**
 * @brief Finish session
 */
void ArduinoRealtimeDialog::finishSession() {
  if (!_sessionStarted) {
    return;
  }
  
  Serial.println("Finishing session");
  sendFinishSession();
  _sessionStarted = false;
}

/**
 * @brief Start recording
 */
bool ArduinoRealtimeDialog::startRecording() {
  if (!_sessionStarted) {
    Serial.println("Session not started!");
    return false;
  }
  
  if (_isRecording) {
    // Silent handling, avoid duplicate prints
    return false;
  }
  
  Serial.println("\n[System] Listening... Please speak");
  
  _isRecording = true;
  _userSpeaking = false;
  _recognizedText = "";
  _lastASRText = "";
  _sendBufferPos = 0;
  
  return true;
}

/**
 * @brief Stop recording
 */
void ArduinoRealtimeDialog::stopRecording() {
  if (!_isRecording) {
    return;
  }
  
  // Send remaining audio data in buffer
  if (_sendBufferPos > 0) {
    sendAudioChunk((uint8_t*)_sendBuffer, _sendBufferPos * 2);
    _sendBufferPos = 0;
  }
  
  // Removed recording stop log to reduce output
  
  _isRecording = false;
}

/**
 * @brief Check if recording
 */
bool ArduinoRealtimeDialog::isRecording() {
  return _isRecording;
}

/**
 * @brief Check if playing TTS
 */
bool ArduinoRealtimeDialog::isPlayingTTS() {
  return _isPlayingTTS;
}

/**
 * @brief Get recognized text
 */
String ArduinoRealtimeDialog::getRecognizedText() {
  return _recognizedText;
}

/**
 * @brief Clear recognized text
 */
void ArduinoRealtimeDialog::clearRecognizedText() {
  _recognizedText = "";
  _lastASRText = "";
}

/**
 * @brief Set ASR speech detected callback
 */
void ArduinoRealtimeDialog::setASRDetectedCallback(ASRDetectedCallback callback) {
  _asrDetectedCallback = callback;
}

/**
 * @brief Set ASR ended callback
 */
void ArduinoRealtimeDialog::setASREndedCallback(ASREndedCallback callback) {
  _asrEndedCallback = callback;
}

/**
 * @brief Set TTS started callback
 */
void ArduinoRealtimeDialog::setTTSStartedCallback(TTSStartedCallback callback) {
  _ttsStartedCallback = callback;
}

/**
 * @brief Set TTS ended callback
 */
void ArduinoRealtimeDialog::setTTSEndedCallback(TTSEndedCallback callback) {
  _ttsEndedCallback = callback;
}

/**
 * @brief Main loop processing function
 */
void ArduinoRealtimeDialog::loop() {
  static unsigned long lastDebugTime = 0;
  static unsigned long lastDataTime = 0;
  
  // Check connection status
  if (_wsConnected && !_client.connected()) {
    Serial.println("Connection lost");
    _wsConnected = false;
    _sessionStarted = false;
    _isRecording = false;
  }
  
  if (!_wsConnected) {
    return;
  }
  
  // Process audio sending during recording
  if (_isRecording) {
    processAudioSending();
  }
  
  // Process received data
  if (_client.available()) {
    lastDataTime = millis();
    handleWebSocketData();
  }
  
}

/**
 * @brief Process audio data sending
 */
void ArduinoRealtimeDialog::processAudioSending() {
  // Read audio samples and send
  for (int i = 0; i < _samplesPerRead; i++) {
    if (!_I2S.available()) {
      break;
    }
    
    int sample = _I2S.read();
    
    // Filter invalid data
    if (sample != 0 && sample != -1 && sample != 1) {
      _sendBuffer[_sendBufferPos++] = (int16_t)sample;
      
      // Buffer full, send immediately
      if (_sendBufferPos >= _sendBatchSize / 2) {
        sendAudioChunk((uint8_t*)_sendBuffer, _sendBufferPos * 2);
        _sendBufferPos = 0;
      }
    }
  }
  
  yield();
}

/**
 * @brief Send WebSocket frame
 */
void ArduinoRealtimeDialog::sendWebSocketFrame(uint8_t* data, size_t len, uint8_t opcode) {
  if (!_wsConnected || !_client.connected()) return;
  
  // Build WebSocket frame header
  uint8_t header[10];
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
  for (size_t i = 0; i < len; i++) {
    data[i] ^= mask_key[i % 4];
  }
  _client.write(data, len);
}
/**
 * @brief Send StartConnection event
 */
void ArduinoRealtimeDialog::sendStartConnection() {
  // Protocol header: version(4bit) + header_size(4bit) + message_type(4bit) + flags(4bit) +
  //                  serialization(4bit) + compression(4bit) + reserved(8bit)
  uint8_t header[4] = {0x11, (MSG_TYPE_CLIENT_FULL << 4) | MSG_FLAG_WITH_EVENT, (SERIAL_JSON << 4) | COMPRESS_NONE, 0x00};
  
  // Event ID (4 bytes)
  uint8_t event_bytes[4];
  event_bytes[0] = (EVENT_START_CONNECTION >> 24) & 0xFF;
  event_bytes[1] = (EVENT_START_CONNECTION >> 16) & 0xFF;
  event_bytes[2] = (EVENT_START_CONNECTION >> 8) & 0xFF;
  event_bytes[3] = EVENT_START_CONNECTION & 0xFF;
  
  // Payload
  String payload = "{}";
  uint32_t payload_len = payload.length();
  uint8_t len_bytes[4];
  len_bytes[0] = (payload_len >> 24) & 0xFF;
  len_bytes[1] = (payload_len >> 16) & 0xFF;
  len_bytes[2] = (payload_len >> 8) & 0xFF;
  len_bytes[3] = payload_len & 0xFF;
  
  // Assemble complete request
  uint8_t* request = new uint8_t[4 + 4 + 4 + payload_len];
  memcpy(request, header, 4);
  memcpy(request + 4, event_bytes, 4);
  memcpy(request + 8, len_bytes, 4);
  memcpy(request + 12, payload.c_str(), payload_len);
  
  sendWebSocketFrame(request, 12 + payload_len, 0x02);
  delete[] request;
}

/**
 * @brief Send FinishConnection event
 */
void ArduinoRealtimeDialog::sendFinishConnection() {
  uint8_t header[4] = {0x11, (MSG_TYPE_CLIENT_FULL << 4) | MSG_FLAG_WITH_EVENT, (SERIAL_JSON << 4) | COMPRESS_NONE, 0x00};
  
  uint8_t event_bytes[4];
  event_bytes[0] = (EVENT_FINISH_CONNECTION >> 24) & 0xFF;
  event_bytes[1] = (EVENT_FINISH_CONNECTION >> 16) & 0xFF;
  event_bytes[2] = (EVENT_FINISH_CONNECTION >> 8) & 0xFF;
  event_bytes[3] = EVENT_FINISH_CONNECTION & 0xFF;
  
  String payload = "{}";
  uint32_t payload_len = payload.length();
  uint8_t len_bytes[4];
  len_bytes[0] = (payload_len >> 24) & 0xFF;
  len_bytes[1] = (payload_len >> 16) & 0xFF;
  len_bytes[2] = (payload_len >> 8) & 0xFF;
  len_bytes[3] = payload_len & 0xFF;
  
  uint8_t* request = new uint8_t[4 + 4 + 4 + payload_len];
  memcpy(request, header, 4);
  memcpy(request + 4, event_bytes, 4);
  memcpy(request + 8, len_bytes, 4);
  memcpy(request + 12, payload.c_str(), payload_len);
  
  sendWebSocketFrame(request, 12 + payload_len, 0x02);
  delete[] request;
}

/**
 * @brief Send StartSession event
 */
void ArduinoRealtimeDialog::sendStartSession() {
  // Build JSON configuration
  StaticJsonDocument<1024> doc;
  
  // ASR configuration
  doc["asr"]["extra"]["end_smooth_window_ms"] = 1500;
  
  // TTS configuration - Use PCM format for direct I2S playback
  doc["tts"]["speaker"] = _ttsSpeaker;
  doc["tts"]["audio_config"]["channel"] = 1;
  doc["tts"]["audio_config"]["format"] = "pcm_s16le";  // 16-bit PCM, little-endian
  doc["tts"]["audio_config"]["sample_rate"] = 24000;
  
  // Dialog configuration - Use different config based on model version
  if (_modelVersion == "SC") {
    // SC version: use character_manifest
    if (_characterManifest.length() > 0) {
      doc["dialog"]["character_manifest"] = _characterManifest;
    }
  } else {
    // O version: use bot_name, system_role, speaking_style
    if (_botName.length() > 0) {
      doc["dialog"]["bot_name"] = _botName;
    }
    if (_systemRole.length() > 0) {
      doc["dialog"]["system_role"] = _systemRole;
    }
    if (_speakingStyle.length() > 0) {
      doc["dialog"]["speaking_style"] = _speakingStyle;
    }
  }
  
  if (_dialogId.length() > 0) {
    doc["dialog"]["dialog_id"] = _dialogId;
  }
  
  doc["dialog"]["extra"]["input_mod"] = "audio";
  doc["dialog"]["extra"]["model"] = _modelVersion;
  
  String json_str;
  serializeJson(doc, json_str);
  // Serial.println("StartSession config:");
  // Serial.println(json_str);  
  // Protocol header
  uint8_t header[4] = {0x11, (MSG_TYPE_CLIENT_FULL << 4) | MSG_FLAG_WITH_EVENT, (SERIAL_JSON << 4) | COMPRESS_NONE, 0x00};
  
  // Event ID
  uint8_t event_bytes[4];
  event_bytes[0] = (EVENT_START_SESSION >> 24) & 0xFF;
  event_bytes[1] = (EVENT_START_SESSION >> 16) & 0xFF;
  event_bytes[2] = (EVENT_START_SESSION >> 8) & 0xFF;
  event_bytes[3] = EVENT_START_SESSION & 0xFF;
  
  // Session ID
  uint32_t session_id_len = _sessionId.length();
  uint8_t session_id_len_bytes[4];
  session_id_len_bytes[0] = (session_id_len >> 24) & 0xFF;
  session_id_len_bytes[1] = (session_id_len >> 16) & 0xFF;
  session_id_len_bytes[2] = (session_id_len >> 8) & 0xFF;
  session_id_len_bytes[3] = session_id_len & 0xFF;
  
  // Payload
  uint32_t payload_len = json_str.length();
  uint8_t payload_len_bytes[4];
  payload_len_bytes[0] = (payload_len >> 24) & 0xFF;
  payload_len_bytes[1] = (payload_len >> 16) & 0xFF;
  payload_len_bytes[2] = (payload_len >> 8) & 0xFF;
  payload_len_bytes[3] = payload_len & 0xFF;
  
  // Assemble complete request
  size_t total_len = 4 + 4 + 4 + session_id_len + 4 + payload_len;
  uint8_t* request = new uint8_t[total_len];
  size_t pos = 0;
  
  memcpy(request + pos, header, 4); pos += 4;
  memcpy(request + pos, event_bytes, 4); pos += 4;
  memcpy(request + pos, session_id_len_bytes, 4); pos += 4;
  memcpy(request + pos, _sessionId.c_str(), session_id_len); pos += session_id_len;
  memcpy(request + pos, payload_len_bytes, 4); pos += 4;
  memcpy(request + pos, json_str.c_str(), payload_len);
  
  sendWebSocketFrame(request, total_len, 0x02);
  delete[] request;
}

/**
 * @brief Send FinishSession event
 */
void ArduinoRealtimeDialog::sendFinishSession() {
  uint8_t header[4] = {0x11, (MSG_TYPE_CLIENT_FULL << 4) | MSG_FLAG_WITH_EVENT, (SERIAL_JSON << 4) | COMPRESS_NONE, 0x00};
  
  uint8_t event_bytes[4];
  event_bytes[0] = (EVENT_FINISH_SESSION >> 24) & 0xFF;
  event_bytes[1] = (EVENT_FINISH_SESSION >> 16) & 0xFF;
  event_bytes[2] = (EVENT_FINISH_SESSION >> 8) & 0xFF;
  event_bytes[3] = EVENT_FINISH_SESSION & 0xFF;
  
  uint32_t session_id_len = _sessionId.length();
  uint8_t session_id_len_bytes[4];
  session_id_len_bytes[0] = (session_id_len >> 24) & 0xFF;
  session_id_len_bytes[1] = (session_id_len >> 16) & 0xFF;
  session_id_len_bytes[2] = (session_id_len >> 8) & 0xFF;
  session_id_len_bytes[3] = session_id_len & 0xFF;
  
  String payload = "{}";
  uint32_t payload_len = payload.length();
  uint8_t payload_len_bytes[4];
  payload_len_bytes[0] = (payload_len >> 24) & 0xFF;
  payload_len_bytes[1] = (payload_len >> 16) & 0xFF;
  payload_len_bytes[2] = (payload_len >> 8) & 0xFF;
  payload_len_bytes[3] = payload_len & 0xFF;
  
  size_t total_len = 4 + 4 + 4 + session_id_len + 4 + payload_len;
  uint8_t* request = new uint8_t[total_len];
  size_t pos = 0;
  
  memcpy(request + pos, header, 4); pos += 4;
  memcpy(request + pos, event_bytes, 4); pos += 4;
  memcpy(request + pos, session_id_len_bytes, 4); pos += 4;
  memcpy(request + pos, _sessionId.c_str(), session_id_len); pos += session_id_len;
  memcpy(request + pos, payload_len_bytes, 4); pos += 4;
  memcpy(request + pos, payload.c_str(), payload_len);
  
  sendWebSocketFrame(request, total_len, 0x02);
  delete[] request;
}

/**
 * @brief Send audio data chunk
 */
void ArduinoRealtimeDialog::sendAudioChunk(uint8_t* data, size_t len) {
  // Protocol header (audio data doesn't need JSON serialization and no compression)
  uint8_t header[4] = {0x11, (MSG_TYPE_CLIENT_AUDIO << 4) | MSG_FLAG_WITH_EVENT, (SERIAL_RAW << 4) | COMPRESS_NONE, 0x00};
  
  // Event ID
  uint8_t event_bytes[4];
  event_bytes[0] = (EVENT_TASK_REQUEST >> 24) & 0xFF;
  event_bytes[1] = (EVENT_TASK_REQUEST >> 16) & 0xFF;
  event_bytes[2] = (EVENT_TASK_REQUEST >> 8) & 0xFF;
  event_bytes[3] = EVENT_TASK_REQUEST & 0xFF;
  
  // Session ID
  uint32_t session_id_len = _sessionId.length();
  uint8_t session_id_len_bytes[4];
  session_id_len_bytes[0] = (session_id_len >> 24) & 0xFF;
  session_id_len_bytes[1] = (session_id_len >> 16) & 0xFF;
  session_id_len_bytes[2] = (session_id_len >> 8) & 0xFF;
  session_id_len_bytes[3] = session_id_len & 0xFF;
  
  // Payload length
  uint8_t payload_len_bytes[4];
  payload_len_bytes[0] = (len >> 24) & 0xFF;
  payload_len_bytes[1] = (len >> 16) & 0xFF;
  payload_len_bytes[2] = (len >> 8) & 0xFF;
  payload_len_bytes[3] = len & 0xFF;
  
  // Assemble audio request
  size_t total_len = 4 + 4 + 4 + session_id_len + 4 + len;
  uint8_t* request = new uint8_t[total_len];
  size_t pos = 0;
  
  memcpy(request + pos, header, 4); pos += 4;
  memcpy(request + pos, event_bytes, 4); pos += 4;
  memcpy(request + pos, session_id_len_bytes, 4); pos += 4;
  memcpy(request + pos, _sessionId.c_str(), session_id_len); pos += session_id_len;
  memcpy(request + pos, payload_len_bytes, 4); pos += 4;
  memcpy(request + pos, data, len);
  
  sendWebSocketFrame(request, total_len, 0x02);
  delete[] request;
}

/**
 * @brief Send Pong response
 */
void ArduinoRealtimeDialog::sendPong() {
  uint8_t pong_data[1] = {0};
  sendWebSocketFrame(pong_data, 0, 0x0A);
}

/**
 * @brief Handle received WebSocket data
 */
void ArduinoRealtimeDialog::handleWebSocketData() {
  // Only process one complete WebSocket frame
  if (_client.available() < 2) {
    return;  // Insufficient data, wait for more
  }
  
  // Read WebSocket frame header (2 bytes)
  uint8_t header[2];
  if (_client.readBytes(header, 2) != 2) {
    return;
  }
  
  bool fin = header[0] & 0x80;
  uint8_t opcode = header[0] & 0x0F;
  bool masked = header[1] & 0x80;
  uint64_t payload_len = header[1] & 0x7F;
  
  // Handle extended length
  if (payload_len == 126) {
    uint8_t len_bytes[2];
    if (_client.readBytes(len_bytes, 2) != 2) {
      return;
    }
    payload_len = (len_bytes[0] << 8) | len_bytes[1];
  } else if (payload_len == 127) {
    uint8_t len_bytes[8];
    if (_client.readBytes(len_bytes, 8) != 8) {
      return;
    }
    payload_len = 0;
    for (int i = 0; i < 8; i++) {
      payload_len = (payload_len << 8) | len_bytes[i];
    }
  }
 Serial.printf("[WS Frame] opcode=0x%X, masked=%d, payload_len=%llu\n", opcode, masked, payload_len);  
  // Read mask key (if present)
  uint8_t mask_key[4] = {0};
  if (masked) {
    if (_client.readBytes(mask_key, 4) != 4) {
      return;
    }
  }
  
  // Read payload data - Critical fix: must fully consume payload to maintain frame sync
  if (payload_len > 0 && payload_len < 1000000) {  // Increased to 1MB limit, fully utilize 8MB PSRAM
    uint8_t* payload = nullptr;
    
    // Try to allocate from PSRAM first
    if (psramFound()) {
      payload = (uint8_t*)ps_malloc(payload_len);
      if (payload == nullptr) {
        Serial.printf("[Warning] PSRAM allocation of %llu bytes failed, trying heap\n", payload_len);
      }
    }
    
    // If PSRAM unavailable or allocation failed, try heap allocation
    if (payload == nullptr) {
      payload = (uint8_t*)malloc(payload_len);
      if (payload == nullptr) {
Serial.printf("[Error] Memory allocation failed! Must skip %llu bytes byte-by-byte to maintain sync\n", payload_len);
        // Even if allocation fails, must consume payload to maintain frame sync
        for (size_t i = 0; i < payload_len; i++) {
          _client.read();
        }
        return;
      }
    }
    
    // Read payload - Ultimate optimization: streaming read, read as much as available, don't wait for complete block
    size_t bytes_read = 0;
    size_t remaining = payload_len;
    uint8_t* write_ptr = payload;
    
    unsigned long read_start = millis();
    
    while (remaining > 0) {
      // Check if data is available to read
      int available = _client.available();
      
      if (available > 0) {
        // Read immediately if data available, don't wait for complete block
        size_t to_read = (available < remaining) ? available : remaining;
        size_t read_this_time = _client.readBytes(write_ptr, to_read);
        
        if (read_this_time > 0) {
          bytes_read += read_this_time;
          write_ptr += read_this_time;
          remaining -= read_this_time;
        }
      } else {
        // Only yield when no data, avoid busy waiting
        yield();
        
        // Timeout check (5 second timeout)
        if (millis() - read_start > 5000) {
          // Try to skip remaining bytes
          for (size_t i = 0; i < remaining; i++) {
            if (_client.available()) {
              _client.read();
            } else {
              break;
            }
          }
          free(payload);
          return;
        }
      }
    }
    
    if (bytes_read != payload_len) {
      free(payload);
      return;
    }
    
    // Process after successfully reading complete payload
    // Unmask
    if (masked) {
      for (size_t i = 0; i < payload_len; i++) {
        payload[i] ^= mask_key[i % 4];
      }
    }
    
    // Handle different opcodes
    if (opcode == 0x02) {  // Binary data - custom protocol
      parseResponse(payload, payload_len);
    } else if (opcode == 0x08) {  // Close connection
      Serial.println("Server closed connection");
      _wsConnected = false;
      _client.stop();
    } else if (opcode == 0x09) {  // Ping
      sendPong();
    }
    
    // Free memory (memory allocated by ps_malloc is also freed with free)
    free(payload);
  } else if (payload_len >= 1000000) {
    // Payload too large (over 1MB), must consume to maintain sync
    for (size_t i = 0; i < payload_len; i++) {
      _client.read();
    }
  }
}

/**
 * @brief Parse server response
 */
void ArduinoRealtimeDialog::parseResponse(uint8_t* data, size_t len) {
  if (len < 4) {
    return;
  }
  
  // Parse protocol header
  uint8_t protocol_version = data[0] >> 4;
  uint8_t header_size = data[0] & 0x0F;
  uint8_t message_type = data[1] >> 4;
  uint8_t message_flags = data[1] & 0x0F;
  uint8_t serialization = data[2] >> 4;
  uint8_t compression = data[2] & 0x0F;
  
  // Skip invalid message types (may be continuation frames of fragmented messages)
  if (message_type == 0x0) {
    return;
  }
  
  if (len < header_size * 4) return;
  
  // Skip protocol header
  uint8_t* payload = data + header_size * 4;
  size_t payload_len = len - header_size * 4;
  
  // Parse optional fields
  int eventId = 0;
  
  // Check if event ID present
  if (message_flags & MSG_FLAG_WITH_EVENT) {
    if (payload_len >= 4) {
      eventId = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
      payload += 4;
      payload_len -= 4;
    }
  }
  
  // Handle different message types
  if (message_type == MSG_TYPE_SERVER_FULL || message_type == MSG_TYPE_SERVER_ACK) {
    // Skip session ID
    if (payload_len >= 4) {
      uint32_t session_id_len = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
      payload += 4;
      payload_len -= 4;
      
      if (payload_len >= session_id_len) {
        payload += session_id_len;
        payload_len -= session_id_len;
      }
    }
    
    // Read payload length
    if (payload_len >= 4) {
      uint32_t data_len = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
      payload += 4;
      payload_len -= 4;
      
      // Process payload data
      if (message_type == MSG_TYPE_SERVER_ACK && serialization == SERIAL_RAW) {
        // This is TTS audio data
        if (compression == COMPRESS_GZIP) {
          Serial.println("TTS audio uses GZIP compression, not supported yet");
        } else {
          processTTSAudio(payload, payload_len);
        }
      } else if (serialization == SERIAL_JSON && payload_len > 0) {
        // Parse JSON data
        StaticJsonDocument<2048> doc;
        DeserializationError error = deserializeJson(doc, payload, payload_len);
        
        if (!error) {
          JsonObject root = doc.as<JsonObject>();
          handleServerEvent(eventId, root);
        }
      }
    }
  } else if (message_type == MSG_TYPE_SERVER_ERROR) {
    // Error response
    if (payload_len >= 4) {
      uint32_t error_code = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
      Serial.print("Server error code: ");
      Serial.println(error_code);
    }
  }
}

/**
 * @brief Handle server events
 */
void ArduinoRealtimeDialog::handleServerEvent(int eventId, JsonObject& payload) {
  switch (eventId) {
    case EVENT_CONNECTION_STARTED:
      Serial.println("Connection started");
      break;
      
    case EVENT_SESSION_STARTED:
      Serial.println("Session started");
      if (payload.containsKey("dialog_id")) {
        _dialogId = payload["dialog_id"].as<String>();
        Serial.println("Dialog ID: " + _dialogId);
      }
      break;
      
    case EVENT_ASR_INFO:
      // ASR detected speech start
      Serial.println("\n[ASR] Speech detected!");
      _userSpeaking = true;
      if (_asrDetectedCallback != nullptr) {
        _asrDetectedCallback();
      }
      break;
      
    case EVENT_ASR_RESPONSE:
      // ASR recognition result
      if (payload.containsKey("results")) {
        JsonArray results = payload["results"];
        if (results.size() > 0) {
          String text = results[0]["text"].as<String>();
          bool is_interim = results[0]["is_interim"] | false;
          
          if (text.length() > 0) {
            _lastASRText = text;
            Serial.print("[ASR] ");
            Serial.print(is_interim ? "Interim" : "Final");
            Serial.print(": ");
            Serial.println(text);
          }
        }
      }
      break;
      
    case EVENT_ASR_ENDED:
      // ASR recognition ended
      Serial.println("\n[ASR] Recognition ended");
      _userSpeaking = false;
      _recognizedText = _lastASRText;
      
      if (_asrEndedCallback != nullptr && _recognizedText.length() > 0) {
        _asrEndedCallback(_recognizedText);
      }
      break;
      
    case EVENT_TTS_SENTENCE_START:
      // TTS sentence start
      if (payload.containsKey("text")) {
        String ttsText = payload["text"].as<String>();
        Serial.println("\n[TTS] Starting playback: " + ttsText);
      } else {
        Serial.println("\n[TTS] Starting playback");
      }
      
      if (!_isPlayingTTS) {
        _isPlayingTTS = true;
        _ttsBufferPos = 0;
        
        if (_ttsStartedCallback != nullptr) {
          _ttsStartedCallback();
        }
      }
      break;
      
    case EVENT_TTS_ENDED:
      // TTS audio reception complete, now play the complete sentence at once
      
      // Play complete audio from buffer
      if (_ttsBufferPos > 0) {
        _i2sPlayer.play(_ttsBuffer, _ttsBufferPos);
        
        // Calculate playback duration: bytes / (sample_rate * bytes_per_sample * channels)
        // For 24kHz, 16-bit (2 bytes), mono: duration_ms = bytes / (24000 * 2) * 1000
        unsigned long playback_duration_ms = (_ttsBufferPos * 1000) / (24000 * 2);
        
        // Add 200ms buffer to ensure complete playback
        delay(playback_duration_ms + 200);
        
        _ttsBufferPos = 0;
      }
      
      // Stop I2S playback after audio finishes
      _i2sPlayer.stop();
      
      // Reset state
      _isPlayingTTS = false;
      _ttsBufferPos = 0;
      
      if (_ttsEndedCallback != nullptr) {
        _ttsEndedCallback();
      }
      break;
      
    case EVENT_CHAT_RESPONSE:
      // Chat response text
      if (payload.containsKey("content")) {
        String content = payload["content"].as<String>();
        Serial.print("[Chat] ");
        Serial.println(content);
      } else {
        Serial.println("[Chat] Received empty response");
      }
      break;
      
    default:
      // Other events
      Serial.print("Event ");
      Serial.print(eventId);
      Serial.println(" received");
      break;
  }
}

/**
 * @brief Process TTS audio data (PCM format)
 */
void ArduinoRealtimeDialog::processTTSAudio(uint8_t* data, size_t len) {
  // Check if buffer is allocated
  if (_ttsBuffer == nullptr) {
    return;
  }
  
  // Add received PCM data to buffer
  size_t space_available = _ttsBufferSize - _ttsBufferPos;
  size_t to_copy = (len < space_available) ? len : space_available;
  
  if (to_copy > 0) {
    memcpy(_ttsBuffer + _ttsBufferPos, data, to_copy);
    _ttsBufferPos += to_copy;
  }
  
  // Don't play immediately, wait for EVENT_TTS_ENDED event to play complete sentence at once
  // This avoids segmented playback, making speech more coherent and smooth
}