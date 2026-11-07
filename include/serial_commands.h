#pragma once
// Comandos por el Monitor serial (115200 baudios). Sirven para sacar los datos y
// revisar la trampa por cable cuando el WiFi falla. Para usarlos con la trampa
// dormida, primero se despierta con el botón del pin 32; cada comando reconocido la
// mantiene despierta FORCE_AWAKE_MS más.
//
//   ayuda                                          lista los comandos
//   respaldo                                       resumen del respaldo interno
//   respaldo <bme|ina|bat|fan> [AAAA-MM|AAAA-MM-DD] CSV de un mes (por defecto el actual) o de un día
//   diag                                           pila libre, memoria, motivo del último reinicio

void serialCommandsPoll(); // Desde taskComm: lee el serial sin bloquear y ejecuta cada línea completa
