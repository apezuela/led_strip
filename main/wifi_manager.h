/**
 * @file wifi_manager.h
 * @brief Gestor de conexión WiFi para ESP32
 * 
 * Este módulo maneja toda la lógica de conexión WiFi en modo Station (cliente).
 * Proporciona funciones para inicializar, conectar y gestionar reintentos
 * automáticos de conexión.
 * 
 * Características:
 * - Conexión automática a red WiFi configurada
 * - Sistema de reintentos con límite configurable
 * - Retroalimentación visual mediante LEDs
 * - Sincronización mediante event groups de FreeRTOS
 * - Desactivación de ahorro de energía para mejor rendimiento OTA
 * 
 * @author Tu Nombre
 * @date 2025
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

/**
 * @brief Inicializa y conecta WiFi en modo Station
 * 
 * Esta función realiza la inicialización completa del subsistema WiFi:
 * 1. Crea el event group para sincronización
 * 2. Inicializa la pila TCP/IP (netif)
 * 3. Configura credenciales WiFi desde menuconfig
 * 4. Registra manejadores de eventos
 * 5. Inicia la conexión
 * 6. Espera de forma bloqueante hasta obtener IP o agotar reintentos
 * 7. Desactiva power save para estabilidad en OTA
 * 
 * Indicadores visuales LED durante el proceso:
 * - Naranja: Reintentando conexión
 * - Rojo: Falló completamente (agotó reintentos)
 * - Verde: Conexión exitosa (IP obtenida)
 * 
 * @note Esta función es BLOQUEANTE hasta que se conecte WiFi o falle
 * @note Requiere configuración previa de WIFI_SSID y WIFI_PASS en menuconfig
 * @note Los LEDs deben estar inicializados antes de llamar esta función
 * 
 * @warning Si falla la conexión después de MAXIMUM_RETRY intentos,
 *          la función retorna pero WiFi queda en estado ERROR
 * 
 * Ejemplo de uso:
 * @code
 * led_control_init();        // Inicializar LEDs primero
 * wifi_init_sta();           // Conectar WiFi (bloqueante)
 * // Aquí WiFi ya está conectado y con IP
 * @endcode
 */

void wifi_init_sta(void);

#endif // WIFI_MANAGER_H
