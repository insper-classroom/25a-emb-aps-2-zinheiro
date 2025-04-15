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

#define JANELA 3
#define ZONA_MORTA 30

typedef struct {
    int axis;  
    int val;  
} adc_t;

typedef struct {
    uint8_t codigo;   
    bool pressionado;  
} botao_evento_t;

QueueHandle_t xQueueADC;
QueueHandle_t xQueueBotoes;

const uint BOTAO_GPIOS[] = {16, 17, 18, 19, 20};
const uint8_t BOTOES_CODIGOS[] = {' ', 'R', 1, 2, 'E'};  // 1 = RightClick, 2 = Aim

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

void gpio_callback(uint gpio, uint32_t eventos) {
    botao_evento_t evento;

    for (int i = 0; i < 5; i++) {
        if (gpio == BOTAO_GPIOS[i]) {
            evento.codigo = BOTOES_CODIGOS[i];
            evento.pressionado = (eventos & GPIO_IRQ_EDGE_FALL) ? true : false;
            break;
        }
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(xQueueBotoes, &evento, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void x_task(void *p) {
    adc_gpio_init(26);  
    int buffer[JANELA] = {0}, idx = 0;
    adc_t dado;
    bool ultimo_parado = true;

    while (1) {
        adc_select_input(0);
        int leitura = adc_read();

        buffer[idx] = leitura;
        idx = (idx + 1) % JANELA;

        int media = (buffer[0] + buffer[1] + buffer[2]) / JANELA;

        bool parado;
        int convertido = converter_adc_para_mouse(media, &parado);

        if (parado != ultimo_parado || !parado) {
            dado.axis = 0; 
            dado.val = convertido;
            xQueueSend(xQueueADC, &dado, 0);
            ultimo_parado = parado;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void y_task(void *p) {
    adc_gpio_init(27);  
    int buffer[JANELA] = {0}, idx = 0;
    adc_t dado;
    bool ultimo_parado = true;

    while (1) {
        adc_select_input(1);
        int leitura = adc_read();

        buffer[idx] = leitura;
        idx = (idx + 1) % JANELA;

        int media = (buffer[0] + buffer[1] + buffer[2]) / JANELA;

        bool parado;
        int convertido = converter_adc_para_mouse(media, &parado);

        if (parado != ultimo_parado || !parado) {
            dado.axis = 1;  // eixo Y
            dado.val = convertido;
            xQueueSend(xQueueADC, &dado, 0);
            ultimo_parado = parado;
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
    botao_evento_t evento;
    while (1) {
        if (xQueueReceive(xQueueBotoes, &evento, portMAX_DELAY)) {
            putchar_raw(0xB0);                   
            putchar_raw(evento.codigo);          
            putchar_raw(evento.pressionado ? 1 : 0); 
            putchar_raw(0xFE);                 
        }
    }
}

int main() {
    stdio_init_all();
    adc_init();

    xQueueADC = xQueueCreate(32, sizeof(adc_t));
    xQueueBotoes = xQueueCreate(32, sizeof(botao_evento_t));

    for (int i = 0; i < 5; i++) {
        gpio_init(BOTAO_GPIOS[i]);
        gpio_set_dir(BOTAO_GPIOS[i], GPIO_IN);
        gpio_pull_up(BOTAO_GPIOS[i]);
        gpio_set_irq_enabled_with_callback(BOTAO_GPIOS[i],
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_callback);
    }

    xTaskCreate(x_task, "X Task", 2048, NULL, 1, NULL);
    xTaskCreate(y_task, "Y Task", 2048, NULL, 1, NULL);
    xTaskCreate(uart_task, "UART Task", 2048, NULL, 1, NULL);
    xTaskCreate(botao_task, "Botao Task", 2048, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}
