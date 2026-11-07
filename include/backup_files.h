#pragma once
// Respaldo interno en el explorador del dashboard. Las rutas de la SD (/archivos,
// /descargar, /descargarZip, /borrar) con src=flash muestran el respaldo como un
// árbol virtual con las mismas carpetas que la SD:
//   BME_Data/2026-11/2026-11-07.csv    Fans_Data/2026-11/2026-11-07_events.csv
// Los CSV se generan al vuelo desde los registros binarios (ver backup.h).

bool isFlashRequest();         // ¿la petición trae src=flash?
void handleArchivesFlash();    // GET /archivos?src=flash&dir=           flujos, meses o días (con "records")
void handleDownloadFlash();    // GET /descargar?src=flash&dir=&file=    CSV de un día
void handleDownloadZipFlash(); // GET /descargarZip?src=flash&dir=       .zip con la estructura de la SD
void handleDeleteFlash();      // POST /borrar?src=flash&path=           un mes o un flujo completo (clave de admin)
void handleFormatBackup();     // POST /respaldo/formatear               solo si el montaje falló (clave de admin)
