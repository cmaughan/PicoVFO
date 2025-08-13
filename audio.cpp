#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "pico/stdlib.h"

#define WSEL 14
#define DATA 15
#define BCLK 13

audio_buffer_pool* ap = nullptr;

namespace vfo_audio
{

#define SINE_WAVE_TABLE_LEN 2048
uint32_t step = 0x200000;
uint32_t pos = 0;
uint32_t pos_max = 0x10000 * SINE_WAVE_TABLE_LEN;
uint vol = 128;

#define SAMPLES_PER_BUFFER 256

static int16_t sine_wave_table[SINE_WAVE_TABLE_LEN];
bool start_audio()
{
    // gpio_set_function(BCLK, GPIO_FUNC_SIO);
    // gpio_set_function(DATA, GPIO_FUNC_SIO);

    // gpio_set_dir(BCLK, GPIO_OUT);
    // gpio_set_dir(DATA, GPIO_OUT);

    // gpio_pull_up(WSEL);
    for (int i = 0; i < SINE_WAVE_TABLE_LEN; i++)
    {
        sine_wave_table[i] = 32767 * cosf(i * 2 * (float)(M_PI / SINE_WAVE_TABLE_LEN));
    }
    ap = init_audio(44100, DATA, BCLK, 0, 0);
    return ap != nullptr;
}

int16_t get_audio_frame()
{
    auto v = (vol * sine_wave_table[pos >> 16u]) >> 8u;
    v += 0x7FFF;
    pos += step;
    if (pos >= pos_max)
    {
        pos -= pos_max;
    }
    return v;
}

void update_audio_buffer()
{
    update_buffer(ap, get_audio_frame);
}
} // namespace vfo_audio

typedef int16_t (*buffer_callback)(void);

struct audio_buffer_pool* init_audio(uint32_t sample_rate, uint8_t pin_data, uint8_t pin_bclk, uint8_t pio_sm, uint8_t dma_ch)
{
    static audio_format_t audio_format = {
        .sample_freq = sample_rate,
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .channel_count = 1,
    };

    static struct audio_buffer_format producer_format = {
        .format = &audio_format,
        .sample_stride = 2
    };

    struct audio_buffer_pool* producer_pool = audio_new_producer_pool(
        &producer_format,
        3,
        SAMPLES_PER_BUFFER);

    const struct audio_format* output_format;

    struct audio_i2s_config config = {
        .data_pin = pin_data,
        .clock_pin_base = pin_bclk,
        .dma_channel = dma_ch,
        .pio_sm = pio_sm,
    };

    output_format = audio_i2s_setup(&audio_format, &config);
    if (!output_format)
    {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    bool status = audio_i2s_connect(producer_pool);
    if (!status)
    {
        panic("PicoAudio: Unable to connect to audio device.\n");
    }

    audio_i2s_set_enabled(true);

    return producer_pool;
}

void update_buffer(struct audio_buffer_pool* ap, buffer_callback cb)
{
    struct audio_buffer* buffer = take_audio_buffer(ap, false);
    if (!buffer)
    {
        return;
    }
    int16_t* samples = (int16_t*)buffer->buffer->bytes;
    for (uint i = 0; i < buffer->max_sample_count; i++)
    {
        samples[i] = cb();
    }
    buffer->sample_count = buffer->max_sample_count;
    give_audio_buffer(ap, buffer);
}