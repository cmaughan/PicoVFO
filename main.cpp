#include "pico/bootrom.h"
#include "pico/stdlib.h"

// Display Library
#include "pico-ssd1306/shapeRenderer/ShapeRenderer.h"
#include "pico-ssd1306/ssd1306.h"
#include "pico-ssd1306/textRenderer/TextRenderer.h"

#include "hardware/i2c.h"

// 5351 Frequency Synthesizer library
extern "C" {
#include "si5351/si5351.h"
}

#include <array>
#include <atomic>
#include <format>

#include "audio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// Use the namespace for convenience
using namespace pico_ssd1306;
using myclock = std::chrono::system_clock;
using duration = std::chrono::duration<double>;

// Utility function to blind the light for debugging
void blink(uint32_t count)
{
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(count);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(count);
}

// Rotary encoder connections
#define ENCODER_SWITCH 2
#define ENCODER_CLK 3 // Pin for A (CLK)
#define ENCODER_DT 4 // Pin for B (DT)

#define DISPLAY_CLOCK 1
#define DISPLAY_DATA 0
#define DISPLAY_ADDRESS 0x3C // The display's address on the bus

std::atomic<int> encoder_count = 0; // Counter for the encoder position
std::atomic<bool> button_pressed = false;
std::atomic<bool> button_state = false;

uint32_t now_ms = 0;
// ----- Rolling velocity (detents/sec) with time-based EMA -----
struct RollingVelocity
{
    // alpha_per_sec: effective smoothing rate in 1/seconds. Higher = snappier.
    explicit RollingVelocity(double alpha_per_sec = 6.0)
        : alpha_per_sec(alpha_per_sec)
    {
    }

    // Call per event with the signed detents since last event and dt (seconds)
    double update(int detents, double dt)
    {
        if (dt <= 0.0)
            return v_ema;
        double v_instant = std::abs(detents) / dt; // detents/sec (magnitude)
        double alpha = 1.0 - std::exp(-alpha_per_sec * dt);
        v_ema = (1.0 - alpha) * v_ema + alpha * v_instant;
        return v_ema;
    }

    double value() const
    {
        return v_ema;
    }

private:
    double alpha_per_sec;
    double v_ema = 0.0;
};

// ----- Adaptive step tuner (ballistic tuning) -----
struct TunerIDI {
    // Bigger ladder so fast spins actually traverse the band.
    static constexpr int steps[10] = { 1, 10, 50, 100, 500, 1000, 2000, 5000, 10000, 20000 };

    // Aggressive thresholds. Tune to taste.
    static constexpr uint16_t up_ms [10] = { 9999, 260, 200, 160, 120,  95,  80,  65,  55,   0 }; // last unused
    static constexpr uint16_t down_ms[10] = { 9999, 320, 250, 200, 160, 130, 110,  90,  75,   0 };

    int current = 0;                         // index into steps[]
    double freqHz = 7'100'000.0;
    static constexpr double fMin = 7'000'000.0;
    static constexpr double fMax = 7'200'000.0;

    // Optional: small "turbo" window after sustained fast spin
    uint32_t turbo_until_ms = 0;

    // Simple speed→multiplier from IDI (ms). Fast = more per-detent oomph.
    static int multiplier_from_idi(uint32_t idi_ms) {
        // Smooth-ish mapping: ~300/idi, clamped 1..8
        int m = (int)(300.0 / (double)std::max<uint32_t>(idi_ms, 50));
        if (m < 1) m = 1;
        if (m > 8) m = 8;
        return m;
    }

    void update(int detents, uint32_t now_ms) {
        static uint32_t last_move_ms = now_ms;
        static uint32_t last_detent_ms = now_ms;

        // Idle tick?
        if (detents == 0) {
            // Precision dwell: brief pause -> 1 Hz
            if (now_ms - last_move_ms > 150) current = 0;
            return;
        }

        // We have ±1 (or rarely ±2)
        uint32_t idi = now_ms - last_detent_ms;   // inter-detent interval
        last_detent_ms = now_ms;
        last_move_ms = now_ms;

        // Velocity→step index with hysteresis
        while (current < 9 && idi <= up_ms[current + 1]) ++current;
        while (current > 0 && idi >= down_ms[current]) --current;

        // Momentum/turbo: after 3 consecutive "fast" detents (<70 ms) bump for 250 ms
        static int fast_streak = 0;
        if (idi < 70) {
            if (++fast_streak >= 3) {
                turbo_until_ms = now_ms + 250;
                fast_streak = 0;
            }
        } else {
            fast_streak = 0;
        }

        int step = steps[current];

        // Multiplier: more movement per detent when spinning fast
        int mult = multiplier_from_idi(idi);

        // Turbo: temporarily bump coarse step one notch (keeps fine snap-back after pause)
        if (now_ms < turbo_until_ms && current < 9) {
            step = steps[current + 1];
        }

        // Apply. If detents has magnitude >1, scale by that too.
        int deltaHz = step * mult * (detents > 0 ? +1 : -1);
        freqHz += (double)deltaHz;

        // Clamp to band
        if (freqHz < fMin) freqHz = fMin;
        if (freqHz > fMax) freqHz = fMax;
    }

    int stepHz() const { return steps[current]; }
};
TunerIDI tuner;

uint64_t frequency = 7000000;
// Get the encoder state
uint8_t read_encoder_state(void)
{
    return (gpio_get(ENCODER_DT) > 0 ? 1 : 0) | ((gpio_get(ENCODER_CLK) > 0 ? 1 : 0) << 1);
}

// Handle the encoder switch
long long int handle_switch(long int a, void* p)
{
    if (gpio_get(ENCODER_SWITCH) == 1 && button_state == false)
    {
        // Trigger button press
        button_pressed = true;
        button_state = true;
    }
    else if (gpio_get(ENCODER_SWITCH) == 0 && button_state == true)
    {
        button_state = 0;
    }
    return 0;
}

// Handle the encoder
void encoder_callback(uint gpio, uint32_t events)
{
    if (gpio == ENCODER_SWITCH)
    {
        // Debounce the switch
        add_alarm_in_ms(50, handle_switch, nullptr, true);
    }
    else if (gpio == ENCODER_CLK || gpio == ENCODER_DT)
    {

        // Track the pulses on the encoder and turn into a sensible rotary count.
        static uint8_t saved_enc = 0;
        uint8_t enc_now, enc_prev;

        enc_now = read_encoder_state();
        if (enc_now == saved_enc)
        {
            return;
        }

        // swap the state before we return
        enc_prev = saved_enc;
        saved_enc = enc_now;

        if ((enc_prev == 2 && enc_now == 3) || (enc_prev == 3 && enc_now == 1) || (enc_prev == 1 && enc_now == 0) || (enc_prev == 0 && enc_now == 2))
        {
            encoder_count++;
        }
        else if ((enc_prev == 3 && enc_now == 2) || (enc_prev == 2 && enc_now == 0) || (enc_prev == 0 && enc_now == 1) || (enc_prev == 1 && enc_now == 3))
        {
            encoder_count--;
        }
    }
}

int main()
{
    stdio_init_all();

    // Init i2c0 controller
    i2c_init(i2c0, 48000);

    // Set up pins 0 and 1 for I2C, pull both up internally
    gpio_set_function(DISPLAY_CLOCK, GPIO_FUNC_I2C);
    gpio_set_function(DISPLAY_DATA, GPIO_FUNC_I2C);
    gpio_pull_up(DISPLAY_CLOCK);
    gpio_pull_up(DISPLAY_DATA);

    gpio_set_dir(DISPLAY_CLOCK, GPIO_IN);
    gpio_set_dir(DISPLAY_DATA, GPIO_IN);

    // Rotary encoder
    gpio_set_function(ENCODER_SWITCH, GPIO_FUNC_SIO);
    gpio_set_function(ENCODER_CLK, GPIO_FUNC_SIO);
    gpio_set_function(ENCODER_DT, GPIO_FUNC_SIO);

    gpio_set_dir(ENCODER_CLK, GPIO_IN);
    gpio_set_dir(ENCODER_DT, GPIO_IN);
    gpio_set_dir(ENCODER_SWITCH, GPIO_IN);

    gpio_pull_up(ENCODER_CLK);
    gpio_pull_up(ENCODER_DT);
    gpio_pull_up(ENCODER_SWITCH);

    gpio_set_irq_enabled_with_callback(ENCODER_CLK, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &encoder_callback);
    gpio_set_irq_enabled(ENCODER_DT, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(ENCODER_SWITCH, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);

    // LED
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // blink();

    // If you don't do anything before initializing a display pi pico is too fast and starts sending
    // commands before the screen controller had time to set itself up, so we add an artificial delay for
    // ssd1306 to set itself up
    sleep_ms(250);

    // Initialize the Si5351; 7Mhz
    // Calibration to be done later; this is roughly correct
    si5351_init(0x60, SI5351_CRYSTAL_LOAD_8PF, 25000000, 140000); // I am using a 25 MHz TCXO

    // Just clock 0 for now
    si5351_set_clock_pwr(SI5351_CLK0, 1); // safety first
    si5351_set_clock_pwr(SI5351_CLK1, 0); // safety first
    si5351_set_clock_pwr(SI5351_CLK2, 0); // safety first

    si5351_drive_strength(SI5351_CLK0, SI5351_DRIVE_6MA);

    // Start at the base of 40m band
    si5351_set_freq(7000000ULL * 100ULL, SI5351_CLK0);
    si5351_output_enable(SI5351_CLK0, 1);
    si5351_output_enable(SI5351_CLK1, 0);
    si5351_output_enable(SI5351_CLK2, 0);

    // Create a new display object at address 0x3C and size of 128x64
    SSD1306 display = SSD1306(i2c0, DISPLAY_ADDRESS, Size::W128xH64);

    // Here we rotate the display by 180 degrees, so that it's not upside down from my perspective
    // If your screen is upside down try setting it to 1 or 0
    display.setOrientation(0);
    display.clear();
    display.sendBuffer();

    // Draw text on display
    // After passing a pointer to display, we need to tell the function what font and text to use
    // Available fonts are listed in textRenderer's readme
    // Last we tell this function where to anchor the text
    // Anchor means top left of what we draw
    std::array<int, 2> rows = { 3, 34 };

    // uint32_t currentDigit = 6;
    uint32_t x_offset = 4;

    sleep_ms(500);

    // Audio
    bool audio_ok = vfo_audio::start_audio();

    std::cout << "t(ms)  det  vel[dps]  step[Hz]   freq[Hz]\n";
    std::cout << "-------------------------------------------\n";

    /*
    for (const auto& e : script)
    {
        now_ms += e.dt_ms;
        double dt = e.dt_ms / 1000.0;
        double vel = rv.update(e.detents, dt);
        tuner.update(e.detents, vel, now_ms);

        std::cout
            << now_ms << "  "
            << (e.detents >= 0 ? " +" : " ")
            << e.detents << "    "
            << std::fixed << std::setprecision(2) << vel << "      "
            << tuner.stepHz() << "      "
            << std::fixed << std::setprecision(1) << tuner.freqHz << "\n";
    }
    */

    double vel;
    double dt;
        
    int count_was;
    auto drawDisplay = [&] {
        // Name of band
        display.clear();

        // drawRect(&display, 0, 0, 127, 63);

        std::ostringstream strstr;
        strstr << tuner.stepHz() << "x";
            /*
            << encoder_count << " "
            << std::fixed << std::setprecision(2) << vel << " "
            << tuner.stepHz() << " "
            << std::fixed << std::setprecision(1) << tuner.freqHz;
            */
            drawText(&display, font_12x16, strstr.str().c_str(), x_offset, 2);
            /*
        {
            drawText(&display, font_12x16, "40 metre", x_offset, 2);
        }
            */

        auto x_bar = 120;
        auto x_bar_width = 6;
        auto x_bar_height = 3;
        auto x_bar_gap = 2;

        for (int i = 0; i < (audio_ok ? 3 : 1); i++)
        {
            fillRect(&display, x_bar, ((x_bar_height + x_bar_gap) * i), x_bar + x_bar_width, x_bar_height + ((x_bar_height + x_bar_gap) * i));
        }

        // Frequency
        auto str = std::to_string(frequency) + "Mhz";
        drawText(&display, font_12x16, str.c_str(), x_offset, rows[1]);

        // Underline for the current counter digit to change
        /*
        const uint32_t fontHeight = 16;
        const uint32_t fontWidth = 12;
        uint32_t pad = 1;
        fillRect(&display, (currentDigit * fontWidth) + pad + x_offset, rows[1] + fontHeight, ((currentDigit + 1) * fontWidth) + x_offset, rows[1] + fontHeight + 2);
        */

        // Send buffer to the display
        display.sendBuffer();
    };
    drawDisplay();

    while (true)
    {
        // When the encoder ticks, advance
        bool update_clock = false;
        bool update_display = true;

        //static std::optional<myclock::time_point> last_;
        uint32_t now_ms = to_ms_since_boot(get_absolute_time()) / 2;
        tuner.update(-encoder_count, now_ms);
        encoder_count = 0;
        frequency = tuner.freqHz;

        // Encoder button pressed, choose the next unit to change
        /*
        if (button_pressed)
        {
            currentDigit++;
            if (currentDigit > 6)
            {
                currentDigit = 1;
            }
            button_pressed = false;
            update_display = true;
        }
            */

        // Update the clock
        if (update_clock)
        {
            si5351_set_freq(frequency * 100ULL, SI5351_CLK0);
        }

        // Update the display
        if (update_display)
        {
            drawDisplay();
        }

        // Back off, just a bit
        // sleep_ms(1);
        vfo_audio::update_audio_buffer();
    }

    reset_usb_boot(0, 0);

    while (true)
    {
        tight_loop_contents();
    }
}