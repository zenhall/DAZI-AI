# M5CoreS3 使用注意事项

本文档说明 M5CoreS3 与普通 ESP32+MAX98357A 开发板在使用 ArduinoTTSChat 库时的差异。

## 1. TTS 对象必须延迟创建

M5CoreS3 的 PSRAM 需要在 `CoreS3.begin()` 之后才能正常使用。ArduinoTTSChat 构造函数会分配 512KB PSRAM，因此：

**错误写法（全局对象）：**
```cpp
ArduinoTTSChat ttsChat(apiKey);  // 构造函数在 setup() 之前执行，PSRAM 未就绪

void setup() {
  CoreS3.begin(cfg);
  // ...
}
```

**正确写法（指针）：**
```cpp
ArduinoTTSChat* ttsChat = nullptr;

void setup() {
  CoreS3.begin(cfg);  // 先初始化 M5CoreS3
  // ...
  ttsChat = new ArduinoTTSChat(apiKey);  // 再创建 TTS 对象
}
```

## 2. 使用回调函数播放音频

M5CoreS3 使用 M5Unified 库管理喇叭，需要通过回调函数播放音频：

```cpp
// 双缓冲（playRaw 只保存指针，需要持久化数据）
static int16_t* audioBufferA = nullptr;
static int16_t* audioBufferB = nullptr;
static const size_t AUDIO_CHUNK_SIZE = 2048;
static bool useBufferA = true;

bool playAudioCallback(const int16_t* data, size_t samples, uint32_t sampleRate) {
  // 等待喇叭队列有空位
  while (CoreS3.Speaker.isPlaying(0) >= 2) {
    delay(1);
  }

  // 选择缓冲区并复制数据
  int16_t* buffer = useBufferA ? audioBufferA : audioBufferB;
  useBufferA = !useBufferA;
  memcpy(buffer, data, min(samples, AUDIO_CHUNK_SIZE) * sizeof(int16_t));

  return CoreS3.Speaker.playRaw(buffer, samples, sampleRate, false, 1, 0, false);
}

void setup() {
  // ...
  ttsChat->initM5CoreS3Speaker();
  ttsChat->setAudioPlayCallback(playAudioCallback);
}
```

## 3. 初始化顺序

| 步骤 | ESP32+MAX98357A | M5CoreS3 |
|------|-----------------|----------|
| 1 | WiFi 连接 | `CoreS3.begin()` |
| 2 | 创建 TTS 对象 | `CoreS3.Speaker.begin()` |
| 3 | `initMAX98357Speaker(bclk, lrclk, dout)` | WiFi 连接 |
| 4 | - | 创建 TTS 对象（`new`） |
| 5 | - | `initM5CoreS3Speaker()` |
| 6 | - | `setAudioPlayCallback()` |

## 4. 主要 API 差异

| 功能 | ESP32+MAX98357A | M5CoreS3 |
|------|-----------------|----------|
| Speaker 初始化 | `initMAX98357Speaker(bclk, lrclk, dout)` | `initM5CoreS3Speaker()` |
| 音频输出 | 内部 I2S 直接输出 | 通过回调函数 |
| 音量控制 | 硬件增益 | `CoreS3.Speaker.setVolume(0-255)` |
| 对象访问 | `ttsChat.method()` | `ttsChat->method()` |
