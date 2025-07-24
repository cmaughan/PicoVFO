#include "pico/bootrom.h"
#include "pico/stdlib.h"

// Display Library
#include "pico-ssd1306/shapeRenderer/ShapeRenderer.h"
#include "pico-ssd1306/ssd1306.h"
#include "pico-ssd1306/textRenderer/TextRenderer.h"

#include "hardware/i2c.h"

//##include <badger2040.hpp>

// 5351 Frequency Synthesizer library
extern "C" {
#include "si5351/si5351.h"
}

#include <array>
#include <atomic>
#include <format>

// Use the namespace for convenience
using namespace pico_ssd1306;

// Utility function to blind the light for debugging
void blink()
{
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(500);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(500);
}

// Rotary encoder connections
#define ENCODER_SWITCH 2
#define ENCODER_CLK 4 // Pin for A (CLK)
#define ENCODER_DT 3 // Pin for B (DT)
#define ENCODER_ADDRESS 0x3C // The encoder's address on the I2C Bus

std::atomic<int> encoder_count = 0; // Counter for the encoder position
std::atomic<bool> button_pressed = false;
std::atomic<bool> button_state = false;

// Get the encoder state
uint8_t enc_state(void)
{
    static uint8_t prev_state = 0;

    uint8_t new_state = gpio_get(ENCODER_DT) + (2 * gpio_get(ENCODER_CLK));
    return new_state;
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

        enc_now = enc_state();
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
        if ((enc_prev == 3 && enc_now == 2) || (enc_prev == 2 && enc_now == 0) || (enc_prev == 0 && enc_now == 1) || (enc_prev == 1 && enc_now == 3))
        {
            encoder_count--;
        }
    }
}

int main()
{
    stdio_init_all();

    // Init i2c0 controller
    i2c_init(i2c0, 1000000);

    // Set up pins 0 and 1 for I2C, pull both up internally
    gpio_set_function(0, GPIO_FUNC_I2C);
    gpio_set_function(1, GPIO_FUNC_I2C);
    gpio_pull_up(0);
    gpio_pull_up(1);

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

    gpio_set_irq_enabled_with_callback(ENCODER_DT, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &encoder_callback);
    gpio_set_irq_enabled(ENCODER_CLK, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(ENCODER_SWITCH, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);

    // LED
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // If you don't do anything before initializing a display pi pico is too fast and starts sending
    // commands before the screen controller had time to set itself up, so we add an artificial delay for
    // ssd1306 to set itself up
    sleep_ms(250);

    // Initialize the Si5351; 7Mhz
    // Calibration to be done later; this is roughly correct
    si5351_init(0x60, SI5351_CRYSTAL_LOAD_8PF, 25000000, -40000); // I am using a 25 MHz TCXO

    // Just clock 0 for now
    si5351_set_clock_pwr(SI5351_CLK0, 1); // safety first
    si5351_set_clock_pwr(SI5351_CLK1, 0); // safety first
    si5351_set_clock_pwr(SI5351_CLK2, 0); // safety first

    si5351_drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);

    // Start at the base of 40m band
    si5351_set_freq(7000000ULL * 100ULL, SI5351_CLK0);
    si5351_output_enable(SI5351_CLK0, 1);
    si5351_output_enable(SI5351_CLK1, 0);
    si5351_output_enable(SI5351_CLK2, 0);

    // Create a new display object at address 0x3C and size of 128x64
    SSD1306 display = SSD1306(i2c0, ENCODER_ADDRESS, Size::W128xH64);

    // Here we rotate the display by 180 degrees, so that it's not upside down from my perspective
    // If your screen is upside down try setting it to 1 or 0
    display.setOrientation(0);

    // Draw text on display
    // After passing a pointer to display, we need to tell the function what font and text to use
    // Available fonts are listed in textRenderer's readme
    // Last we tell this function where to anchor the text
    // Anchor means top left of what we draw
    std::array<int, 2> rows = { 0, 34 };

    uint64_t value = 7000000;
    uint32_t currentDigit = 6;

    auto drawDisplay = [&] {
        // Name of band
        display.clear();

        drawRect(&display, 0, 0, 128, 64);

        drawText(&display, font_12x16, "40 Meter", 0, 0);

        // Frequency
        auto str = std::to_string(value) + "Mhz";
        drawText(&display, font_12x16, str.c_str(), 0, rows[1]);

        // Underline for the current counter digit to change
        const uint32_t fontHeight = 16;
        const uint32_t fontWidth = 12;
        uint32_t pad = 1;
        fillRect(&display, (currentDigit * fontWidth) + pad, rows[1] + fontHeight, ((currentDigit + 1) * fontWidth), rows[1] + fontHeight + 2);

        // Send buffer to the display
        display.sendBuffer();
    };
    drawDisplay();

    while (true)
    {
        // When the encoder ticks, advance
        bool update_clock = false;
        bool update_display = false;

        if (abs(encoder_count) > 2)
        {
            auto count = encoder_count / 2;
            value += (count * pow(10, (6 - currentDigit)));
            encoder_count = 0;
            update_clock = true;
            update_display = true;

            value = std::clamp(value, 7000000ull, 7200000ull);
        }

        // Encoder button pressed, choose the next unit to change
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

        // Update the clock
        if (update_clock)
        {
            si5351_set_freq(value * 100ULL, SI5351_CLK0);
        }

        // Update the display
        if (update_display)
        {
            drawDisplay();
        }

        // Back off, just a bit
        sleep_ms(1);
    }

    reset_usb_boot(0, 0);

    while (true)
    {
        tight_loop_contents();
    }
}