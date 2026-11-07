#pragma once
// Respaldo de todos los registros en la flash interna del ESP32 (LittleFS en la
// partición "spiffs"). Guarda registros binarios compactos, un archivo por flujo y
// por mes (/bme_2026-11.bin, ...), y los convierte a CSV con el mismo formato de la
// SD al descargarlos. Cómo sacar los datos: README, sección "Respaldo en la flash interna".
//
// Todo se llama desde taskComm (o desde setup() antes de crear las tareas).
#include <Arduino.h>
#include <functional>
#include <vector>
#include "state.h"
#include "log_format.h"

// Un flujo por cada carpeta de la SD
enum BackupStream : uint8_t
{
  BK_BME = 0, // BME_Data
  BK_INA,     // INA_Data
  BK_BAT,     // BAT_Data
  BK_FAN,     // Fans_Data
  BK_STREAM_COUNT
};

// -------- Registros binarios (little-endian, sin relleno) --------
// Si cambias uno, cambia también tools/respaldo_a_csv.py. ts = DateTime::unixtime()
// de la hora local del RTC (sin zona horaria).
struct __attribute__((packed)) BmeRec
{
  uint32_t ts;
  int16_t temp_c100; // °C × 100
  uint16_t hum_c100; // % × 100
  uint32_t pres_Pa;  // hPa × 100 (la altitud se recalcula al exportar)
};
struct __attribute__((packed)) InaRec
{
  uint32_t ts;
  uint8_t flags; // ver BK_FLAG_*
  uint8_t rsv[3];
  int32_t p_mW;  // potencia promedio del intervalo
  int32_t e_mWh; // energía acumulada del día
};
struct __attribute__((packed)) BatRec
{
  uint32_t ts;
  uint8_t evento; // BatEvent
  uint8_t flags;  // ver BK_FLAG_*
  uint16_t v_mV;
  int32_t i_mA10; // mA × 10
};
struct __attribute__((packed)) FanRec
{
  uint32_t ts;
  uint8_t area;     // 1-4
  uint8_t fansMask; // bit i = ventilador i+1
  uint16_t conteo;  // detecciones que dispararon el área
};
static_assert(sizeof(BmeRec) == 12 && sizeof(InaRec) == 16 && sizeof(BatRec) == 12 && sizeof(FanRec) == 8,
              "El tamaño de los registros es parte del formato del respaldo");

// flags de InaRec y BatRec: bit 0 = luz, bits 1-3 = ventiladores encendidos, bit 4 = AP
const uint8_t BK_FLAG_LED = 0x01;
const uint8_t BK_FLAG_AP = 0x10;

// -------- Montaje y escritura --------
bool backupBegin();                        // Monta (y formatea solo la primera vez); idempotente
bool backupFormat();                       // Formatea y monta; solo si el montaje falló
bool backupLogAllowed(BackupStream s);     // Montado, RTC válido y (batería no crítica o flujo BAT)
void backupLogBME();                       // Registro con los valores de systemData
void backupLogINA(float avgPower_W);       // Potencia promedio del intervalo + E_Wh del día
void backupLogBattery(BatEvent ev);        // Voltaje/corriente actuales con su evento
void backupLogFanEvent(const FanEvent &e); // Activación de un área
void backupCheckSpace();                   // Rotación: borra el mes más antiguo si pasa de BACKUP_ROTATE_PCT
float backupLastEnergyToday();             // E_Wh del último registro INA de hoy; -1 si no hay
int backupUsedPct();                       // % de la partición en uso (-1 si no está montado)

// -------- Lectura y exportación (árbol virtual, ver backup_files.cpp) --------
using CsvSink = std::function<void(const char *data, size_t len)>;

const char *backupStreamFolder(BackupStream s);                      // "BME_Data", ...
bool backupStreamFromFolder(const String &folder, BackupStream &s);  // inversa de la anterior
std::vector<String> backupListMonths(BackupStream s);                // meses con datos ("2026-11"), en orden
uint32_t backupMonthRecords(BackupStream s, int year, int month);    // registros guardados en el mes
void backupCountDays(BackupStream s, int year, int month, uint16_t counts[32]); // registros por día (1-31)
String backupDayFileName(BackupStream s, int year, int month, int day);          // "2026-11-07.csv" (Fans: "_events.csv")
bool backupParseDayFileName(BackupStream s, const String &name, int &year, int &month, int &day);
uint32_t backupExportCsv(BackupStream s, int year, int month, int day, const CsvSink &out); // day = 0: mes completo
bool backupDeleteMonth(BackupStream s, int year, int month);

void backupHandleSerial(); // Comandos "respaldo ..." por el Monitor serial (desde taskComm)
