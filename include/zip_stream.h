#pragma once
// .zip sin compresión ("stored") armado mientras se envía por el servidor web.
// Comprimir (DEFLATE) no cabe en la RAM libre con el WiFi encendido. El CRC y los
// tamaños van en un descriptor después de cada archivo (bit 3), así cada archivo se
// lee o se genera una sola vez. Lo usan la SD (sd_files.cpp) y el respaldo interno
// (backup_files.cpp).
#include <Arduino.h>
#include <functional>

// Escribe bytes del archivo actual dentro del .zip
using ZipWriteFn = std::function<void(const uint8_t *data, size_t len)>;
// Contenido de un archivo: lo escribe con write (leyéndolo de la SD o generándolo)
using ZipContentFn = std::function<void(const ZipWriteFn &write)>;
// Por cada archivo: su nombre dentro del .zip y su contenido. Devuelve false para cortar.
using ZipVisitFn = std::function<bool(const String &name, const ZipContentFn &content)>;
// Recorrido de los archivos a empaquetar: llama a visit por cada uno, siempre en el
// mismo orden. Devuelve false si visit pidió cortar.
using ZipWalkFn = std::function<bool(const ZipVisitFn &visit)>;

// Envía el .zip "<baseName>_AAAA-MM-DD.zip". Recorre los archivos 3 veces: contar
// (responde 404 si no hay o 413 si son demasiados), datos y directorio central.
// Ocupa a taskComm mientras dura; IR, ventiladores y OLED siguen en el otro núcleo.
void sendZipStream(const String &baseName, const ZipWalkFn &walk);
