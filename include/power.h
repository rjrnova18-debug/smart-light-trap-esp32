#pragma once
// Energía: protección por bajo voltaje y batería crítica, muestreo durante el
// deep sleep, botón de despertar, "despierto a la fuerza" y entrada al deep sleep.
#include <Arduino.h>

// "Despierto a la fuerza" FORCE_AWAKE_MS (botón del pin 32, "Posponer" del dashboard,
// comandos serie): mantiene el equipo despierto y el AP encendido aunque no haya
// ventana. Seguro para llamar desde una interrupción y desde cualquier núcleo.
void IRAM_ATTR forceAwakeStartNow();   // Empieza (o renueva) los FORCE_AWAKE_MS
bool forceAwakeActive();               // true mientras no haya vencido
unsigned long forceAwakeRemainingMs(); // Lo que falta (0 si no está activo)

void IRAM_ATTR WakeButton_rutine();  // ISR del botón del pin 32
void enterDeepSleep();               // Calcula el próximo despertar y duerme
bool quickBatterySample();           // Despertar de muestreo: guarda el voltaje y vuelve a dormir
void checkDeepSleep();               // Duerme si no queda ninguna ventana ni override activo
void updateLowBatteryProtection();   // Bloquea luz/ventiladores (y la SD si es crítica) por bajo voltaje
void sampleBatteryNow();             // Mide la batería al despertar, antes de montar la SD
void updatePowerSourceAtBoot();      // Con esa medición: ¿batería o USB (modo banco)? (ver USB_POWER_*)
void updateCriticalBatteryAtBoot();  // Bloqueo crítico con la medición del despertar
