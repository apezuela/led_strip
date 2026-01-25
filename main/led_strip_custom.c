
/* 
 * ============================================================================
 * ESP32 LED STRIP CON ACTUALIZACIONES OTA (Over-The-Air)
 * ============================================================================
 * 
 * Descripción:
 * Este programa controla una tira de LEDs addressable (WS2812B/NeoPixel) y
 * permite actualizaciones de firmware remotas mediante OTA sobre HTTPS.
 * 
 * Características principales:
 * - Control de 5 LEDs RGB con diferentes colores
 * - Conexión WiFi automática con reintentos
 * - Actualización OTA segura con validación de firmware
 * - Indicadores visuales LED para cada estado del sistema
 * - Soporte para rollback en caso de firmware defectuoso
 * 
 * Licencia: Public Domain / CC0
 * ============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

// ==================== INCLUDES DE FREERTOS ====================
#include "freertos/FreeRTOS.h"      // Sistema operativo en tiempo real
#include "freertos/task.h"          // Gestión de tareas
#include "freertos/event_groups.h"  // Sincronización mediante bits de eventos

// ==================== INCLUDES DE ESP-IDF ====================
#include "esp_system.h"             // Funciones del sistema ESP32
#include "esp_event.h"              // Sistema de eventos
#include "esp_log.h"                // Sistema de logging
#include "esp_check.h"              // Macros de verificación de errores
#include "esp_http_client.h"        // Cliente HTTP/HTTPS
#include "nvs.h"                    // Non-Volatile Storage (almacenamiento flash)
#include "nvs_flash.h"              // Inicialización de NVS
#include "esp_netif.h"              // Interfaz de red

// ==================== INCLUDES DE OTA ====================
#include "esp_ota_ops.h"            // Operaciones de actualización OTA
#include "esp_https_ota.h"          // OTA sobre HTTPS

// ==================== INCLUDES DE WIFI ====================
#include "esp_wifi.h"               // API de WiFi

// ==================== INCLUDES DE PERIFÉRICOS ====================
#include "driver/gpio.h"            // Control de pines GPIO
#include "led_strip.h"              // Driver para tiras LED addressable
#include "sdkconfig.h"              // Configuración del proyecto

// ============================================================================
// CONFIGURACIÓN DE WIFI
// ============================================================================
#define WIFI_SSID  CONFIG_WIFI_SSID  //  Nombre de la red WiFi
#define WIFI_PASS  CONFIG_WIFI_PASS  //  Contraseña de la red WiFi
#define MAXIMUM_RETRY  5            // Número máximo de intentos de reconexión

// ============================================================================
// CONFIGURACIÓN DE OTA
// ============================================================================
// URL donde se encuentra el nuevo firmware (.bin)
// Puede configurarse en menuconfig o directamente aquí
#ifndef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL
#define FIRMWARE_UPGRADE_URL "https://tu-servidor.com/firmware.bin"
#else
#define FIRMWARE_UPGRADE_URL CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL
#endif

// ============================================================================
// CONFIGURACIÓN DE LEDS
// ============================================================================
#define BLINK_GPIO CONFIG_BLINK_GPIO  // Pin GPIO para la tira LED (configurado en menuconfig)
#define NUM_LEDS 5                    // Cantidad de LEDs en la tira

// ============================================================================
// DEFINICIÓN DE COLORES RGB
// ============================================================================
// Los valores están en formato GRB (Green-Red-Blue) para WS2812B
// AZUL - Indica proceso OTA en progreso
#define BLUE_R  184
#define BLUE_G  179
#define BLUE_B  44

// ROJO - Indica error o fallo en la operación
#define RED_R   184
#define RED_G   44
#define RED_B   181

// VERDE - Indica éxito en la operación
#define GREEN_R  0
#define GREEN_G  245
#define GREEN_B  210

// NARANJA - Indica reconexión WiFi
#define ORANGE_R  0
#define ORANGE_G  245
#define ORANGE_B  210

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================
static const char *TAG = "LED_OTA_APP";  // Tag para mensajes de log

// Certificado CA del servidor HTTPS para verificar la conexión segura
// Estos símbolos son generados por el sistema de build desde el archivo ca_cert.pem
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

// Handle (manejador) de la tira LED
static led_strip_handle_t led_strip;

// Event group para sincronizar la conexión WiFi
static EventGroupHandle_t s_wifi_event_group;

// Bit que indica que WiFi está conectado
static const int WIFI_CONNECTED_BIT = BIT0;

// Contador de reintentos de conexión WiFi
static int s_retry_num = 0;

// Contador global para el parpadeo durante escritura flash en OTA
static int ota_flash_write_count = 0;


// ============================================================================
// FUNCIONES DE CONTROL DE LEDS
// ============================================================================

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
static void set_all_leds(uint8_t r, uint8_t g, uint8_t b)
{
    // Iterar sobre todos los LEDs de la tira
    for (int i = 0; i < NUM_LEDS; i++) {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }
    // Enviar los datos a la tira LED para que se muestren los cambios
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
static void blink_led(void)
{
    // ROJO durante 5 segundos
    set_all_leds(RED_R, RED_G, RED_B);
    vTaskDelay(pdMS_TO_TICKS(5000));  // pdMS_TO_TICKS convierte ms a ticks del sistema

    // AZUL durante 5 segundos
    set_all_leds(BLUE_R, BLUE_G, BLUE_B);
    vTaskDelay(pdMS_TO_TICKS(5000));

    // VERDE durante 5 segundos
    set_all_leds(GREEN_R, GREEN_G, GREEN_B);
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/**
 * @brief Configura e inicializa la tira LED addressable
 * 
 * Inicializa el driver RMT (Remote Control) para controlar la tira LED.
 * El protocolo RMT permite generar las señales de timing precisas que
 * requieren los LEDs WS2812B.
 * 
 * @note Esta función debe llamarse antes de usar cualquier función de LED
 */
static void configure_led(void)
{
    ESP_LOGI(TAG, "Configurando LED addressable en GPIO %d", BLINK_GPIO);
    
    // Configuración de la tira LED
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,   // Pin GPIO a utilizar
        .max_leds = NUM_LEDS,           // Número máximo de LEDs
    };

    // Configuración del periférico RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz - Resolución del timer
        .flags.with_dma = false,            // No usar DMA (para tiras pequeñas no es necesario)
    };
    
    // Crear el dispositivo LED con las configuraciones
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    // Limpiar (apagar) todos los LEDs al inicio
    led_strip_clear(led_strip);
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
void led_strip_task(void *pvParameter)
{
    // Configurar los LEDs antes de entrar al loop
    //configure_led();

    // Loop infinito de la tarea
    while (1) {
        blink_led();  // Ejecutar secuencia de parpadeo
    }
}



// ============================================================================
// FUNCIONES DE WIFI
// ============================================================================

/**
 * @brief Manejador de eventos WiFi e IP
 * 
 * Esta función callback se ejecuta cuando ocurren eventos relacionados
 * con WiFi o asignación de IP. Gestiona:
 * - Inicio de conexión WiFi
 * - Reintentos en caso de desconexión
 * - Indicación visual mediante LEDs del estado de conexión
 * - Obtención de dirección IP
 * 
 * @param arg Argumento personalizado (no utilizado)
 * @param event_base Base del evento (WIFI_EVENT o IP_EVENT)
 * @param event_id ID específico del evento
 * @param event_data Datos adicionales del evento
 */
static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    // ===== EVENTOS DE WIFI =====
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // WiFi iniciado, intentar conectar
        esp_wifi_connect();
        ESP_LOGI(TAG, "Iniciando conexión WiFi...");
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // WiFi desconectado, intentar reconectar
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Reintento %d de conexión WiFi", s_retry_num);
            // LED naranja durante reconexión
            set_all_leds(ORANGE_R, ORANGE_G, ORANGE_B);
        } else {
            // Se agotaron los reintentos
            ESP_LOGE(TAG, "Fallo al conectar a WiFi");
            // LED rojo fijo si falla completamente
            set_all_leds(RED_R, RED_G, RED_B);
        }
        
    // ===== EVENTOS DE IP =====
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Se obtuvo dirección IP - conexión exitosa
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP obtenida: " IPSTR, IP2STR(&event->ip_info.ip));
        
        // Resetear contador de reintentos
        s_retry_num = 0;
        
        // Señalar que WiFi está conectado mediante el event group
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        // LED verde al conectar exitosamente
        set_all_leds(GREEN_R, GREEN_G, GREEN_B);
        vTaskDelay(pdMS_TO_TICKS(2000));  // Mostrar verde 2 segundos
    }
}

/**
 * @brief Inicializa WiFi en modo Station (cliente)
 * 
 * Configura y arranca el WiFi para conectarse a un Access Point.
 * Esta función:
 * 1. Crea el event group para sincronización
 * 2. Inicializa la pila TCP/IP
 * 3. Configura los parámetros WiFi (SSID, password)
 * 4. Registra los manejadores de eventos
 * 5. Inicia la conexión
 * 6. Espera hasta obtener IP
 * 7. Desactiva el ahorro de energía para mejor rendimiento OTA
 * 
 * @note Esta función es bloqueante hasta que se obtiene IP o falla la conexión
 */
static void wifi_init_sta(void)
{
    // Crear event group para señalización entre tareas
    s_wifi_event_group = xEventGroupCreate();

    // Inicializar la pila TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Crear el loop de eventos por defecto
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Crear interfaz de red WiFi en modo estación
    esp_netif_create_default_wifi_sta();

    // Configuración WiFi con valores por defecto
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Variables para almacenar las instancias de los manejadores de eventos
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    
    // Registrar manejador para TODOS los eventos WiFi
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    
    // Registrar manejador específico para obtención de IP
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // Configuración de credenciales WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,                      // Nombre de la red
            .password = WIFI_PASS,                  // Contraseña
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Modo de autenticación mínimo
        },
    };
    
    // Configurar WiFi en modo estación (cliente)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // Aplicar la configuración
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Iniciar WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Inicialización WiFi completada.");
    
    // Esperar de forma bloqueante hasta que se conecte WiFi
    // portMAX_DELAY = esperar indefinidamente
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,  // No limpiar el bit al leer
                                           pdFALSE,  // No esperar todos los bits
                                           portMAX_DELAY);

    // Verificar si se obtuvo conexión
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado a WiFi SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "No se pudo conectar a WiFi");
    }

    // Desactivar ahorro de energía WiFi para mejor rendimiento durante OTA
    // Esto previene desconexiones durante la descarga del firmware
    esp_wifi_set_ps(WIFI_PS_NONE);
}
// ============================================================================
// FUNCIONES OTA (Over-The-Air Update)
// ============================================================================

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
    // Verificar que el evento es de tipo OTA
    if (event_base == ESP_HTTPS_OTA_EVENT) {
        switch (event_id) {
            case ESP_HTTPS_OTA_START:
                // OTA iniciado
                ESP_LOGI(TAG, "OTA iniciado");
                ota_flash_write_count = 0;  // Resetear contador de escrituras
                set_all_leds(BLUE_R, BLUE_G, BLUE_B);  // Azul = OTA en progreso
                break;
                
            case ESP_HTTPS_OTA_CONNECTED:
                // Conexión establecida con el servidor OTA
                ESP_LOGI(TAG, "Conectado al servidor OTA");
                break;
                
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                // Leyendo el descriptor de imagen del firmware
                ESP_LOGI(TAG, "Leyendo descripción de imagen");
                break;
                
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                // Verificando que el firmware es para este modelo de chip
                ESP_LOGI(TAG, "Verificando chip ID: %d", *(esp_chip_id_t *)event_data);
                break;
                
            case ESP_HTTPS_OTA_WRITE_FLASH:
            {
                // Escribiendo datos en la memoria flash
                // Este evento ocurre MUCHAS veces durante la actualización
                int bytes_written = *(int *)event_data;
                ESP_LOGD(TAG, "Escribiendo en flash: %d bytes", bytes_written);
                
                // Parpadeo para indicar progreso visualmente
                // Cada 10 escrituras cambia el estado del LED
                ota_flash_write_count++;
                if (ota_flash_write_count % 10 == 0) {
                    // Encender LED azul
                    set_all_leds(BLUE_R, BLUE_G, BLUE_B);
                } else if (ota_flash_write_count % 10 == 5) {
                    // Apagar LED a mitad del ciclo para crear parpadeo visible
                    led_strip_clear(led_strip);
                    led_strip_refresh(led_strip);
                }
                break;
            }
                
            case ESP_HTTPS_OTA_FINISH:
                // OTA finalizado exitosamente
                ESP_LOGI(TAG, "OTA finalizado exitosamente");
                set_all_leds(GREEN_R, GREEN_G, GREEN_B);  // Verde = éxito
                vTaskDelay(pdMS_TO_TICKS(2000));  // Mostrar verde 2 segundos
                break;
                
            case ESP_HTTPS_OTA_ABORT:
                // OTA abortado o fallido
                ESP_LOGE(TAG, "OTA abortado");
                set_all_leds(RED_R, RED_G, RED_B);  // Rojo = error
                break;
                
            default:
                // Evento OTA desconocido (para debug)
                ESP_LOGW(TAG, "Evento OTA desconocido: %ld", event_id);
                break;
        }
    }
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
static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    // Verificar que el puntero no sea nulo
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Obtener información de la partición actualmente en ejecución
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    
    // Leer la descripción del firmware actual
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Versión de firmware actual: %s", running_app_info.version);
    }

    ESP_LOGI(TAG, "Nueva versión de firmware: %s", new_app_info->version);

#ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
    // Verificar que la versión sea diferente
    // Evita desperdiciar tiempo actualizando con la misma versión
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        ESP_LOGW(TAG, "La versión actual es la misma que la nueva. No se continuará la actualización.");
        return ESP_FAIL;
    }
#endif

#ifdef CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    // Verificación de anti-rollback (seguridad)
    // Previene que se instale una versión antigua con vulnerabilidades conocidas
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
void advanced_ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Iniciando tarea OTA");

    // Esperar 10 segundos antes de iniciar OTA
    // Da tiempo para que el sistema se estabilice y se conecte a WiFi
    vTaskDelay(pdMS_TO_TICKS(10000));

    esp_err_t err;
    esp_err_t ota_finish_err = ESP_OK;
    
    // Configuración del cliente HTTP para la descarga
    esp_http_client_config_t config = {
        .url = FIRMWARE_UPGRADE_URL,                // URL del firmware
        .cert_pem = (char *)server_cert_pem_start,  // Certificado CA para HTTPS
        .timeout_ms = 5000,                         // Timeout de 5 segundos
        .keep_alive_enable = true,                  // Mantener conexión viva
    };

    // Configuración específica de OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    // Handle para el proceso OTA
    esp_https_ota_handle_t https_ota_handle = NULL;
    
    // ==== FASE 1: Iniciar OTA ====
    err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin falló");
        set_all_leds(RED_R, RED_G, RED_B);
        vTaskDelete(NULL);  // Eliminar esta tarea
    }

    // ==== FASE 2: Obtener y validar descriptor de imagen ====
    esp_app_desc_t app_desc = {};
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc falló");
        goto ota_end;  // Ir a limpieza
    }
    
    // Validar el header del firmware (versión, compatibilidad, etc.)
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Verificación del header de imagen falló");
        goto ota_end;
    }

    // ==== FASE 3: Descargar e instalar firmware ====
    // Este loop descarga y escribe el firmware en bloques
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        
        // Si retorna algo diferente de "EN PROGRESO", salir del loop
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        
        // Log de progreso (debug)
        const size_t len = esp_https_ota_get_image_len_read(https_ota_handle);
        ESP_LOGD(TAG, "Bytes de imagen leídos: %d", len);
    }

    // ==== FASE 4: Verificar y finalizar ====
    // Verificar que se recibieron TODOS los datos
    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        ESP_LOGE(TAG, "No se recibieron los datos completos.");
        set_all_leds(RED_R, RED_G, RED_B);
    } else {
        // Finalizar el proceso OTA
        // Esto valida la imagen completa y la marca como booteable
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            // ¡ÉXITO! OTA completado
            ESP_LOGI(TAG, "Actualización OTA exitosa. Reiniciando...");
            set_all_leds(GREEN_R, GREEN_G, GREEN_B);
            vTaskDelay(pdMS_TO_TICKS(2000));  // Mostrar éxito 2 segundos
            esp_restart();  // Reiniciar con el nuevo firmware
        } else {
            // Error en la finalización
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Validación de imagen falló, imagen corrupta");
            }
            ESP_LOGE(TAG, "Actualización OTA falló 0x%x", ota_finish_err);
            set_all_leds(RED_R, RED_G, RED_B);
            vTaskDelete(NULL);
        }
    }

ota_end:
    // ==== LIMPIEZA EN CASO DE ERROR ====
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "Actualización OTA falló");
    set_all_leds(RED_R, RED_G, RED_B);
    vTaskDelete(NULL);  // Eliminar esta tarea
}


// ============================================================================
// FUNCIÓN PRINCIPAL
// ============================================================================

/**
 * @brief Punto de entrada principal de la aplicación
 * 
 * Esta función inicializa todos los componentes del sistema:
 * 1. Muestra información del firmware actual
 * 2. Inicializa NVS (necesario para WiFi)
 * 3. Registra manejadores de eventos OTA
 * 4. Conecta a WiFi
 * 5. Valida el firmware actual (si hay rollback habilitado)
 * 6. Crea las tareas de FreeRTOS
 * 
 * @note Esta función se ejecuta automáticamente al iniciar el ESP32
 * @note Después de crear las tareas, esta función retorna y el scheduler
 *       de FreeRTOS toma el control
 */
void app_main(void)
{
    // ========================================================================
    // BANNER DE INICIO
    // ========================================================================
    ESP_LOGI(TAG, "=== Iniciando aplicación LED con OTA ===");

    // ========================================================================
    // OBTENER Y MOSTRAR INFORMACIÓN DEL FIRMWARE ACTUAL
    // ========================================================================
    // Esto es útil para debugging y para saber qué versión está corriendo
    
    // Obtener la partición que está actualmente en ejecución
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    
    // Leer la descripción de la aplicación desde la partición
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        // Mostrar versión del firmware
        // La versión se define en el archivo project_description.txt o CMakeLists.txt
        ESP_LOGI(TAG, "Versión actual: %s", running_app_info.version);
        
        // Mostrar fecha y hora de compilación
        // Útil para identificar exactamente cuándo se compiló este firmware
        ESP_LOGI(TAG, "Fecha de compilación: %s %s", 
                 running_app_info.date, running_app_info.time);
    }

    // ========================================================================
    // INICIALIZAR NVS (Non-Volatile Storage)
    // ========================================================================
    // NVS es necesario para:
    // - Almacenar configuración WiFi
    // - Guardar datos de calibración PHY
    // - Otros datos persistentes del sistema
    
    esp_err_t ret = nvs_flash_init();
    
    // Verificar si NVS necesita ser borrado
    // Esto puede ocurrir si:
    // - No hay espacio libre en NVS
    // - Se detecta una nueva versión del formato NVS
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Borrar NVS y reintentar inicialización
        ESP_LOGI(TAG, "Borrando NVS debido a: %s", 
                 ret == ESP_ERR_NVS_NO_FREE_PAGES ? "sin espacio" : "nueva versión");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    // Verificar que NVS se inicializó correctamente
    // Si falla aquí, el programa se detiene (ESP_ERROR_CHECK)
    ESP_ERROR_CHECK(ret);

	// ========================================================================
	// *** IMPORTANTE: INICIALIZAR LEDs ANTES DE WIFI ***
	// ========================================================================
	ESP_LOGI(TAG, "Inicializando tira LED...");
	configure_led();
	ESP_LOGI(TAG, "LEDs inicializados correctamente");


    // ========================================================================
    // INICIALIZAR Y CONECTAR WIFI
    // ========================================================================
    // Esta función es bloqueante y esperará hasta que:
    // - Se obtenga una dirección IP exitosamente, o
    // - Se agoten los reintentos de conexión
   
    ESP_LOGI(TAG, "Iniciando conexión WiFi...");
    wifi_init_sta();
    ESP_LOGI(TAG, "WiFi inicializado y conectado");
	
	// ========================================================================
	// REGISTRAR MANEJADOR DE EVENTOS OTA
	// ========================================================================
	// Este handler recibirá notificaciones de todos los eventos OTA
	// y proporcionará retroalimentación visual mediante los LEDs

	ESP_ERROR_CHECK(esp_event_handler_register(
	    ESP_HTTPS_OTA_EVENT,      // Base de eventos OTA
	    ESP_EVENT_ANY_ID,         // Registrar para TODOS los eventos OTA
	    &ota_event_handler,       // Función callback
	    NULL                      // Sin argumentos personalizados
	));

	ESP_LOGI(TAG, "Manejador de eventos OTA registrado");
	
    // ========================================================================
    // MANEJO DE ROLLBACK (solo si está habilitado en menuconfig)
    // ========================================================================
    // El rollback permite revertir automáticamente a la versión anterior
    // si el nuevo firmware no se valida correctamente
    
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    
    // Verificar el estado de la imagen OTA actual
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        
        // Si la imagen está pendiente de verificación, significa que:
        // 1. Acabamos de actualizar a esta versión mediante OTA
        // 2. Es la primera vez que arranca
        // 3. Necesitamos confirmar que funciona correctamente
        
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            
            // Marcar la aplicación como válida
            // Esto previene que el bootloader haga rollback automático
            // en el siguiente reinicio
            
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                ESP_LOGI(TAG, "App válida, rollback cancelado exitosamente");
                
                // En este punto, el firmware se considera estable
                // Si el dispositivo se reinicia ahora, arrancará con esta versión
                
            } else {
                ESP_LOGE(TAG, "Fallo al cancelar rollback");
                
                // Si falla al marcar como válida, podría haber rollback
                // en el próximo reinicio
            }
        } else {
            // Estados posibles:
            // - ESP_OTA_IMG_VALID: Imagen ya validada previamente
            // - ESP_OTA_IMG_INVALID: Imagen marcada como inválida
            // - ESP_OTA_IMG_ABORTED: OTA fue abortado
            // - ESP_OTA_IMG_UNDEFINED: Estado no definido
            
            ESP_LOGI(TAG, "Estado de imagen OTA: %d", ota_state);
        }
    }
    
#endif // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE

    // ========================================================================
    // CREAR TAREAS DE FREERTOS
    // ========================================================================
    
    // ---- TAREA DE CONTROL DE LEDS ----
    // Esta tarea maneja la secuencia de parpadeo demostrativa
    // Normalmente se comenta en producción para que los LEDs solo
    // muestren estados del sistema (WiFi, OTA, etc.)
    
    ESP_LOGI(TAG, "Creando tarea de control de LEDs");
    xTaskCreate(
        led_strip_task,     // Función de la tarea
        "LED_STRIP",        // Nombre descriptivo (para debugging)
        4096,               // Tamaño del stack en bytes
        NULL,               // Parámetro pasado a la tarea
        3,                  // Prioridad (0-24, mayor = más prioritaria)
        NULL                // Handle de la tarea (no lo necesitamos)
    );

    // ---- TAREA OTA (OPCIONAL) ----
    // Esta tarea ejecuta el proceso de actualización OTA
    // 
    // IMPORTANTE: Descomentar esta línea solo cuando:
    // - Tienes un servidor con el firmware disponible
    // - La URL está correctamente configurada
    // - Quieres actualización automática al inicio
    //
    // Alternativamente, puedes:
    // - Crear esta tarea bajo demanda (botón, comando, etc.)
    // - Ejecutarla en intervalos periódicos
    // - Dispararla mediante MQTT, HTTP, etc.
    
    // xTaskCreate(
    //     &advanced_ota_task, // Función de la tarea OTA
    //     "OTA_Task",         // Nombre descriptivo
    //     1024 * 8,           // 8KB de stack (OTA necesita más memoria)
    //     NULL,               // Sin parámetros
    //     5,                  // Prioridad alta para OTA
    //     NULL                // Sin handle
    // );
    
    // NOTA: Si descomentas la tarea OTA, se ejecutará automáticamente
    // después de 10 segundos (ver advanced_ota_task)

    // ========================================================================
    // SISTEMA INICIADO
    // ========================================================================
    ESP_LOGI(TAG, "=== Sistema iniciado correctamente ===");
    
    // A partir de aquí, app_main() retorna y el scheduler de FreeRTOS
    // toma el control, ejecutando las tareas creadas según sus prioridades
    
    // Las tareas ejecutándose son:
    // 1. led_strip_task - Parpadeo de LEDs
    // 2. Tareas internas de WiFi y TCP/IP
    // 3. Tareas del sistema FreeRTOS (idle, timer, etc.)
    // 4. advanced_ota_task (si está descomentada)
    
    // FLUJO TÍPICO:
    // 1. Sistema arranca
    // 2. app_main() se ejecuta
    // 3. Se inicializa NVS, WiFi, etc.
    // 4. Se crean las tareas
    // 5. app_main() retorna
    // 6. Las tareas siguen ejecutándose indefinidamente
    // 7. Los LEDs parpadean continuamente
    // 8. El sistema queda esperando eventos (WiFi, OTA, etc.)
	
}

// ============================================================================
// FIN DEL CÓDIGO
// ============================================================================