#pragma once
// Estado compartido entre módulos. Cada variable se define una sola vez en
// state.cpp; aquí solo se declara (extern).
#include <Arduino.h>

// -------- ESTRUCTURA GLOBAL DEL SISTEMA --------
struct SystemData
{
  // Sensores ambientales
  float temperature, humidity, pressure, altitude;
  // Tiempo
  int hour, minute, second, day, month, year;
  // Datos del INA219
  float busVoltage, current_mA, power_W;
  float powerSumInterval; // Acumula potencia para cálculo de energía en el intervalo
  int intervalSamples;    // Contador de muestras en el intervalo
  float totalEnergy_Wh;   // Nueva variable para energía total acumulada
  // Estado de los ventiladores y LED
  bool ledState;
  bool loads[4];      // Estado de las 4 cargas
  bool irSensors[16]; // Estado de los 16 sensores IR
  int areaCounts[4];
  // Banderas de disponibilidad
  bool rtcAvailable, bmeAvailable, inaAvailable, sdAvailable, oledAvailable;
  bool backupAvailable; // respaldo LittleFS en la flash interna montado
};

extern SystemData systemData;

// Eventos de ventilador: una fila por activación de ÁREA. taskControl los encola y
// solo taskComm los escribe (antes logEvent() escribía desde taskControl, en
// paralelo con los otros logs).
struct FanEvent
{
  int area;         // área que disparó (1-4)
  uint8_t fansMask; // ventiladores que encendió (bit i = ventilador i+1)
  int triggerCount; // conteo de detecciones que la disparó
  int day, month, year;
  int hour, minute, second;
};

extern QueueHandle_t fanEventQueue;
extern SemaphoreHandle_t xI2C_Mutex; // bus I2C (RTC, BME280, INA219, OLED) y systemData al registrar

// -------- Manual Mode --------
extern bool manualFanOverride[4];
extern bool manualFanState[4];
extern bool manualLedOverride;
extern bool manualLedState;

// -------- Ventiladores y áreas --------
extern int FAN_PINS[4];
extern int areaCounter[4];
extern int FAN_ON_TIME;
extern int AREA_THRESHOLD;
extern int MAX_CONCURRENT_FANS;
extern uint8_t pendingAreaMask;

// -------- Horarios (ver settings.cpp) --------
extern int apMorningStartH, apMorningStartM;
extern int apMorningEndH, apMorningEndM;
extern int apEveningStartH, apEveningStartM;
extern int apEveningEndH, apEveningEndM;
extern int ledStartH, ledStartM;
extern int ledEndH, ledEndM;
extern int fanStartH, fanStartM;
extern int fanEndH, fanEndM;
extern int sdStartH, sdStartM;
extern int sdEndH, sdEndM;

// -------- Energía, deep sleep y modos --------
extern bool forceAwake;
extern unsigned long forceAwakeStart;
extern bool sampleWake;
extern bool lowBatteryLock;
extern bool criticalBatteryLock;
extern bool chargeOnlyMode;
extern volatile bool oledEnabled;
extern volatile bool oledTempOn;
extern volatile unsigned long oledTempOnStart;
extern bool apIsActive;
