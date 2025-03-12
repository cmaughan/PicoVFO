#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "pico-ssd1306/ssd1306.h"
#include "pico-ssd1306/textRenderer/TextRenderer.h"
#include "pico-ssd1306/shapeRenderer/ShapeRenderer.h"

#include "hardware/i2c.h"

#include <array>

// Use the namespace for convenience
using namespace pico_ssd1306;

void blink()
{
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(500);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(500);
}

int main()
{
    // Init i2c0 controller
    i2c_init(i2c0, 1000000);
    // Set up pins 12 and 13
    gpio_set_function(0, GPIO_FUNC_I2C);
    gpio_set_function(1, GPIO_FUNC_I2C);
    gpio_pull_up(0);
    gpio_pull_up(1);

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

    std::array<int, 2> rows = {0, 34};

    drawText(&display, font_12x16, "40 Meter", 0, 0);
    drawText(&display, font_12x16, "7.000000Mhz", 0, rows[1]);

    // Underline 
    const uint32_t fontHeight = 16;
    const uint32_t fontWidth = 12;
    uint32_t currentDigit = 7;
    uint32_t pad = 1;
    fillRect(&display, (currentDigit * fontWidth) + pad, rows[1] + fontHeight, ((currentDigit + 1) * fontWidth), rows[1] + fontHeight + 2);

    // Send buffer to the display
    display.sendBuffer();

    blink();

    sleep_ms(1000);

    reset_usb_boot(0, 0);

    while (true)
        tight_loop_contents();
}