/**
 * @file wifi_manager.c
 * @brief Implementación del gestor de WiFi
 * 
 * Gestiona la conexión WiFi en modo estación (cliente) con manejo
 * robusto de eventos, reintentos automáticos y retroalimentación visual.
 */

#include "wifi_manager.h"
#include "led_control.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

// ============================================================================
// DEFINICIONES Y CONFIGURACIÓN
// ============================================================================


static const char *TAG = "WIFI_MANAGER";

#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASS CONFIG_WIFI_PASS
#define MAXIMUM_RETRY 5

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static int s_retry_num = 0;

// ============================================================================
// FUNCIONES PRIVADAS (STATIC)
// ============================================================================

/**
 * @brief Manejador de eventos WiFi e IP
 * 
 * Esta función callback se ejecuta automáticamente cuando ocurren eventos
 * relacionados con WiFi o asignación de direcciones IP. Es el corazón
 * del sistema de gestión WiFi.
 * 
 * Eventos manejados:
 * 
 * 1. WIFI_EVENT_STA_START:
 *    - Se dispara cuando WiFi arranca en modo estación
 *    - Acción: Iniciar intento de conexión al AP
 * 
 * 2. WIFI_EVENT_STA_DISCONNECTED:
 *    - Se dispara cuando se pierde la conexión
 *    - Acción: Reintentar hasta MAXIMUM_RETRY veces
 *    - Feedback visual: LED naranja (reconectando) o rojo (falló)
 * 
 * 3. IP_EVENT_STA_GOT_IP:
 *    - Se dispara cuando DHCP asigna una IP
 *    - Acción: Señalar éxito mediante event group
 *    - Feedback visual: LED verde por 2 segundos
 * 
 * @param arg           Argumento personalizado (no utilizado)
 * @param event_base    Base del evento (WIFI_EVENT o IP_EVENT)
 * @param event_id      ID específico del evento
 * @param event_data    Datos adicionales del evento (struct específica según evento)
 * 
 * @note Esta función se ejecuta en el contexto del loop de eventos,
 *       NO debe hacer operaciones bloqueantes pesadas
 */

static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Iniciando conexión WiFi...");
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Reintento %d de conexión WiFi", s_retry_num);
            led_set_color_orange();
        } else {
            ESP_LOGE(TAG, "Fallo al conectar a WiFi");
            led_set_color_red();
        }
        
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP obtenida: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        led_set_color_green();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ============================================================================
// FUNCIONES PÚBLICAS
// ============================================================================

/**
 * @brief Inicializa WiFi en modo Station (cliente) y espera conexión
 * 
 * FLUJO COMPLETO DE INICIALIZACIÓN:
 * 
 * 1. PREPARACIÓN:
 *    - Crea event group para sincronización entre eventos
 *    - Inicializa la pila TCP/IP (esp_netif)
 *    - Crea interfaz de red WiFi en modo estación
 * 
 * 2. CONFIGURACIÓN:
 *    - Inicializa driver WiFi con configuración por defecto
 *    - Registra manejadores para eventos WiFi e IP
 *    - Configura SSID, password y modo de autenticación
 * 
 * 3. INICIO:
 *    - Activa WiFi en modo estación
 *    - Espera BLOQUEANTE hasta obtener IP o fallar
 * 
 * 4. POST-CONEXIÓN:
 *    - Desactiva ahorro de energía WiFi
 *    - Mejora rendimiento y estabilidad para OTA
 * 
 * ESTADOS VISUALES (mediante LEDs):
 * - Naranja: Intentando conectar/reconectar
 * - Rojo: Falló completamente
 * - Verde: Conectado exitosamente
 * 
 * COMPORTAMIENTO DE BLOQUEO:
 * La función NO retorna hasta que:
 * - WiFi se conecta exitosamente (obtiene IP), O
 * - Se agotan los reintentos (queda esperando indefinidamente)
 * 
 * @warning Asegúrate de haber inicializado los LEDs antes de llamar
 *          esta función, ya que usa led_set_color_*() para feedback visual
 * 
 * @warning Si la conexión falla completamente, esta función
 *          quedará bloqueada esperando. Considera implementar
 *          un timeout si necesitas continuar la ejecución.
 * 
 * REQUISITOS PREVIOS:
 * - NVS debe estar inicializado (nvs_flash_init)
 * - LEDs deben estar inicializados (led_control_init)
 * - WIFI_SSID y WIFI_PASS configurados en menuconfig
 * 
 * Ejemplo de uso:
 * @code
 * // En app_main():
 * nvs_flash_init();          // 1. Inicializar NVS
 * led_control_init();        // 2. Inicializar LEDs
 * wifi_init_sta();           // 3. Conectar WiFi (bloqueante)
 * // Aquí WiFi ya está conectado
 * ota_init();                // 4. Ahora se puede usar OTA
 * @endcode
 */

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Inicialización WiFi completada.");
    
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado a WiFi SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "No se pudo conectar a WiFi");
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
}
