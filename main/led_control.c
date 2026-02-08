#include "led_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

static const char *TAG = "LED_CONTROL";
static led_strip_handle_t led_strip;

#define BLINK_GPIO CONFIG_BLINK_GPIO
#define NUM_LEDS 5

/**
 * @brief Configura e inicializa la tira LED addressable
 * 
 * Inicializa el driver RMT (Remote Control) para controlar la tira LED.
 * El protocolo RMT permite generar las señales de timing precisas que
 * requieren los LEDs WS2812B.
 * 
 * @note Esta función debe llamarse antes de usar cualquier función de LED
 */
void led_control_init(void)
{
    ESP_LOGI(TAG, "Configurando LED addressable en GPIO %d", BLINK_GPIO);
    
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = NUM_LEDS,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

/**
 * @brief Establece el mismo color en todos los LEDs de la tira
 * 
 * Esta función configura todos los LEDs al color especificado y
 * actualiza inmediatamente la tira para mostrar el cambio.
 * 
 * @param r Componente rojo (0-255)
 * @param g Componente verde (0-255)
 * @param b Componente azul (0-255)
 * 
 * @note Los LEDs WS2812B usan formato GRB internamente, pero esta
 *       función acepta RGB y el driver realiza la conversión
 */
void led_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }
    led_strip_refresh(led_strip);
}

/**
 * @brief Funciones helper para colores predefinidos
 */
void led_set_color_blue(void)   { led_set_all(BLUE_R, BLUE_G, BLUE_B); }
void led_set_color_red(void)    { led_set_all(RED_R, RED_G, RED_B); }
void led_set_color_green(void)  { led_set_all(GREEN_R, GREEN_G, GREEN_B); }
void led_set_color_orange(void) { led_set_all(ORANGE_R, ORANGE_G, ORANGE_B); }

/**
 * @brief Apaga todos los LEDs
 */
void led_clear(void)
{
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
}

/**
 * @brief Secuencia de parpadeo demostrativa de los LEDs
 * 
 * Ciclo de 3 colores:
 * - ROJO durante 5 segundos
 * - AZUL durante 5 segundos
 * - VERDE durante 5 segundos
 * 
 * @note Esta función es bloqueante (usa vTaskDelay)
 */
void led_blink_sequence(void)
{
    led_set_color_red();
    vTaskDelay(pdMS_TO_TICKS(5000));

    led_set_color_blue();
    vTaskDelay(pdMS_TO_TICKS(5000));

    led_set_color_green();
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/**
 * @brief Tarea FreeRTOS que ejecuta la secuencia de parpadeo LED
 * 
 * Esta tarea se ejecuta continuamente mostrando la secuencia de colores.
 * Es útil para demostración o testing, pero normalmente se comenta en
 * producción para que los LEDs solo muestren estados del sistema.
 * 
 * @param pvParameter Parámetro de la tarea (no utilizado)
 */
void led_task(void *pvParameter)
{
    while (1) {
        led_blink_sequence();
    }
}
