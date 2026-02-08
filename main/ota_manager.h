#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"
#include "esp_app_desc.h"

// Funciones públicas

void ota_init(void);

/**
 * @brief Tarea FreeRTOS que ejecuta el proceso completo de actualización OTA
 * 
 * Esta tarea realiza todo el proceso OTA:
 * 1. Espera un tiempo antes de iniciar
 * 2. Configura la conexión HTTPS al servidor
 * 3. Inicia la descarga del firmware
 * 4. Valida el header del nuevo firmware
 * 5. Descarga e instala el firmware completo
 * 6. Verifica que se recibieron todos los datos
 * 7. Valida la imagen completa
 * 8. Reinicia el dispositivo con el nuevo firmware
 * 
 * @param pvParameter Parámetro de la tarea (no utilizado)
 * 
 * @note Esta tarea se auto-elimina al finalizar (éxito o error)
 */
void ota_task(void *pvParameter);

/**
 * @brief Valida el header de la nueva imagen de firmware
 * 
 * Esta función realiza varias verificaciones de seguridad:
 * 1. Verifica que la versión sea diferente (evita actualizar con la misma versión)
 * 2. Verifica la versión de seguridad (previene downgrades maliciosos)
 * 3. Compara con el firmware actualmente en ejecución
 * 
 * @param new_app_info Puntero al descriptor de la nueva aplicación
 * @return ESP_OK si la imagen es válida, ESP_FAIL o ESP_ERR_INVALID_ARG si no
 * 
 * @note Esta función es crítica para la seguridad del sistema
 */
esp_err_t ota_validate_image_header(esp_app_desc_t *new_app_info);

#endif // OTA_MANAGER_H
