#pragma once
// Servidor web del punto de acceso: dashboard (embebido en el firmware), API, /admin y OTA.
#include <WebServer.h>

extern WebServer server;
extern const char *adminUser;     // /admin, /update y /borrar
extern const char *adminPassword;

void setupWebServer(); // Registra todas las rutas (el servidor arranca con el AP en taskComm)
