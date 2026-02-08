#include "ota_manager.h"
#include "led_control.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "OTA_MANAGER";
static int ota_flash_write_count = 0;

// Certificado CA
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

#ifndef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL
#define FIRMWARE_UPGRADE_URL "https://tu-servidor.com/firmware.bin"
#else
#define FIRMWARE_UPGRADE_URL CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL
#endif

/**
 * @brief Manejador de eventos del proceso OTA
 * 
 * Esta función callback se ejecuta en diferentes etapas del proceso OTA
 * y proporciona retroalimentación visual mediante los LEDs.
 * 
 * Eventos manejados:
 * - START: Inicio del proceso OTA
 * - CONNECTED: Conexión establecida con el servidor
 * - GET_IMG_DESC: Leyendo descripción del firmware
 * - VERIFY_CHIP_ID: Verificando compatibilidad del chip
 * - WRITE_FLASH: Escribiendo firmware en flash (ocurre múltiples veces)
 * - FINISH: OTA completado exitosamente
 * - ABORT: OTA cancelado o fallido
 * 
 * @param arg Argumento personalizado (no utilizado)
 * @param event_base Base del evento (ESP_HTTPS_OTA_EVENT)
 * @param event_id ID del evento específico
 * @param event_data Datos del evento (varía según el tipo)
 */
static void ota_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT) {
        switch (event_id) {
            case ESP_HTTPS_OTA_START:
                ESP_LOGI(TAG, "OTA iniciado");
                ota_flash_write_count = 0;
                led_set_color_blue();
                break;
                
            case ESP_HTTPS_OTA_CONNECTED:
                ESP_LOGI(TAG, "Conectado al servidor OTA");
                break;
                
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                ESP_LOGI(TAG, "Leyendo descripción de imagen");
                break;
                
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                ESP_LOGI(TAG, "Verificando chip ID: %d", *(esp_chip_id_t *)event_data);
                break;
                
            case ESP_HTTPS_OTA_WRITE_FLASH:
            {
                int bytes_written = *(int *)event_data;
                ESP_LOGD(TAG, "Escribiendo en flash: %d bytes", bytes_written);
                
                ota_flash_write_count++;
                if (ota_flash_write_count % 10 == 0) {
                    led_set_color_blue();
                } else if (ota_flash_write_count % 10 == 5) {
                    led_clear();
                }
                break;
            }
                
            case ESP_HTTPS_OTA_FINISH:
                ESP_LOGI(TAG, "OTA finalizado exitosamente");
                led_set_color_green();
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;
                
            case ESP_HTTPS_OTA_ABORT:
                ESP_LOGE(TAG, "OTA abortado");
                led_set_color_red();
                break;
                
            default:
                ESP_LOGW(TAG, "Evento OTA desconocido: %ld", event_id);
                break;
        }
    }
}

/**
 * @brief Inicializa el sistema OTA
 */
void ota_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(
        ESP_HTTPS_OTA_EVENT,
        ESP_EVENT_ANY_ID,
        &ota_event_handler,
        NULL
    ));
    
    ESP_LOGI(TAG, "Manejador de eventos OTA registrado");
}

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
esp_err_t ota_validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Versión de firmware actual: %s", running_app_info.version);
    }

    ESP_LOGI(TAG, "Nueva versión de firmware: %s", new_app_info->version);

#ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        ESP_LOGW(TAG, "La versión actual es la misma que la nueva. No se continuará la actualización.");
        return ESP_FAIL;
    }
#endif

#ifdef CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    const uint32_t hw_sec_version = esp_efuse_read_secure_version();
    if (new_app_info->secure_version < hw_sec_version) {
        ESP_LOGW(TAG, "La versión de seguridad del nuevo firmware es menor que la actual");
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}

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
void ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Iniciando tarea OTA");
    vTaskDelay(pdMS_TO_TICKS(10000));

    esp_err_t err;
    esp_err_t ota_finish_err = ESP_OK;
    
    esp_http_client_config_t config = {
        .url = FIRMWARE_UPGRADE_URL,
        .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = 5000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    
    err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin falló");
        led_set_color_red();
        vTaskDelete(NULL);
    }

    esp_app_desc_t app_desc = {};
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc falló");
        goto ota_end;
    }
    
    err = ota_validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Verificación del header de imagen falló");
        goto ota_end;
    }

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        const size_t len = esp_https_ota_get_image_len_read(https_ota_handle);
        ESP_LOGD(TAG, "Bytes de imagen leídos: %d", len);
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        ESP_LOGE(TAG, "No se recibieron los datos completos.");
        led_set_color_red();
    } else {
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            ESP_LOGI(TAG, "Actualización OTA exitosa. Reiniciando...");
            led_set_color_green();
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        } else {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Validación de imagen falló, imagen corrupta");
            }
            ESP_LOGE(TAG, "Actualización OTA falló 0x%x", ota_finish_err);
            led_set_color_red();
            vTaskDelete(NULL);
        }
    }

ota_end:
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "Actualización OTA falló");
    led_set_color_red();
    vTaskDelete(NULL);
}
