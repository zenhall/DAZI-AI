#include "ArduinoGPTChat.h"
#include <SPIFFS.h>

// Default API configuration - users can modify these values or set their own configuration via setApiConfig()
const char* DEFAULT_API_KEY = "";
const char* DEFAULT_API_BASE_URL = "";

// Global variable used by Audio library
String g_api_host = "";

// Base64 encoding table
const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Base64 encoding function
 * @param input Input data pointer
 * @param length Input data length
 * @param output Output buffer (must be large enough)
 *
 * Encodes binary data to Base64 string format
 * Handles standard Base64 encoding converting 3 bytes input to 4 bytes output
 */
void ArduinoGPTChat::base64_encode(const uint8_t* input, size_t length, char* output) {
  size_t i = 0, j = 0;
  uint8_t char_array_3[3];  // Store 3 bytes of input data
  uint8_t char_array_4[4];  // Store 4 bytes of Base64 output

  // Process input data, 3 bytes at a time
  while (length--) {
    char_array_3[i++] = *(input++);
    if (i == 3) {
      // Convert 3 bytes to 4 Base64 characters
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;  // Take first 6 bits
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);  // Bits 2-6
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);  // Bits 7-12
      char_array_4[3] = char_array_3[2] & 0x3f;  // Last 6 bits

      // Write encoded characters to output
      for(i = 0; i < 4; i++)
        output[j++] = base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  // Handle remaining data less than 3 bytes
  if (i) {
    // Fill remaining bytes with 0
    for(size_t k = i; k < 3; k++)
      char_array_3[k] = '\0';

    // Same conversion logic
    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    char_array_4[3] = char_array_3[2] & 0x3f;

    // Output valid characters, pad remaining with '='
    for (size_t k = 0; k < i + 1; k++)
      output[j++] = base64_chars[char_array_4[k]];

    while(i++ < 3)
      output[j++] = '=';
  }
  output[j] = '\0';  // String terminator
}

/**
 * @brief Calculate required buffer length for Base64 encoding
 * @param input_length Input data length
 * @return Required buffer length after encoding (including terminator)
 *
 * Base64 encoding converts 3 bytes to 4 bytes, padding needed if less than 3 bytes
 * Therefore output length is always a multiple of 4, plus 1 terminator
 */
size_t ArduinoGPTChat::base64_encode_length(size_t input_length) {
  size_t ret_size = input_length;
  // If length is not a multiple of 3, calculate padding bytes needed
  if (ret_size % 3 != 0)
    ret_size += 3 - (ret_size % 3);
  ret_size /= 3;        // Calculate how many 3-byte groups
  ret_size *= 4;        // Each group converts to 4 Base64 characters
  return ret_size + 1;  // +1 for null terminator
}

/**
 * @brief Send image message to GPT API
 * @param imageFilePath Image file path (in SPIFFS)
 * @param question Question about the image
 * @return GPT response text
 *
 * This function implements a memory-efficient image processing workflow:
 * 1. Read image file in chunks and encode to Base64
 * 2. Save Base64 data to temporary file
 * 3. Build JSON request containing image and text
 * 4. Use streaming HTTP send to avoid memory overflow
 */
String ArduinoGPTChat::sendImageMessage(const char* imageFilePath, String question) {
  Serial.println("Opening image file...");
  File imageFile = SPIFFS.open(imageFilePath);
  if (!imageFile) {
    Serial.println("Failed to open image file");
    return "Error: Failed to open image file";
  }

  size_t fileSize = imageFile.size();
  Serial.printf("File size: %d bytes\n", fileSize);
  
  // Solution 2: Use flash space to store Base64 encoded data
  const char* tempBase64File = "/temp_base64.txt";

  // Delete possibly existing temporary file
  if (SPIFFS.exists(tempBase64File)) {
    SPIFFS.remove(tempBase64File);
  }

  // Create temporary file to store Base64 data
  File base64File = SPIFFS.open(tempBase64File, FILE_WRITE);
  if (!base64File) {
    Serial.println("Failed to create temp base64 file");
    imageFile.close();
    return "Error: Failed to create temp file";
  }

  // Write Base64 prefix (MIME type)
  base64File.print("data:image/png;base64,");
  
  Serial.println("Starting base64 encoding and writing to SD...");

  // Use smaller chunk size and dynamic memory allocation to avoid stack overflow
  const size_t chunkSize = 1500; // Reduced to 1.5KB chunks (must be multiple of 3 for Base64 encoding)
  
  uint8_t* buffer = (uint8_t*)malloc(chunkSize);
  if (!buffer) {
    Serial.println("Failed to allocate buffer memory");
    imageFile.close();
    base64File.close();
    SPIFFS.remove(tempBase64File);
    return "Error: Failed to allocate buffer";
  }
  
  char* encodedChunk = (char*)malloc(chunkSize * 4 / 3 + 10); // Base64 expansion + padding
  if (!encodedChunk) {
    Serial.println("Failed to allocate encoded buffer memory");
    free(buffer);
    imageFile.close();
    base64File.close();
    SPIFFS.remove(tempBase64File);
    return "Error: Failed to allocate encoded buffer";
  }
  
  size_t totalProcessed = 0;
  while (totalProcessed < fileSize) {
    size_t currentChunkSize = min(chunkSize, fileSize - totalProcessed);
    size_t bytesRead = imageFile.read(buffer, currentChunkSize);
    
    if (bytesRead != currentChunkSize) {
      Serial.println("Error reading file chunk");
      free(buffer);
      free(encodedChunk);
      imageFile.close();
      base64File.close();
      SPIFFS.remove(tempBase64File);
      return "Error: Failed to read file chunk";
    }
    
    // Encode current chunk
    base64_encode(buffer, bytesRead, encodedChunk);

    // Write encoded data to SPIFFS
    base64File.print(encodedChunk);

    totalProcessed += bytesRead;

    // Display progress
    if (totalProcessed % 15000 == 0 || totalProcessed == fileSize) {
      Serial.printf("Processed: %d/%d bytes (%.1f%%)\n",
                   totalProcessed, fileSize,
                   (float)totalProcessed / fileSize * 100);
    }

    // Add small delay to give system time to handle other tasks
    delay(10);
  }
  
  // Free buffer memory
  free(buffer);
  free(encodedChunk);

  imageFile.close();
  base64File.close();

  Serial.println("Base64 encoding completed and saved to SPIFFS");

  // Check generated base64 file size
  File checkFile = SPIFFS.open(tempBase64File);
  if (!checkFile) {
    Serial.println("Failed to open temp base64 file for reading");
    return "Error: Failed to open temp base64 file";
  }
  
  size_t base64FileSize = checkFile.size();
  Serial.printf("Base64 file size: %d bytes\n", base64FileSize);
  checkFile.close();
  
  // Now build JSON using smaller buffer
  DynamicJsonDocument doc(2048); // Only need small buffer since Base64 data not included
  doc["model"] = "gpt-4.1-nano";
  doc["messages"] = JsonArray();
  JsonObject message = doc["messages"].createNestedObject();
  message["role"] = "user";
  JsonArray content = message.createNestedArray("content");
  
  // Add text part
  JsonObject textPart = content.createNestedObject();
  textPart["type"] = "text";
  textPart["text"] = question;

  // Add image part
  JsonObject imagePart = content.createNestedObject();
  imagePart["type"] = "image_url";
  JsonObject imageUrl = imagePart.createNestedObject("image_url");

  // Set placeholder, replace later
  imageUrl["url"] = "PLACEHOLDER_FOR_BASE64_DATA";
  
  doc["max_tokens"] = 300;

  // Serialize JSON to string
  String jsonTemplate;
  serializeJson(doc, jsonTemplate);

  Serial.println("JSON template created");
  Serial.printf("Template size: %d bytes\n", jsonTemplate.length());

  // Build complete JSON, but read Base64 data in chunks to save memory
  Serial.println("Building JSON with chunked base64 reading...");

  // Calculate final JSON size
  int placeholderPos = jsonTemplate.indexOf("PLACEHOLDER_FOR_BASE64_DATA");
  String jsonPart1 = jsonTemplate.substring(0, placeholderPos);
  String jsonPart2 = jsonTemplate.substring(placeholderPos + strlen("PLACEHOLDER_FOR_BASE64_DATA"));
  
  Serial.printf("JSON part 1 size: %d bytes\n", jsonPart1.length());
  Serial.printf("JSON part 2 size: %d bytes\n", jsonPart2.length());
  Serial.printf("Base64 data size: %d bytes\n", base64FileSize);
  
  // Create temporary file to store complete JSON
  const char* tempJsonFile = "/temp_json.txt";
  if (SPIFFS.exists(tempJsonFile)) {
    SPIFFS.remove(tempJsonFile);
  }

  File jsonFile = SPIFFS.open(tempJsonFile, FILE_WRITE);
  if (!jsonFile) {
    Serial.println("Failed to create temp JSON file");
    SPIFFS.remove(tempBase64File);
    return "Error: Failed to create temp JSON file";
  }

  // Write first part of JSON
  jsonFile.print(jsonPart1);

  // Read Base64 data in chunks and write to JSON file
  Serial.println("Copying base64 data to JSON file...");
  File base64ReadFile = SPIFFS.open(tempBase64File);
  if (!base64ReadFile) {
    Serial.println("Failed to open base64 file for reading");
    jsonFile.close();
    SPIFFS.remove(tempBase64File);
    SPIFFS.remove(tempJsonFile);
    return "Error: Failed to read base64 file";
  }
  
  const size_t copyChunkSize = 2048;
  uint8_t* copyBuffer = (uint8_t*)malloc(copyChunkSize);
  if (!copyBuffer) {
    Serial.println("Failed to allocate copy buffer");
    base64ReadFile.close();
    jsonFile.close();
    SPIFFS.remove(tempBase64File);
    SPIFFS.remove(tempJsonFile);
    return "Error: Failed to allocate copy buffer";
  }
  
  size_t totalCopied = 0;
  while (base64ReadFile.available()) {
    size_t bytesToRead = min(copyChunkSize, (size_t)base64ReadFile.available());
    size_t bytesRead = base64ReadFile.read(copyBuffer, bytesToRead);
    
    jsonFile.write(copyBuffer, bytesRead);
    totalCopied += bytesRead;
    
    if (totalCopied % 20480 == 0) { // Display progress every 20KB
      Serial.printf("Copied: %d/%d bytes\n", totalCopied, base64FileSize);
    }
  }
  
  free(copyBuffer);
  base64ReadFile.close();
  
  // Write second part of JSON
  jsonFile.print(jsonPart2);
  jsonFile.close();

  // Clean up Base64 temporary file
  SPIFFS.remove(tempBase64File);

  // Check JSON file size
  File checkJsonFile = SPIFFS.open(tempJsonFile);
  if (!checkJsonFile) {
    Serial.println("Failed to open JSON file for size check");
  SPIFFS.remove(tempJsonFile);
    return "Error: Failed to open JSON file";
  }
  
  size_t jsonFileSize = checkJsonFile.size();
  Serial.printf("Final JSON file size: %d bytes\n", jsonFileSize);
  checkJsonFile.close();
  
  // Use real streaming HTTP send to avoid loading large JSON into memory
  Serial.println("Starting streaming HTTP POST...");

  WiFiClientSecure client;
  client.setInsecure(); // Skip SSL certificate verification

  if (!client.connect("api.chatanywhere.tech", 443)) {
    Serial.println("Failed to connect to server");
    SPIFFS.remove(tempJsonFile);
    return "Error: Failed to connect to server";
  }

  // Send HTTP request headers
  client.print("POST /v1/chat/completions HTTP/1.1\r\n");
  client.print("Host: api.chatanywhere.tech\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Authorization: Bearer ");
  client.print(_apiKey);
  client.print("\r\n");
  client.print("Content-Length: ");
  client.print(jsonFileSize);
  client.print("\r\n");
  client.print("Connection: close\r\n");
  client.print("\r\n");

  Serial.println("Streaming JSON file...");
  
  // Stream send JSON file
  File sendJsonFile = SPIFFS.open(tempJsonFile);
  if (!sendJsonFile) {
    Serial.println("Failed to open JSON file for streaming");
    client.stop();
    SPIFFS.remove(tempJsonFile);
    return "Error: Failed to open JSON file for streaming";
  }
  
  const size_t streamChunkSize = 1024;
  uint8_t* streamBuffer = (uint8_t*)malloc(streamChunkSize);
  if (!streamBuffer) {
    Serial.println("Failed to allocate stream buffer");
    sendJsonFile.close();
    client.stop();
    SPIFFS.remove(tempJsonFile);
    return "Error: Failed to allocate stream buffer";
  }
  
  size_t totalStreamed = 0;
  while (sendJsonFile.available()) {
    size_t bytesToRead = min(streamChunkSize, (size_t)sendJsonFile.available());
    size_t bytesRead = sendJsonFile.read(streamBuffer, bytesToRead);
    
    client.write(streamBuffer, bytesRead);
    totalStreamed += bytesRead;
    
    if (totalStreamed % 10240 == 0) { // Display progress every 10KB
      Serial.printf("Streamed: %d/%d bytes\n", totalStreamed, jsonFileSize);
    }

    delay(1); // Small delay to ensure stable data transmission
  }
  
  free(streamBuffer);
  sendJsonFile.close();
  SPIFFS.remove(tempJsonFile);
  
  Serial.printf("Total streamed: %d bytes\n", totalStreamed);
  Serial.println("Waiting for response...");

  // Wait for response
  unsigned long timeout = millis() + 30000; // 30 second timeout
  while (!client.available() && millis() < timeout) {
    delay(100);
  }
  
  if (millis() >= timeout) {
    Serial.println("HTTP response timeout");
    client.stop();
    return "Error: HTTP response timeout";
  }
  
  // Read response
  String response = "";
  String line = "";
  bool headersPassed = false;
  int statusCode = 0;

  Serial.println("Reading response headers...");
  
  while (client.available()) {
    char c = client.read();
    if (c == '\n') {
      if (line.length() <= 1) { // Empty line or only \r, indicates end of headers
        if (!headersPassed) {
          headersPassed = true;
          Serial.println("Headers passed, reading body...");
        }
        line = "";
        continue;
      }
      if (headersPassed) {
        response += line + "\n";
      } else {
        // Parse response headers
        if (line.startsWith("HTTP/1.1 ")) {
          statusCode = line.substring(9, 12).toInt();
          Serial.printf("HTTP Response code: %d\n", statusCode);
        }
        Serial.println("Header: " + line);
      }
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
  
  // Handle last line
  if (line.length() > 0) {
    if (headersPassed) {
      response += line;
    } else {
      Serial.println("Last header: " + line);
    }
  }

  client.stop();

  if (statusCode == 200 && response.length() > 0) {
    Serial.println("Raw response:");
    Serial.println(response);

    // Handle chunked transfer encoding - find JSON start position
    String cleanResponse = response;
    int jsonStart = cleanResponse.indexOf('{');
    if (jsonStart > 0) {
      cleanResponse = cleanResponse.substring(jsonStart);
      Serial.println("Cleaned JSON response:");
      Serial.println(cleanResponse);
    }

    return _processResponse(cleanResponse);
  } else {
    Serial.println("Error response:");
    Serial.println(response);
    return "Error: HTTP request failed with code " + String(statusCode);
  }
}

/**
 * @brief Constructor
 * @param apiKey API key
 * @param apiBaseUrl API base URL
 *
 * Initialize ArduinoGPTChat instance, set API configuration and update API URLs
 */
ArduinoGPTChat::ArduinoGPTChat(const char* apiKey, const char* apiBaseUrl) {
  _apiKey = (apiKey != nullptr) ? apiKey : DEFAULT_API_KEY;
  _apiBaseUrl = (apiBaseUrl != nullptr) ? apiBaseUrl : DEFAULT_API_BASE_URL;
  _systemPrompt = "";
  _updateApiUrls();
}

/**
 * @brief Set API configuration
 * @param apiKey API key
 * @param apiBaseUrl API base URL
 *
 * Update API key and base URL, update all API endpoints if new URL provided
 */
void ArduinoGPTChat::setApiConfig(const char* apiKey, const char* apiBaseUrl) {
  if (apiKey != nullptr) {
    _apiKey = apiKey;
  }
  if (apiBaseUrl != nullptr) {
    _apiBaseUrl = apiBaseUrl;
    _updateApiUrls();
  }
}

/**
 * @brief Set system prompt
 * @param systemPrompt System prompt text
 *
 * Set GPT system prompt to guide AI behavior and role
 */
void ArduinoGPTChat::setSystemPrompt(const char* systemPrompt) {
  if (systemPrompt != nullptr) {
    _systemPrompt = systemPrompt;
  }
}

/**
 * @brief Enable or disable conversation memory
 * @param enable true to enable memory, false to disable
 *
 * Control whether to save conversation history for context memory
 * Automatically clears existing conversation history when disabled
 */
void ArduinoGPTChat::enableMemory(bool enable) {
  _memoryEnabled = enable;
  if (!enable) {
    clearMemory();
  }
}

/**
 * @brief Clear conversation memory
 *
 * Clear all saved conversation history
 */
void ArduinoGPTChat::clearMemory() {
  _conversationHistory.clear();
  Serial.println("Conversation memory cleared");
}

/**
 * @brief Update API URLs
 *
 * Build complete URLs for each API endpoint based on base URL
 * Also update global host variable used by Audio library
 */
void ArduinoGPTChat::_updateApiUrls() {
  _apiUrl = _apiBaseUrl + "/v1/chat/completions";
  _ttsApiUrl = _apiBaseUrl + "/v1/audio/speech";
  _sttApiUrl = _apiBaseUrl + "/v1/audio/transcriptions";

  // Update global variable used by Audio library
  if(_apiBaseUrl.startsWith("https://")) {
    g_api_host = _apiBaseUrl.substring(8);  // Remove "https://"
  } else if(_apiBaseUrl.startsWith("http://")) {
    g_api_host = _apiBaseUrl.substring(7);  // Remove "http://"
  } else {
    g_api_host = _apiBaseUrl;
  }
}

/**
 * @brief Send text message to GPT
 * @param message User message
 * @return GPT response text
 *
 * Build HTTP request to send to GPT API, process response and manage conversation history
 */
String ArduinoGPTChat::sendMessage(String message) {
  HTTPClient http;
  http.begin(_apiUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(_apiKey));

  String payload = _buildPayload(message);
  int httpResponseCode = http.POST(payload);

  if (httpResponseCode == 200) {
    String response = http.getString();
    String assistantResponse = _processResponse(response);

    // If memory enabled, save to conversation history
    if (_memoryEnabled && assistantResponse.length() > 0) {
      _conversationHistory.push_back(std::make_pair(message, assistantResponse));

      // Only keep last N conversation pairs to avoid memory overflow
      while (_conversationHistory.size() > _maxHistoryPairs) {
        _conversationHistory.erase(_conversationHistory.begin());
      }

      Serial.printf("Memory: %d/%d conversation pairs stored\n",
                    _conversationHistory.size(), _maxHistoryPairs);
    }

    return assistantResponse;
  }
  return "";
}

/**
 * @brief Build JSON payload for HTTP request
 * @param message Current user message
 * @return JSON formatted request string
 *
 * Build complete GPT request JSON based on system prompt and conversation history
 * Contains system message, conversation history and current user message
 */
String ArduinoGPTChat::_buildPayload(String message) {
  // Calculate required buffer size based on history
  size_t bufferSize = 768;
  if (_memoryEnabled && _conversationHistory.size() > 0) {
    bufferSize = 768 + (_conversationHistory.size() * 512);  // Reserve extra space for history
  }

  DynamicJsonDocument doc(bufferSize);
  doc["model"] = "gpt-4.1-nano";
  JsonArray messages = doc.createNestedArray("messages");

  // If system prompt configured, add system message
  if (_systemPrompt.length() > 0) {
    JsonObject sysMsg = messages.createNestedObject();
    sysMsg["role"] = "system";
    sysMsg["content"] = _systemPrompt;
  }

  // If memory enabled, add conversation history
  if (_memoryEnabled) {
    for (size_t i = 0; i < _conversationHistory.size(); i++) {
      // Add user message from history
      JsonObject historyUserMsg = messages.createNestedObject();
      historyUserMsg["role"] = "user";
      historyUserMsg["content"] = _conversationHistory[i].first;

      // Add assistant reply from history
      JsonObject historyAssistantMsg = messages.createNestedObject();
      historyAssistantMsg["role"] = "assistant";
      historyAssistantMsg["content"] = _conversationHistory[i].second;
    }
  }

  // Add current user message
  JsonObject userMsg = messages.createNestedObject();
  userMsg["role"] = "user";
  userMsg["content"] = message;

  String output;
  serializeJson(doc, output);
  return output;
}

/**
 * @brief Process GPT API response
 * @param response Raw JSON response
 * @return Extracted text reply
 *
 * Parse JSON response and extract assistant reply content
 * Remove newlines to get clean text output
 */
String ArduinoGPTChat::_processResponse(String response) {
  DynamicJsonDocument jsonDoc(1024);
  deserializeJson(jsonDoc, response);
  String outputText = jsonDoc["choices"][0]["message"]["content"];
  outputText.remove(outputText.indexOf('\n'));
  return outputText;
}

/**
 * @brief Text to speech
 * @param text Text to convert
 * @return Whether conversion succeeded
 *
 * Use Audio library's OpenAI speech function to convert text to speech
 * Uses gpt-4o-mini-tts model, alloy voice, mp3 format
 */
bool ArduinoGPTChat::textToSpeech(String text) {
  // Create temporary Audio object
  extern Audio audio;

  // Use Audio library's openai_speech function
  return audio.openai_speech(
    String(_apiKey),     // API key
    "gpt-4o-mini-tts",   // Model
    text,                // Input text
    "alloy",             // Voice
    "mp3",               // Response format
    "1.0"                // Speed
  );
}

/**
 * @brief Build TTS request payload (deprecated)
 * @param text Input text
 * @return JSON formatted request string
 *
 * Note: This method is no longer used, method signature retained for compatibility
 */
String ArduinoGPTChat::_buildTTSPayload(String text) {
  text.replace("\"", "\\\"");
  return "{\"model\": \"gpt-4o-mini-tts\", \"input\": \"" + text + "\", \"voice\": \"alloy\"}";
}

/**
 * @brief Speech to text (from file)
 * @param audioFilePath Audio file path (in SD card)
 * @return Transcribed text
 *
 * Read audio file from SD card, use Whisper API for speech recognition
 * Supports WAV format audio files
 */
String ArduinoGPTChat::speechToText(const char* audioFilePath) {
  String response = "";

  // Check if file exists
  if (!SD.exists(audioFilePath)) {
    Serial.println("Audio file not found: " + String(audioFilePath));
    return response;
  }

  // Open audio file
  File audioFile = SD.open(audioFilePath, FILE_READ);
  if (!audioFile) {
    Serial.println("Failed to open audio file!");
    return response;
  }

  // Get file size
  size_t fileSize = audioFile.size();
  Serial.println("Audio file size: " + String(fileSize) + " bytes");

  // Create temporary buffer to store entire file content
  // Note: This may use large amount of memory, may fail if file is very large
  uint8_t* fileData = (uint8_t*)malloc(fileSize);
  if (!fileData) {
    Serial.println("Failed to allocate memory for file!");
    audioFile.close();
    return response;
  }

  // Read file content into buffer
  size_t bytesRead = audioFile.read(fileData, fileSize);
  audioFile.close();

  if (bytesRead != fileSize) {
    Serial.println("Failed to read entire file!");
    free(fileData);
    return response;
  }
  
  Serial.println("File read into memory successfully.");

  // Use same boundary as Python example
  String boundary = "wL36Yn8afVp8Ag7AmP8qZ0SA4n1v9T";

  // Build multipart/form-data request body parts
  // File part
  String part1 = "--" + boundary + "\r\n";
  part1 += "Content-Disposition: form-data; name=file; filename=audio.wav\r\n";
  part1 += "Content-Type: audio/wav\r\n\r\n";

  // Model part
  String part2 = "\r\n--" + boundary + "\r\n";
  part2 += "Content-Disposition: form-data; name=model;\r\n";
  part2 += "Content-Type: text/plain\r\n\r\n";
  part2 += "whisper-1";

  // Prompt part (matching Python example)
  String part3 = "\r\n--" + boundary + "\r\n";
  part3 += "Content-Disposition: form-data; name=prompt;\r\n";
  part3 += "Content-Type: text/plain\r\n\r\n";
  part3 += "eiusmod nulla";

  // Response format part
  String part4 = "\r\n--" + boundary + "\r\n";
  part4 += "Content-Disposition: form-data; name=response_format;\r\n";
  part4 += "Content-Type: text/plain\r\n\r\n";
  part4 += "json";

  // Temperature part
  String part5 = "\r\n--" + boundary + "\r\n";
  part5 += "Content-Disposition: form-data; name=temperature;\r\n";
  part5 += "Content-Type: text/plain\r\n\r\n";
  part5 += "0";

  // Language part (matching Python example)
  String part6 = "\r\n--" + boundary + "\r\n";
  part6 += "Content-Disposition: form-data; name=language;\r\n";
  part6 += "Content-Type: text/plain\r\n\r\n";
  part6 += "";

  // End boundary
  String part7 = "\r\n--" + boundary + "--\r\n";

  // Calculate total content length
  size_t totalLength = part1.length() + fileSize + part2.length() + part3.length() +
                      part4.length() + part5.length() + part6.length() + part7.length();
  
  // Initialize HTTP client
  HTTPClient http;
  http.begin(_sttApiUrl);

  // Set request headers
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Authorization", "Bearer " + String(_apiKey));
  http.addHeader("Content-Length", String(totalLength));

  // Merge all parts into complete request body
  Serial.println("Preparing request body...");
  uint8_t* requestBody = (uint8_t*)malloc(totalLength);
  if (!requestBody) {
    Serial.println("Failed to allocate memory for request body!");
    free(fileData);
    return response;
  }

  // Copy each part to request body
  size_t pos = 0;

  // Copy part1
  memcpy(requestBody + pos, part1.c_str(), part1.length());
  pos += part1.length();

  // Copy file data
  memcpy(requestBody + pos, fileData, fileSize);
  pos += fileSize;
  free(fileData); // Free file data memory

  // Copy remaining parts
  memcpy(requestBody + pos, part2.c_str(), part2.length());
  pos += part2.length();

  memcpy(requestBody + pos, part3.c_str(), part3.length());
  pos += part3.length();

  memcpy(requestBody + pos, part4.c_str(), part4.length());
  pos += part4.length();

  memcpy(requestBody + pos, part5.c_str(), part5.length());
  pos += part5.length();

  memcpy(requestBody + pos, part6.c_str(), part6.length());
  pos += part6.length();

  memcpy(requestBody + pos, part7.c_str(), part7.length());
  pos += part7.length();

  // Confirm total length matches
  if (pos != totalLength) {
    Serial.println("Warning: actual body length doesn't match calculated length");
    Serial.println("Calculated: " + String(totalLength) + ", Actual: " + String(pos));
  }

  // Send request
  Serial.println("Sending STT request...");
  int httpCode = http.POST(requestBody, totalLength);

  // Free request body memory
  free(requestBody);
  
  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);
  
  if (httpCode == 200) {
    // Get response body
    response = http.getString();
    Serial.println("Got STT response: " + response);

    // Parse JSON response
    DynamicJsonDocument jsonDoc(1024);
    DeserializationError error = deserializeJson(jsonDoc, response);

    if (!error) {
      // Extract transcribed text
      response = jsonDoc["text"].as<String>();
    } else {
      Serial.print("JSON parsing error: ");
      Serial.println(error.c_str());
      response = "";
    }
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(httpCode);
    // Try to get error response content
    String errorResponse = http.getString();
    if (errorResponse.length() > 0) {
      Serial.println("Error response: " + errorResponse);
    }
    response = "";
  }
  
  http.end();
  return response;
}

// Recording control functions implementation
/**
 * @brief Initialize recording configuration
 * @param micClkPin Microphone clock pin
 * @param micWsWordSelectPin Microphone word select pin
 * @param micDataPin Microphone data pin
 * @param sampleRate Sample rate
 * @param mode I2S mode
 * @param bitWidth Data bit width
 * @param slotMode Slot mode
 * @param slotMask Slot mask
 *
 * Set I2S recording parameters, but don't start recording
 */
void ArduinoGPTChat::initializeRecording(int micClkPin, int micWsPin, int micDataPin, int sampleRate,
                                         i2s_mode_t mode, i2s_data_bit_width_t bitWidth,
                                         i2s_slot_mode_t slotMode, i2s_std_slot_mask_t slotMask) {
  _micClkPin = micClkPin;
  _micWsPin = micWsPin;
  _micDataPin = micDataPin;
  _sampleRate = sampleRate;
  _i2sMode = mode;
  _i2sBitWidth = bitWidth;
  _i2sSlotMode = slotMode;
  _i2sSlotMask = slotMask;
  _isRecording = false;
}

/**
 * @brief Start recording
 * @return Whether recording started successfully
 *
 * Initialize I2S and start recording, return false if already recording
 */
bool ArduinoGPTChat::startRecording() {
  if (_isRecording) {
    return false; // Already recording
  }

  Serial.println("Starting recording...");

  // Clear audio buffer
  _audioBuffer.clear();

  // Set microphone I2S pins
  _recordingI2S.setPins(_micClkPin, _micWsPin, -1, _micDataPin);

  // Initialize I2S recording using stored configuration
  if (!_recordingI2S.begin(_i2sMode, _sampleRate, _i2sBitWidth, _i2sSlotMode, _i2sSlotMask)) {
    Serial.println("Failed to initialize I2S!");
    return false;
  }

  _isRecording = true;
  return true;
}

/**
 * @brief Continue recording
 *
 * Read audio samples from I2S and add to buffer
 * Should be called in loop to continue recording
 */
void ArduinoGPTChat::continueRecording() {
  if (!_isRecording) return;

  int16_t samples[_bufferSize];

  // Read audio samples
  size_t bytesRead = _recordingI2S.readBytes((char*)samples, _bufferSize * sizeof(int16_t));

  if (bytesRead > 0) {
    // Add samples to buffer
    size_t samplesRead = bytesRead / sizeof(int16_t);
    for (size_t i = 0; i < samplesRead; i++) {
      _audioBuffer.push_back(samples[i]);
    }
  }
}

/**
 * @brief Stop recording and process audio
 * @return Transcribed text
 *
 * Stop I2S recording, convert audio data to WAV format,
 * then process audio using speech-to-text function
 */
String ArduinoGPTChat::stopRecordingAndProcess() {
  if (!_isRecording) {
    return "";
  }

  // Stop I2S
  _recordingI2S.end();
  _isRecording = false;

  if (_audioBuffer.empty()) {
    Serial.println("No audio data recorded!");
    return "";
  }

  Serial.println("Recording completed, samples: " + String(_audioBuffer.size()));
  Serial.println("Converting speech to text...");

  // Convert audio buffer to WAV format
  uint8_t* wavBuffer = createWAVBuffer(_audioBuffer.data(), _audioBuffer.size());
  size_t wavSize = calculateWAVSize(_audioBuffer.size());

  if (wavBuffer == nullptr) {
    Serial.println("Failed to create WAV buffer!");
    return "";
  }

  // Convert speech to text
  String transcribedText = speechToTextFromBuffer(wavBuffer, wavSize);

  // Free buffer memory
  free(wavBuffer);

  return transcribedText;
}

/**
 * @brief Check if currently recording
 * @return true if recording, false if not
 */
bool ArduinoGPTChat::isRecording() {
  return _isRecording;
}

/**
 * @brief Get number of recorded samples
 * @return Number of audio samples
 */
size_t ArduinoGPTChat::getRecordedSampleCount() {
  return _audioBuffer.size();
}

// WAV file handling functions
/**
 * @brief Create WAV file buffer
 * @param samples Audio sample data
 * @param numSamples Number of samples
 * @return WAV format data buffer
 *
 * Convert 16-bit PCM audio samples to WAV format file
 * Contains standard WAV file header and audio data
 */
uint8_t* ArduinoGPTChat::createWAVBuffer(int16_t* samples, size_t numSamples) {
  size_t wavSize = calculateWAVSize(numSamples);
  uint8_t* wavBuffer = (uint8_t*)malloc(wavSize);

  if (wavBuffer == nullptr) {
    return nullptr;
  }

  // WAV file header
  uint8_t header[44] = {
    'R','I','F','F',  // ChunkID
    0,0,0,0,          // ChunkSize (to be filled)
    'W','A','V','E',  // Format
    'f','m','t',' ',  // Subchunk1ID
    16,0,0,0,         // Subchunk1Size
    1,0,              // AudioFormat (PCM)
    1,0,              // NumChannels (mono)
    0,0,0,0,          // SampleRate (to be filled)
    0,0,0,0,          // ByteRate (to be filled)
    2,0,              // BlockAlign
    16,0,             // BitsPerSample
    'd','a','t','a',  // Subchunk2ID
    0,0,0,0           // Subchunk2Size (to be filled)
  };

  // Fill in values
  uint32_t chunkSize = wavSize - 8;
  uint32_t sampleRate = _sampleRate;
  uint32_t byteRate = sampleRate * 2; // 16-bit mono
  uint32_t dataSize = numSamples * 2;

  memcpy(&header[4], &chunkSize, 4);
  memcpy(&header[24], &sampleRate, 4);
  memcpy(&header[28], &byteRate, 4);
  memcpy(&header[40], &dataSize, 4);

  // Copy file header
  memcpy(wavBuffer, header, 44);

  // Copy audio data
  memcpy(wavBuffer + 44, samples, numSamples * 2);

  return wavBuffer;
}

/**
 * @brief Calculate WAV file size
 * @param numSamples Number of audio samples
 * @return Total WAV file size (bytes)
 *
 * Calculate total WAV file size including header and audio data
 * Header 44 bytes + 16-bit sample data (2 bytes per sample)
 */
size_t ArduinoGPTChat::calculateWAVSize(size_t numSamples) {
  return 44 + (numSamples * 2); // Header + 16-bit samples
}

/**
 * @brief Build multipart form (deprecated)
 * @param audioFilePath Audio file path
 * @param boundary Boundary delimiter
 * @return Empty string
 *
 * Note: This method is no longer used, method signature retained for compatibility
 */
String ArduinoGPTChat::_buildMultipartForm(const char* audioFilePath, String boundary) {
  // This method is no longer used, method signature retained for compatibility
  return "";
}

/**
 * @brief Speech to text from audio buffer
 * @param audioBuffer Audio data buffer
 * @param bufferSize Buffer size
 * @return Transcribed text
 *
 * Perform speech recognition directly from audio buffer in memory,
 * no need to save to file first, suitable for real-time processing
 */
String ArduinoGPTChat::speechToTextFromBuffer(uint8_t* audioBuffer, size_t bufferSize) {
  String response = "";
  
  if (audioBuffer == NULL || bufferSize == 0) {
    Serial.println("Invalid audio buffer or size!");
    return response;
  }
  
  Serial.println("Audio buffer size: " + String(bufferSize) + " bytes");
  
  // Use same boundary as Python example
  String boundary = "wL36Yn8afVp8Ag7AmP8qZ0SA4n1v9T";

  // Build multipart/form-data request body parts
  // File part
  String part1 = "--" + boundary + "\r\n";
  part1 += "Content-Disposition: form-data; name=file; filename=audio.wav\r\n";
  part1 += "Content-Type: audio/wav\r\n\r\n";

  // Model part
  String part2 = "\r\n--" + boundary + "\r\n";
  part2 += "Content-Disposition: form-data; name=model;\r\n";
  part2 += "Content-Type: text/plain\r\n\r\n";
  part2 += "whisper-1";

  // Prompt part (matching Python example)
  String part3 = "\r\n--" + boundary + "\r\n";
  part3 += "Content-Disposition: form-data; name=prompt;\r\n";
  part3 += "Content-Type: text/plain\r\n\r\n";
  part3 += "eiusmod nulla";

  // Response format part
  String part4 = "\r\n--" + boundary + "\r\n";
  part4 += "Content-Disposition: form-data; name=response_format;\r\n";
  part4 += "Content-Type: text/plain\r\n\r\n";
  part4 += "json";

  // Temperature part
  String part5 = "\r\n--" + boundary + "\r\n";
  part5 += "Content-Disposition: form-data; name=temperature;\r\n";
  part5 += "Content-Type: text/plain\r\n\r\n";
  part5 += "0";

  // Language part (matching Python example)
  String part6 = "\r\n--" + boundary + "\r\n";
  part6 += "Content-Disposition: form-data; name=language;\r\n";
  part6 += "Content-Type: text/plain\r\n\r\n";
  part6 += "";

  // End boundary
  String part7 = "\r\n--" + boundary + "--\r\n";

  // Calculate total content length
  size_t totalLength = part1.length() + bufferSize + part2.length() + part3.length() +
                      part4.length() + part5.length() + part6.length() + part7.length();
  
  // Initialize HTTP client
  HTTPClient http;
  http.begin(_sttApiUrl);

  // Set request headers
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Authorization", "Bearer " + String(_apiKey));
  http.addHeader("Content-Length", String(totalLength));

  // Merge all parts into complete request body
  Serial.println("Preparing request body...");
  uint8_t* requestBody = (uint8_t*)malloc(totalLength);
  if (!requestBody) {
    Serial.println("Failed to allocate memory for request body!");
    return response;
  }

  // Copy each part to request body
  size_t pos = 0;

  // Copy part1
  memcpy(requestBody + pos, part1.c_str(), part1.length());
  pos += part1.length();

  // Copy audio data
  memcpy(requestBody + pos, audioBuffer, bufferSize);
  pos += bufferSize;

  // Copy remaining parts
  memcpy(requestBody + pos, part2.c_str(), part2.length());
  pos += part2.length();

  memcpy(requestBody + pos, part3.c_str(), part3.length());
  pos += part3.length();

  memcpy(requestBody + pos, part4.c_str(), part4.length());
  pos += part4.length();

  memcpy(requestBody + pos, part5.c_str(), part5.length());
  pos += part5.length();

  memcpy(requestBody + pos, part6.c_str(), part6.length());
  pos += part6.length();

  memcpy(requestBody + pos, part7.c_str(), part7.length());
  pos += part7.length();

  // Confirm total length matches
  if (pos != totalLength) {
    Serial.println("Warning: actual body length doesn't match calculated length");
    Serial.println("Calculated: " + String(totalLength) + ", Actual: " + String(pos));
  }

  // Send request
  Serial.println("Sending STT request...");
  int httpCode = http.POST(requestBody, totalLength);

  // Free request body memory
  free(requestBody);
  
  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);
  
  if (httpCode == 200) {
    // Get response body
    response = http.getString();
    Serial.println("Got STT response: " + response);

    // Parse JSON response
    DynamicJsonDocument jsonDoc(1024);
    DeserializationError error = deserializeJson(jsonDoc, response);

    if (!error) {
      // Extract transcribed text
      response = jsonDoc["text"].as<String>();
    } else {
      Serial.print("JSON parsing error: ");
      Serial.println(error.c_str());
      response = "";
    }
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(httpCode);
    // Try to get error response content
    String errorResponse = http.getString();
    if (errorResponse.length() > 0) {
      Serial.println("Error response: " + errorResponse);
    }
    response = "";
  }
  
  http.end();
  return response;
}
