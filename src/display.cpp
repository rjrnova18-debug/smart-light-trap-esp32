#include <Wire.h>
#include <Adafruit_GFX.h>
#include "display.h"
#include "config.h"
#include "state.h"
#include "settings.h"
#include "backup.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

enum DisplayMode
{
  MODE_BME,
  MODE_INA,
  MODE_IR,
  MODE_FANS,
  MODE_COUNTERS,
  MODE_OPERATING_WINDOWS,
  MODE_TOTAL
};
volatile DisplayMode currentMode = MODE_BME;

volatile unsigned long lastInterruptTime = 0;
const unsigned long INTERRUPT_DEBOUNCE = 300; // Debounce button time (300ms)

void IRAM_ATTR Interrupt_rutine()
{
  unsigned long now = millis();

  if (now - lastInterruptTime > INTERRUPT_DEBOUNCE)
  {
    if (!oledEnabled && !oledTempOn)
    {
      // OLED apagada: la primera pulsación solo la enciende un rato, sin cambiar de pantalla
      oledTempOnStart = now;
      oledTempOn = true;
    }
    else
    {
      currentMode = (DisplayMode)((currentMode + 1) % MODE_TOTAL);
      if (oledTempOn)
        oledTempOnStart = now; // cada pulsación renueva el encendido temporal
    }
    lastInterruptTime = now;
  }
}

void showData()
{
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(TEXT_SIZE);
  display.setCursor(0, 0);

  if (currentMode == MODE_BME)
  {
    // SHOW RTC VALUES
    display.print("Fecha: ");
    display.print(systemData.day);
    display.print("/");
    display.print(systemData.month);
    display.print("/");
    display.println(systemData.year);

    display.print("Hora: ");
    if (systemData.hour < 10)
    {
      display.print("0");
    }
    display.print(systemData.hour);
    display.print(":");
    if (systemData.minute < 10)
    {
      display.print("0");
    }
    display.println(systemData.minute);

    display.println();

    // SHOW BME VALUES
    display.print("Temp: ");
    display.print(systemData.temperature, 1);
    display.println(" C");

    display.print("Hum : ");
    display.print(systemData.humidity, 1);
    display.println(" %");

    display.print("Pres: ");
    display.print(systemData.pressure, 1);
    display.println(" hPa");

    display.print("Alt : ");
    display.print(systemData.altitude, 1);
    display.println(" m");
  }

  else if (currentMode == MODE_INA)
  {
    // SHOW RTC VALUES
    display.print("Fecha: ");
    display.print(systemData.day);
    display.print("/");
    display.print(systemData.month);
    display.print("/");
    display.println(systemData.year);

    display.print("Hora: ");
    if (systemData.hour < 10)
    {
      display.print("0");
    }
    display.print(systemData.hour);
    display.print(":");
    if (systemData.minute < 10)
    {
      display.print("0");
    }
    display.println(systemData.minute);

    display.println();

    // SHOW INA VALUES
    display.print("Volt: ");
    display.print(systemData.busVoltage, 2);
    display.println(" V");

    display.print("Curr: ");
    display.print(systemData.current_mA, 2);
    display.println(" mA");

    display.print("Pwr : ");
    display.print(systemData.power_W, 3);
    display.println(" W");

    if (criticalBatteryLock)
      display.println("BAT CRITICA: SD OFF");
    else if (lowBatteryLock)
      display.println("BAT BAJA: TRAMPA OFF");
    else if (chargeOnlyMode)
      display.println("MODO SOLO CARGA");
  }

  // OPCIONAL IR SENSORS
  else if (currentMode == MODE_IR)
  {
    display.println("IR Sensors:");
    display.println("1=ON 0=OFF");
    display.println();

    for (int i = 0; i < 16; i++)
    {
      display.print(systemData.irSensors[IR_DISPLAY_ORDER[i]] ? "1" : "0");
      display.print(" ");
      if ((i + 1) % 4 == 0)
        display.println();
    }
  }
  else if (currentMode == MODE_FANS)
  {
    display.print("LED Status: ");
    display.println(systemData.ledState ? "ON" : "OFF");
    display.println("--------------------");
    display.println("Fan Status:");
    display.println();

    for (int i = 0; i < 4; i++)
    {
      display.print("Fan ");
      display.print(i + 1);
      display.print(": ");
      display.println(systemData.loads[i] ? "ON" : "OFF");
    }
  }
  else if (currentMode == MODE_COUNTERS)
  {
    display.println("Area Counters:");
    display.println("--------------------");
    for (int i = 0; i < 4; i++)
    {
      display.print("Area ");
      display.print(i + 1);
      display.print(": ");
      display.println(areaCounter[i]);
    }
  }
  else if (currentMode == MODE_OPERATING_WINDOWS)
  {
    display.println("Ventanas Activas:");
    display.println("--------------------");

    // Ventana AP
    display.print("WiFi AP: ");
    display.println(operatingWindow_AP() || forceAwake ? "ACTIVA" : "INACTIVA");

    // Ventana LED
    display.print("LEDs   : ");
    display.println(operatingWindow_LED() ? "ACTIVA" : "INACTIVA");

    // Almacenamiento (la ventana SD ya no controla los registros): SD y respaldo interno
    display.print("SD: ");
    display.print(!systemData.sdAvailable ? "NO" : (criticalBatteryLock ? "PAUSA" : "OK"));
    display.print("  Mem: ");
    if (systemData.backupAvailable)
    {
      display.print(backupUsedPct());
      display.println("%");
    }
    else
      display.println("ERROR");

    // Estado de Forzado (Manual o Despertado por botón)
    display.println();

    bool fanManual = false;
    for (int i = 0; i < 4; i++)
      if (manualFanOverride[i])
        fanManual = true;

    if (forceAwake)
    {
      display.println("Modo: DESPIERTO (EXT)");
    }
    else if (manualLedOverride || fanManual)
    {
      display.println("Modo: MANUAL ACTIVO");
    }
    else if (chargeOnlyMode)
    {
      display.println("Modo: SOLO CARGA");
    }
    else
    {
      display.println("Modo: AUTOMATICO");
    }
  }
  display.display();
}
