/**
 * @file I2SAudioPlayer.h
 * @brief I2S Audio Player - for playing PCM audio data
 * @author Kilo Code
 * @date 2025-12-11
 */

#ifndef I2SAudioPlayer_h
#define I2SAudioPlayer_h

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>

/**
 * @class I2SAudioPlayer
 * @brief I2S Audio Player class
 * 
 * Supports playing PCM format audio data (16-bit, 24kHz sample rate, mono)
 * Uses I2S_NUM_1 port to avoid conflict with microphone (I2S_NUM_0)
 */
class I2SAudioPlayer {
public:
  /**
   * @brief Constructor
   */
  I2SAudioPlayer();
  
  /**
   * @brief Destructor
   */
  ~I2SAudioPlayer();
  
  /**
   * @brief Initialize I2S player
   * @param bclk Bit clock pin
   * @param lrc Left/right channel clock pin (WS)
   * @param dout Data output pin
   * @param sample_rate Sample rate (default 24000Hz)
   * @return true if initialization successful, false if failed
   */
  bool init(int bclk, int lrc, int dout, int sample_rate = 24000);
  
  /**
   * @brief Play PCM audio data
   * @param data PCM data pointer
   * @param len Data length (bytes)
   * @return Actual bytes written
   */
  size_t play(const uint8_t* data, size_t len);
  
  /**
   * @brief Stop playback and clear buffer
   */
  void stop();
  
  /**
   * @brief Check if currently playing
   * @return true if playing, false if not playing
   */
  bool isPlaying() const { return _isPlaying; }
  
  /**
   * @brief Unload I2S driver
   */
  void deinit();

private:
  i2s_chan_handle_t _tx_handle;  ///< I2S transmit channel handle
  bool _isPlaying;               ///< Playback status flag
  bool _initialized;             ///< Initialization status flag
  int _sampleRate;               ///< Sample rate
};

#endif