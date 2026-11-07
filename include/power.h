#pragma once
// Energía: protección por bajo voltaje y batería crítica, muestreo durante el
// deep sleep, botón de despertar y entrada al deep sleep.
#include <Arduino.h>

void IRAM_ATTR WakeButton_rutine(); // ISR del botón del pin 32 (fuerza el AP 5 min)
void enterDeepSleep();             // Calcula el próximo despertar y duerme
bool quickBatterySample();         // Despertar de muestreo: guarda el voltaje y vuelve a dormir
void checkDeepSleep();             // Duerme si no queda ninguna ventana ni override activo
void updateLowBatteryProtection(); // Bloquea luz/ventiladores (y la SD si es crítica) por bajo voltaje
void sampleBatteryNow();           // Mide la batería al despertar, antes de montar la SD
void updateCriticalBatteryAtBoot(); // Bloqueo crítico con la medición del despertar
