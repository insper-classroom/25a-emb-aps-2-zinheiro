#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <stdbool.h>
#include <string.h>

#define JANELA       3
#define ZONA_MORTA  30
#define BUZZER_PIN  15    

typedef struct {
    int axis;
    int val;
} adc_t;

typedef struct {
    uint8_t codigo;
    bool    pressionado;
} botao_evento_t;

QueueHandle_t xQueueADC;
QueueHandle_t xQueueBotoes;

const uint BOTAO_GPIOS[]      = {16, 17, 18, 19, 20};
const uint8_t BOTOES_CODIGOS[] = {' ', 'R', 1, 2, 'E'};

int converter_adc_para_mouse(int leitura, bool *parado) {
    int centralizado = leitura - 2048;
    if (centralizado > -ZONA_MORTA && centralizado < ZONA_MORTA) {
        *parado = true;
        return 0;
    }
    *parado = false;
    int reduzido = centralizado * 50 / 2048;
    if (reduzido > 50) return 50;
    if (reduzido < -50) return -50;
    return reduzido;
}

void select_mux_channel(uint8_t channel) {
    gpio_put(11, channel & 0x01);
    gpio_put(12, (channel >> 1) & 0x01);
    gpio_put(13, (channel >> 2) & 0x01);
    busy_wait_us(50);
}

void gpio_callback(uint gpio, uint32_t eventos) {
    botao_evento_t evento;
    for (int i = 0; i < 5; i++) {
        if (gpio == BOTAO_GPIOS[i]) {
            evento.codigo      = BOTOES_CODIGOS[i];
            evento.pressionado = (eventos & GPIO_IRQ_EDGE_FALL) ? true : false;
            break;
        }
    }
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(xQueueBotoes, &evento, &woken);
    portYIELD_FROM_ISR(woken);
}

void gerar_buzzer_tiro() {
    const int freq_hz    = 2000;                      
    const int period_us  = 1000000 / freq_hz;         
    const int half_us    = period_us / 2;             
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
    int buffer[JANELA] = {0}, idx = 0;
    adc_t dado;
    bool ultimo_parado = true;
    while (1) {
        select_mux_channel(2);
        adc_select_input(1);
        int leitura = adc_read();
        buffer[idx] = leitura;
        idx = (idx + 1) % JANELA;
        int media = (buffer[0] + buffer[1] + buffer[2]) / JANELA;
        bool parado;
        int convertido = converter_adc_para_mouse(media, &parado);
        if (parado != ultimo_parado || !parado) {
            dado.axis = 0;
            dado.val  = convertido;
            xQueueSend(xQueueADC, &dado, 0);
            ultimo_parado = parado;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void y_task(void *p) {
    adc_gpio_init(27);
    int buffer[JANELA] = {0}, idx = 0;
    adc_t dado;
    bool ultimo_parado = true;
    while (1) {
        select_mux_channel(3);
        adc_select_input(1);
        int leitura = adc_read();
        buffer[idx] = leitura;
        idx = (idx + 1) % JANELA;
        int media = (buffer[0] + buffer[1] + buffer[2]) / JANELA;
        bool parado;
        int convertido = converter_adc_para_mouse(media, &parado);
        if (parado != ultimo_parado || !parado) {
            dado.axis = 1;
            dado.val  = convertido;
            xQueueSend(xQueueADC, &dado, 0);
            ultimo_parado = parado;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void direcional_task(void *p) {
    adc_gpio_init(27);
    adc_gpio_init(28);
    int buffer_h[JANELA] = {0}, idx_h = 0;
    int buffer_v[JANELA] = {0}, idx_v = 0;
    bool last_h = true, last_v = true;
    while (1) {
        select_mux_channel(0);
        adc_select_input(1);
        int lh = adc_read();
        buffer_h[idx_h] = lh;
        idx_h = (idx_h + 1) % JANELA;
        int mh = (buffer_h[0] + buffer_h[1] + buffer_h[2]) / JANELA;
        bool ph; int ch = converter_adc_para_mouse(mh, &ph);
        if (ph != last_h || !ph) {
            uint8_t cmd = ch < 0 ? 65 : (ch > 0 ? 68 : 0);
            if (cmd) {
                putchar_raw(0xC0);
                putchar_raw(0);
                putchar_raw(cmd);
                putchar_raw(0xCF);
            }
            last_h = ph;
        }
        select_mux_channel(1);
        adc_select_input(1);
        int lv = adc_read();
        buffer_v[idx_v] = lv;
        idx_v = (idx_v + 1) % JANELA;
        int mv = (buffer_v[0] + buffer_v[1] + buffer_v[2]) / JANELA;
        bool pv; int cv = converter_adc_para_mouse(mv, &pv);
        if (pv != last_v || !pv) {
            uint8_t cmd = cv < 0 ? 87 : (cv > 0 ? 83 : 0);
            if (cmd) {
                putchar_raw(0xC0);
                putchar_raw(1);
                putchar_raw(cmd);
                putchar_raw(0xCF);
            }
            last_v = pv;
        }
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

void botao_task(void *p) {
    botao_evento_t ev;
    while (1) {
        if (xQueueReceive(xQueueBotoes, &ev, portMAX_DELAY)) {
            putchar_raw(0xB0);
            putchar_raw(ev.codigo);
            putchar_raw(ev.pressionado ? 1 : 0);
            putchar_raw(0xFE);
            if (ev.codigo == 'R' && ev.pressionado) {
                gerar_buzzer_tiro();
            }
        }
    }
}

int main() {
   stdio_init_all();
    adc_init();

    xQueueADC    = xQueueCreate(32, sizeof(adc_t));
    xQueueBotoes = xQueueCreate(32, sizeof(botao_evento_t));

    // botões
    for (int i = 0; i < 5; i++) {
        gpio_init(BOTAO_GPIOS[i]);
        gpio_set_dir(BOTAO_GPIOS[i], GPIO_IN);
        gpio_pull_up(BOTAO_GPIOS[i]);
        gpio_set_irq_enabled_with_callback(
            BOTAO_GPIOS[i],
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
            true,
            gpio_callback
        );
    }
    gpio_init(11); gpio_set_dir(11, GPIO_OUT);
    gpio_init(12); gpio_set_dir(12, GPIO_OUT);
    gpio_init(13); gpio_set_dir(13, GPIO_OUT);
    
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);

    xTaskCreate(x_task,          "X Task",         2048, NULL, 1, NULL);
    xTaskCreate(y_task,          "Y Task",         2048, NULL, 1, NULL);
    xTaskCreate(direcional_task, "Dir Task",       2048, NULL, 1, NULL);
    xTaskCreate(uart_task,       "UART Task",      2048, NULL, 1, NULL);
    xTaskCreate(botao_task,      "Botao Task",     2048, NULL, 1, NULL);

    vTaskStartScheduler();
    while (1);
}
