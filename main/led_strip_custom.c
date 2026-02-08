/**
 * @file main.c
 * @brief Aplicación principal - Control de LED Strip con actualización OTA
 * 
 * DESCRIPCIÓN GENERAL:
 * ===================
 * Este programa controla una tira de LEDs RGB addressable (WS2812B) y
 * permite actualizaciones remotas de firmware mediante OTA (Over-The-Air)
 * sobre conexión WiFi segura (HTTPS).
 * 
 * ARQUITECTURA DEL SISTEMA:
 * ========================
 * El código está organizado en módulos independientes:
 * 
 * - main.c:          Punto de entrada, inicialización y orquestación
 * - led_control:     Gestión de la tira LED y efectos visuales
 * - wifi_manager:    Conexión y mantenimiento de WiFi
 * - ota_manager:     Descarga e instalación de actualizaciones OTA
 * 
 * FLUJO DE EJECUCIÓN:
 * ==================
 * 1. Arranque del sistema ESP32
 * 2. app_main() se ejecuta automáticamente
 * 3. Inicialización secuencial de subsistemas
 * 4. Creación de tareas FreeRTOS
 * 5. app_main() retorna
 * 6. El scheduler de FreeRTOS toma control
 * 7. Las tareas se ejecutan concurrentemente según prioridades
 * 
 * CARACTERÍSTICAS:
 * ===============
 * ✓ Control de 5 LEDs RGB con retroalimentación visual de estados
 * ✓ Conexión WiFi automática con reintentos
 * ✓ Actualización OTA segura con validación de firmware
 * ✓ Soporte para rollback automático en caso de firmware defectuoso
 * ✓ Sistema operativo en tiempo real (FreeRTOS)
 * 
 * HARDWARE REQUERIDO:
 * ==================
 * - ESP32 (cualquier modelo)
 * - Tira LED WS2812B/NeoPixel (5 LEDs)
 * - Conexión WiFi disponible
 * - Alimentación adecuada para LEDs
 * 
 * CONFIGURACIÓN:
 * =============
 * Antes de compilar, configurar mediante menuconfig:
 * - WiFi SSID y Password
 * - GPIO para la tira LED
 * - URL del servidor OTA (opcional)
 * 
 * @author Tu Nombre
 * @version 1.0.0
 * @date 2025
 * @license Public Domain / CC0
 */

// ============================================================================
// INCLUDES DEL SISTEMA
// ============================================================================

#include <stdio.h>                  // Funciones estándar de C (printf, etc)

// --- FreeRTOS ---
#include "freertos/FreeRTOS.h"      // Sistema operativo en tiempo real
#include "freertos/task.h"          // Gestión de tareas concurrentes

// --- ESP-IDF Core ---
#include "esp_system.h"             // Funciones del sistema ESP32 (reinicio, etc)
#include "esp_log.h"                // Sistema de logging con niveles (INFO, ERROR, etc)
#include "nvs_flash.h"              // Almacenamiento no volátil (flash)

// --- OTA ---
#include "esp_ota_ops.h"            // Operaciones de actualización OTA

// ============================================================================
// INCLUDES DE MÓDULOS PROPIOS
// ============================================================================

#include "led_control.h"            // Control de tira LED
#include "wifi_manager.h"           // Gestión de WiFi
#include "ota_manager.h"            // Gestión de actualizaciones OTA

// ============================================================================
// DEFINICIONES Y CONFIGURACIÓN
// ============================================================================

/**
 * @brief Tag para identificar mensajes de log del módulo principal
 * 
 * Se usa en todas las llamadas ESP_LOGx() para filtrar mensajes.
 * Ejemplo: ESP_LOGI(TAG, "Sistema iniciado");
 */
static const char *TAG = "MAIN";

// ============================================================================
// FUNCIÓN PRINCIPAL DE LA APLICACIÓN
// ============================================================================

/**
 * @brief Punto de entrada principal de la aplicación ESP32
 * 
 * PROPÓSITO:
 * ==========
 * Esta función se ejecuta automáticamente cuando el ESP32 arranca.
 * Su responsabilidad es inicializar todos los subsistemas del proyecto
 * en el orden correcto y crear las tareas que se ejecutarán continuamente.
 * 
 * FASES DE INICIALIZACIÓN:
 * ========================
 * 
 * FASE 1: INFORMACIÓN DEL SISTEMA
 *   - Muestra versión del firmware actual
 *   - Muestra fecha y hora de compilación
 *   - Útil para debugging y trazabilidad
 * 
 * FASE 2: INICIALIZACIÓN NVS
 *   - Prepara el sistema de almacenamiento flash
 *   - Necesario para: WiFi, configuración, datos persistentes
 *   - Maneja errores comunes (sin espacio, nueva versión)
 * 
 * FASE 3: INICIALIZACIÓN DE PERIFÉRICOS
 *   - LEDs: Debe ser PRIMERO para dar feedback visual
 *   - WiFi: Conexión a red (bloqueante)
 *   - OTA: Registro de manejadores de eventos
 * 
 * FASE 4: VALIDACIÓN DE FIRMWARE (si rollback habilitado)
 *   - Verifica si arrancamos después de una actualización OTA
 *   - Marca el firmware como válido o invalida para rollback
 * 
 * FASE 5: CREACIÓN DE TAREAS FreeRTOS
 *   - Tarea de LEDs: Parpadeo continuo (demostración)
 *   - Tarea OTA: Actualización automática (opcional)
 * 
 * FLUJO POST app_main():
 * =====================
 * 1. app_main() retorna
 * 2. El scheduler de FreeRTOS comienza
 * 3. Las tareas creadas se ejecutan según sus prioridades
 * 4. El sistema queda en ejecución indefinida
 * 
 * DIAGRAMA DE DEPENDENCIAS:
 * ========================
 * 
 *     NVS
 *      ↓
 *    LEDs ← (feedback visual para todo)
 *      ↓
 *    WiFi → OTA
 *      ↓     ↓
 *    Tareas
 * 
 * NOTAS IMPORTANTES:
 * =================
 * - Esta función NO debe contener loops infinitos
 * - Debe completarse y retornar para que FreeRTOS tome control
 * - Los bloques try-catch no existen en C (usar ESP_ERROR_CHECK)
 * - Las tareas creadas se ejecutan en paralelo después del retorno
 * 
 * @warning NO llamar vTaskDelay() o loops infinitos aquí
 * @warning Si falla una inicialización, el sistema se detendrá (ESP_ERROR_CHECK)
 * 
 * @note Esta función se ejecuta en el contexto de la tarea "main"
 *       que tiene prioridad 1 y stack de 3584 bytes por defecto
 */
void app_main(void)
{
    // ========================================================================
    // FASE 1: BANNER DE INICIO Y DIAGNÓSTICO
    // ========================================================================
    
    // Banner visual en el monitor serial
    // Ayuda a identificar reinicios del sistema en los logs
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   INICIANDO APLICACIÓN LED STRIP CON OTA              ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");

    // ------------------------------------------------------------------------
    // OBTENER INFORMACIÓN DEL FIRMWARE ACTUAL
    // ------------------------------------------------------------------------
    
    /**
     * PROPÓSITO DE MOSTRAR VERSIÓN:
     * - Debugging: Saber qué versión está ejecutándose
     * - Trazabilidad: Relacionar comportamiento con versión específica
     * - Validación OTA: Confirmar que la actualización fue exitosa
     * - Soporte: Los usuarios pueden reportar la versión exacta
     */
    
    // Obtener puntero a la partición que está actualmente ejecutándose
    // El ESP32 tiene múltiples particiones: ota_0, ota_1, factory, etc.
    const esp_partition_t *running = esp_ota_get_running_partition();
    
    // Estructura que almacenará la descripción de la aplicación
    // Contiene: versión, nombre del proyecto, fecha, hora, IDF version, etc.
    esp_app_desc_t running_app_info;
    
    // Leer la descripción desde la partición actual
    // Esta información está embebida en el .bin durante la compilación
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        
        // Mostrar versión del firmware
        // La versión se define en:
        // - project_description.txt, O
        // - CMakeLists.txt con set(PROJECT_VER "x.y.z")
        ESP_LOGI(TAG, "📌 Versión actual: %s", running_app_info.version);
        
        // Mostrar fecha y hora de compilación
        // Formato: "MMM DD YYYY" "HH:MM:SS"
        // Ejemplo: "Feb 08 2025" "14:30:45"
        // ÚTIL: Diferencia builds hechos el mismo día
        ESP_LOGI(TAG, "🕐 Compilado: %s %s", 
                 running_app_info.date,      // __DATE__ del compilador
                 running_app_info.time);     // __TIME__ del compilador
        
        // INFORMACIÓN ADICIONAL DISPONIBLE (pero no mostrada):
        // - running_app_info.project_name
        // - running_app_info.idf_ver (versión de ESP-IDF)
        // - running_app_info.secure_version (para anti-rollback)
        
    } else {
        // Si falla, probablemente la partición está corrupta
        // Esto NO debería ocurrir nunca en condiciones normales
        ESP_LOGW(TAG, "⚠️  No se pudo leer la descripción del firmware");
    }

    // ========================================================================
    // FASE 2: INICIALIZACIÓN DE NVS (Non-Volatile Storage)
    // ========================================================================
    
    /**
     * ¿QUÉ ES NVS?
     * ===========
     * NVS (Non-Volatile Storage) es un sistema de almacenamiento clave-valor
     * en la memoria flash del ESP32. Es como una pequeña base de datos
     * que persiste después de reinicios y pérdidas de energía.
     * 
     * ¿PARA QUÉ SE USA?
     * ================
     * - WiFi: Guardar credenciales y configuración
     * - PHY: Datos de calibración de radio
     * - Bluetooth: Configuración y emparejamientos
     * - Aplicación: Cualquier dato que deba persistir
     * 
     * EJEMPLOS DE USO:
     * ===============
     * nvs_set_str(handle, "wifi_ssid", "MiRed");
     * nvs_set_i32(handle, "led_brightness", 128);
     * nvs_get_str(handle, "wifi_ssid", buffer, &length);
     * 
     * ESTRUCTURA EN FLASH:
     * ===================
     * La flash tiene una partición "nvs" definida en partitions.csv
     * Típicamente: nvs, data, 0x9000, 0x6000
     */
    
    ESP_LOGI(TAG, "Inicializando NVS...");
    
    // Intentar inicializar NVS
    esp_err_t ret = nvs_flash_init();
    
    // ------------------------------------------------------------------------
    // MANEJO DE ERRORES COMUNES DE NVS
    // ------------------------------------------------------------------------
    
    /**
     * ERROR 1: ESP_ERR_NVS_NO_FREE_PAGES
     * ==================================
     * Causa: La partición NVS está llena
     * Solución: Borrar NVS y reinicializar
     * Consecuencia: Se pierden todos los datos guardados
     * 
     * ERROR 2: ESP_ERR_NVS_NEW_VERSION_FOUND
     * ======================================
     * Causa: El formato de NVS cambió entre versiones de ESP-IDF
     * Solución: Borrar NVS con el formato antiguo
     * Consecuencia: Se pierden datos, pero es necesario para compatibilidad
     */
    
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        
        // Log explicativo del problema
        ESP_LOGW(TAG, "⚠️  NVS requiere borrado: %s", 
                 ret == ESP_ERR_NVS_NO_FREE_PAGES ? 
                 "Sin espacio libre" : 
                 "Nueva versión detectada");
        
        // Borrar completamente la partición NVS
        // ADVERTENCIA: Esto elimina:
        // - Credenciales WiFi guardadas
        // - Configuraciones de aplicación
        // - Datos de calibración (se recalibrarán)
        ESP_ERROR_CHECK(nvs_flash_erase());
        
        // Reintentar inicialización después del borrado
        ret = nvs_flash_init();
        
        ESP_LOGI(TAG, "✓ NVS reinicializado exitosamente");
    }
    
    // Verificar que NVS se inicializó correctamente
    // Si falla aquí, el dispositivo se detendrá (ESP_ERROR_CHECK)
    // Posibles causas de fallo:
    // - Hardware defectuoso (flash corrupta)
    // - Partición NVS mal configurada en partitions.csv
    // - Problema grave del sistema
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "✓ NVS inicializado correctamente");

    // ========================================================================
    // FASE 3: INICIALIZACIÓN DE PERIFÉRICOS Y CONECTIVIDAD
    // ========================================================================
    
    // ------------------------------------------------------------------------
    // SUBSISTEMA 1: CONTROL DE LEDs
    // ------------------------------------------------------------------------
    
    /**
     * ¿POR QUÉ INICIALIZAR LEDs PRIMERO?
     * ==================================
     * Los LEDs proporcionan retroalimentación visual inmediata sobre el
     * estado del sistema. Todos los demás módulos (WiFi, OTA) usan los
     * LEDs para indicar su estado, por lo que DEBEN estar listos primero.
     * 
     * CÓDIGO DE COLORES (definidos en led_control.h):
     * - 🟠 Naranja: Conectando/Reconectando WiFi
     * - 🔴 Rojo:    Error (WiFi, OTA, etc)
     * - 🟢 Verde:   Operación exitosa
     * - 🔵 Azul:    Proceso OTA en curso
     * 
     * DEPENDENCIAS:
     * - GPIO (automático en ESP-IDF)
     * - RMT peripheral (para protocolo WS2812B)
     */
    
    ESP_LOGI(TAG, "Inicializando control de LEDs...");
    
    // Llamar a la función de inicialización del módulo LED
    // Esta función:
    // 1. Configura el periférico RMT
    // 2. Asocia el GPIO configurado
    // 3. Inicializa el driver led_strip
    // 4. Apaga todos los LEDs (estado limpio)
    led_control_init();
    
    ESP_LOGI(TAG, "✓ LEDs inicializados (GPIO %d)", CONFIG_BLINK_GPIO);
    
    // NOTA: En este punto los LEDs están apagados
    // Los módulos siguientes (WiFi, OTA) los controlarán según necesiten

    // ------------------------------------------------------------------------
    // SUBSISTEMA 2: CONECTIVIDAD WiFi
    // ------------------------------------------------------------------------
    
    /**
     * ORDEN DE INICIALIZACIÓN:
     * =======================
     * WiFi DEBE inicializarse DESPUÉS de:
     * - NVS (usa NVS para guardar configuración)
     * - LEDs (usa LEDs para feedback visual)
     * 
     * COMPORTAMIENTO:
     * ==============
     * wifi_init_sta() es una función BLOQUEANTE
     * No retorna hasta que:
     * - WiFi se conecta exitosamente (obtiene IP), O
     * - Falla después de MAXIMUM_RETRY intentos
     * 
     * DURANTE LA CONEXIÓN:
     * ===================
     * - LEDs naranjas: Intentando conectar
     * - LEDs rojos: Falló completamente
     * - LEDs verdes: Conectado exitosamente
     * 
     * CONFIGURACIÓN:
     * =============
     * SSID y Password se configuran en menuconfig:
     * Component config → WiFi Config
     */
    
    ESP_LOGI(TAG, "Iniciando conexión WiFi...");
    ESP_LOGI(TAG, "SSID objetivo: %s", CONFIG_WIFI_SSID);
    
    // Inicializar y conectar WiFi (BLOQUEANTE)
    // El programa se detiene aquí hasta que WiFi se conecte
    wifi_init_sta();
    
    // Si llegamos aquí, WiFi está conectado y con IP asignada
    ESP_LOGI(TAG, "✓ WiFi conectado exitosamente");
    
    // IMPORTANTE: A partir de aquí, el sistema tiene conectividad
    // Ya podemos usar HTTP, MQTT, NTP, OTA, etc.

    // ------------------------------------------------------------------------
    // SUBSISTEMA 3: SISTEMA OTA
    // ------------------------------------------------------------------------
    
    /**
     * INICIALIZACIÓN OTA:
     * ==================
     * ota_init() registra los manejadores de eventos OTA
     * NO inicia una actualización, solo prepara el sistema
     * 
     * EVENTOS MANEJADOS:
     * - START: OTA comenzó
     * - CONNECTED: Conectado al servidor
     * - WRITE_FLASH: Escribiendo firmware (parpadeo LED)
     * - FINISH: OTA completado
     * - ABORT: OTA cancelado/fallido
     * 
     * INDICADORES VISUALES:
     * - LEDs azules parpadeando: Descargando/escribiendo
     * - LEDs verdes: OTA exitoso
     * - LEDs rojos: OTA fallido
     */
    
    ESP_LOGI(TAG, "Inicializando sistema OTA...");
    
    // Registrar manejadores de eventos OTA
    // Esto NO inicia una actualización, solo prepara el sistema
    ota_init();
    
    ESP_LOGI(TAG, "✓ Sistema OTA listo");

    // ========================================================================
    // FASE 4: VALIDACIÓN DE FIRMWARE (ROLLBACK SUPPORT)
    // ========================================================================
    
    /**
     * ¿QUÉ ES ROLLBACK?
     * =================
     * El rollback es un mecanismo de seguridad que permite volver
     * automáticamente a la versión anterior del firmware si la nueva
     * versión no funciona correctamente.
     * 
     * ¿CÓMO FUNCIONA?
     * ==============
     * 1. OTA descarga nuevo firmware a partición inactiva (ota_1)
     * 2. Marca la nueva partición como "pendiente de verificación"
     * 3. Reinicia el ESP32
     * 4. Bootloader arranca desde la nueva partición
     * 5. Si app_main() llega hasta aquí y marca como válido → OK
     * 6. Si el ESP32 se reinicia antes de marcar válido → ROLLBACK
     * 
     * ESTADOS DE PARTICIÓN OTA:
     * ========================
     * - ESP_OTA_IMG_NEW: Recién instalada, no arrancada aún
     * - ESP_OTA_IMG_PENDING_VERIFY: Primera ejecución, pendiente validación
     * - ESP_OTA_IMG_VALID: Validada, OK para usar
     * - ESP_OTA_IMG_INVALID: Marcada como mala, no arrancar
     * - ESP_OTA_IMG_ABORTED: OTA cancelado
     * - ESP_OTA_IMG_UNDEFINED: Estado desconocido
     * 
     * ¿CUÁNDO OCURRE ROLLBACK?
     * ========================
     * - Firmware crashea antes de marcar como válido
     * - Watchdog timer resetea el sistema
     * - Panic/Exception no manejada
     * - Usuario presiona RESET antes de validación
     * 
     * CONFIGURACIÓN:
     * =============
     * Se habilita en menuconfig:
     * Bootloader config → App rollback support
     */
    
    // Esta característica solo está disponible si se configuró en menuconfig
    #if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    
    ESP_LOGI(TAG, "Verificando estado de firmware (rollback habilitado)...");
    
    // Variable para almacenar el estado de la imagen OTA
    esp_ota_img_states_t ota_state;
    
    // Obtener el estado de la partición actual
    // running = partición desde la que estamos ejecutando
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        
        // ----------------------------------------------------------------
        // CASO 1: PRIMERA EJECUCIÓN DESPUÉS DE OTA
        // ----------------------------------------------------------------
        
        /**
         * Estado PENDING_VERIFY indica:
         * - Acabamos de actualizar mediante OTA
         * - Es la primera vez que arranca esta versión
         * - El bootloader está esperando confirmación
         * - Si no confirmamos, habrá rollback en el próximo reinicio
         */
        
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            
            ESP_LOGI(TAG, "🔄 Detectada primera ejecución post-OTA");
            ESP_LOGI(TAG, "   Validando nuevo firmware...");
            
            /**
             * MARCAR FIRMWARE COMO VÁLIDO:
             * ===========================
             * Esta llamada es CRÍTICA. Le dice al bootloader:
             * "Este firmware funciona bien, no hagas rollback"
             * 
             * CUÁNDO LLAMAR:
             * - Después de verificar que todo funciona
             * - Una vez que el sistema está estable
             * - Típicamente en app_main() después de inicializaciones
             * 
             * SI NO SE LLAMA:
             * - El bootloader cuenta los reinicios
             * - Después de N reinicios sin validar → ROLLBACK automático
             * - El sistema vuelve a la versión anterior
             * 
             * ESTRATEGIAS AVANZADAS:
             * - Validar después de X minutos de uptime
             * - Validar después de test funcional completo
             * - Validar después de conectividad comprobada
             */
            
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                
                // ¡ÉXITO! Firmware validado
                ESP_LOGI(TAG, "✅ Firmware validado exitosamente");
                ESP_LOGI(TAG, "   Rollback cancelado, esta versión es estable");
                
                // OPCIONAL: Aquí podrías:
                // - Enviar telemetría de actualización exitosa
                // - Actualizar contador de versión en NVS
                // - Notificar al servidor que la actualización funcionó
                // - Guardar timestamp de última actualización
                
            } else {
                // Error al marcar como válido
                // Esto es GRAVE y poco común
                ESP_LOGE(TAG, "❌ ERROR: No se pudo validar el firmware");
                ESP_LOGE(TAG, "   Posible rollback en próximo reinicio");
                
                // POSIBLES CAUSAS:
                // - Partición OTA corrupta
                // - Flash defectuosa
                // - Error interno del bootloader
                
                // ACCIÓN RECOMENDADA:
                // - Log del error para debugging
                // - Considerar reinicio forzado para intentar rollback
                // - Notificar al servidor del problema
            }
            
        // ----------------------------------------------------------------
        // CASO 2: FIRMWARE YA VALIDADO PREVIAMENTE
        // ----------------------------------------------------------------
        
        } else if (ota_state == ESP_OTA_IMG_VALID) {
            
            // Este es el caso normal en ejecuciones posteriores
            // El firmware ya fue validado en un arranque anterior
            ESP_LOGI(TAG, "✓ Firmware previamente validado, estado: VÁLIDO");
            
        // ----------------------------------------------------------------
        // CASO 3: OTROS ESTADOS
        // ----------------------------------------------------------------
        
        } else {
            
            // Estados menos comunes
            // Pueden indicar problemas o situaciones especiales
            ESP_LOGW(TAG, "⚠️  Estado de imagen OTA: %d", ota_state);
            
            // Interpretación de estados:
            switch (ota_state) {
                case ESP_OTA_IMG_NEW:
                    ESP_LOGW(TAG, "   NEW: Imagen nueva sin arrancar");
                    break;
                case ESP_OTA_IMG_INVALID:
                    ESP_LOGW(TAG, "   INVALID: Imagen marcada como inválida");
                    break;
                case ESP_OTA_IMG_ABORTED:
                    ESP_LOGW(TAG, "   ABORTED: OTA fue abortado");
                    break;
                case ESP_OTA_IMG_UNDEFINED:
                    ESP_LOGW(TAG, "   UNDEFINED: Estado no definido");
                    break;
                default:
                    ESP_LOGW(TAG, "   Desconocido");
            }
        }
        
    } else {
        // No se pudo leer el estado - error poco común
        ESP_LOGW(TAG, "⚠️  No se pudo determinar estado de la imagen OTA");
    }
    
    #else
    
    // Rollback NO está habilitado en la configuración
    // El código de validación no se compila
    ESP_LOGI(TAG, "ℹ️  Rollback deshabilitado en configuración");
    
    #endif // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE

    // ========================================================================
    // FASE 5: CREACIÓN DE TAREAS FreeRTOS
    // ========================================================================
    
    /**
     * SISTEMA MULTITAREA:
     * ==================
     * FreeRTOS permite ejecutar múltiples tareas concurrentemente.
     * Cada tarea es como un "mini-programa" que se ejecuta en paralelo.
     * 
     * SCHEDULER DE FreeRTOS:
     * =====================
     * - Prioridades: 0 (menor) a 24 (mayor)
     * - Preemptive: Una tarea de mayor prioridad interrumpe a una de menor
     * - Round-robin: Tareas de igual prioridad se turnan
     * - Tick: El scheduler cambia de contexto cada tick (típicamente 1ms)
     * 
     * STACK SIZE:
     * ==========
     * Cada tarea tiene su propio stack. Si es muy pequeño → STACK OVERFLOW
     * Si es muy grande → Desperdicio de RAM
     * Típicos: 2048-4096 bytes para tareas simples
     *          8192+ bytes para tareas con HTTP, SSL, etc.
     * 
     * ANATOMÍA DE UNA TAREA:
     * =====================
     * void mi_tarea(void *parametro) {
     *     // Inicialización
     *     while(1) {
     *         // Trabajo de la tarea
     *         vTaskDelay(pdMS_TO_TICKS(100)); // Ceder control
     *     }
     *     // NUNCA llegar aquí
     *     vTaskDelete(NULL); // Si la tarea termina
     * }
     */
    
    ESP_LOGI(TAG, "Creando tareas FreeRTOS...");

    // ------------------------------------------------------------------------
    // TAREA 1: CONTROL DE LEDs (DEMOSTRACIÓN)
    // ------------------------------------------------------------------------
    
    /**
     * PROPÓSITO:
     * - Mostrar secuencia de colores continua
     * - Verificar que el sistema está funcionando
     * - Demo visual para testing
     * 
     * PRODUCCIÓN:
     * En una aplicación real, probablemente:
     * - Comentarías esta tarea
     * - Los LEDs solo mostrarían estados del sistema
     * - O los controlarías bajo demanda (eventos, comandos)
     */
    
    ESP_LOGI(TAG, "  → Tarea LED_STRIP (parpadeo continuo)");
    
    xTaskCreate(
        led_task,               // Función de la tarea (en led_control.c)
        "LED_STRIP",            // Nombre descriptivo (para debugging)
                                // Visible en: "uxTaskGetSystemState()"
        4096,                   // Tamaño del stack en bytes
                                // 4KB es suficiente para esta tarea simple
        NULL,                   // Parámetro pasado a la tarea (void *pvParameter)
                                // NULL = sin parámetros
        3,                      // Prioridad (0-24)
                                // 3 = Media-baja, no crítica
        NULL                    // Handle de la tarea (TaskHandle_t *)
                                // NULL = no necesitamos referencia
    );
    
    /**
     * ALTERNATIVAS DE USO:
     * ===================
     * TaskHandle_t led_handle;
     * xTaskCreate(led_task, "LED", 4096, NULL, 3, &led_handle);
     * // Ahora puedes:
     * vTaskSuspend(led_handle);    // Pausar la tarea
     * vTaskResume(led_handle);     // Reanudar la tarea
     * vTaskDelete(led_handle);     // Eliminar la tarea
     */

    // ------------------------------------------------------------------------
    // TAREA 2: ACTUALIZACIÓN OTA (OPCIONAL - COMENTADA)
    // ------------------------------------------------------------------------
    
    /**
     * TAREA OTA:
     * =========
     * Esta tarea ejecuta el proceso completo de actualización OTA:
     * 1. Espera 10 segundos (dar tiempo al sistema a estabilizarse)
     * 2. Se conecta al servidor HTTPS
     * 3. Descarga el nuevo firmware
     * 4. Valida la imagen
     * 5. Escribe en la partición OTA inactiva
     * 6. Reinicia el ESP32 con el nuevo firmware
     * 
     * ¿POR QUÉ ESTÁ COMENTADA?
     * ========================
     * - Actualización automática puede no ser deseada
     * - Requiere servidor configurado con firmware
     * - URL debe estar correctamente configurada
     * - Consume ancho de banda en cada arranque
     * 
     * CUÁNDO DESCOMENTAR:
     * ==================
     * - Cuando tengas un servidor con firmware .bin
     * - URL configurada en menuconfig o código
     * - Certificado CA correcto (para HTTPS)
     * - Quieras actualización al arranque
     * 
     * ALTERNATIVAS DE ACTIVACIÓN:
     * ==========================
     * En lugar de auto-iniciar, puedes activar OTA mediante:
     * - Botón físico presionado al arrancar
     * - Comando recibido por MQTT
     * - Petición HTTP a un servidor embebido
     * - Timer periódico (verificar actualizaciones cada N horas)
     * - Condición específica (ej: si versión < X.Y.Z)
     * 
     * EJEMPLO DE ACTIVACIÓN POR BOTÓN:
     * ================================
     * if (gpio_get_level(BUTTON_GPIO) == 0) {
     *     ESP_LOGI(TAG, "Botón presionado, iniciando OTA");
     *     xTaskCreate(ota_task, "OTA", 8192, NULL, 5, NULL);
     * }
     */
    
    // DESCOMENTAR PARA HABILITAR OTA AUTOMÁTICO AL ARRANQUE:
    // 
    // ESP_LOGI(TAG, "  → Tarea OTA (actualización automática)");
    // xTaskCreate(
    //     ota_task,           // Función de la tarea (en ota_manager.c)
    //     "OTA_Task",         // Nombre descriptivo
    //     1024 * 8,           // 8KB de stack (OTA necesita más memoria)
    //                         // Razón: HTTP client, SSL, buffers grandes
    //     NULL,               // Sin parámetros
    //     5,                  // Prioridad ALTA (5)
    //                         // Razón: OTA es crítico, queremos que termine rápido
    //     NULL                // Sin handle
    // );
    //
    // NOTA: La tarea OTA se auto-elimina al terminar (éxito o fallo)
    // mediante vTaskDelete(NULL) en ota_manager.c
    
    ESP_LOGI(TAG, "  ℹ️  Tarea OTA deshabilitada (descomentar para activar)");

    // ========================================================================
    // FASE 6: SISTEMA COMPLETAMENTE INICIALIZADO
    // ========================================================================
    
    /**
     * ESTADO DEL SISTEMA EN ESTE PUNTO:
     * =================================
     * ✅ NVS inicializado y funcional
     * ✅ LEDs configurados y listos
     * ✅ WiFi conectado con IP asignada
     * ✅ OTA preparado (manejadores registrados)
     * ✅ Firmware validado (si había actualización)
     * ✅ Tareas FreeRTOS creadas y listas
     * 
     * QUÉ SUCEDE DESPUÉS:
     * ==================
     * 1. app_main() RETORNA
     * 2. La tarea "main" se elimina automáticamente
     * 3. El scheduler de FreeRTOS toma control total
     * 4. Las tareas creadas comienzan a ejecutarse:
     *    - led_task: Parpadea LEDs continuamente
     *    - (ota_task: Si está descomentada)
     *    - Tareas internas de ESP-IDF (WiFi, TCP/IP, etc)
     * 
     * TAREAS DEL SISTEMA (automáticas):
     * =================================
     * Además de nuestras tareas, FreeRTOS ejecuta:
     * - IDLE task (prioridad 0): Se ejecuta cuando ninguna tarea está activa
     * - Timer task: Gestiona timers software
     * - WiFi task: Maneja eventos WiFi internos
     * - LWIP task: Stack TCP/IP
     * - Event task: Procesa eventos del sistema
     * 
     * MONITOREO:
     * =========
     * Para ver todas las tareas en ejecución:
     * TaskStatus_t tasks[10];
     * UBaseType_t count = uxTaskGetSystemState(tasks, 10, NULL);
     * 
     * O simplemente mira los logs con 'idf.py monitor'
     */
    
    // Banner de sistema listo
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ✅ SISTEMA INICIADO CORRECTAMENTE                   ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Estado del sistema:");
    ESP_LOGI(TAG, "  • LEDs:     ✓ Operativos");
    ESP_LOGI(TAG, "  • WiFi:     ✓ Conectado (%s)", CONFIG_WIFI_SSID);
    ESP_LOGI(TAG, "  • OTA:      ✓ Listo");
    ESP_LOGI(TAG, "  • Tareas:   ✓ Ejecutándose");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "El sistema está operativo y ejecutando tareas...");
    
    /**
     * DEBUGGING:
     * =========
     * Si necesitas debug, añade aquí:
     * 
     * // Mostrar memoria libre
     * ESP_LOGI(TAG, "RAM libre: %d bytes", esp_get_free_heap_size());
     * 
     * // Mostrar número de tareas
     * ESP_LOGI(TAG, "Tareas activas: %d", uxTaskGetNumberOfTasks());
     * 
     * // Mostrar uptime
     * ESP_LOGI(TAG, "Uptime: %lld ms", esp_timer_get_time() / 1000);
     */
    
    // ========================================================================
    // FIN DE app_main()
    // ========================================================================
    
    /**
     * IMPORTANTE:
     * ==========
     * - NO añadir loops infinitos aquí (while(1))
     * - NO añadir vTaskDelay() aquí
     * - app_main() DEBE retornar para que FreeRTOS funcione
     * 
     * A partir de aquí:
     * - El scheduler de FreeRTOS controla la ejecución
     * - Las tareas se ejecutan concurrentemente
     * - El sistema continúa indefinidamente
     * - Solo se detiene por: reinicio, panic, o apagado
     * 
     * PRÓXIMOS PASOS:
     * ==============
     * 1. Compilar: idf.py build
     * 2. Flashear: idf.py flash
     * 3. Monitorear: idf.py monitor
     * 4. Observar: Los LEDs parpadearán en secuencia
     * 5. (Opcional) Descomentar tarea OTA para actualización automática
     */
    
    // La función retorna, FreeRTOS toma control
    // ¡El sistema está vivo! 🚀
}

// ============================================================================
// FIN DEL ARCHIVO
// ============================================================================