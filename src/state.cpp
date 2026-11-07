// Definición única del estado compartido (declarado en state.h).
#include "state.h"
#include "config.h"

SystemData systemData = {0};

QueueHandle_t fanEventQueue;
SemaphoreHandle_t xI2C_Mutex;

// -------- Manual Mode --------
bool manualFanOverride[4] = {false, false, false, false};
bool manualFanState[4] = {false, false, false, false};
bool manualLedOverride = false;
bool manualLedState = false;

int FAN_PINS[4] = {FAN1, FAN2, FAN3, FAN4};
int areaCounter[4] = {0, 0, 0, 0}; // Contadores de eventos por área

// Valores por defecto en loadDefaultSettings(); el NVS los sobrescribe si existen
int FAN_ON_TIME;         // Tiempo de activación del Fan (ms)
int AREA_THRESHOLD;      // Umbral de activación (1 - 30)
int MAX_CONCURRENT_FANS; // Máximo de ventiladores encendidos a la vez (1 - 4), configurable

// -------- INTERLOCK: máximo N ventiladores a la vez (solo automático) --------
// El disparo AUTOMÁTICO por IR nunca enciende más de MAX_CONCURRENT_FANS
// ventiladores a la vez; si se supera el cupo, el área queda en cola y se
// activa en cuanto se libera cupo suficiente. El control MANUAL (dashboard) no
// respeta esta regla a propósito.
// La cola es POR ÁREA (no por ventilador): como un área puede controlar 2
// ventiladores (ver AREA_TO_FANS), siempre se encienden juntos, nunca uno solo
// esperando al otro.
uint8_t pendingAreaMask = 0;              // bit i = área i esperando cupo para encender su(s) ventilador(es) juntos

// HORARIOS CONFIGURABLES PARA AP, LEDS/FAN Y SD CARD
// El AP tiene DOS rangos independientes (mañana y tarde/noche) para no tener
// que dejar el WiFi prendido todo el día — solo en los momentos en que se
// revisa la trampa, ahorrando batería el resto del tiempo.
// Valores por defecto en loadDefaultSettings(); el NVS los sobrescribe si existen.
int apMorningStartH, apMorningStartM;
int apMorningEndH, apMorningEndM;
int apEveningStartH, apEveningStartM;
int apEveningEndH, apEveningEndM;

int ledStartH, ledStartM;
int ledEndH, ledEndM;

int fanStartH, fanStartM;
int fanEndH, fanEndM;

// Ventana SD: ya no limita los registros (se guardan siempre que el equipo esté
// despierto); solo sirve como ventana extra para mantenerlo despierto.
int sdStartH, sdStartM;
int sdEndH, sdEndM;

// Variable para control de deepsleep
bool forceAwake = false;
unsigned long forceAwakeStart = 0;

// true si el próximo despertar por timer es solo de muestreo (ver quickBatterySample())
RTC_DATA_ATTR bool sampleWake = false; // true si el próximo despertar por timer es solo de muestreo

RTC_DATA_ATTR bool lowBatteryLock = false; // se conserva durante el deep sleep
RTC_DATA_ATTR bool criticalBatteryLock = false; // se conserva durante el deep sleep

// -------- MODO SOLO CARGA Y PANTALLA (se guardan en NVS, ver /setOptions) --------
// Modo solo carga: luz y ventiladores no operan en automático y el ESP32 solo
// despierta para el AP; el resto del tiempo duerme y registra la batería cada 30 min.
// Sirve para comprobar que el panel recarga la batería. El manual sigue disponible.
bool chargeOnlyMode = false;
// OLED: con la tapa cerrada no se ve, así que se puede apagar desde el dashboard.
// Apagada, el botón del pin 35 la enciende OLED_TEMP_ON_MS.
volatile bool oledEnabled = true;
volatile bool oledTempOn = false;
volatile unsigned long oledTempOnStart = 0;

bool apIsActive = false;
