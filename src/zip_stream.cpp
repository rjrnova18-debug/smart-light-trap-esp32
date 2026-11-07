#include <vector>
#include "zip_stream.h"
#include "state.h"
#include "web_server.h"

const uint32_t ZIP_MAX_FILES = 3000; // 12 bytes de RAM por archivo (~2 años de datos de las 4 carpetas)

struct ZipEntry
{
  uint32_t crc, size, offset;
};
static uint8_t zipOutBuf[2048];
static size_t zipOutLen = 0;
static uint32_t zipOffset = 0;

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len)
{
  static uint32_t table[256];
  static bool tableReady = false;
  if (!tableReady)
  {
    for (uint32_t i = 0; i < 256; i++)
    {
      uint32_t c = i;
      for (int k = 0; k < 8; k++)
        c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    tableReady = true;
  }
  crc = ~crc;
  while (len--)
    crc = table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

static void zipFlush()
{
  if (zipOutLen > 0)
  {
    server.sendContent((const char *)zipOutBuf, zipOutLen);
    zipOutLen = 0;
  }
}

static void zipWrite(const uint8_t *data, size_t len)
{
  zipOffset += len;
  while (len > 0)
  {
    size_t n = min(len, sizeof(zipOutBuf) - zipOutLen);
    memcpy(zipOutBuf + zipOutLen, data, n);
    zipOutLen += n;
    data += n;
    len -= n;
    if (zipOutLen == sizeof(zipOutBuf))
      zipFlush();
  }
}

static void zipWrite16(uint16_t v)
{
  uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
  zipWrite(b, 2);
}

static void zipWrite32(uint32_t v)
{
  uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  zipWrite(b, 4);
}

static void zipWriteName(const String &name)
{
  zipWrite((const uint8_t *)name.c_str(), name.length());
}

void sendZipStream(const String &baseName, const ZipWalkFn &walk)
{
  uint32_t fileCount = 0;
  walk([&](const String &, const ZipContentFn &)
       { return ++fileCount <= ZIP_MAX_FILES; });
  if (fileCount == 0)
  {
    server.send(404, "text/plain", "La carpeta está vacía");
    return;
  }
  if (fileCount > ZIP_MAX_FILES || ESP.getMaxAllocHeap() < fileCount * sizeof(ZipEntry) + 16384)
  {
    server.send(413, "text/plain", "Demasiados archivos para un solo .zip: descarga una subcarpeta");
    return;
  }

  char dateBuf[12];
  snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", systemData.year, systemData.month, systemData.day);
  String zipName = baseName + "_" + String(dateBuf) + ".zip";

  // Fecha/hora (formato DOS) de todas las entradas: la hora actual del RTC
  uint16_t dosTime = (systemData.hour << 11) | (systemData.minute << 5) | (systemData.second / 2);
  uint16_t dosDate = ((max(systemData.year, 1980) - 1980) << 9) | (systemData.month << 5) | systemData.day;

  std::vector<ZipEntry> entries;
  entries.reserve(fileCount);
  zipOutLen = 0;
  zipOffset = 0;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + zipName + "\"");
  server.send(200, "application/zip", "");

  // Pasada 1: encabezado local + datos + descriptor de cada archivo
  bool completed = walk([&](const String &name, const ZipContentFn &content)
                        {
    if (entries.size() >= fileCount)
      return false;
    ZipEntry e = {0, 0, zipOffset};
    zipWrite32(0x04034b50); // encabezado local
    zipWrite16(20);         // versión necesaria (2.0)
    zipWrite16(0x0008);     // bit 3: CRC y tamaños en el descriptor
    zipWrite16(0);          // sin compresión
    zipWrite16(dosTime);
    zipWrite16(dosDate);
    zipWrite32(0); // CRC, tamaño comprimido y original: van en el descriptor
    zipWrite32(0);
    zipWrite32(0);
    zipWrite16(name.length());
    zipWrite16(0); // sin campo extra
    zipWriteName(name);
    content([&](const uint8_t *data, size_t len)
            {
      e.crc = crc32Update(e.crc, data, len);
      e.size += len;
      zipWrite(data, len); });
    zipWrite32(0x08074b50); // descriptor de datos
    zipWrite32(e.crc);
    zipWrite32(e.size); // sin compresión: tamaño comprimido = original
    zipWrite32(e.size);
    entries.push_back(e);
    vTaskDelay(1); // deja correr las tareas de menor prioridad del núcleo 0 (watchdog)
    return (bool)server.client().connected(); });
  if (!completed)
  {
    Serial.println("Descarga .zip cancelada (cliente desconectado)");
    return;
  }

  // Pasada 2: directorio central en el mismo orden. Solo taskComm escribe en el
  // almacenamiento y está ocupada aquí, así que nada cambió entre las dos pasadas.
  uint32_t cdStart = zipOffset;
  size_t idx = 0;
  walk([&](const String &name, const ZipContentFn &)
       {
    if (idx >= entries.size())
      return false;
    const ZipEntry &e = entries[idx++];
    zipWrite32(0x02014b50); // encabezado central
    zipWrite16(20);         // versión que lo creó
    zipWrite16(20);         // versión necesaria
    zipWrite16(0x0008);
    zipWrite16(0);
    zipWrite16(dosTime);
    zipWrite16(dosDate);
    zipWrite32(e.crc);
    zipWrite32(e.size);
    zipWrite32(e.size);
    zipWrite16(name.length());
    zipWrite16(0); // campo extra
    zipWrite16(0); // comentario
    zipWrite16(0); // disco
    zipWrite16(0); // atributos internos
    zipWrite32(0); // atributos externos
    zipWrite32(e.offset);
    zipWriteName(name);
    return true; });
  uint32_t cdSize = zipOffset - cdStart;
  zipWrite32(0x06054b50); // fin del directorio central
  zipWrite16(0);
  zipWrite16(0);
  zipWrite16(idx);
  zipWrite16(idx);
  zipWrite32(cdSize);
  zipWrite32(cdStart);
  zipWrite16(0); // sin comentario
  zipFlush();
  server.sendContent(""); // fin de la respuesta chunked
  Serial.printf("Descarga .zip: %s (%u archivos, %u bytes)\n", zipName.c_str(), (unsigned)idx, (unsigned)zipOffset);
}
