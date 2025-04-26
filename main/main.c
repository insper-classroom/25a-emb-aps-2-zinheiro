#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
#include <stdlib.h>    
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <stdbool.h>

#define JANELA              3
#define ZONA_MORTA        800
#define BUZZER_PIN         15
#define ENABLE_BUTTON_PIN  14  
#define LED_PIN             2  
#define DEBOUNCE_MS        50  

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
static QueueHandle_t xQueueBuzzer;
static SemaphoreHandle_t xSemEnable;

static TaskHandle_t xHandleX;
static TaskHandle_t xHandleY;
static TaskHandle_t xHandleDir;
static TaskHandle_t xHandleUART;
static TaskHandle_t xHandleBotao;
static TaskHandle_t xHandleBuzzer;
static TaskHandle_t xHandlePower;

static void gpio_callback(uint gpio, uint32_t events);
static void gerar_buzzer_tiro();
static void buzzer_task(void* p);
static int converter_adc_para_mouse(int leitura, bool *parado);
static void select_mux_channel(uint8_t channel);
static void x_task(void* p);
static void y_task(void* p);
static void direcional_task(void* p);
static void uart_task(void* p);
static void botao_task(void* p);
static void power_task(void* p);

static void select_mux_channel(uint8_t channel) {
    gpio_put(11,  channel & 0x01);
    gpio_put(12, (channel >> 1) & 0x01);
    gpio_put(13, (channel >> 2) & 0x01);
    vTaskDelay(pdMS_TO_TICKS(1)); 
}

static void gpio_callback(uint gpio, uint32_t events) {
    BaseType_t woken = pdFALSE;
    if (!(events & GPIO_IRQ_EDGE_FALL)) return;
    if (gpio == ENABLE_BUTTON_PIN) {
        xSemaphoreGiveFromISR(xSemEnable, &woken);
        portYIELD_FROM_ISR(woken);
        return;
    }
    botao_evento_t ev;
    if      (gpio == 16) { ev.codigo = ' '; ev.pressionado = true; }
    else if (gpio == 17) { ev.codigo = 'R'; ev.pressionado = true; }
    else if (gpio == 18) { ev.codigo = 1;   ev.pressionado = true; }
    else if (gpio == 19) { ev.codigo = 2;   ev.pressionado = true; }
    else if (gpio == 20) { ev.codigo = 'E'; ev.pressionado = true; }
    else return;
    xQueueSendFromISR(xQueueBotoes, &ev, &woken);
    portYIELD_FROM_ISR(woken);
}

static void gerar_buzzer_tiro() {
    gpio_put(BUZZER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_put(BUZZER_PIN, 0);
}

static void buzzer_task(void *p) {
    (void)p;
    uint8_t token;
    while (1) {
        if (xQueueReceive(xQueueBuzzer, &token, portMAX_DELAY)) {
            gerar_buzzer_tiro();
        }
    }
}

static int converter_adc_para_mouse(int leitura, bool *parado) {
    int c = leitura - 2048;
    if (abs(c) < ZONA_MORTA) {
        *parado = true;
        return 0;
    }
    *parado = false;
    int r = c * 50 / 2048;
    if (r > 50)  return 50;
    if (r < -50) return -50;
    return r;
}

static void x_task(void *p) {
    (void)p;
    adc_gpio_init(27);
    int buf[JANELA] = {0};
    int idx = 0;
    bool last_par = true;
    for (int i = 0; i < JANELA; i++) {
        select_mux_channel(2);
        adc_select_input(1);
        buf[i] = adc_read();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    while (1) {
        select_mux_channel(2);
        adc_select_input(1);
        buf[idx] = adc_read();
        idx = (idx + 1) % JANELA;
        int sum = buf[0] + buf[1] + buf[2];
        bool par;
        int d = converter_adc_para_mouse(sum / JANELA, &par);
        adc_t pkt = { .axis = 0, .val = d };
        if (!par || par != last_par) xQueueSend(xQueueADC, &pkt, 0);
        last_par = par;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void y_task(void *p) {
    (void)p;
    adc_gpio_init(27);
    int buf[JANELA] = {0};
    int idx = 0;
    bool last_par = true;
    for (int i = 0; i < JANELA; i++) {
        select_mux_channel(3);
        adc_select_input(1);
        buf[i] = adc_read();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    while (1) {
        select_mux_channel(3);
        adc_select_input(1);
        buf[idx] = adc_read();
        idx = (idx + 1) % JANELA;
        int sum = buf[0] + buf[1] + buf[2];
        bool par;
        int d = converter_adc_para_mouse(sum / JANELA, &par);
        adc_t pkt = { .axis = 1, .val = d };
        if (!par || par != last_par) xQueueSend(xQueueADC, &pkt, 0);
        last_par = par;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void direcional_task(void *p) {
    (void)p;
    adc_gpio_init(27);
    int bufh[JANELA] = {0}, idxh = 0;
    int bufv[JANELA] = {0}, idxv = 0;
    bool ph = true, pv = true;
    while (1) {
        select_mux_channel(0);
        adc_select_input(1);
        bufh[idxh] = adc_read(); idxh = (idxh + 1) % JANELA;
        int mh = bufh[0] + bufh[1] + bufh[2];
        bool new_ph; int ch = converter_adc_para_mouse(mh / JANELA, &new_ph);
        if (!new_ph || new_ph != ph) {
            uint8_t cmd = ch < 0 ? 'A' : (ch > 0 ? 'S' : 0);
            if (cmd) { putchar_raw(0xC0); putchar_raw(0); putchar_raw(cmd); putchar_raw(0xCF); }
        }
        ph = new_ph;
        select_mux_channel(1);
        adc_select_input(1);
        bufv[idxv] = adc_read(); idxv = (idxv + 1) % JANELA;
        int mv = bufv[0] + bufv[1] + bufv[2];
        bool new_pv; int cv = converter_adc_para_mouse(mv / JANELA, &new_pv);
        if (!new_pv || new_pv != pv) {
            uint8_t cmd = cv < 0 ? 'A' : (cv > 0 ? 'S' : 0);
            if (cmd) { putchar_raw(0xC0); putchar_raw(1); putchar_raw(cmd); putchar_raw(0xCF); }
        }
        pv = new_pv;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void uart_task(void *p) {
    (void)p;
    adc_t pkt;
    while (1) {
        if (xQueueReceive(xQueueADC, &pkt, portMAX_DELAY)) {
            putchar_raw(0xA0);
            putchar_raw((uint8_t)pkt.axis);
            putchar_raw((pkt.val >> 8) & 0xFF);
            putchar_raw(pkt.val & 0xFF);
            putchar_raw(0xFF);
        }
    }
}

static void botao_task(void *p) {
    (void)p;
    botao_evento_t ev;
    uint32_t last_space=0, last_shift=0;
    while (1) {
        if (xQueueReceive(xQueueBotoes, &ev, portMAX_DELAY)) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if (ev.codigo==' ' && now-last_space<DEBOUNCE_MS) continue;
            if (ev.codigo==2   && now-last_shift<DEBOUNCE_MS) continue;
            if (ev.codigo==' ') last_space=now;
            if (ev.codigo==2)   last_shift=now;
            putchar_raw(0xB0); putchar_raw(ev.codigo); putchar_raw(ev.pressionado?1:0); putchar_raw(0xFE);
            if (ev.codigo==1 && ev.pressionado) { uint8_t one=1; xQueueSend(xQueueBuzzer,&one,0); }
        }
    }
}

static void power_task(void *p) {
    (void)p;
    bool enabled=false;
    while (1) {
        if (xSemaphoreTake(xSemEnable,portMAX_DELAY)==pdTRUE) {
            static uint32_t last_toggle=0; uint32_t now=to_ms_since_boot(get_absolute_time());
            if (now-last_toggle<5000) continue; last_toggle=now;
            if (!enabled) {
                gpio_put(LED_PIN,1);
                xQueueReset(xQueueADC); xQueueReset(xQueueBotoes);
                vTaskResume(xHandleX); vTaskResume(xHandleY); vTaskResume(xHandleDir);
                vTaskResume(xHandleUART); vTaskResume(xHandleBotao);
            } else {
                gpio_put(LED_PIN,0);
                vTaskSuspend(xHandleX); vTaskSuspend(xHandleY); vTaskSuspend(xHandleDir);
                vTaskSuspend(xHandleUART); vTaskSuspend(xHandleBotao);
                xQueueReset(xQueueADC); xQueueReset(xQueueBotoes);
            }
            enabled=!enabled;
        }
    }
}

int main() {
    stdio_init_all();
    adc_init();
    xQueueADC    = xQueueCreate(32, sizeof(adc_t));
    xQueueBotoes = xQueueCreate(32, sizeof(botao_evento_t));
    xQueueBuzzer = xQueueCreate(8,  sizeof(uint8_t));
    xSemEnable   = xSemaphoreCreateBinary();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    gpio_init(11);
    gpio_set_dir(11, GPIO_OUT);
    gpio_init(12);
    gpio_set_dir(12, GPIO_OUT);
    gpio_init(13);
    gpio_set_dir(13, GPIO_OUT);

    gpio_init(ENABLE_BUTTON_PIN);
    gpio_set_dir(ENABLE_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(ENABLE_BUTTON_PIN);
    gpio_set_irq_enabled_with_callback(
        ENABLE_BUTTON_PIN,
        GPIO_IRQ_EDGE_FALL,
        true,
        gpio_callback
    );

    uint button_pins[5] = {16,17,18,19,20};
    for (int i = 0; i < 5; i++) {
        gpio_init(button_pins[i]);
        gpio_set_dir(button_pins[i], GPIO_IN);
        gpio_pull_up(button_pins[i]);
        gpio_set_irq_enabled(button_pins[i], GPIO_IRQ_EDGE_FALL, true);
    }

    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);

    xTaskCreate(x_task,           "X Task",    2048, NULL, 1, &xHandleX);
    xTaskCreate(y_task,           "Y Task",    2048, NULL, 1, &xHandleY);
    xTaskCreate(direcional_task,  "Dir Task",  2048, NULL, 1, &xHandleDir);
    xTaskCreate(uart_task,        "UART Task", 2048, NULL, 1, &xHandleUART);
    xTaskCreate(botao_task,       "Botao Task",2048, NULL, 1, &xHandleBotao);
    xTaskCreate(buzzer_task,      "Buzzer",    1024, NULL, 2, &xHandleBuzzer);
    xTaskCreate(power_task,       "Power",     1024, NULL, 3, &xHandlePower);

    vTaskSuspend(xHandleX);
    vTaskSuspend(xHandleY);
    vTaskSuspend(xHandleDir);
    vTaskSuspend(xHandleUART);
    vTaskSuspend(xHandleBotao);

    vTaskStartScheduler();
    while (1) tight_loop_contents();
    return 0;
}
