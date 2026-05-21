#include "hc06.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define HC06_TIMEOUT_RESPOSTA_US 1200
#define HC06_ESPERA_AT_MS        500
#define HC06_ESPERA_CMD_MS       1500
#define HC06_TENTATIVAS_POR_BAUD 5

static void hc06_limpar_rx(void) {
    while (uart_is_readable_within_us(HC06_UART_ID, HC06_TIMEOUT_RESPOSTA_US))
        uart_getc(HC06_UART_ID);
}

static int hc06_ler_resposta(char *buf, size_t len) {
    int i = 0;
    while (uart_is_readable_within_us(HC06_UART_ID, HC06_TIMEOUT_RESPOSTA_US) && i < (int)(len - 1)) {
        buf[i++] = uart_getc(HC06_UART_ID);
    }
    buf[i] = '\0';
    return i;
}

bool hc06_check_connection() {
    char str[64];
    uart_puts(HC06_UART_ID, "AT");
    sleep_ms(HC06_ESPERA_AT_MS);
    hc06_ler_resposta(str, sizeof(str));
    return strstr(str, "OK") != NULL;
}

bool hc06_set_name(char name[]) {
    char cmd[32];
    char str[64];
    snprintf(cmd, sizeof(cmd), "AT+NAME%s", name);
    uart_puts(HC06_UART_ID, cmd);
    sleep_ms(HC06_ESPERA_CMD_MS);
    hc06_ler_resposta(str, sizeof(str));
    return strstr(str, "OKsetname") != NULL;
}

bool hc06_set_pin(char pin[]) {
    char cmd[32];
    char str[64];
    snprintf(cmd, sizeof(cmd), "AT+PIN%s", pin);
    uart_puts(HC06_UART_ID, cmd);
    sleep_ms(HC06_ESPERA_CMD_MS);
    hc06_ler_resposta(str, sizeof(str));
    return strstr(str, "OKsetPIN") != NULL;
}

bool hc06_set_baud_115200() {
    char str[64];
    uart_puts(HC06_UART_ID, "AT+BAUD8");
    sleep_ms(HC06_ESPERA_AT_MS);
    hc06_ler_resposta(str, sizeof(str));
    return strstr(str, "OK115200") != NULL;
}

bool hc06_set_at_mode(int on) {
    gpio_init(HC06_ENABLE_PIN);
    gpio_set_dir(HC06_ENABLE_PIN, GPIO_OUT);
    gpio_put(HC06_ENABLE_PIN, on);
    return true;
}

static bool hc06_tentar_baud(uint baud) {
    printf("Tentando baud = %u...\n", baud);
    uart_set_baudrate(HC06_UART_ID, baud);
    sleep_ms(HC06_ESPERA_AT_MS);
    for (int i = 0; i < HC06_TENTATIVAS_POR_BAUD; i++) {
        if (hc06_check_connection()) return true;
        printf("baud = %u nao respondendo\n", baud);
        sleep_ms(HC06_ESPERA_AT_MS);
    }
    return false;
}

bool hc06_config(char name[], char pin[]) {
    hc06_set_at_mode(1);

    bool connected = false;
    bool already_115200 = false;

    if (hc06_tentar_baud(9600)) {
        connected = true;
        already_115200 = false;
    } else {
        if (hc06_tentar_baud(115200)) {
            connected = true;
            already_115200 = true;
        }
    }

    if (!connected) {
        printf("ERRO: HC-06 nao respondeu. Verifique as conexoes e reinicie.\n");
        while (1);
    }

    printf("Conectado!\n\n");

    if (!already_115200) {
        printf("Alterando baud rate para 115200...\n");
        while (!hc06_set_baud_115200()) {
            printf("set baud failed\n");
            sleep_ms(HC06_ESPERA_AT_MS);
        }
        uart_init(HC06_UART_ID, 115200);
        sleep_ms(HC06_ESPERA_AT_MS);
        hc06_limpar_rx();
        printf("Baud rate OK\n\n");
    }

    printf("Configurando nome...\n");
    while (!hc06_set_name(name)) {
        printf("set name failed\n");
        sleep_ms(HC06_ESPERA_AT_MS);
    }
    printf("Nome OK\n\n");

    printf("Configurando PIN...\n");
    while (!hc06_set_pin(pin)) {
        printf("set pin failed\n");
        sleep_ms(HC06_ESPERA_AT_MS);
    }
    printf("PIN OK\n\n");

    printf("HC-06 configurado!\n");
    hc06_set_at_mode(0);
    return true;
}
