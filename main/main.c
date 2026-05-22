#include <FreeRTOS.h>
#include <queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <task.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/uart.h"
#include "hc06.h"
#include "pico/stdlib.h"
#include "ssd1306.h"

#define HC06_NAME "ENZO-BLUETOOTH"
#define HC06_PIN "1234"

#define LED_STATUS_PIN 15
#define BUTTON_PIN 10
#define OLED_SDA_PIN 2
#define OLED_SCL_PIN 3

#define JOYSTICK_VRX_PIN 26
#define JOYSTICK_VRY_PIN 27
#define JOYSTICK_DEADZONE 200
#define JOYSTICK_OUT_MAX 127
#define JOYSTICK_FILTER_N 5

#define LED_R_PIN 12
#define LED_G_PIN 13
#define LED_B_PIN 14
#define COLOR_DEADZONE 150

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

QueueHandle_t xQueueRX;
QueueHandle_t xQueueTX;
QueueHandle_t xQueuePin;
QueueHandle_t xQueueColor;

static ssd1306_t g_disp;

void uart_rx_handler() {
        uint8_t ch = uart_getc(HC06_UART_ID);
        xQueueSendFromISR(xQueueRX, &ch, 0);
}

void init_uart_hc06(void) {
    uart_init(HC06_UART_ID, HC06_BAUD_RATE);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(HC06_TX_PIN, UART_FUNCSEL_NUM(HC06_UART_ID, HC06_TX_PIN));
    gpio_set_function(HC06_RX_PIN, UART_FUNCSEL_NUM(HC06_UART_ID, HC06_RX_PIN));

    int __unused actual = uart_set_baudrate(HC06_UART_ID, HC06_BAUD_RATE);

    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_hw_flow(HC06_UART_ID, false, false);

    // Set our data format
    uart_set_format(HC06_UART_ID, 8, 1, UART_PARITY_NONE);
}

void init_uart_irq() {
     // Turn off FIFO's - we want to do this character by character
    uart_set_fifo_enabled(HC06_UART_ID, false);

    // Set up a RX interrupt
    // We need to set up the handler first
    // Select correct interrupt for the UART we are using
    int UART_IRQ = HC06_UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, uart_rx_handler);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(HC06_UART_ID, true, false);
}

static void tx_task(void* p) {
    uint8_t ch;
    while (true) {
        if (xQueueReceive(xQueueTX, &ch, portMAX_DELAY) == pdTRUE) {
            uart_putc_raw(HC06_UART_ID, ch);
        }
    }
}

static void serial_task(void* p) {
    uint8_t ch;
    while (true) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            ch = (uint8_t)c;
            xQueueSend(xQueueTX, &ch, 0);
        }

        while (xQueueReceive(xQueueRX, &ch, 0) == pdTRUE) {
            putchar_raw(ch);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void led_status_task(void* p) {
    gpio_init(HC06_STATE_PIN);
    gpio_set_dir(HC06_STATE_PIN, GPIO_IN);

    gpio_set_function(LED_STATUS_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(LED_STATUS_PIN);
    uint chan  = pwm_gpio_to_channel(LED_STATUS_PIN);
    pwm_set_wrap(slice, 255);
    pwm_set_chan_level(slice, chan, 0);
    pwm_set_enabled(slice, true);

    int level = 0;
    int dir = 4;
    TickType_t last_high_tick = 0;
    const TickType_t connected_window = pdMS_TO_TICKS(1500);

    while (true) {
        if (gpio_get(HC06_STATE_PIN)) {
            last_high_tick = xTaskGetTickCount();
        }
        bool connected = (xTaskGetTickCount() - last_high_tick) < connected_window
                         && last_high_tick != 0;

        if (connected) {
            pwm_set_chan_level(slice, chan, 255);
            level = 0;
            dir = 4;
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            level += dir;
            if (level >= 255) { level = 255; dir = -dir; }
            else if (level <= 0) { level = 0; dir = -dir; }
            pwm_set_chan_level(slice, chan, (uint16_t)level);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

static void button_task(void* p) {
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    srand(to_us_since_boot(get_absolute_time()));

    int UART_IRQ = HC06_UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    while (true) {
        if (gpio_get(BUTTON_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(30));
            if (gpio_get(BUTTON_PIN) == 0) {
                char pin[5];
                snprintf(pin, sizeof(pin), "%04d", rand() % 10000);
                printf("%s\n", pin);

                irq_set_enabled(UART_IRQ, false);
                hc06_set_at_mode(1);
                hc06_set_pin(pin);
                hc06_set_at_mode(0);
                irq_set_enabled(UART_IRQ, true);

                xQueueOverwrite(xQueuePin, pin);

                while (gpio_get(BUTTON_PIN) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void joystick_send_axis(uint8_t axis, int16_t value) {
    uint8_t packet[4];
    packet[0] = 0xFF;
    packet[1] = axis;
    packet[2] = (uint8_t)(value & 0xFF);
    packet[3] = (uint8_t)((value >> 8) & 0xFF);
    for (int i = 0; i < 4; i++) {
        xQueueSend(xQueueTX, &packet[i], 0);
    }
}

static void joystick_compute_color(uint16_t avg_x, uint16_t avg_y, rgb_t *out) {
    int dx = (int)avg_x - 2048;
    int dy = (int)avg_y - 2048;

    if (dx > -COLOR_DEADZONE && dx < COLOR_DEADZONE) dx = 0;
    if (dy > -COLOR_DEADZONE && dy < COLOR_DEADZONE) dy = 0;

    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    int r = (dy > 0) ? (ady * 255) / 2048 : 0;
    int g = (dy < 0) ? (ady * 255) / 2048 : 0;
    int b = (adx * 255) / 2048;

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

    out->r = (uint8_t)r;
    out->g = (uint8_t)g;
    out->b = (uint8_t)b;
}

static int16_t joystick_process(uint16_t raw) {
    int delta = (int)raw - 2048;
    if (delta > -JOYSTICK_DEADZONE && delta < JOYSTICK_DEADZONE) {
        return 0;
    }
    int scaled;
    if (delta > 0) {
        scaled = ((delta - JOYSTICK_DEADZONE) * JOYSTICK_OUT_MAX) / (2047 - JOYSTICK_DEADZONE);
    } else {
        scaled = ((delta + JOYSTICK_DEADZONE) * JOYSTICK_OUT_MAX) / (2048 - JOYSTICK_DEADZONE);
    }
    if (scaled > JOYSTICK_OUT_MAX) scaled = JOYSTICK_OUT_MAX;
    if (scaled < -JOYSTICK_OUT_MAX) scaled = -JOYSTICK_OUT_MAX;
    return (int16_t)scaled;
}

static void joystick_task(void* p) {
    adc_init();
    adc_gpio_init(JOYSTICK_VRX_PIN);
    adc_gpio_init(JOYSTICK_VRY_PIN);

    uint16_t buf_x[JOYSTICK_FILTER_N] = {0};
    uint16_t buf_y[JOYSTICK_FILTER_N] = {0};
    int idx = 0;

    while (true) {
        adc_select_input(0);
        buf_x[idx] = adc_read();
        adc_select_input(1);
        buf_y[idx] = adc_read();
        idx = (idx + 1) % JOYSTICK_FILTER_N;

        uint32_t sum_x = 0, sum_y = 0;
        for (int i = 0; i < JOYSTICK_FILTER_N; i++) {
            sum_x += buf_x[i];
            sum_y += buf_y[i];
        }
        uint16_t avg_x = sum_x / JOYSTICK_FILTER_N;
        uint16_t avg_y = sum_y / JOYSTICK_FILTER_N;

        int16_t vx = joystick_process(avg_x);
        int16_t vy = joystick_process(avg_y);

        if (vx != 0) joystick_send_axis(0, vx);
        if (vy != 0) joystick_send_axis(1, vy);

        rgb_t color;
        joystick_compute_color(avg_x, avg_y, &color);
        xQueueOverwrite(xQueueColor, &color);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void rgb_task(void* p) {
    gpio_set_function(LED_R_PIN, GPIO_FUNC_PWM);
    gpio_set_function(LED_G_PIN, GPIO_FUNC_PWM);
    gpio_set_function(LED_B_PIN, GPIO_FUNC_PWM);

    uint slice_r = pwm_gpio_to_slice_num(LED_R_PIN);
    uint chan_r  = pwm_gpio_to_channel(LED_R_PIN);
    uint slice_g = pwm_gpio_to_slice_num(LED_G_PIN);
    uint chan_g  = pwm_gpio_to_channel(LED_G_PIN);
    uint slice_b = pwm_gpio_to_slice_num(LED_B_PIN);
    uint chan_b  = pwm_gpio_to_channel(LED_B_PIN);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 4.0f);
    pwm_config_set_wrap(&cfg, 255);

    pwm_init(slice_r, &cfg, true);
    if (slice_g != slice_r)                       pwm_init(slice_g, &cfg, true);
    if (slice_b != slice_r && slice_b != slice_g) pwm_init(slice_b, &cfg, true);

    pwm_set_chan_level(slice_r, chan_r, 0);
    pwm_set_chan_level(slice_g, chan_g, 0);
    pwm_set_chan_level(slice_b, chan_b, 0);

    rgb_t color = {0};
    while (true) {
        if (xQueueReceive(xQueueColor, &color, portMAX_DELAY) == pdTRUE) {
            pwm_set_chan_level(slice_r, chan_r, color.r);
            pwm_set_chan_level(slice_g, chan_g, color.g);
            pwm_set_chan_level(slice_b, chan_b, color.b);
        }
    }
}

static void oled_task(void* p) {
    i2c_init(i2c1, 400000);
    gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_SDA_PIN);
    gpio_pull_up(OLED_SCL_PIN);

    g_disp.external_vcc = false;
    ssd1306_init(&g_disp, 128, 32, 0x3C, i2c1);
    ssd1306_clear(&g_disp);

    char pin[5];
    snprintf(pin, sizeof(pin), "%s", HC06_PIN);

    while (true) {
        ssd1306_clear(&g_disp);
        ssd1306_draw_string(&g_disp, 0, 0, 1, "PIN atual:");
        ssd1306_draw_string(&g_disp, 0, 16, 2, pin);
        ssd1306_show(&g_disp);

        if (xQueueReceive(xQueuePin, pin, portMAX_DELAY) == pdTRUE) {
            // novo PIN recebido, loop redesenha
        }
    }
}

int main(void) {
    stdio_init_all();

    // inicializa uart e hc06
    init_uart_hc06();
    hc06_config(HC06_NAME, HC06_PIN);
    init_uart_irq();

    xQueueRX = xQueueCreate(256, sizeof(uint8_t));
    xQueueTX = xQueueCreate(256, sizeof(uint8_t));
    xQueuePin = xQueueCreate(1, 5 * sizeof(char));
    xQueueColor = xQueueCreate(1, sizeof(rgb_t));

    xTaskCreate(tx_task, "TX", 512, NULL, 2, NULL);
    xTaskCreate(serial_task, "Serial", 1024, NULL, 1, NULL);
    // xTaskCreate(led_status_task, "LED", 256, NULL, 1, NULL);
    xTaskCreate(button_task, "BTN", 512, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 1024, NULL, 1, NULL);
    xTaskCreate(joystick_task, "JOY", 512, NULL, 1, NULL);
    xTaskCreate(rgb_task, "RGB", 256, NULL, 1, NULL);

    vTaskStartScheduler();
    while (true);
}
