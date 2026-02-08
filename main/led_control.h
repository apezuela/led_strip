#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <stdint.h>
#include "led_strip.h"

// Definición de colores RGB
#define BLUE_R  184
#define BLUE_G  179
#define BLUE_B  44

#define RED_R   184
#define RED_G   44
#define RED_B   181

#define GREEN_R  0
#define GREEN_G  245
#define GREEN_B  210

#define ORANGE_R  0
#define ORANGE_G  245
#define ORANGE_B  210

// Funciones públicas

/**
 * @brief Configura e inicializa la tira LED addressable
 * 
 * Inicializa el driver RMT (Remote Control) para controlar la tira LED.
 * El protocolo RMT permite generar las señales de timing precisas que
 * requieren los LEDs WS2812B.
 * 
 * @note Esta función debe llamarse antes de usar cualquier función de LED
 */
void led_control_init(void);

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
void led_set_all(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Funciones helper para colores predefinidos
 */
void led_set_color_blue(void);
void led_set_color_red(void);
void led_set_color_green(void);
void led_set_color_orange(void);

/**
 * @brief Apaga todos los LEDs
 */
void led_clear(void);

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
void led_blink_sequence(void);

/**
 * @brief Tarea FreeRTOS que ejecuta la secuencia de parpadeo LED
 * 
 * Esta tarea se ejecuta continuamente mostrando la secuencia de colores.
 * Es útil para demostración o testing, pero normalmente se comenta en
 * producción para que los LEDs solo muestren estados del sistema.
 * 
 * @param pvParameter Parámetro de la tarea (no utilizado)
 */
void led_task(void *pvParameter);

#endif // LED_CONTROL_H
