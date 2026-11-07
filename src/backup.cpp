#include <LittleFS.h>
#include <Preferences.h>
#include <algorithm>
#include "backup.h"
#include "config.h"
#include "sensors.h"
#include "trap.h"

// Por flujo: prefijo del archivo mensual, carpeta en la SD / árbol virtual,
// tamaño del registro y encabezado del CSV.
static const char *const STREAM_PREFIX[BK_STREAM_COUNT] = {"bme", "ina", "bat", "fan"};
static const char *const STREAM_FOLDER[BK_STREAM_COUNT] = {"BME_Data", "INA_Data", "BAT_Data", "Fans_Data"};
static const size_t STREAM_REC_SIZE[BK_STREAM_COUNT] = {sizeof(BmeRec), sizeof(InaRec), sizeof(BatRec), sizeof(FanRec)};
static const char *const STREAM_HEADER[BK_STREAM_COUNT] = {CSV_HEADER_BME, CSV_HEADER_INA, CSV_HEADER_BAT, CSV_HEADER_FAN};

// % de uso calculado después de cada escritura o borrado (usedBytes() recorre el
// sistema de archivos: no conviene llamarlo cada vez que la OLED o /estado lo piden).
// El total no cambia: se lee una vez al montar.
static size_t totalBytesCache = 0;
static int usedPctCache = -1;

static void refreshUsage()
{
  if (totalBytesCache == 0)
    totalBytesCache = LittleFS.totalBytes();
  usedPctCache = totalBytesCache ? (int)((uint64_t)LittleFS.usedBytes() * 100 / totalBytesCache) : -1;
}

static void monthPath(char *buf, size_t size, BackupStream s, int year, int month)
{
  snprintf(buf, size, "/%s_%04d-%02d.bin", STREAM_PREFIX[s], year, month);
}

// ---------------------------------------------------------------------------
// Montaje
// ---------------------------------------------------------------------------

// La primera vez (sin la marca bkFmt en NVS) la partición viene vacía o con basura y
// se formatea. Después, si el montaje falla NO se formatea: los datos todavía se
// pueden sacar con esptool (ver README). Se formatea a mano desde /admin.
bool backupBegin()
{
  if (systemData.backupAvailable)
    return true;

  Preferences prefs;
  prefs.begin("backup", false);
  bool firstTime = !prefs.getBool("bkFmt", false);
  if (firstTime)
    Serial.println("Respaldo interno: primer montaje (se formatea si hace falta, puede tardar unos segundos)...");
  bool ok = LittleFS.begin(firstTime, BACKUP_MOUNT_PATH, 5, BACKUP_PARTITION);
  if (ok && firstTime)
    prefs.putBool("bkFmt", true);
  prefs.end();

  systemData.backupAvailable = ok;
  if (!ok)
  {
    Serial.println("[ERROR] No se pudo montar el respaldo interno. No se formatea para no borrar datos: ver /admin");
    return false;
  }
  refreshUsage();
  Serial.printf("Respaldo interno montado: %d %% en uso\n", usedPctCache);
  backupCheckSpace();
  return true;
}

bool backupFormat()
{
  if (systemData.backupAvailable)
    return false; // montado: para borrar datos se usa "Borrar" por mes o por flujo
  Serial.println("Respaldo interno: formateando a pedido de /admin...");
  bool ok = LittleFS.begin(true, BACKUP_MOUNT_PATH, 5, BACKUP_PARTITION); // formatea si no monta
  if (ok)
  {
    Preferences prefs;
    prefs.begin("backup", false);
    prefs.putBool("bkFmt", true);
    prefs.end();
    refreshUsage();
  }
  systemData.backupAvailable = ok;
  return ok;
}

bool backupLogAllowed(BackupStream s)
{
  // Con la batería crítica la SD no se toca, pero el respaldo sigue guardando las
  // filas BAT: LittleFS está hecho para sobrevivir cortes de energía.
  return systemData.backupAvailable && systemData.rtcAvailable && (!criticalBatteryLock || s == BK_BAT);
}

int backupUsedPct()
{
  return systemData.backupAvailable ? usedPctCache : -1;
}

// ---------------------------------------------------------------------------
// Escritura
// ---------------------------------------------------------------------------

// Abre, agrega y cierra: LittleFS confirma el registro completo o nada. El archivo
// se elige por la fecha del propio registro (un evento de las 23:59:59 escrito a
// las 00:00:01 del mes siguiente queda en su mes).
static bool appendRecord(BackupStream s, const void *rec, const DateTime &t)
{
  size_t len = STREAM_REC_SIZE[s];
  char path[24];
  monthPath(path, sizeof(path), s, t.year(), t.month());

  File f = LittleFS.open(path, FILE_APPEND);
  if (!f)
  {
    Serial.printf("[ERROR] Respaldo: no se pudo abrir %s\n", path);
    return false;
  }
  // Cola incompleta (no debería pasar): se rellena con ceros para que los registros
  // siguientes queden alineados. Al leer, el registro de relleno se descarta (su ts
  // no corresponde al mes del archivo).
  size_t tail = f.size() % len;
  if (tail)
  {
    static const uint8_t zeros[16] = {0};
    f.write(zeros, len - tail);
  }
  bool ok = f.write((const uint8_t *)rec, len) == len;
  f.close();
  if (!ok)
    Serial.printf("[ERROR] Respaldo: escritura incompleta en %s\n", path);
  refreshUsage();
  return ok;
}

static uint8_t currentFlags()
{
  int fans = countActiveFans();
  return (systemData.ledState ? BK_FLAG_LED : 0) | ((fans & 0x07) << 1) | (apIsActive ? BK_FLAG_AP : 0);
}

void backupLogBME()
{
  DateTime t = systemDateTime();
  BmeRec r;
  r.ts = t.unixtime();
  r.temp_c100 = (int16_t)lroundf(constrain(systemData.temperature, -300.0f, 300.0f) * 100);
  r.hum_c100 = (uint16_t)lroundf(constrain(systemData.humidity, 0.0f, 100.0f) * 100);
  r.pres_Pa = (uint32_t)lroundf(constrain(systemData.pressure, 0.0f, 2000.0f) * 100);
  appendRecord(BK_BME, &r, t);
}

void backupLogINA(float avgPower_W)
{
  DateTime t = systemDateTime();
  InaRec r = {};
  r.ts = t.unixtime();
  r.flags = currentFlags();
  r.p_mW = lroundf(avgPower_W * 1000);
  r.e_mWh = lroundf(systemData.totalEnergy_Wh * 1000);
  appendRecord(BK_INA, &r, t);
}

void backupLogBattery(BatEvent ev)
{
  DateTime t = systemDateTime();
  BatRec r;
  r.ts = t.unixtime();
  r.evento = ev;
  r.flags = currentFlags();
  r.v_mV = (uint16_t)lroundf(constrain(systemData.busVoltage, 0.0f, 60.0f) * 1000);
  r.i_mA10 = lroundf(systemData.current_mA * 10);
  appendRecord(BK_BAT, &r, t);
}

void backupLogFanEvent(const FanEvent &e)
{
  DateTime t(e.year, e.month, e.day, e.hour, e.minute, e.second);
  FanRec r;
  r.ts = t.unixtime();
  r.area = e.area;
  r.fansMask = e.fansMask;
  r.conteo = (uint16_t)constrain(e.triggerCount, 0, 65535);
  appendRecord(BK_FAN, &r, t);
}

// ---------------------------------------------------------------------------
// Lectura
// ---------------------------------------------------------------------------

// Recorre los registros de un mes en orden. Descarta los que no son de ese mes
// (relleno o basura) y una cola incompleta al final. fn devuelve false para cortar.
static void forEachRecord(BackupStream s, int year, int month,
                          const std::function<bool(const uint8_t *rec, const DateTime &t)> &fn)
{
  char path[24];
  monthPath(path, sizeof(path), s, year, month);
  if (!LittleFS.exists(path))
    return;
  File f = LittleFS.open(path, FILE_READ);
  if (!f)
    return;

  const size_t len = STREAM_REC_SIZE[s];
  uint8_t buf[480]; // múltiplo de 8, 12 y 16: cada lectura trae registros completos
  bool keepGoing = true;
  size_t n;
  while (keepGoing && (n = f.read(buf, sizeof(buf))) >= len)
  {
    for (size_t off = 0; keepGoing && off + len <= n; off += len)
    {
      uint32_t ts;
      memcpy(&ts, buf + off, sizeof(ts));
      DateTime t(ts);
      if (t.year() != year || t.month() != month)
        continue;
      keepGoing = fn(buf + off, t);
    }
  }
  f.close();
}

// Convierte un registro binario a su línea CSV (mismo formato que la SD)
static int recordToCsv(BackupStream s, const uint8_t *p, const DateTime &t, char *line, size_t size)
{
  switch (s)
  {
  case BK_BME:
  {
    BmeRec r;
    memcpy(&r, p, sizeof(r));
    float pres = r.pres_Pa / 100.0f;
    float alt = 44330.0f * (1.0f - powf(pres / SEALEVELPRESSURE_HPA, 0.1903f)); // igual que Adafruit_BME280::readAltitude()
    return csvLineBME(line, size, t, r.temp_c100 / 100.0f, r.hum_c100 / 100.0f, pres, alt);
  }
  case BK_INA:
  {
    InaRec r;
    memcpy(&r, p, sizeof(r));
    return csvLineINA(line, size, t, r.flags & BK_FLAG_LED, (r.flags >> 1) & 0x07, r.flags & BK_FLAG_AP,
                      r.p_mW / 1000.0f, r.e_mWh / 1000.0f);
  }
  case BK_BAT:
  {
    BatRec r;
    memcpy(&r, p, sizeof(r));
    return csvLineBAT(line, size, t, r.evento, r.v_mV / 1000.0f, r.i_mA10 / 10.0f,
                      r.flags & BK_FLAG_LED, (r.flags >> 1) & 0x07, r.flags & BK_FLAG_AP);
  }
  case BK_FAN:
  {
    FanRec r;
    memcpy(&r, p, sizeof(r));
    return csvLineFan(line, size, t, r.area, r.conteo);
  }
  default:
    return 0;
  }
}

uint32_t backupExportCsv(BackupStream s, int year, int month, int day, const CsvSink &out)
{
  char line[CSV_LINE_MAX];
  int n = snprintf(line, sizeof(line), "%s\n", STREAM_HEADER[s]);
  out(line, n);
  uint32_t rows = 0;
  forEachRecord(s, year, month, [&](const uint8_t *rec, const DateTime &t)
                {
    if (day == 0 || t.day() == day)
    {
      out(line, recordToCsv(s, rec, t, line, sizeof(line)));
      rows++;
    }
    return true; });
  return rows;
}

float backupLastEnergyToday()
{
  if (!systemData.backupAvailable || !systemData.rtcAvailable)
    return -1;
  bool found = false;
  int32_t lastEnergy = 0;
  forEachRecord(BK_INA, systemData.year, systemData.month, [&](const uint8_t *rec, const DateTime &t)
                {
    if (t.day() == systemData.day)
    {
      InaRec r;
      memcpy(&r, rec, sizeof(r));
      lastEnergy = r.e_mWh;
      found = true;
    }
    return true; });
  return found ? lastEnergy / 1000.0f : -1;
}

// ---------------------------------------------------------------------------
// Árbol virtual: carpetas, meses y días
// ---------------------------------------------------------------------------

const char *backupStreamFolder(BackupStream s)
{
  return s < BK_STREAM_COUNT ? STREAM_FOLDER[s] : "";
}

bool backupStreamFromFolder(const String &folder, BackupStream &s)
{
  for (int i = 0; i < BK_STREAM_COUNT; i++)
  {
    if (folder == STREAM_FOLDER[i])
    {
      s = (BackupStream)i;
      return true;
    }
  }
  return false;
}

// "bme_2026-11.bin" -> "2026-11" si el archivo es del flujo s
static bool monthFromFileName(BackupStream s, String name, String &month)
{
  int slash = name.lastIndexOf('/');
  if (slash >= 0)
    name = name.substring(slash + 1);
  String prefix = String(STREAM_PREFIX[s]) + "_";
  if (!name.startsWith(prefix) || !name.endsWith(".bin") || name.length() != prefix.length() + 11)
    return false;
  month = name.substring(prefix.length(), prefix.length() + 7);
  int y, m;
  return sscanf(month.c_str(), "%4d-%2d", &y, &m) == 2 && m >= 1 && m <= 12;
}

std::vector<String> backupListMonths(BackupStream s)
{
  std::vector<String> months;
  if (!systemData.backupAvailable)
    return months;
  File root = LittleFS.open("/");
  if (!root)
    return months;
  for (File f = root.openNextFile(); f; f = root.openNextFile())
  {
    String month;
    if (!f.isDirectory() && f.size() > 0 && monthFromFileName(s, f.name(), month))
      months.push_back(month);
    f.close();
  }
  root.close();
  std::sort(months.begin(), months.end());
  return months;
}

uint32_t backupMonthRecords(BackupStream s, int year, int month)
{
  char path[24];
  monthPath(path, sizeof(path), s, year, month);
  File f = LittleFS.open(path, FILE_READ);
  if (!f)
    return 0;
  uint32_t n = f.size() / STREAM_REC_SIZE[s];
  f.close();
  return n;
}

void backupCountDays(BackupStream s, int year, int month, uint16_t counts[32])
{
  memset(counts, 0, 32 * sizeof(uint16_t));
  forEachRecord(s, year, month, [&](const uint8_t *, const DateTime &t)
                {
    if (counts[t.day()] < 0xFFFF)
      counts[t.day()]++;
    return true; });
}

// Mismos nombres que los CSV diarios de la SD (ver updateFileNames())
String backupDayFileName(BackupStream s, int year, int month, int day)
{
  char name[32];
  snprintf(name, sizeof(name), "%04d-%02d-%02d%s", year, month, day, s == BK_FAN ? "_events.csv" : ".csv");
  return String(name);
}

bool backupParseDayFileName(BackupStream s, const String &name, int &year, int &month, int &day)
{
  if (sscanf(name.c_str(), "%4d-%2d-%2d", &year, &month, &day) != 3)
    return false;
  return month >= 1 && month <= 12 && day >= 1 && day <= 31 && name == backupDayFileName(s, year, month, day);
}

bool backupDeleteMonth(BackupStream s, int year, int month)
{
  char path[24];
  monthPath(path, sizeof(path), s, year, month);
  bool ok = !LittleFS.exists(path) || LittleFS.remove(path);
  refreshUsage();
  return ok;
}

// ---------------------------------------------------------------------------
// Rotación
// ---------------------------------------------------------------------------

// Si el uso pasa de BACKUP_ROTATE_PCT, borra los archivos del mes más antiguo (de los
// 4 flujos) hasta bajar del umbral. Nunca borra el mes actual. Se llama al montar y
// en cada cambio de día (no antes de cada escritura: usedBytes() recorre todo).
void backupCheckSpace()
{
  if (!systemData.backupAvailable || !systemData.rtcAvailable)
    return;
  char current[8];
  snprintf(current, sizeof(current), "%04d-%02d", systemData.year, systemData.month);

  while (true)
  {
    refreshUsage();
    if (usedPctCache < BACKUP_ROTATE_PCT)
      return;
    String oldest = "";
    for (int s = 0; s < BK_STREAM_COUNT; s++)
    {
      std::vector<String> months = backupListMonths((BackupStream)s);
      if (!months.empty() && (oldest == "" || months.front() < oldest))
        oldest = months.front();
    }
    if (oldest == "" || oldest >= current)
    {
      Serial.printf("[AVISO] Respaldo interno al %d %% y solo queda el mes actual: descarga y borra datos\n", usedPctCache);
      return;
    }
    int y, m;
    sscanf(oldest.c_str(), "%4d-%2d", &y, &m);
    for (int s = 0; s < BK_STREAM_COUNT; s++)
      backupDeleteMonth((BackupStream)s, y, m);
    Serial.printf("Respaldo interno: se borró el mes %s para liberar espacio\n", oldest.c_str());
  }
}

// ---------------------------------------------------------------------------
// Comandos por el Monitor serial (vía de extracción si el WiFi falla)
// ---------------------------------------------------------------------------

static void printBackupInfo()
{
  if (!systemData.backupAvailable)
  {
    Serial.println("Respaldo interno: NO montado (ver /admin para formatear)");
    return;
  }
  Serial.printf("Respaldo interno: %u KB de %u KB en uso (%d %%)\n",
                (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(totalBytesCache / 1024), usedPctCache);
  for (int s = 0; s < BK_STREAM_COUNT; s++)
  {
    Serial.printf("  %-9s:", STREAM_FOLDER[s]);
    std::vector<String> months = backupListMonths((BackupStream)s);
    if (months.empty())
      Serial.print(" (vacío)");
    for (const String &month : months)
    {
      int y, m;
      sscanf(month.c_str(), "%4d-%2d", &y, &m);
      Serial.printf(" %s (%u reg)", month.c_str(), (unsigned)backupMonthRecords((BackupStream)s, y, m));
    }
    Serial.println();
  }
  Serial.println("Para descargar: respaldo <bme|ina|bat|fan> [AAAA-MM | AAAA-MM-DD]");
}

static void runSerialCommand(const char *cmd)
{
  char word[16] = "", type[8] = "", when[16] = "";
  int n = sscanf(cmd, "%15s %7s %15s", word, type, when);
  if (n < 1)
    return;
  if (strcmp(word, "respaldo") != 0)
  {
    Serial.println("Comandos: respaldo | respaldo <bme|ina|bat|fan> [AAAA-MM | AAAA-MM-DD]");
    return;
  }
  // Cada comando mantiene despierta la trampa 5 min (igual que el botón del pin 32)
  forceAwake = true;
  forceAwakeStart = millis();

  if (n == 1)
  {
    printBackupInfo();
    return;
  }
  int stream = -1;
  for (int s = 0; s < BK_STREAM_COUNT; s++)
    if (strcmp(type, STREAM_PREFIX[s]) == 0)
      stream = s;
  int year = systemData.year, month = systemData.month, day = 0;
  if (stream < 0 || (n == 3 && sscanf(when, "%4d-%2d-%2d", &year, &month, &day) < 2))
  {
    Serial.println("Uso: respaldo <bme|ina|bat|fan> [AAAA-MM | AAAA-MM-DD]");
    return;
  }
  if (!systemData.backupAvailable)
  {
    printBackupInfo();
    return;
  }
  Serial.printf("----- %s %04d-%02d%s (CSV) -----\n", STREAM_FOLDER[stream], year, month, day ? " (un día)" : "");
  uint32_t rows = backupExportCsv((BackupStream)stream, year, month, day, [](const char *data, size_t len)
                                  { Serial.write((const uint8_t *)data, len); });
  Serial.printf("----- fin: %u filas -----\n", (unsigned)rows);
}

void backupHandleSerial()
{
  static char line[48];
  static size_t len = 0;
  while (Serial.available() > 0)
  {
    char c = Serial.read();
    if (c == '\r')
      continue;
    if (c != '\n')
    {
      if (len < sizeof(line) - 1)
        line[len++] = c;
      continue;
    }
    line[len] = '\0';
    len = 0;
    runSerialCommand(line);
  }
}
