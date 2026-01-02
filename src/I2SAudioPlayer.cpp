/**
 * @file I2SAudioPlayer.cpp
 * @brief I2S Audio Player Implementation
 * @author Kilo Code
 * @date 2025-12-11
 */

#include "I2SAudioPlayer.h"

I2SAudioPlayer::I2SAudioPlayer()
  : _tx_handle(NULL)
  , _isPlaying(false)
  , _initialized(false)
  , _sampleRate(24000)
{
}

I2SAudioPlayer::~I2SAudioPlayer() {
  deinit();
}

bool I2SAudioPlayer::init(int bclk, int lrc, int dout, int sample_rate) {
  if (_initialized) {
    Serial.println("[I2S] Already initialized");
    return true;
  }
  
  _sampleRate = sample_rate;
  
  // Create I2S channel configuration
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  chan_cfg.dma_desc_num = 8;
  chan_cfg.dma_frame_num = 1024;
  
  // Create new I2S TX channel
  esp_err_t err = i2s_new_channel(&chan_cfg, &_tx_handle, NULL);
  if (err != ESP_OK) {
    Serial.printf("[I2S] Create channel failed: %d\n", err);
    return false;
  }
  
  // Standard I2S configuration
  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)_sampleRate),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)bclk,
      .ws = (gpio_num_t)lrc,
      .dout = (gpio_num_t)dout,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };
  
  // Initialize channel
  err = i2s_channel_init_std_mode(_tx_handle, &std_cfg);
  if (err != ESP_OK) {
    Serial.printf("[I2S] Init std mode failed: %d\n", err);
    i2s_del_channel(_tx_handle);
    return false;
  }
  
  // Enable channel
  err = i2s_channel_enable(_tx_handle);
  if (err != ESP_OK) {
    Serial.printf("[I2S] Enable channel failed: %d\n", err);
    i2s_del_channel(_tx_handle);
    return false;
  }
  
  _initialized = true;
  Serial.printf("[I2S] Initialized successfully (BCLK=%d, LRC=%d, DOUT=%d, SR=%d)\n",
                bclk, lrc, dout, _sampleRate);
  
  return true;
}

size_t I2SAudioPlayer::play(const uint8_t* data, size_t len) {
  if (!_initialized || _tx_handle == NULL) {
    Serial.println("[I2S] Not initialized!");
    return 0;
  }
  
  if (data == nullptr || len == 0) {
    return 0;
  }
  
  size_t bytes_written = 0;
  // Use shorter timeout (100ms) to avoid blocking WebSocket heartbeat
  esp_err_t err = i2s_channel_write(_tx_handle, data, len, &bytes_written, pdMS_TO_TICKS(100));
  
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
    Serial.printf("[I2S] Write failed: %d\n", err);
    return 0;
  }
  
  // Don't set _isPlaying flag, because I2S write is synchronous, completed when write finishes
  // _isPlaying flag should be managed by upper layer
  
  return bytes_written;
}

void I2SAudioPlayer::stop() {
  if (!_initialized || _tx_handle == NULL) {
    return;
  }
  
  // Pre-write zeros to clear buffer
  uint8_t zero_buf[128] = {0};
  size_t bytes_written;
  i2s_channel_write(_tx_handle, zero_buf, sizeof(zero_buf), &bytes_written, 100);
  
  _isPlaying = false;
  
  Serial.println("[I2S] Stopped");
}

void I2SAudioPlayer::deinit() {
  if (!_initialized || _tx_handle == NULL) {
    return;
  }
  
  stop();
  
  // Disable and delete channel
  i2s_channel_disable(_tx_handle);
  i2s_del_channel(_tx_handle);
  
  _tx_handle = NULL;
  _initialized = false;
  
  Serial.println("[I2S] Deinitialized");
}