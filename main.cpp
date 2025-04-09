#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "pico-ssd1306/shapeRenderer/ShapeRenderer.h"
#include "pico-ssd1306/ssd1306.h"
#include "pico-ssd1306/textRenderer/TextRenderer.h"

#include "hardware/i2c.h"

#include "si5351/si5351.h"

#include <array>
#include <atomic>
#include <format>

// Use the namespace for convenience
using namespace pico_ssd1306;

void blink()
{
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(500);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(500);
}

#define ENCODER_SWITCH 2
#define ENCODER_CLK 4 // Pin for A (CLK)
#define ENCODER_DT 3 // Pin for B (DT)

std::atomic<int> encoder_count = 0; // Counter for the encoder position
std::atomic<bool> button_pressed = false;
std::atomic<bool> button_state = false;

/* encoder routines */
uint8_t enc_state(void)
{
    static uint8_t prev_state = 0;

    uint8_t new_state = gpio_get(ENCODER_DT) + (2 * gpio_get(ENCODER_CLK));
    return new_state;
}

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

void encoder_callback(uint gpio, uint32_t events)
{
    if (gpio == ENCODER_SWITCH)
    {
        add_alarm_in_ms(50, handle_switch, nullptr, true);
    }
    else if (gpio == ENCODER_CLK || gpio == ENCODER_DT)
    {
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
    // Set up pins 12 and 13
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

    // Create a new display object at address 0x3D and size of 128x64
    SSD1306 display = SSD1306(i2c0, 0x3C, Size::W128xH64);

    // Here we rotate the display by 180 degrees, so that it's not upside down from my perspective
    // If your screen is upside down try setting it to 1 or 0
    display.setOrientation(0);

    // Draw text on display
    // After passing a pointer to display, we need to tell the function what font and text to use
    // Available fonts are listed in textRenderer's readme
    // Last we tell this function where to anchor the text
    // Anchor means top left of what we draw

    std::array<int, 2> rows = { 0, 34 };

    // Initialize the Si5351; 7Mhz
    si5351_init(0x60, SI5351_CRYSTAL_LOAD_8PF, 25000000, 0); // I am using a 25 MHz TCXO
    si5351_set_clock_pwr(SI5351_CLK0, 0); // safety first

    si5351_drive_strength(SI5351_CLK1, SI5351_DRIVE_8MA);
    si5351_drive_strength(SI5351_CLK2, SI5351_DRIVE_8MA);

    si5351_set_freq(7074000ULL * 100ULL, SI5351_CLK0);
    si5351_output_enable(SI5351_CLK0, 1);
    si5351_output_enable(SI5351_CLK1, 0);
    si5351_output_enable(SI5351_CLK2, 0);


    uint64_t value = 7000000;
    uint32_t currentDigit = 6;

    auto drawDisplay = [&] {
        display.clear();
        drawText(&display, font_12x16, "40 Meter", 0, 0);

        auto str = std::to_string(value) + "Mhz";
        drawText(&display, font_12x16, str.c_str(), 0, rows[1]);

        // Underline
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
        bool update = false;
        if (abs(encoder_count) > 1)
        {
            printf("EncoderCount: %d\n", encoder_count.load());
            printf("currentDigit: %d\n\n", currentDigit);
            value += ((encoder_count / 2) * pow(10, (6 - currentDigit)));
            encoder_count = 0;
            update = true;
            value = std::clamp(value, 7000000ull, 7200000ull);
        }

        if (button_pressed)
        {
            currentDigit++;
            if (currentDigit > 6)
            {
                currentDigit = 1;
            }
            button_pressed = false;
            update = true;
        }

        if (update)
        {
            drawDisplay();
        }
    }

    reset_usb_boot(0, 0);

    while (true)
    {
        tight_loop_contents();
    }
}