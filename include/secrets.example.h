#pragma once
// Plantilla de claves. Cópiala como include/secrets.h y completa los valores.
// secrets.h está en .gitignore: las claves reales no se suben al repositorio.

// Punto de acceso WiFi de la trampa (cada trampa con su número)
#define WIFI_SSID "LightTrap_1_ESP32Server"
#define WIFI_PASSWORD "cambia-esta-clave" // mínimo 8 caracteres

// Usuario y clave para /admin (OTA y descarga de datos), para borrar archivos de la SD
// o del respaldo interno y para formatear el respaldo
#define ADMIN_USER "admin"
#define ADMIN_PASSWORD "cambia-esta-clave"
