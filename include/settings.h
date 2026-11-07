#pragma once
// Horarios y parámetros de captura (NVS) y ventanas de operación.
#include <Preferences.h>

extern Preferences preferences;

void loadSettingsFromNVS(); // Al arrancar: predeterminados y encima lo guardado desde el dashboard
void loadDefaultSettings(); // Valores predeterminados de horarios y parámetros de captura
void saveSettingsToNVS();   // Guarda horarios y parámetros de captura en NVS
bool isInsideWindow(int startH, int startM, int endH, int endM);
bool operatingWindow_AP();
bool operatingWindow_SD();
bool operatingWindow_LED();
bool operatingWindow_Fan();
bool anyWakeWindowActive();
