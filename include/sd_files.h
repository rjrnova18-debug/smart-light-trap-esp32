#pragma once
// Explorador de archivos de la SD (pestaña "Archivos" del dashboard): listar,
// descargar archivos o carpetas completas en .zip y borrar. Con src=flash cada
// ruta delega en backup_files.h (respaldo interno, mismas carpetas que la SD).

void handleArchives();    // GET /archivos?dir=      lista una carpeta (con tamaños)
void handleDownload();    // GET /descargar?dir=&file=
void handleDownloadZip(); // GET /descargarZip?dir=  carpeta completa como .zip (respaldo)
void handleDeletePath();  // POST /borrar?path=      pide la clave de /admin
