#include <SD.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>
#include "sd_files.h"
#include "config.h"
#include "state.h"
#include "sd_logger.h"
#include "web_server.h"
#include "zip_stream.h"
#include "backup_files.h"

const int SD_MAX_DEPTH = 4; // niveles bajo la carpeta pedida (raíz/BAT_Data/2026-10/x.csv = 3)

// Ruta pedida por el dashboard ("BAT_Data/2026-10", "" = raíz) → "/BAT_Data/2026-10".
// Devuelve "" si no es válida (".." para subir de carpeta, "\" o "//").
String sdPathFromArg(String arg)
{
  arg.trim();
  while (arg.startsWith("/"))
    arg.remove(0, 1);
  while (arg.endsWith("/"))
    arg.remove(arg.length() - 1);
  if (arg.indexOf("..") >= 0 || arg.indexOf('\\') >= 0 || arg.indexOf("//") >= 0)
    return "";
  return "/" + arg;
}
String joinSdPath(const String &dir, const String &name)
{
  return dir == "/" ? "/" + name : dir + "/" + name;
}
// La raíz no se puede borrar entera desde la pestaña Archivos. El dashboard ya no
// depende de la SD (va en el firmware), así que sus copias viejas sí se pueden borrar.
bool isProtectedSdPath(const String &path)
{
  return path == "/";
}

// Recorre en profundidad los archivos de dirPath; visit recibe cada archivo abierto y
// su nombre dentro del .zip (relPrefix + nombre). Devuelve false si visit pidió cortar.
bool walkSdFiles(const String &dirPath, const String &relPrefix, int depth,
                 const std::function<bool(File &, const String &)> &visit)
{
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory())
    return true;
  bool keepGoing = true;
  while (keepGoing)
  {
    File entry = dir.openNextFile();
    if (!entry)
      break;
    String name = entry.name();
    if (entry.isDirectory())
    {
      entry.close();
      if (depth < SD_MAX_DEPTH)
        keepGoing = walkSdFiles(joinSdPath(dirPath, name), relPrefix + name + "/", depth + 1, visit);
    }
    else
    {
      keepGoing = visit(entry, relPrefix + name);
      entry.close();
    }
  }
  dir.close();
  return keepGoing;
}

// Borra un archivo o una carpeta con todo su contenido. De cada carpeta junta primero
// los nombres y después borra (borrar mientras se recorre no es seguro).
bool deleteSdPath(const String &path, int depth)
{
  File f = SD.open(path);
  if (!f)
    return false;
  if (!f.isDirectory())
  {
    f.close();
    return SD.remove(path);
  }
  std::vector<String> children;
  for (File entry = f.openNextFile(); entry; entry = f.openNextFile())
  {
    children.push_back(joinSdPath(path, entry.name()));
    entry.close();
  }
  f.close();
  if (depth >= SD_MAX_DEPTH && !children.empty())
    return false;
  bool ok = true;
  for (const String &child : children)
    ok = deleteSdPath(child, depth + 1) && ok;
  return ok && SD.rmdir(path);
}

void handleArchives()
{
  if (isFlashRequest())
  {
    handleArchivesFlash();
    return;
  }
  if (!server.hasArg("dir"))
  {
    server.send(400, "text/plain", "Falta parametro dir");
    return;
  }

  String directory = sdPathFromArg(server.arg("dir"));

  if (directory == "" || !SD.exists(directory))
  {
    server.send(404, "text/plain", "Directorio no encontrado");
    return;
  }

  File root = SD.open(directory);
  if (!root || !root.isDirectory())
  {
    server.send(404, "text/plain", "No es un directorio válido");
    return;
  }

  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  File file = root.openNextFile();

  while (file)
  {

    JsonObject item = array.add<JsonObject>();
    item["name"] = file.name();
    item["type"] = file.isDirectory() ? "dir" : "file";
    if (!file.isDirectory())
      item["size"] = file.size(); // bytes, para la pestaña Archivos

    file = root.openNextFile();
  }

  root.close();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleDownload()
{
  if (isFlashRequest())
  {
    handleDownloadFlash();
    return;
  }
  if (!server.hasArg("dir") || !server.hasArg("file"))
  {
    server.send(400, "text/plain", "Faltan parametros");
    return;
  }

  String directory = sdPathFromArg(server.arg("dir"));
  String fileName = server.arg("file");
  if (directory == "" || fileName == "" || fileName.indexOf('/') >= 0 || fileName.indexOf("..") >= 0)
  {
    server.send(400, "text/plain", "Ruta inválida");
    return;
  }
  String path = joinSdPath(directory, fileName); // también funciona en la raíz (dir vacío)

  if (!SD.exists(path))
  {
    server.send(404, "text/plain", "Archivo no encontrado");
    return;
  }

  File file = SD.open(path);

  if (!file)
  {
    server.send(500, "text/plain", "Error abriendo archivo");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
  server.streamFile(file, "text/csv");
  file.close();
}

// Copia un archivo de la SD al .zip por bloques
static void copySdFile(File &f, const ZipWriteFn &write)
{
  static uint8_t readBuf[1024];
  size_t n;
  while ((n = f.read(readBuf, sizeof(readBuf))) > 0)
    write(readBuf, n);
}

// GET /descargarZip?dir=BAT_Data/2026-10 ("" = toda la SD). Respaldo en un solo archivo.
// Ocupa a taskComm mientras dura (~5 s por mes de datos); IR, ventiladores y OLED siguen.
void handleDownloadZip()
{
  if (isFlashRequest())
  {
    handleDownloadZipFlash();
    return;
  }
  String dir = sdPathFromArg(server.arg("dir"));
  File d = (dir == "") ? File() : SD.open(dir);
  if (!d || !d.isDirectory())
  {
    server.send(404, "text/plain", "Carpeta no encontrada");
    return;
  }
  d.close();

  // Dentro del .zip todo cuelga de la carpeta pedida, así al descomprimir queda la carpeta
  String base = dir.substring(dir.lastIndexOf('/') + 1); // "" en la raíz
  String relPrefix = (base == "") ? "" : base + "/";
  String zipName = (dir == "/") ? "SD" : dir.substring(1);
  zipName.replace("/", "_");

  // Cada archivo de la SD se copia al .zip por bloques
  sendZipStream(zipName, [&](const ZipVisitFn &visit)
                { return walkSdFiles(dir, relPrefix, 0, [&](File &f, const String &rel)
                                     { return visit(rel, [&](const ZipWriteFn &write)
                                                    { copySdFile(f, write); }); }); });
}

// POST /borrar?path=BAT_Data/2026-09 — archivo o carpeta completa. Pide la clave de /admin.
void handleDeletePath()
{
  if (isFlashRequest())
  {
    handleDeleteFlash();
    return;
  }
  if (!server.authenticate(adminUser, adminPassword))
  {
    server.requestAuthentication();
    return;
  }

  String path = sdPathFromArg(server.arg("path"));
  JsonDocument doc;
  int status = 200;
  if (path == "" || isProtectedSdPath(path))
  {
    status = 403;
    doc["error"] = "Ruta protegida o inválida";
  }
  else if (!sdWritable())
  {
    status = 503;
    doc["error"] = "SD no disponible o batería crítica";
  }
  else if (!SD.exists(path))
  {
    status = 404;
    String msg = "No existe: " + path;
    doc["error"] = msg;
  }
  else
  {
    bool ok = deleteSdPath(path, 0);
    // Si se borró la carpeta del mes en uso, se recrea para que el próximo registro tenga dónde escribir
    if (xSemaphoreTake(xI2C_Mutex, pdMS_TO_TICKS(200)) == pdTRUE)
    {
      initLogFolders();
      xSemaphoreGive(xI2C_Mutex);
    }
    Serial.printf("Borrado desde el dashboard: %s (%s)\n", path.c_str(), ok ? "OK" : "incompleto");
    String msg = (ok ? "Borrado: " : "No se pudo borrar todo: ") + path;
    if (ok)
      doc["message"] = msg;
    else
    {
      status = 500;
      doc["error"] = msg;
    }
  }
  String response;
  serializeJson(doc, response);
  server.send(status, "application/json", response);
}
