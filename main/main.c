#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <stdbool.h>

#define JANELA             3
#define ZONA_MORTA        800
#define BUZZER_PIN        15
#define ENABLE_BUTTON_PIN 14  // Botão branco
#define LED_PIN            2  // Indica fila ativa

typedef struct {
    int axis;
    int val;
} adc_t;

typedef struct {
    uint8_t codigo;
    bool    pressionado;
} botao_evento_t;

static QueueHandle_t xQueueADC;
static QueueHandle_t xQueueBotoes;
static volatile bool sending_enabled = false;
static uint32_t last_toggle_ms = 0;

const uint BOTAO_GPIOS[]      = {16, 17, 18, 19, 20};
const uint8_t BOTOES_CODIGOS[] = {' ', 'R', 1, 2, 'E'};

// Converte leitura ADC em movimento (-50..50) com zona morta
int converter_adc_para_mouse(int leitura, bool *parado) {
    int centralizado = leitura - 2048;
    if (abs(centralizado) < ZONA_MORTA) {
        *parado = true;
        return 0;
    }
    *parado = false;
    int reduzido = centralizado * 50 / 2048;
    if (reduzido > 50)  return 50;
    if (reduzido < -50) return -50;
    return reduzido;
}

// Seleciona canal do multiplexador CD4051
void select_mux_channel(uint8_t channel) {
    gpio_put(11, channel & 0x01);
    gpio_put(12, (channel >> 1) & 0x01);
    gpio_put(13, (channel >> 2) & 0x01);
    busy_wait_us(50);
}

// ISR unificado: botão de enable e botões de jogo
void gpio_callback(uint gpio, uint32_t events) {
    BaseType_t woken = pdFALSE;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Botão de enable
    if (gpio == ENABLE_BUTTON_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        if (now - last_toggle_ms >= 5000) {
            // Alterna estado e atualiza LED
            sending_enabled = !sending_enabled;
            gpio_put(LED_PIN, sending_enabled);
            last_toggle_ms = now;
        }
        return;
    }

    // Botões de jogo
    if (sending_enabled) {
        botao_evento_t evento;
        for (int i = 0; i < 5; i++) {
            if (gpio == BOTAO_GPIOS[i]) {
                evento.codigo      = BOTOES_CODIGOS[i];
                evento.pressionado = (events & GPIO_IRQ_EDGE_FALL) ? true : false;
                break;
            }
        }
        xQueueSendFromISR(xQueueBotoes, &evento, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

void gerar_buzzer_tiro() {
    const int freq_hz     = 2000;
    const int period_us   = 1000000 / freq_hz;
    const int half_us     = period_us / 2;
    const int duration_ms = 100;
    int cycles = freq_hz * duration_ms / 1000;
    for (int i = 0; i < cycles; i++) {
        gpio_put(BUZZER_PIN, 1);
        busy_wait_us(half_us);
        gpio_put(BUZZER_PIN, 0);
        busy_wait_us(half_us);
    }
}

void x_task(void *p) {
    adc_gpio_init(27);
    int buffer[JANELA] = {0};
    int idx = 0;
    bool ultimo_parado = true;

    // Preencher buffer inicial
    for (int i = 0; i < JANELA; i++) {
        select_mux_channel(2);
        adc_select_input(2);
        buffer[i] = adc_read();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (1) {
        select_mux_channel(2);
        adc_select_input(2);
        buffer[idx] = adc_read();
        idx = (idx + 1) % JANELA;
        int media = (buffer[0] + buffer[1] + buffer[2]) / JANELA;
        bool parado;
        int convertido = converter_adc_para_mouse(media, &parado);
        if (sending_enabled && (!parado || parado != ultimo_parado)) {
            adc_t dado = { .axis = 0, .val = convertido };
            xQueueSend(xQueueADC, &dado, 0);
        }
        ultimo_parado = parado;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void y_task(void *p) {
    adc_gpio_init(27);
    int buffer[JANELA] = {0};
    int idx = 0;
    bool ultimo_parado = true;

    for (int i = 0; i < JANELA; i++) {
        select_mux_channel(3);
        adc_select_input(2);
        buffer[i] = adc_read();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (1) {
        select_mux_channel(3);
        adc_select_input(2);
        buffer[idx] = adc_read();
        idx = (idx + 1) % JANELA;
        int media = (buffer[0] + buffer[1] + buffer[2]) / JANELA;
        bool parado;
        int convertido = converter_adc_para_mouse(media, &parado);
        if (sending_enabled && (!parado || parado != ultimo_parado)) {
            adc_t dado = { .axis = 1, .val = convertido };
            xQueueSend(xQueueADC, &dado, 0);
        }
        ultimo_parado = parado;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void direcional_task(void *p) {
    adc_gpio_init(27);
    adc_gpio_init(28);
    int buffer_h[JANELA] = {0};
    int buffer_v[JANELA] = {0};
    int idx_h = 0, idx_v = 0;
    bool last_h = true, last_v = true;

    while (1) {
        // Horizontal WASD
        select_mux_channel(0);
        adc_select_input(2);
        buffer_h[idx_h] = adc_read();
        idx_h = (idx_h + 1) % JANELA;
        int mh = (buffer_h[0] + buffer_h[1] + buffer_h[2]) / JANELA;
        bool ph;
        int ch = converter_adc_para_mouse(mh, &ph);
        if (sending_enabled && (!ph || ph != last_h)) {
            uint8_t cmd = ch < 0 ? 'A' : (ch > 0 ? 'D' : 0);
            if (cmd) {
                putchar_raw(0xC0);
                putchar_raw(0);
                putchar_raw(cmd);
                putchar_raw(0xCF);
            }
        }
        last_h = ph;

        // Vertical WASD
        select_mux_channel(1);
        adc_select_input(2);
        buffer_v[idx_v] = adc_read();
        idx_v = (idx_v + 1) % JANELA;
        int mv = (buffer_v[0] + buffer_v[1] + buffer_v[2]) / JANELA;
        bool pv;
        int cv = converter_adc_para_mouse(mv, &pv);
        if (sending_enabled && (!pv || pv != last_v)) {
            uint8_t cmd = cv < 0 ? 'W' : (cv > 0 ? 'S' : 0);
            if (cmd) {
                putchar_raw(0xC0);
                putchar_raw(1);
                putchar_raw(cmd);
                putchar_raw(0xCF);
            }
        }
        last_v = pv;

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void uart_task(void *p) {
    adc_t dado;
    while (1) {
        if (xQueueReceive(xQueueADC, &dado, portMAX_DELAY)) {
            putchar_raw(0xA0);
            putchar_raw((uint8_t)dado.axis);
            putchar_raw((dado.val >> 8) & 0xFF);
            putchar_raw(dado.val & 0xFF);
            putchar_raw(0xFF);
        }
    }
}

int main() {
    stdio_init_all();
    adc_init();

    // Inicialização de filas
    xQueueADC    = xQueueCreate(32, sizeof(adc_t));
    xQueueBotoes = xQueueCreate(32, sizeof(botao_evento_t));

    // Configura LED indicador
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    // Configura botão de enable
    gpio_init(ENABLE_BUTTON_PIN);
    gpio_set_dir(ENABLE_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(ENABLE_BUTTON_PIN);
    gpio_set_irq_enabled_with_callback(
        ENABLE_BUTTON_PIN,
        GPIO_IRQ_EDGE_FALL,
        true,
        gpio_callback
    );

    // Configura botões de jogo
    for (int i = 0; i < 5; i++) {
        gpio_init(BOTAO_GPIOS[i]);
        gpio_set_dir(BOTAO_GPIOS[i], GPIO_IN);
        gpio_pull_up(BOTAO_GPIOS[i]);
        gpio_set_irq_enabled(
            BOTAO_GPIOS[i],
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
            true
        );
    }

    // Linhas do multiplexador
    gpio_init(11); gpio_set_dir(11, GPIO_OUT);
    gpio_init(12); gpio_set_dir(12, GPIO_OUT);
    gpio_init(13); gpio_set_dir(13, GPIO_OUT);

    // Buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);

    // Cria tasks
    xTaskCreate(x_task,          "X Task",         2048, NULL, 1, NULL);
    xTaskCreate(y_task,          "Y Task",         2048, NULL, 1, NULL);
    xTaskCreate(direcional_task, "Dir Task",       2048, NULL, 1, NULL);
    xTaskCreate(uart_task,       "UART Task",      2048, NULL, 1, NULL);

    vTaskStartScheduler();
    while (1) tight_loop_contents();
    return 0;
}
