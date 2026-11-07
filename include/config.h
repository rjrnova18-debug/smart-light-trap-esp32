#pragma once
// Pines, tiempos y umbrales de la trampa. El SSID y las claves van aparte, en
// include/secrets.h, que no se sube al repositorio.
#include <Arduino.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Falta include/secrets.h: copia include/secrets.example.h como include/secrets.h y completa las claves"
#endif

// -------- LED (luz de la trampa) --------
// Encendido ON-OFF simple (HIGH = encendida). Antes iba por PWM con
// ledcWrite(50/255) ≈ 20% del tiempo encendida, así que ahora consume más.
const int ledPin = 26;

// -------- MULTIPLEXOR --------
#define MUX_S0 25
#define MUX_S1 33
#define MUX_S2 4
#define MUX_S3 15
#define MUX_SIG 34

const unsigned long DEBOUNCE_TIME = 30; // miliseg

// Los sensores de Área 1 (C0-C3) y Área 4 (C12-C15) quedaron desconectados por
// falta de corriente del panel — solo quedan Área 2 (C4-C7) y Área 3 (C8-C11).
// Igual que la vez anterior con el mapeo inverso, restringimos el escaneo a los
// canales realmente conectados: evita ruido/falsos positivos en C0-C3/C12-C15
// (que quedan flotando) y acelera el barrido (8 canales en vez de 16).
const uint8_t ACTIVE_MUX_CHANNELS[] = {4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t ACTIVE_MUX_CHANNEL_COUNT = sizeof(ACTIVE_MUX_CHANNELS) / sizeof(ACTIVE_MUX_CHANNELS[0]);

// Orden de canales para mostrar en la OLED (MODE_IR). Por el ruteo del PCB, el
// cableado físico de los sensores de las filas 2, 3 y 4 no coincide 1 a 1 con el
// número de canal del mux — esta tabla solo reordena lo que se IMPRIME en pantalla
// para que cada posición muestre el sensor físico correcto; no afecta el conteo
// de detecciones por área (evaluateAreas() sigue agrupando por área, no por
// sensor individual, así que le da igual el orden).
const uint8_t IR_DISPLAY_ORDER[16] = {
    0,
    1,
    2,
    3, // Fila 1 (Área 1) — orden directo
    4,
    7,
    6,
    5, // Fila 2 (Área 2) — columnas 2 y 4 intercambiadas
    8,
    11,
    10,
    9, // Fila 3 (Área 3) — mismo intercambio que la fila 2
    15,
    12,
    13,
    14, // Fila 4 (Área 4) — rotación de las 4 posiciones
};

const unsigned long EVALUATION_WINDOW = 60000 * 5; // Ventana de evaluación de 5 minutos para contadores de áreas

// -------- FAN LOADS --------
#define FAN1 27
#define FAN2 14
#define FAN3 12
#define FAN4 13

// Mapeo área de sensores -> ventilador(es) que controla. Ya no es 1 a 1: al
// quedar sin sensores Área 1 y Área 4, Área 2 pasó a controlar Ventilador 1 y
// Ventilador 2, y Área 3 pasó a controlar Ventilador 3 y Ventilador 4. -1 =
// sin ventilador asociado en ese slot.
const int8_t AREA_TO_FANS[4][2] = {
    {-1, -1}, // Área 1: sin sensores, sin ventilador asociado
    {0, 1},   // Área 2 -> Ventilador 1 y Ventilador 2
    {2, 3},   // Área 3 -> Ventilador 3 y Ventilador 4
    {-1, -1}, // Área 4: sin sensores, sin ventilador asociado
};

// -------- I2C --------
#define SDA_PIN 21
#define SCL_PIN 22

// -------- OLED --------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define TEXT_SIZE 1
#define INTERRUPT_BUTTON 35

// -------- BME280 --------
#define BME_ADDR 0x76
#define SEALEVELPRESSURE_HPA 1013.25

// Muestreo de batería durante el deep sleep: el ESP32 despierta en cada :00 y
// :30, guarda el voltaje (~1 s, sin WiFi ni OLED) y vuelve a dormir.
const int SAMPLE_PERIOD_MIN = 30;

// -------- PROTECCIÓN POR BAJO VOLTAJE --------
// Por debajo de LOW_BAT_CUTOFF_V (sostenido LOW_BAT_CONFIRM_MS) se bloquean luz y
// ventiladores, también en manual. Se liberan al volver a LOW_BAT_RECONNECT_V:
// la histéresis evita que se prendan y apaguen al recuperarse el voltaje sin carga.
const float LOW_BAT_CUTOFF_V = 11.0;    // ≈ 10-15 % en reposo; se mide con carga, así que deja algo de margen
const float LOW_BAT_RECONNECT_V = 11.4; // al quitar luz+ventiladores el voltaje rebota ~0.2 V, no alcanza para reconectar
const unsigned long LOW_BAT_CONFIRM_MS = 30000;

// -------- BATERÍA CRÍTICA (PROTECCIÓN DE LA SD) --------
// Por debajo de CRIT_BAT_CUTOFF_V la batería está casi vacía y el BMS puede cortar
// la energía en cualquier momento. Si corta en plena escritura, o si al reconectar
// (al amanecer) el equipo queda arrancando y apagándose mientras escribe, la SD se
// daña: lo más probable con la primera tarjeta. En ese estado no se escribe en la
// SD y el ESP32 duerme aunque haya ventana del AP (salvo despertado por el botón);
// los despertares de muestreo solo miden. Se libera al volver a CRIT_BAT_RECONNECT_V.
const float CRIT_BAT_CUTOFF_V = 10.2;    // ≈ 3.4 V/celda (~5 %), muy por encima del corte del BMS
const float CRIT_BAT_RECONNECT_V = 10.6; // la histéresis evita despertar y volver a dormir en cada muestreo

// -------- ALIMENTACIÓN POR USB (BANCO DE PRUEBAS) --------
// La batería 3S nunca está en esta franja mientras alimenta el circuito: el BMS corta
// mucho antes (~7.5-9 V). Si el INA219 lee esto, el ESP32 está alimentado por su USB
// con la batería desconectada o apagada: MODO BANCO (no se duerme, las protecciones de
// batería quedan en pausa y el AP queda encendido). La franja excluye 0 V a propósito:
// un INA219 que falla y lee 0 no debe dejar despierta a una trampa en campo.
const float USB_POWER_MIN_V = 4.0;
const float USB_POWER_MAX_V = 5.6;
const unsigned long USB_POWER_CONFIRM_MS = 5000; // durante la operación, la lectura se sostiene 5 s antes de cambiar

// OLED encendida temporalmente por el botón del pin 35 (ver oledTempOn)
const unsigned long OLED_TEMP_ON_MS = 120000;

// "Despierto a la fuerza": el botón del pin 32, "Posponer" en el dashboard y los
// comandos serie mantienen el equipo despierto (y el AP encendido) este tiempo.
const unsigned long FORCE_AWAKE_MS = 300000; // 5 min

// -------- RELOJ (RTC) --------
#define RTC_ADDR 0x68 // DS1307 en el bus I2C
// Lecturas inválidas seguidas (taskUI lee cada ~100 ms) antes de dar la hora por no
// confiable y pasar a modo seguro: sin hora no se opera por horario (ver getTime()).
const int RTC_MAX_BAD_READS = 10;

// -------- SALUD DEL FIRMWARE (ver include/health.h) --------
// Watchdog: si una tarea vigilada no avisa en este tiempo, el ESP32 se reinicia.
const uint32_t TASK_WDT_TIMEOUT_S = 30;
// Un firmware recién instalado por OTA queda "a prueba" este tiempo: si se reinicia
// antes (cuelgue, error, watchdog), el bootloader vuelve solo al firmware anterior.
const unsigned long OTA_TRIAL_MS = 60000;

// -------- SD CARD --------
#define SD_CS 5
const int LOG_EVERY_MIN = 5; // Registro BME/INA/BAT en cada minuto múltiplo de 5 (:00, :05, ...)
// Escrituras fallidas seguidas antes de dar la SD por muerta (hasta el próximo
// arranque): se registra SD_FALLA en el respaldo y no se pierde tiempo cada 5 min.
const int SD_MAX_FAILS = 3;

// -------- RESPALDO EN LA FLASH INTERNA (LittleFS, ver include/backup.h) --------
// Partición "spiffs" de la tabla por defecto (0x290000, 1,375 MB). Al pasar de
// BACKUP_ROTATE_PCT % de uso se borra el mes más antiguo (nunca el actual).
#define BACKUP_PARTITION "spiffs"
#define BACKUP_MOUNT_PATH "/lfs"
const int BACKUP_ROTATE_PCT = 85;
