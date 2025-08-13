
#pragma once
#include <pico/audio_i2s.h>

#define SAMPLES_PER_BUFFER 256

namespace vfo_audio {
bool start_audio();
void update_audio_buffer();
}
 
typedef int16_t (*buffer_callback)(void);

struct audio_buffer_pool *init_audio(uint32_t sample_rate, uint8_t pin_data, uint8_t pin_bclk, uint8_t pio_sm, uint8_t dma_ch);
void update_buffer(struct audio_buffer_pool *ap, buffer_callback cb);