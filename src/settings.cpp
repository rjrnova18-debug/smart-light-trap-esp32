#include "settings.h"
#include "config.h"
#include "state.h"

Preferences preferences;

// En cada arranque (también al despertar del deep sleep)
void loadSettingsFromNVS()
{
  // Valores predeterminados; lo guardado en NVS (dashboard) los sobrescribe
  loadDefaultSettings();
  preferences.begin("config", false);

  // Cargar valores guardados en NVS
  ledStartH = preferences.getInt("ledSH", ledStartH);
  ledStartM = preferences.getInt("ledSM", ledStartM);
  ledEndH = preferences.getInt("ledEH", ledEndH);
  ledEndM = preferences.getInt("ledEM", ledEndM);

  fanStartH = preferences.getInt("fanSH", fanStartH);
  fanStartM = preferences.getInt("fanSM", fanStartM);
  fanEndH = preferences.getInt("fanEH", fanEndH);
  fanEndM = preferences.getInt("fanEM", fanEndM);

  apMorningStartH = preferences.getInt("apMoSH", apMorningStartH);
  apMorningStartM = preferences.getInt("apMoSM", apMorningStartM);
  apMorningEndH = preferences.getInt("apMoEH", apMorningEndH);
  apMorningEndM = preferences.getInt("apMoEM", apMorningEndM);

  apEveningStartH = preferences.getInt("apEvSH", apEveningStartH);
  apEveningStartM = preferences.getInt("apEvSM", apEveningStartM);
  apEveningEndH = preferences.getInt("apEvEH", apEveningEndH);
  apEveningEndM = preferences.getInt("apEvEM", apEveningEndM);

  sdStartH = preferences.getInt("sdSH", sdStartH);
  sdStartM = preferences.getInt("sdSM", sdStartM);
  sdEndH = preferences.getInt("sdEH", sdEndH);
  sdEndM = preferences.getInt("sdEM", sdEndM);

  FAN_ON_TIME = preferences.getInt("fanDur", FAN_ON_TIME);
  AREA_THRESHOLD = preferences.getInt("threshold", AREA_THRESHOLD);
  MAX_CONCURRENT_FANS = preferences.getInt("maxFans", MAX_CONCURRENT_FANS);

  chargeOnlyMode = preferences.getBool("chargeMode", false);
  oledEnabled = preferences.getBool("oledOn", true);
}

// Valores predeterminados de horarios y parámetros de captura (los mismos que
// restaura el botón "Restaurar valores predeterminados" del dashboard):
// trampa (luz + ventiladores) 18:00-03:00 (9 h: inicio y pico de actividad de
// S. frugiperda), AP 08:00-12:30 y 18:00-20:00.
// El AP de la noche arranca con la trampa: abrirlo antes mantendría despierto al
// ESP32 solo por el WiFi (~180 mA), y después de las 20:00 cada hora cuesta ~40 mA.
void loadDefaultSettings()
{
  apMorningStartH = 8;
  apMorningStartM = 0;
  apMorningEndH = 12;
  apMorningEndM = 30;
  apEveningStartH = 18;
  apEveningStartM = 0;
  apEveningEndH = 20;
  apEveningEndM = 0;

  ledStartH = 18;
  ledStartM = 0;
  ledEndH = 3;
  ledEndM = 0;

  fanStartH = 18;
  fanStartM = 0;
  fanEndH = 3;
  fanEndM = 0;

  // Igual a la operación nocturna: no agrega tiempo despierto
  sdStartH = 18;
  sdStartM = 0;
  sdEndH = 3;
  sdEndM = 0;

  FAN_ON_TIME = 60000;
  AREA_THRESHOLD = 10;
  MAX_CONCURRENT_FANS = 3;
}

void saveSettingsToNVS()
{
  // GUARDAR TODO EN NVS (Memoria Flash)
  preferences.begin("config", false);

  preferences.putInt("ledSH", ledStartH);
  preferences.putInt("ledSM", ledStartM);
  preferences.putInt("ledEH", ledEndH);
  preferences.putInt("ledEM", ledEndM);
  preferences.putInt("fanSH", fanStartH);
  preferences.putInt("fanSM", fanStartM);
  preferences.putInt("fanEH", fanEndH);
  preferences.putInt("fanEM", fanEndM);
  preferences.putInt("apMoSH", apMorningStartH);
  preferences.putInt("apMoSM", apMorningStartM);
  preferences.putInt("apMoEH", apMorningEndH);
  preferences.putInt("apMoEM", apMorningEndM);
  preferences.putInt("apEvSH", apEveningStartH);
  preferences.putInt("apEvSM", apEveningStartM);
  preferences.putInt("apEvEH", apEveningEndH);
  preferences.putInt("apEvEM", apEveningEndM);
  preferences.putInt("sdSH", sdStartH);
  preferences.putInt("sdSM", sdStartM);
  preferences.putInt("sdEH", sdEndH);
  preferences.putInt("sdEM", sdEndM);

  // Parámetros de captura
  preferences.putInt("fanDur", FAN_ON_TIME);
  preferences.putInt("threshold", AREA_THRESHOLD);
  preferences.putInt("maxFans", MAX_CONCURRENT_FANS);

  preferences.end();
}

// true si la hora actual está dentro de la ventana [inicio, fin). Dos casos especiales:
//  - Modo seguro: sin hora confiable del RTC ninguna ventana está activa (la trampa no
//    opera por horario; el control manual y el botón del pin 32 siguen funcionando).
//  - Inicio = fin: ventana vacía (desactivada). Antes contaba como "siempre activa", así
//    que un horario guardado en 00:00-00:00 dejaba el AP o la luz encendidos todo el día.
bool isInsideWindow(int startH, int startM, int endH, int endM)
{
  if (!systemData.rtcAvailable)
    return false;

  int current = systemData.hour * 60 + systemData.minute;
  int start = startH * 60 + startM;
  int end = endH * 60 + endM;

  if (start == end)
    return false;
  if (start < end)
  {
    // ventana normal (no cruza medianoche)
    return (current >= start && current < end);
  }
  else
  {
    // ventana cruza medianoche
    return (current >= start || current < end);
  }
}

bool operatingWindow_AP()
{
  return isInsideWindow(apMorningStartH, apMorningStartM, apMorningEndH, apMorningEndM) ||
         isInsideWindow(apEveningStartH, apEveningStartM, apEveningEndH, apEveningEndM);
}

bool operatingWindow_LED()
{
  return isInsideWindow(ledStartH, ledStartM, ledEndH, ledEndM);
}

bool operatingWindow_SD()
{
  return isInsideWindow(sdStartH, sdStartM, sdEndH, sdEndM);
}

bool operatingWindow_Fan()
{
  return isInsideWindow(fanStartH, fanStartM, fanEndH, fanEndM);
}

// true si alguna ventana mantiene despierto al ESP32. Con la protección por bajo
// voltaje activa (o en modo solo carga) solo cuentan las del AP: luz/ventiladores/SD
// no tienen nada que hacer y quedarse despierto gastaría la batería. Con la batería
// crítica no cuenta ninguna: el WiFi la llevaría hasta el corte del BMS.
bool anyWakeWindowActive()
{
  if (criticalBatteryLock)
    return false;
  if (operatingWindow_AP())
    return true;
  if (lowBatteryLock || chargeOnlyMode)
    return false;
  return operatingWindow_LED() || operatingWindow_Fan() || operatingWindow_SD();
}
