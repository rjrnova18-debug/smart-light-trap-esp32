#include "serial_commands.h"
#include "backup.h"
#include "health.h"
#include "power.h"
#include "state.h"

static void printHelp()
{
  Serial.println("Comandos:");
  Serial.println("  ayuda                                          esta lista");
  Serial.println("  respaldo                                       resumen del respaldo interno");
  Serial.println("  respaldo <bme|ina|bat|fan> [AAAA-MM|AAAA-MM-DD] CSV de un mes (por defecto el actual) o de un día");
  Serial.println("  diag                                           pila libre, memoria, motivo del último reinicio");
}

// "respaldo" o "respaldo <flujo> [fecha]". Un mes completo tarda ~40 s a 115200 baudios.
static void runBackupCommand(int argCount, const char *streamName, const char *when)
{
  if (argCount == 1)
  {
    backupPrintInfo();
    return;
  }
  BackupStream stream;
  int year = systemData.year, month = systemData.month, day = 0; // por defecto: el mes actual
  bool dateOk = (argCount < 3) || sscanf(when, "%4d-%2d-%2d", &year, &month, &day) >= 2;
  if (!backupStreamFromName(streamName, stream) || !dateOk)
  {
    Serial.println("Uso: respaldo <bme|ina|bat|fan> [AAAA-MM | AAAA-MM-DD]");
    return;
  }
  if (!systemData.backupAvailable)
  {
    backupPrintInfo(); // explica que no está montado
    return;
  }
  Serial.printf("----- %s %04d-%02d%s (CSV) -----\n", backupStreamFolder(stream), year, month, day ? " (un día)" : "");
  uint32_t rows = backupExportCsv(stream, year, month, day, [](const char *data, size_t len)
                                  { Serial.write((const uint8_t *)data, len); });
  Serial.printf("----- fin: %u filas -----\n", (unsigned)rows);
}

static void runCommand(const char *line)
{
  char command[16] = "", arg1[8] = "", arg2[16] = "";
  int argCount = sscanf(line, "%15s %7s %15s", command, arg1, arg2);
  if (argCount < 1)
    return; // línea vacía

  if (strcmp(command, "respaldo") == 0)
    runBackupCommand(argCount, arg1, arg2);
  else if (strcmp(command, "diag") == 0)
    healthPrintDiag();
  else if (strcmp(command, "ayuda") == 0)
    printHelp();
  else
  {
    // Solo los comandos reconocidos renuevan el tiempo despierto: con el USB
    // desconectado, ruido en la línea RX podría dejar la trampa despierta gastando batería.
    Serial.println("Comando desconocido (escribe: ayuda)");
    return;
  }
  // Mantiene despierta la trampa FORCE_AWAKE_MS más (igual que el botón del pin 32)
  forceAwakeStartNow();
}

void serialCommandsPoll()
{
  static char line[48];
  static size_t length = 0;
  while (Serial.available() > 0)
  {
    char c = Serial.read();
    if (c == '\r')
      continue;
    if (c != '\n')
    {
      if (length < sizeof(line) - 1)
        line[length++] = c;
      continue;
    }
    line[length] = '\0'; // línea completa: se ejecuta
    length = 0;
    runCommand(line);
  }
}
