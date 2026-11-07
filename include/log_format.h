#pragma once
// Formato de los registros: eventos de batería, encabezados y líneas CSV. Lo usan la
// SD al escribir (sd_logger.cpp) y el respaldo interno al exportar (backup.cpp), así
// los dos CSV salen idénticos.
#include <Arduino.h>
#include <RTClib.h>

// Eventos del registro de batería (BAT_Data). El número se guarda en el respaldo
// binario y lo usa tools/respaldo_a_csv.py: NO reordenar; los nuevos van al final.
enum BatEvent : uint8_t
{
  EV_INICIO = 0,         // primera fila después de despertar
  EV_PERIODICO = 1,      // cada LOG_EVERY_MIN minutos mientras está despierto
  EV_DORMIR = 2,         // justo antes del deep sleep
  EV_MUESTREO = 3,       // despertar de muestreo durante el sueño (cada SAMPLE_PERIOD_MIN)
  EV_CORTE_BAJO_V = 4,   // protección por bajo voltaje: luz y ventiladores bloqueados
  EV_RECONEXION = 5,     // protección por bajo voltaje liberada
  EV_BAT_CRITICA = 6,    // batería crítica: la SD se deja de usar
  EV_MODO_CARGA_ON = 7,  // modo solo carga activado desde el dashboard
  EV_MODO_CARGA_OFF = 8, // modo solo carga desactivado
  EV_SD_FALLA = 9,       // la SD falló SD_MAX_FAILS veces seguidas y se dejó de usar
  EV_COUNT
};

const char *batEventName(uint8_t ev); // "PERIODICO", ...; "OTRO" si el número no existe

// Encabezados de los CSV (sin salto de línea)
extern const char CSV_HEADER_BME[];
extern const char CSV_HEADER_INA[];
extern const char CSV_HEADER_BAT[];
extern const char CSV_HEADER_FAN[];

// Cada función escribe UNA línea CSV terminada en '\n' y devuelve su largo.
const size_t CSV_LINE_MAX = 96;
int csvLineBME(char *buf, size_t size, const DateTime &t, float temp, float hum, float pres, float alt);
int csvLineINA(char *buf, size_t size, const DateTime &t, bool led, int fansOn, bool ap, float power_W, float energy_Wh);
int csvLineBAT(char *buf, size_t size, const DateTime &t, uint8_t ev, float volts, float current_mA, bool led, int fansOn, bool ap);
int csvLineFan(char *buf, size_t size, const DateTime &t, int area, int count);
