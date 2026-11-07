#include <SD.h>
#include "sd_logger.h"
#include "config.h"
#include "sensors.h"
#include "trap.h"
#include "backup.h"

int lastLoggedDay = -1;

// Variables para manejo de rutas de archivos
String folderBME = "/BME_Data";
String monthFolderBME = "";
String filenameBME = "";

String folderINA = "/INA_Data";
String monthFolderINA = "";
String filenameINA = "";

String folderFans = "/Fans_Data";
String monthFolderFans = "";
String filenameEvents = "";

// Registro de voltaje de batería con el evento que lo originó (ver BatEvent en
// log_format.h): INICIO al despertar, PERIODICO cada 5 min, DORMIR antes del deep
// sleep, MUESTREO durante el sueño y los eventos de la protección.
String folderBAT = "/BAT_Data";
String monthFolderBAT = "";
String filenameBAT = "";

float recuperarUltimaEnergia(String ubicacionArchivo)
{
  File file = SD.open(ubicacionArchivo, FILE_READ);
  if (!file)
  {
    Serial.println("No hay archivo previo para recuperar energía.");
    return 0.0;
  }

  String lastLine = "";
  // Leemos el archivo para encontrar la última línea con datos
  while (file.available())
  {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0)
    {
      lastLine = line;
    }
  }
  file.close();

  // Si la última línea es el encabezado o está vacía, retornar 0
  if (lastLine == "" || lastLine.startsWith("Fecha"))
  {
    return 0.0;
  }

  // Buscamos la última coma para extraer el valor de Energía(Wh)
  int lastCommaIndex = lastLine.lastIndexOf(',');
  if (lastCommaIndex != -1)
  {
    String energyStr = lastLine.substring(lastCommaIndex + 1);
    float lastEnergy = energyStr.toFloat();
    Serial.print("Energía recuperada del SD: ");
    Serial.println(lastEnergy);
    return lastEnergy;
  }
  return 0.0;
}

void updateFileNames()
{
  char dateBuffer[20];
  char monthBuffer[10];

  // Carpeta mensual: AAAA-MM
  sprintf(monthBuffer, "%04d-%02d", systemData.year, systemData.month);
  // Archivo diario: AAAA-MM-DD
  sprintf(dateBuffer, "%04d-%02d-%02d", systemData.year, systemData.month, systemData.day);

  monthFolderBME = folderBME + "/" + String(monthBuffer);
  filenameBME = monthFolderBME + "/" + String(dateBuffer) + ".csv";
  monthFolderINA = folderINA + "/" + String(monthBuffer);
  filenameINA = monthFolderINA + "/" + String(dateBuffer) + ".csv";

  monthFolderFans = folderFans + "/" + String(monthBuffer);
  filenameEvents = monthFolderFans + "/" + String(dateBuffer) + "_events.csv";

  monthFolderBAT = folderBAT + "/" + String(monthBuffer);
  filenameBAT = monthFolderBAT + "/" + String(dateBuffer) + ".csv";
}

void initLogFolders()
{
  updateFileNames();

  // -------- BME --------
  if (!SD.exists(folderBME))
  {
    if (!SD.mkdir(folderBME))
    {
      Serial.println("Error creando carpeta BME_Data");
      return;
    }
  }
  if (!SD.exists(monthFolderBME))
  {
    if (!SD.mkdir(monthFolderBME))
    {
      Serial.println("Error creando carpeta mensual BME");
      return;
    }
  }
  // -------- INA --------
  if (!SD.exists(folderINA))
  {
    if (!SD.mkdir(folderINA))
    {
      Serial.println("Error creando carpeta INA_Data");
      return;
    }
  }
  if (!SD.exists(monthFolderINA))
  {
    if (!SD.mkdir(monthFolderINA))
    {
      Serial.println("Error creando carpeta mensual INA");
      return;
    }
  }
  // -------- FANS --------
  if (!SD.exists(folderFans))
  {
    if (!SD.mkdir(folderFans))
    {
      Serial.println("Error creando carpeta Fans_Data");
      return;
    }
  }
  if (!SD.exists(monthFolderFans))
  {
    if (!SD.mkdir(monthFolderFans))
    {
      Serial.println("Error creando carpeta mensual Fans");
      return;
    }
  }
  // -------- BATERIA --------
  if (!SD.exists(folderBAT))
  {
    if (!SD.mkdir(folderBAT))
    {
      Serial.println("Error creando carpeta BAT_Data");
      return;
    }
  }
  if (!SD.exists(monthFolderBAT))
  {
    if (!SD.mkdir(monthFolderBAT))
    {
      Serial.println("Error creando carpeta mensual BAT");
      return;
    }
  }
}

// Cambio de día: rutas nuevas, carpetas, rotación del respaldo y reinicio de la
// energía diaria. Es el único lugar que toca lastLoggedDay. Solo desde taskComm con
// el mutex tomado, porque readINA() acumula la energía bajo el mismo mutex.
void checkDayRollover()
{
  if (systemData.day == lastLoggedDay)
    return;
  if (sdWritable())
    initLogFolders(); // Actualiza rutas (updateFileNames) y crea directorios si no existen
  else
    updateFileNames();
  backupCheckSpace();
  systemData.totalEnergy_Wh = 0.0; // RESET de consumo diario
  lastLoggedDay = systemData.day;
}

// ---------------------------------------------------------------------------
// Escritura en la SD
// ---------------------------------------------------------------------------

static int sdConsecutiveFails = 0;

// Lleva la cuenta de fallas seguidas. Con SD_MAX_FAILS la SD se deja de usar hasta el
// próximo arranque (una tarjeta corroída puede tardar cada intento) y queda
// constancia en el respaldo con el evento SD_FALLA.
static void sdWriteResult(bool ok)
{
  if (ok)
  {
    sdConsecutiveFails = 0;
    return;
  }
  if (++sdConsecutiveFails < SD_MAX_FAILS)
    return;
  sdConsecutiveFails = 0;
  systemData.sdAvailable = false;
  Serial.println("[ERROR] La SD falló varias veces seguidas: se deja de usar hasta el próximo arranque");
  if (backupLogAllowed(BK_BAT))
    backupLogBattery(EV_SD_FALLA);
}

// Agrega una línea al CSV del día; si el archivo no existe lo crea con su encabezado.
static bool sdAppendLine(const String &path, const char *header, const char *line, int len)
{
  if (!SD.exists(path))
  {
    File file = SD.open(path, FILE_WRITE);
    if (!file)
      return false;
    file.print(header);
    file.print('\n');
    file.close();
  }
  File file = SD.open(path, FILE_APPEND);
  if (!file)
    return false;
  bool ok = file.write((const uint8_t *)line, len) == (size_t)len;
  file.close();
  return ok;
}

// Escribe la línea en la SD si se puede y devuelve el resultado para el mensaje serial
static const char *sdWrite(const String &path, const char *header, const char *line, int len)
{
  if (!sdWritable())
    return "-";
  bool ok = sdAppendLine(path, header, line, len);
  sdWriteResult(ok);
  return ok ? "ok" : "ERROR";
}

// ---------------------------------------------------------------------------
// Registros (cada uno va a la SD y al respaldo por separado)
// ---------------------------------------------------------------------------

void logBME()
{
  char line[CSV_LINE_MAX];
  int len = csvLineBME(line, sizeof(line), systemDateTime(), systemData.temperature,
                       systemData.humidity, systemData.pressure, systemData.altitude);
  const char *sd = sdWrite(filenameBME, CSV_HEADER_BME, line, len);
  bool bk = backupLogAllowed(BK_BME);
  if (bk)
    backupLogBME();
  Serial.printf("Registro BME (SD: %s, respaldo: %s)\n", sd, bk ? "ok" : "-");
}

void logINA()
{
  // Promedio del intervalo (sin muestras nuevas, p. ej. al dormir justo después de un
  // registro, se usa la última potencia medida en vez de 0)
  float avgPower = (systemData.intervalSamples > 0) ? (systemData.powerSumInterval / systemData.intervalSamples) : systemData.power_W;

  char line[CSV_LINE_MAX];
  int len = csvLineINA(line, sizeof(line), systemDateTime(), systemData.ledState, countActiveFans(),
                       apIsActive, avgPower, systemData.totalEnergy_Wh);
  const char *sd = sdWrite(filenameINA, CSV_HEADER_INA, line, len);
  bool bk = backupLogAllowed(BK_INA);
  if (bk)
    backupLogINA(avgPower);

  // Se reinicia SIEMPRE (antes solo al escribir en la SD: sin SD el promedio se
  // arrastraba de un intervalo a otro)
  systemData.powerSumInterval = 0;
  systemData.intervalSamples = 0;
  Serial.printf("Registro INA (SD: %s, respaldo: %s)\n", sd, bk ? "ok" : "-");
}

void logBattery(BatEvent evento)
{
  char line[CSV_LINE_MAX];
  int len = csvLineBAT(line, sizeof(line), systemDateTime(), evento, systemData.busVoltage,
                       systemData.current_mA, systemData.ledState, countActiveFans(), apIsActive);
  const char *sd = sdWrite(filenameBAT, CSV_HEADER_BAT, line, len);
  bool bk = backupLogAllowed(BK_BAT);
  if (bk)
    backupLogBattery(evento);
  Serial.printf("Batería %s: %.3f V (SD: %s, respaldo: %s)\n", batEventName(evento), systemData.busVoltage, sd, bk ? "ok" : "-");
}

// Escribe los eventos de ventilador encolados por taskControl (desde taskComm).
void writePendingFanEvents()
{
  FanEvent evt;
  while (xQueueReceive(fanEventQueue, &evt, 0) == pdTRUE)
    logEvent(evt);
}

void logEvent(const FanEvent &evt)
{
  DateTime t(evt.year, evt.month, evt.day, evt.hour, evt.minute, evt.second);
  char line[CSV_LINE_MAX];
  int len = csvLineFan(line, sizeof(line), t, evt.area, evt.triggerCount);
  const char *sd = sdWrite(filenameEvents, CSV_HEADER_FAN, line, len);
  bool bk = backupLogAllowed(BK_FAN);
  if (bk)
    backupLogFanEvent(evt);
  Serial.printf("Evento área %d, conteo %d (SD: %s, respaldo: %s)\n", evt.area, evt.triggerCount, sd, bk ? "ok" : "-");
}

void logProtectionEvent(BatEvent evento)
{
  if (loggingAllowed() && xSemaphoreTake(xI2C_Mutex, pdMS_TO_TICKS(200)) == pdTRUE)
  {
    checkDayRollover();
    logBattery(evento);
    xSemaphoreGive(xI2C_Mutex);
  }
}

bool sdWritable()
{
  return systemData.sdAvailable && !criticalBatteryLock;
}

bool loggingAllowed()
{
  // Con la batería crítica la SD no se usa, pero el respaldo sigue con las filas BAT
  return sdWritable() || backupLogAllowed(BK_BAT);
}
