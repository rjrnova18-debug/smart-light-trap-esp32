#include <ArduinoJson.h>
#include "backup_files.h"
#include "backup.h"
#include "state.h"
#include "web_server.h"
#include "zip_stream.h"

bool isFlashRequest()
{
  return server.arg("src") == "flash";
}

// Ruta del árbol virtual: "" (raíz), "BME_Data" (flujo) o "BME_Data/2026-11" (mes)
struct FlashDir
{
  int depth; // 0 = raíz, 1 = flujo, 2 = mes
  BackupStream stream;
  int year, month;
};

static bool parseFlashDir(String arg, FlashDir &d)
{
  arg.trim();
  while (arg.startsWith("/"))
    arg.remove(0, 1);
  while (arg.endsWith("/"))
    arg.remove(arg.length() - 1);
  d.depth = 0;
  if (arg == "")
    return true;

  int slash = arg.indexOf('/');
  if (!backupStreamFromFolder(slash < 0 ? arg : arg.substring(0, slash), d.stream))
    return false;
  d.depth = 1;
  if (slash < 0)
    return true;

  String month = arg.substring(slash + 1);
  if (month.length() != 7 || sscanf(month.c_str(), "%4d-%2d", &d.year, &d.month) != 2 || d.month < 1 || d.month > 12)
    return false;
  d.depth = 2;
  return true;
}

static String monthName(int year, int month)
{
  char buf[8];
  snprintf(buf, sizeof(buf), "%04d-%02d", year, month);
  return String(buf);
}

static bool backupReadyOrFail()
{
  if (systemData.backupAvailable)
    return true;
  server.send(503, "text/plain", "El respaldo interno no está disponible (ver /admin)");
  return false;
}

// Junta el CSV en bloques de 1 KB antes de enviarlo (una línea por envío sería lento)
class ChunkedSender
{
public:
  void add(const char *data, size_t len)
  {
    if (len_ + len > sizeof(buf_))
      flush();
    memcpy(buf_ + len_, data, len); // len <= CSV_LINE_MAX, siempre cabe
    len_ += len;
  }
  void flush()
  {
    if (len_ > 0)
      server.sendContent(buf_, len_);
    len_ = 0;
  }

private:
  char buf_[1024];
  size_t len_ = 0;
};

void handleArchivesFlash()
{
  if (!backupReadyOrFail())
    return;
  FlashDir d;
  if (!server.hasArg("dir") || !parseFlashDir(server.arg("dir"), d))
  {
    server.send(404, "text/plain", "Directorio no encontrado");
    return;
  }

  JsonDocument doc;
  JsonArray items = doc.to<JsonArray>();
  if (d.depth == 0)
  {
    // Raíz: los flujos que tienen datos
    for (int s = 0; s < BK_STREAM_COUNT; s++)
    {
      if (backupListMonths((BackupStream)s).empty())
        continue;
      JsonObject item = items.add<JsonObject>();
      item["name"] = backupStreamFolder((BackupStream)s);
      item["type"] = "dir";
    }
  }
  else if (d.depth == 1)
  {
    // Flujo: sus meses
    for (const String &month : backupListMonths(d.stream))
    {
      JsonObject item = items.add<JsonObject>();
      item["name"] = month;
      item["type"] = "dir";
    }
  }
  else
  {
    // Mes: un "archivo" por día con registros
    uint16_t counts[32];
    backupCountDays(d.stream, d.year, d.month, counts);
    for (int day = 1; day <= 31; day++)
    {
      if (counts[day] == 0)
        continue;
      JsonObject item = items.add<JsonObject>();
      item["name"] = backupDayFileName(d.stream, d.year, d.month, day);
      item["type"] = "file";
      item["records"] = counts[day];
    }
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleDownloadFlash()
{
  if (!backupReadyOrFail())
    return;
  FlashDir d;
  String file = server.arg("file");
  int year, month, day;
  if (!parseFlashDir(server.arg("dir"), d) || d.depth != 2 ||
      !backupParseDayFileName(d.stream, file, year, month, day) || year != d.year || month != d.month)
  {
    server.send(400, "text/plain", "Ruta inválida");
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + file + "\"");
  server.send(200, "text/csv", "");
  ChunkedSender out;
  backupExportCsv(d.stream, year, month, day, [&](const char *data, size_t len)
                  { out.add(data, len); });
  out.flush();
  server.sendContent(""); // fin de la respuesta chunked
}

// Mismo .zip que la SD: descomprimido en una SD nueva, los datos quedan en su lugar.
// Dentro del .zip todo cuelga de la carpeta pedida (igual que handleDownloadZip()).
void handleDownloadZipFlash()
{
  if (!backupReadyOrFail())
    return;
  FlashDir d;
  if (!parseFlashDir(server.arg("dir"), d))
  {
    server.send(404, "text/plain", "Carpeta no encontrada");
    return;
  }

  String zipName = "Respaldo";
  if (d.depth >= 1)
    zipName += String("_") + backupStreamFolder(d.stream);
  if (d.depth == 2)
    zipName += "_" + monthName(d.year, d.month);

  sendZipStream(zipName, [&](const ZipVisitFn &visit)
                {
    for (int s = 0; s < BK_STREAM_COUNT; s++)
    {
      if (d.depth >= 1 && s != d.stream)
        continue;
      BackupStream stream = (BackupStream)s;
      std::vector<String> months = (d.depth == 2) ? std::vector<String>{monthName(d.year, d.month)} : backupListMonths(stream);
      for (const String &monthStr : months)
      {
        int year, month;
        sscanf(monthStr.c_str(), "%4d-%2d", &year, &month);
        // Pedida la raíz o un flujo: "BME_Data/2026-11/..."; pedido un mes: "2026-11/..."
        String prefix = (d.depth == 2) ? monthStr + "/" : String(backupStreamFolder(stream)) + "/" + monthStr + "/";
        uint16_t counts[32];
        backupCountDays(stream, year, month, counts);
        for (int day = 1; day <= 31; day++)
        {
          if (counts[day] == 0)
            continue;
          bool keepGoing = visit(prefix + backupDayFileName(stream, year, month, day), [&](const ZipWriteFn &write)
                                 { backupExportCsv(stream, year, month, day, [&](const char *data, size_t len)
                                                   { write((const uint8_t *)data, len); }); });
          if (!keepGoing)
            return false;
        }
      }
    }
    return true; });
}

// En el respaldo se borra un mes o un flujo completo: un día no se puede borrar
// porque está dentro del archivo binario del mes.
void handleDeleteFlash()
{
  if (!server.authenticate(adminUser, adminPassword))
  {
    server.requestAuthentication();
    return;
  }
  if (!backupReadyOrFail())
    return;

  FlashDir d;
  String path = server.arg("path");
  JsonDocument doc;
  int status = 200;
  if (!parseFlashDir(path, d))
  {
    status = 400;
    doc["error"] = "En la memoria interna se borra un mes o una carpeta completa, no un día";
  }
  else if (d.depth == 0)
  {
    status = 403;
    doc["error"] = "Ruta protegida: borra las carpetas de una en una";
  }
  else
  {
    bool ok = true;
    if (d.depth == 2)
      ok = backupDeleteMonth(d.stream, d.year, d.month);
    else
    {
      for (const String &month : backupListMonths(d.stream))
      {
        int year, m;
        sscanf(month.c_str(), "%4d-%2d", &year, &m);
        ok = backupDeleteMonth(d.stream, year, m) && ok;
      }
    }
    Serial.printf("Respaldo interno: borrado desde el dashboard %s (%s)\n", path.c_str(), ok ? "OK" : "incompleto");
    if (ok)
      doc["message"] = "Borrado de la memoria interna: " + path;
    else
    {
      status = 500;
      doc["error"] = "No se pudo borrar todo: " + path;
    }
  }
  String response;
  serializeJson(doc, response);
  server.send(status, "application/json", response);
}

void handleFormatBackup()
{
  if (!server.authenticate(adminUser, adminPassword))
  {
    server.requestAuthentication();
    return;
  }
  if (systemData.backupAvailable)
  {
    server.send(409, "text/plain; charset=utf-8",
                "El respaldo interno funciona: no se formatea. Para liberar espacio, borra meses desde la pestaña Archivos (origen: Memoria interna).");
    return;
  }
  bool ok = backupFormat();
  server.send(ok ? 200 : 500, "text/plain; charset=utf-8",
              ok ? "Respaldo interno formateado y montado." : "No se pudo formatear el respaldo interno.");
}
