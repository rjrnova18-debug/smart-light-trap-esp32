"""Convierte el respaldo binario de la flash interna a los mismos CSV de la SD.

Es la vía de EMERGENCIA, para cuando el firmware no arranca pero el chip está vivo.
Normalmente el respaldo se descarga ya en CSV desde el dashboard (Archivos → Memoria
interna), desde /admin o por el Monitor serial (comando "respaldo").

Procedimiento (desde la carpeta del proyecto, con la trampa conectada por USB):

  1. Copiar la partición del respaldo (0x290000, 1,375 MB) a un archivo:
       ~/.platformio/penv/Scripts/python.exe ~/.platformio/packages/tool-esptoolpy/esptool.py
           --port COM5 read_flash 0x290000 0x160000 respaldo.bin
  2. Extraer los archivos de la imagen LittleFS:
       ~/.platformio/packages/tool-mklittlefs/mklittlefs.exe
           -u respaldo_bin -b 4096 -p 256 -s 0x160000 respaldo.bin
  3. Convertir a CSV, con el mismo árbol de carpetas que la SD:
       python tools/respaldo_a_csv.py respaldo_bin respaldo_csv

Formato de los registros: little-endian, sin relleno, igual que las structs de
include/backup.h. ts = segundos desde 1970 de la hora LOCAL del RTC (sin zona horaria).
Un archivo por flujo y por mes: bme_AAAA-MM.bin, ina_..., bat_..., fan_...
Solo usa la biblioteca estándar de Python 3.
"""

import argparse
import re
import struct
import sys
from collections import defaultdict
from datetime import datetime, timedelta
from pathlib import Path

SEALEVELPRESSURE_HPA = 1013.25  # igual que include/config.h

# Mismo orden que enum BatEvent (include/log_format.h). No reordenar.
BAT_EVENTS = [
    "INICIO", "PERIODICO", "DORMIR", "MUESTREO", "CORTE_BAJO_V",
    "RECONEXION", "BAT_CRITICA", "MODO_CARGA_ON", "MODO_CARGA_OFF", "SD_FALLA",
    "REINICIO_ERROR", "REINICIO_BROWNOUT", "ALIMENTACION_USB",
]

EPOCH = datetime(1970, 1, 1)


def flags_to_columns(flags):
    """flags de InaRec/BatRec: bit 0 = luz, bits 1-3 = ventiladores, bit 4 = AP."""
    return flags & 0x01, (flags >> 1) & 0x07, 1 if flags & 0x10 else 0


def fecha(t):
    return t.strftime("%d/%m/%Y")


def row_bme(t, temp_c100, hum_c100, pres_pa):
    pres = pres_pa / 100
    alt = 44330.0 * (1.0 - (pres / SEALEVELPRESSURE_HPA) ** 0.1903)
    return f"{fecha(t)},{t:%H:%M},{temp_c100 / 100:.2f},{hum_c100 / 100:.2f},{pres:.2f},{alt:.2f}"


def row_ina(t, flags, p_mw, e_mwh):
    led, fans, ap = flags_to_columns(flags)
    return f"{fecha(t)},{t:%H:%M},{led},{fans},{ap},{p_mw / 1000:.3f},{e_mwh / 1000:.3f}"


def row_bat(t, evento, flags, v_mv, i_ma10):
    led, fans, ap = flags_to_columns(flags)
    nombre = BAT_EVENTS[evento] if evento < len(BAT_EVENTS) else "OTRO"
    return f"{fecha(t)},{t:%H:%M:%S},{nombre},{v_mv / 1000:.3f},{i_ma10 / 10:.1f},{led},{fans},{ap}"


def row_fan(t, area, _fans_mask, conteo):
    return f"{fecha(t)},{t:%H:%M:%S},FAN_ON,Area_{area},Count_{conteo}"


# prefijo del archivo -> (carpeta en la SD, sufijo del CSV diario, formato struct, encabezado, fila)
STREAMS = {
    "bme": ("BME_Data", ".csv", "<IhHI", "Fecha,Hora,Temp,Hum,Pres,Alt", row_bme),
    "ina": ("INA_Data", ".csv", "<IB3xii", "Fecha,Hora,LED,FANS,AP,P_W,E_Wh", row_ina),
    "bat": ("BAT_Data", ".csv", "<IBBHi", "Fecha,Hora,Evento,V_bat,I_mA,LED,FANS,AP", row_bat),
    "fan": ("Fans_Data", "_events.csv", "<IBBH", "Fecha,Hora,Evento,Area,Conteo", row_fan),
}

FILE_NAME = re.compile(r"^(bme|ina|bat|fan)_(\d{4})-(\d{2})\.bin$")


def convert_file(path, out_dir):
    """Convierte un .bin mensual en un CSV por día. Devuelve (filas, descartados)."""
    match = FILE_NAME.match(path.name)
    prefix, year, month = match.group(1), int(match.group(2)), int(match.group(3))
    folder, suffix, fmt, header, make_row = STREAMS[prefix]
    size = struct.calcsize(fmt)
    data = path.read_bytes()

    days = defaultdict(list)
    discarded = 0
    usable = len(data) - len(data) % size  # una cola incompleta al final se ignora
    for (ts, *values) in struct.iter_unpack(fmt, data[:usable]):
        t = EPOCH + timedelta(seconds=ts)
        if t.year != year or t.month != month:  # relleno o registro inválido
            discarded += 1
            continue
        days[t.day].append(make_row(t, *values))

    month_dir = out_dir / folder / f"{year:04d}-{month:02d}"
    month_dir.mkdir(parents=True, exist_ok=True)
    for day, rows in sorted(days.items()):
        csv_path = month_dir / f"{year:04d}-{month:02d}-{day:02d}{suffix}"
        csv_path.write_text(header + "\n" + "\n".join(rows) + "\n", encoding="utf-8", newline="\n")
    return sum(len(rows) for rows in days.values()), discarded


def main():
    parser = argparse.ArgumentParser(description="Convierte el respaldo binario de la trampa a CSV (mismo árbol que la SD).")
    parser.add_argument("entrada", type=Path, help="carpeta con los .bin (salida de mklittlefs -u)")
    parser.add_argument("salida", type=Path, help="carpeta donde se crean BME_Data/, INA_Data/, ...")
    args = parser.parse_args()

    files = sorted(p for p in args.entrada.rglob("*.bin") if FILE_NAME.match(p.name))
    if not files:
        sys.exit(f"No hay archivos del respaldo (bme_AAAA-MM.bin, ...) en {args.entrada}")

    for path in files:
        rows, discarded = convert_file(path, args.salida)
        note = f", {discarded} descartados" if discarded else ""
        print(f"{path.name}: {rows} filas{note}")
    print(f"Listo: CSV en {args.salida}")


if __name__ == "__main__":
    main()
