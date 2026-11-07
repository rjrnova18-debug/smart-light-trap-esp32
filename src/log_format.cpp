#include "log_format.h"

static const char *const BAT_EVENT_NAMES[EV_COUNT] = {
    "INICIO", "PERIODICO", "DORMIR", "MUESTREO", "CORTE_BAJO_V",
    "RECONEXION", "BAT_CRITICA", "MODO_CARGA_ON", "MODO_CARGA_OFF", "SD_FALLA"};

const char *batEventName(uint8_t ev)
{
  return ev < EV_COUNT ? BAT_EVENT_NAMES[ev] : "OTRO";
}

const char CSV_HEADER_BME[] = "Fecha,Hora,Temp,Hum,Pres,Alt";
const char CSV_HEADER_INA[] = "Fecha,Hora,LED,FANS,AP,P_W,E_Wh";
const char CSV_HEADER_BAT[] = "Fecha,Hora,Evento,V_bat,I_mA,LED,FANS,AP";
const char CSV_HEADER_FAN[] = "Fecha,Hora,Evento,Area,Conteo";

// snprintf devuelve el largo que habría tenido la línea completa: se limita al buffer
static int clampLength(int n, size_t size)
{
  if (n < 0)
    return 0;
  return (size_t)n < size ? n : (int)size - 1;
}

// BME e INA llevan la hora sin segundos (HH:MM); BAT y eventos con segundos.
int csvLineBME(char *buf, size_t size, const DateTime &t, float temp, float hum, float pres, float alt)
{
  return clampLength(snprintf(buf, size, "%02d/%02d/%04d,%02d:%02d,%.2f,%.2f,%.2f,%.2f\n",
                              t.day(), t.month(), t.year(), t.hour(), t.minute(),
                              temp, hum, pres, alt),
                     size);
}

int csvLineINA(char *buf, size_t size, const DateTime &t, bool led, int fansOn, bool ap, float power_W, float energy_Wh)
{
  return clampLength(snprintf(buf, size, "%02d/%02d/%04d,%02d:%02d,%d,%d,%d,%.3f,%.3f\n",
                              t.day(), t.month(), t.year(), t.hour(), t.minute(),
                              led ? 1 : 0, fansOn, ap ? 1 : 0, power_W, energy_Wh),
                     size);
}

// V_bat = voltaje de bus del INA219 (lado de la carga, ≈ batería: el INA está después del BMS)
int csvLineBAT(char *buf, size_t size, const DateTime &t, uint8_t ev, float volts, float current_mA, bool led, int fansOn, bool ap)
{
  return clampLength(snprintf(buf, size, "%02d/%02d/%04d,%02d:%02d:%02d,%s,%.3f,%.1f,%d,%d,%d\n",
                              t.day(), t.month(), t.year(), t.hour(), t.minute(), t.second(),
                              batEventName(ev), volts, current_mA, led ? 1 : 0, fansOn, ap ? 1 : 0),
                     size);
}

int csvLineFan(char *buf, size_t size, const DateTime &t, int area, int count)
{
  return clampLength(snprintf(buf, size, "%02d/%02d/%04d,%02d:%02d:%02d,FAN_ON,Area_%d,Count_%d\n",
                              t.day(), t.month(), t.year(), t.hour(), t.minute(), t.second(),
                              area, count),
                     size);
}
