# Light Trap — trampa de luz solar con ESP32

Firmware de una trampa de luz autónoma para el monitoreo de *Spodoptera frugiperda*.
Funciona con energía solar en campo (Santa Marta, Colombia):

- Enciende una luz atractora en el horario configurado.
- Cuenta los insectos con sensores IR y activa ventiladores de captura por área.
- Registra el clima y la energía en una tarjeta SD y, como respaldo, en la flash interna
  del ESP32.
- Ofrece un dashboard web por su propio punto de acceso WiFi.

## Funciones

- **Captura:** sensores IR leídos por un multiplexor y agrupados por área. Al llegar al
  umbral de detecciones, se encienden los ventiladores del área, con un límite de
  ventiladores simultáneos.
- **Horarios configurables:** luz, ventiladores, ventana extra de la SD y dos franjas
  del punto de acceso (mañana y tarde/noche). Una ventana con inicio = fin queda
  desactivada.
- **Ahorro de energía:** deep sleep fuera de los horarios. Durante el sueño despierta cada
  30 min para registrar el voltaje de la batería.
- **Protección de batería:**
  - Bajo 11.0 V se bloquean la luz y los ventiladores; se reconectan sobre 11.4 V.
  - Bajo 10.2 V (batería crítica) deja de escribir en la SD y duerme; se reanuda sobre
    10.6 V. Esto evita dañar la SD si el BMS corta la energía. El respaldo interno sigue
    registrando el voltaje.
- **Respaldo en la flash interna:** cada registro se guarda también dentro del ESP32, por
  si la SD falla (ver [más abajo](#respaldo-en-la-flash-interna)).
- **Modo solo carga:** la trampa no opera en automático, para verificar que el panel
  recarga la batería.
- **Dashboard web:**
  - Datos en tiempo real y control manual.
  - Gráficas del historial, de la SD o del respaldo interno.
  - Configuración de horarios, hora del RTC y pantalla OLED.
  - Pestaña **Archivos**: explorar la SD o el respaldo interno, descargar carpetas completas
    en `.zip` y borrar datos con clave de administrador.
  - Va dentro del firmware, comprimido con gzip, así que funciona aunque la SD falle o no
    esté puesta.
- **Mantenimiento remoto (`/admin`):** actualizar el firmware por WiFi (OTA) sin abrir la
  trampa (con eso también se actualiza el dashboard) y descargar todos los datos.
- **Robustez para operar sola en campo:** watchdog de tareas, vuelta automática al
  firmware anterior si una OTA falla y modo seguro si el reloj deja de dar la hora (ver
  [Robustez](#robustez)).

## Hardware

| Componente | Detalle |
|---|---|
| Microcontrolador | ESP32 DOIT DevKit V1 (4 MB de flash) |
| Sensores | BME280 (clima), INA219 (voltaje/corriente de toda la carga), 16 canales IR vía multiplexor de 16 canales |
| Reloj | RTC DS1307 |
| Pantalla | OLED SSD1306 128×64 (I2C, opcional: sin ella la trampa sigue funcionando) |
| Almacenamiento | Módulo microSD por SPI + respaldo LittleFS en la flash interna |
| Cargas | Luz LED atractora y 4 ventiladores |
| Energía | Panel solar 20 W → controlador CN3722 (MPPT) → BMS 3S → batería Li-ion 3S 11.1 V 10 Ah |

Los pines están en [include/config.h](include/config.h):

| Uso | Pines |
|---|---|
| I2C (SDA, SCL) | 21, 22 |
| SD (CS) | 5 |
| Multiplexor (S0–S3, SIG) | 25, 33, 4, 15, 34 |
| Ventiladores 1–4 | 27, 14, 12, 13 |
| Luz LED | 26 |
| Botón de pantalla | 35 |
| Botón de despertar / WiFi 5 min | 32 |

## Estructura

Los encabezados (`.h`) están en `include/` y las implementaciones (`.cpp`) en `src/`.
Cada módulo tiene su par con el mismo nombre, por ejemplo `include/power.h` +
`src/power.cpp`.

```
include/
  config.h            pines, tiempos y umbrales de batería
  secrets.example.h   plantilla de SSID y claves (secrets.h no se sube)
  admin_page.h        página /admin embebida en el firmware
  *.h                 interfaz de cada módulo de src/
src/
  main.cpp            setup(), loop() y las tareas FreeRTOS
  state.cpp           estado compartido entre módulos
  settings.cpp        horarios (NVS) y ventanas de operación
  sensors.cpp         RTC, BME280, INA219
  trap.cpp            luz, sensores IR, áreas y ventiladores
  power.cpp           protección de batería, muestreo y deep sleep
  sd_logger.cpp       registro de cada dato en la SD y en el respaldo
  log_format.cpp      formato CSV y eventos de batería (común a la SD y al respaldo)
  backup.cpp          respaldo en la flash interna (LittleFS, registros binarios)
  backup_files.cpp    el respaldo en el explorador del dashboard (src=flash)
  sd_files.cpp        explorador de la SD: listar, .zip y borrar
  zip_stream.cpp      .zip armado mientras se envía (SD y respaldo)
  health.cpp          salud del firmware: watchdog, rollback de la OTA y diagnóstico
  serial_commands.cpp comandos por el Monitor serial (respaldo, diag, ayuda)
  display.cpp         OLED y botón de pantalla
  web_server.cpp      dashboard, API, /admin y OTA
web/                  dashboard: dashboard.html, style.css, chart.js (se editan aquí)
tools/
  embed_web.py        comprime web/ y genera include/web_files.h al compilar
  respaldo_a_csv.py   convierte el respaldo extraído con esptool a CSV (emergencia)
```

### Cómo llega el dashboard al firmware

En cada compilación, `tools/embed_web.py` (configurado en `extra_scripts` de
`platformio.ini`) comprime con gzip los archivos de `web/` (336 KB → 93 KB). Los escribe
como arreglos de bytes en `include/web_files.h`, y el servidor los envía tal cual con
`Content-Encoding: gzip`. El navegador los descomprime.

`web_files.h` se genera solo y no se sube al repositorio. Si no cambias `web/`, no se
reescribe ni se recompila.

El firmware usa tres tareas FreeRTOS:

| Tarea | Núcleo | Qué hace |
|---|---|---|
| `taskControl` | 1 | Sensores IR, luz y ventiladores (cada 2 ms) |
| `taskUI` | 1 | RTC, BME280, INA219 y OLED |
| `taskComm` | 0 | WiFi, servidor web, registros, comandos serie y deep sleep. Es la única tarea que escribe en la SD y en el respaldo |

Las tres están vigiladas por el watchdog (ver [Robustez](#robustez)). La tarea de Arduino
(`loop()`) se borra al terminar `setup()`: así el núcleo 1 puede entrar en reposo.

## Puesta en marcha

1. **Claves:** copia `include/secrets.example.h` como `include/secrets.h` y completa el
   SSID (uno distinto por trampa) y las claves.
2. **Compilar y subir** con [PlatformIO](https://platformio.org/):
   ```
   pio run -t upload
   pio device monitor
   ```
   El primer arranque formatea la partición del respaldo interno (tarda unos segundos).
3. **Tarjeta SD:** solo hay que formatearla en FAT32. Se recomienda
   [SD Card Formatter](https://www.sdcard.org/downloads/formatter/). Las carpetas de datos
   se crean solas.
4. **Usar:** conéctate al WiFi de la trampa dentro de su horario o pulsando el botón del
   pin 32, y abre `http://192.168.4.1`.

### Actualizar por WiFi

1. Si cambias el dashboard, edita los archivos de `web/`.
2. Compila con `pio run`.
3. En `http://192.168.4.1/admin` sube `.pio/build/esp32doit-devkit-v1/firmware.bin`. Ese
   archivo incluye el firmware y el dashboard. La OTA no toca el respaldo interno.

## Datos registrados

Hay un archivo CSV por día, dentro de una carpeta por mes (`AAAA-MM`):

| Carpeta | Contenido |
|---|---|
| `BME_Data` | Fecha, hora, temperatura, humedad, presión, altitud (cada 5 min) |
| `INA_Data` | Fecha, hora, estado de luz, ventiladores y AP, potencia y energía acumulada del día (cada 5 min) |
| `BAT_Data` | Voltaje y corriente con el evento que lo originó: `INICIO`, `PERIODICO`, `MUESTREO`, `DORMIR`, `CORTE_BAJO_V`, `RECONEXION`, `BAT_CRITICA`, `MODO_CARGA_ON/OFF`, `SD_FALLA`, `REINICIO_ERROR` (el arranque anterior terminó por un error o el watchdog), `REINICIO_BROWNOUT` (por una caída de voltaje) |
| `Fans_Data` | Cada activación de un área (enciende sus ventiladores) con el conteo que la disparó |

> Desde el 07-11-2026, `Fans_Data` tiene una fila por activación de **área** con el número
> de área real. Los archivos anteriores tienen una fila por ventilador y, en la columna
> `Area`, el número de ventilador.

Para respaldar todo de una vez: pestaña **Archivos** → **Descargar carpeta actual (.zip)**
en la raíz.

## Respaldo en la flash interna

Ya se dañaron dos microSD en campo (humedad, salitre y cortes de energía). Por eso cada
registro se guarda también en la flash interna del ESP32, que no tiene contactos
expuestos:

- **Dónde:** partición `spiffs` de la tabla por defecto (1,375 MB), con LittleFS, que
  resiste los cortes de energía.
- **Cómo:** registros binarios de 8–16 bytes, un archivo por flujo y por mes. Se convierten
  a CSV, con el mismo formato que la SD, al descargarlos.
- **Cuánto ocupa:** ~7 KB por día, o sea ~50 % de la partición en 90 días. Al pasar del 85 %
  se borra el mes más antiguo.
- **Batería crítica:** la SD no se toca, pero el respaldo sigue guardando las filas de
  batería.
- **SD que falla:** tras 3 escrituras fallidas seguidas se deja de usar la SD hasta el
  próximo arranque y queda el evento `SD_FALLA`.

El formato de los registros binarios está documentado en [include/backup.h](include/backup.h)
y en [tools/respaldo_a_csv.py](tools/respaldo_a_csv.py).

### Cómo sacar los datos

| Vía | Cuándo | Cómo |
|---|---|---|
| Dashboard | Uso normal | Pestaña **Archivos** (o **Historial** para graficar) → Origen: **Memoria interna**. Las carpetas son las mismas que en la SD; se descarga por día, por mes o todo en `.zip` |
| `/admin` | Si el dashboard falla | Enlace **Todo el respaldo interno (.zip)** |
| Monitor serial | Si el WiFi falla | Pulsa el botón del pin 32 para despertarla y escribe `respaldo` (resumen) o `respaldo bme 2026-11` (CSV del mes; también `ina`, `bat`, `fan` o un día `AAAA-MM-DD`). `ayuda` lista los comandos. Cada comando la mantiene despierta 5 min más |
| esptool | Si el firmware no arranca | Ver abajo |

El `.zip` tiene la misma estructura de carpetas que la SD: descomprimido en una SD nueva,
los datos quedan en su lugar.

**Extracción de emergencia** (con la trampa conectada por USB, desde la carpeta del
proyecto):

```
~/.platformio/penv/Scripts/python.exe ~/.platformio/packages/tool-esptoolpy/esptool.py --port COM5 read_flash 0x290000 0x160000 respaldo.bin
~/.platformio/packages/tool-mklittlefs/mklittlefs.exe -u respaldo_bin -b 4096 -p 256 -s 0x160000 respaldo.bin
python tools/respaldo_a_csv.py respaldo_bin respaldo_csv
```

> ⚠️ `pio run -t erase` y `pio run -t uploadfs` **borran el respaldo**. `pio run -t upload`
> y la OTA no lo tocan. Antes de borrar la flash de una trampa de campo, descarga el
> respaldo.

## Robustez

La trampa pasa meses sola en campo, así que el firmware se protege de fallas raras pero
graves:

| Falla | Qué hace el firmware |
|---|---|
| Una tarea se cuelga (p. ej. una SD que bloquea el bus) | **Watchdog** de 30 s sobre las tres tareas: el ESP32 se reinicia en vez de quedar despierto sin registrar ni proteger la batería. Los trabajos largos (`.zip`, exportaciones, OTA) avisan "sigo viva" por el camino. Al volver, queda la fila `REINICIO_ERROR` en `BAT_Data` |
| Una OTA instala un firmware que se cuelga o se reinicia | **Rollback**: el firmware nuevo queda a prueba hasta funcionar 60 s (o hasta dormir). Si antes se reinicia, el bootloader vuelve solo al anterior |
| El reloj (RTC) no responde o da una hora imposible | **Modo seguro**: se conserva la última hora buena. Tras ~1 s de lecturas malas la trampa deja de operar por horario (luz y ventiladores apagados, sin registros con fechas falsas), duerme y reintenta cada 30 min. El dashboard y la OLED lo avisan; el control manual y el botón del pin 32 siguen funcionando |
| Una configuración de horarios llega incompleta | `/saveWindows` la rechaza sin cambiar nada |
| La SD falla | Se deja de usar tras 3 errores seguidos (evento `SD_FALLA`); el respaldo interno sigue |

**Diagnóstico por serial:** el comando `diag` muestra la pila libre mínima de cada tarea, la
memoria libre, el motivo del último arranque y si el firmware está confirmado o a prueba.

**Uso de recursos** (firmware del 07-11-2026): Flash 86 % de 1,25 MB (1.129.373 B), RAM
estática 15,9 % (52 KB). Pilas: `ControlTask` 4 KB, `UITask` 4 KB, `CommTask` 8 KB. El margen
real de cada pila se mide con `diag` en el equipo.

## Dependencias

Las instala PlatformIO según [platformio.ini](platformio.ini), con la plataforma fijada en
`espressif32@6.12.0` (Arduino core 2.0.17): Adafruit SSD1306, GFX, Unified Sensor,
BME280, INA219, RTClib y ArduinoJson 7. LittleFS viene con el core. El dashboard usa
[Chart.js](https://www.chartjs.org/) 4.4.1 (licencia MIT), incluido en `web/chart.js` y
embebido en el firmware, porque la trampa no tiene internet.

`tools/respaldo_a_csv.py` solo necesita Python 3, sin paquetes adicionales.
