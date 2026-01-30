<div align="center">

<img src="img/logo.png" alt="DAZI-AI Logo" width="200"/>

# 🤖 DAZI-AI

[![Arduino](https://img.shields.io/badge/Arduino-ESP32-blue.svg)](https://github.com/arduino/arduino-esp32)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32-red.svg)](https://www.espressif.com/)

**Serverless AI Voice Assistant | ESP32 Platform | Pure Arduino Development**

English | [简体中文](./README_CN.md)

</div>

## 📷 Hardware & Tutorials

<div align="center">

| Breadboard Version | M5CoreS3 Version |
|:------------------:|:----------------:|
| <img src="img/dazi-breadboard.jpg" alt="Breadboard Version" width="300"/> | <img src="img/dazi-m5.png" alt="M5CoreS3 Version" width="300"/> |
| [📺 YouTube Tutorial](https://www.youtube.com/watch?v=bXd6o99LcDA) | [📺 YouTube Tutorial](https://www.youtube.com/watch?v=KxlHjt9SkPY) |

</div>

## ✨ Table of Contents

- [Project Introduction](#-project-introduction)
- [Key Features](#-key-features)
- [System Architecture](#-system-architecture)
- [Code Description](#-code-description)
- [Hardware Requirements](#-hardware-requirements)
- [Quick Start](#-quick-start)
- [Example Projects](#-example-projects)
- [Community](#-community)

## 📝 Project Introduction

DAZI-AI is a serverless AI voice assistant developed entirely on the ESP32 platform using the Arduino environment. It allows you to run AI voice interactions directly on ESP32 devices without the need for additional server support. The system provides complete voice interaction capabilities including speech recognition, AI processing, and text-to-speech output.

## 🚀 Key Features

✅ **Serverless Design**:
- More flexible secondary development
- Higher degree of freedom (customize prompts or models)
- Simpler deployment (no additional server required)

✅ **Complete Voice Interaction**:
- Voice input via INMP441 microphone
- Real-time speech recognition using ByteDance ASR API
- AI processing through OpenAI API
- Voice output via MAX98357A I2S audio amplifier

✅ **Continuous Conversation Mode**:
- Automatic speech recognition with VAD (Voice Activity Detection)
- Seamless ASR → LLM → TTS conversation loop
- Configurable conversation memory to maintain context
- One-button control to start/stop continuous mode

✅ **One-Click Voice Cloning & Custom Voice Design**:
- Web-based one-click voice cloning at [steb2.com](https://steb2.com)
- Create and customize unique AI voice personas
- Easy configuration through web interface

<div align="center">
<img src="img/onebuttonclone.png" alt="One-Click Voice Cloning" width="600"/>

*Voice cloning configuration at [steb2.com](https://steb2.com)*
</div>


## 🔧 System Architecture

The system uses a modular design with the following key components:
- **Voice Input**: INMP441 microphone with I2S interface
- **Speech Recognition**: ByteDance ASR API for real-time transcription
- **AI Processing**: OpenAI ChatGPT API for conversation with memory support
- **Voice Output**: MAX98357A I2S audio amplifier for TTS playback
- **Connectivity**: WiFi for API communication

### Two Conversation Modes
1. **Push-to-Talk Mode** (examples/chat): Hold button to record, release to process
2. **Continuous Conversation Mode** (examples/chat_asr): Automatic ASR with VAD, seamless conversation loop

## 💻 Code Description

### DAZI-AI Library
A unified Arduino library that integrates all necessary components for AI voice assistant development.

| Feature | Description |
|---------|-------------|
| ChatGPT Communication | Communicates with OpenAI API, handles requests and responses |
| Conversation Memory | Maintains conversation history for context-aware responses |
| TTS | Text-to-Speech functionality, converts AI replies to voice |
| STT | Speech-to-Text functionality, converts user input to text |
| Real-time ASR | ByteDance ASR integration with WebSocket protocol for streaming recognition |
| VAD | Voice Activity Detection for automatic speech detection and silence handling |
| Audio Processing | Processes and converts audio data formats (modified ESP32-audioI2S) |
| Audio Playback | I2S audio output with support for multiple codecs (MP3, AAC, FLAC, Opus, Vorbis) |

### Code Structure
```
DAZI-AI/
├── library.properties            # Arduino library configuration
├── keywords.txt                  # Syntax highlighting keywords
├── README.md                     # Documentation
├── src/                          # All source code
│   ├── ArduinoGPTChat.cpp        # ChatGPT & TTS implementation
│   ├── ArduinoGPTChat.h          # ChatGPT & TTS header
│   ├── ArduinoASRChat.cpp        # Real-time ASR implementation
│   ├── ArduinoASRChat.h          # Real-time ASR header
│   ├── Audio.cpp                 # Modified ESP32-audioI2S library
│   ├── Audio.h                   # Audio library header
│   ├── aac_decoder/              # AAC audio decoder
│   ├── flac_decoder/             # FLAC audio decoder
│   ├── mp3_decoder/              # MP3 audio decoder
│   ├── opus_decoder/             # Opus audio decoder
│   └── vorbis_decoder/           # Vorbis audio decoder
└── examples/                     # Example projects
    ├── chat/                     # Push-to-talk voice chat example
    │   └── chat.ino              # Push-to-talk mode with INMP441
    └── chat_asr/                 # Continuous conversation example
        └── chat_asr.ino          # ASR-based continuous mode with memory
```

## 🔌 Hardware Requirements

### Recommended Hardware
- **Controller**: ESP32 development board (ESP32-S3 recommended)
- **Audio Amplifier**: MAX98357A or similar I2S amplifier
- **Microphone**: INMP441 I2S MEMS microphone
- **Speaker**: 4Ω 3W speaker or headphones

### INMP441 Pin Connections

| INMP441 Pin | ESP32 Pin | Description |
|-------------|-----------|-------------|
| VDD | 3.3V | Power (DO NOT use 5V!) |
| GND | GND | Ground |
| L/R | GND | Left channel select |
| WS | GPIO 4 | Left/Right clock |
| SCK | GPIO 5 | Serial clock |
| SD | GPIO 6 | Serial data |

### MAX98357A  I2S Audio Output Pin Connections

| Function | ESP32 Pin | Description |
|----------|-----------|-------------|
| I2S_DOUT | GPIO 47 | Audio data output |
| I2S_BCLK | GPIO 48 | Bit clock |
| I2S_LRC | GPIO 45 | Left/Right clock |

## 🚀 Quick Start

1. **Environment Setup**
   - Install [Arduino IDE](https://www.arduino.cc/en/software) (version 2.0+ recommended)
   - Install ESP32 board support in Arduino IDE:
     - Go to `File` → `Preferences`
     - Add ESP32 board manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
     - Go to `Tools` → `Board` → `Boards Manager`
     - Search for "ESP32" and install "esp32 by Espressif Systems"

2. **Library Installation via ZIP**

   **Method 1: Direct ZIP Installation (Recommended)**
   - Download or create a ZIP file of the entire `DAZI-AI` folder
   - Ensure the ZIP file structure has `library.properties` at the root level
   - Open Arduino IDE
   - Go to `Sketch` → `Include Library` → `Add .ZIP Library...`
   - Select the `DAZI-AI.zip` file
   - Wait for installation to complete

   **Method 2: Manual Installation**
   - Copy the entire `DAZI-AI` folder to your Arduino libraries directory:
     - Windows: `Documents\Arduino\libraries\`
     - macOS: `~/Documents/Arduino/libraries/`
     - Linux: `~/Arduino/libraries/`
   - Restart Arduino IDE

3. **Install Required Dependencies**
   - Open Arduino IDE Library Manager (`Tools` → `Manage Libraries...`)
   - Search and install the following libraries:
     - **ArduinoWebsocket** (v0.5.4)
     - **ArduinoJson** (v7.4.1)
     - **Seeed_Arduino_mbedtls** (v3.0.2)

4. **API Key Configuration**

   **For Push-to-Talk Mode** (`examples/chat/chat.ino`):
   - Replace `"your-api-key"` with your actual OpenAI API key
   - Replace `"your-wifi-ssid"` and `"your-wifi-password"` with your WiFi credentials
   - Optionally modify the system prompt to customize AI behavior

   **For Continuous Conversation Mode** (`examples/chat_asr/chat_asr.ino`):
   - Replace `"your-bytedance-asr-api-key"` with your ByteDance ASR API key (line 37)
   - Replace `"your-openai-api-key"` with your OpenAI API key (line 41)
   - Replace WiFi credentials (lines 33-34)
   - Set `ENABLE_CONVERSATION_MEMORY` to 1 to enable memory or 0 to disable (line 7)
   - Optionally modify the system prompt to customize AI personality (lines 81-104)

5. **Hardware Wiring**
   - Connect INMP441 microphone according to pin table above
   - Connect MAX98357A I2S audio amplifier for speaker output

6. **Open Example Projects**
   - After installing the library, examples will be available in Arduino IDE
   - Go to `File` → `Examples` → `DAZI-AI`
   - Choose either:
     - **chat**: Push-to-talk mode example
     - **chat_asr**: Continuous conversation mode example

7. **Compile and Upload**
   - Select the appropriate ESP32 development board
     - This project has been tested on ESP32S3 Dev Module and XIAO ESP32S3
     - Requirements: Flash Size >8M and PSRAM >4Mb
   - In Arduino IDE, configure board settings:
     - Partition Scheme: Select "8M with spiffs"
     - PSRAM: Select "OPI PSRAM"
   - Compile and upload the code to your device

8. **Testing**
   - Open the serial monitor (115200 baud)
   - Wait for WiFi connection
   - Hold the BOOT button on your ESP32 to start recording
   - Speak your question or command while holding the button
   - Release the button to send the recording to ChatGPT
   - Listen to the AI response through your connected speaker

## 📚 Example Projects

### Main Programs (Recommended)

| Version | Example | Description |
|---------|---------|-------------|
| **Breadboard** | `examples/chat_configurable` | Breadboard version with website one-click API configuration |
| **M5CoreS3** | `examples/chat_configurable_m5cores3` | M5CoreS3 version with website one-click API configuration |

These main programs support easy configuration through a web interface - no need to modify code for API keys!

### Legacy Version

| Example | Description |
|---------|-------------|
| `examples/chat_asr` | Original version requiring manual API key configuration in code |

## 💬 Community

Join our Discord community to share development experiences, ask questions, and collaborate with other developers:

[![Discord](https://img.shields.io/badge/Discord-Join%20Community-7289da?style=for-the-badge&logo=discord&logoColor=white)](https://discord.com/invite/GefSMJzwQ3)

**Discord Server**: https://discord.com/invite/GefSMJzwQ3

---

<div align="center">
  <b>Open source collaboration for shared progress!</b><br>
  If you find this project helpful, please give it a ⭐️
</div>
