#include "trap.h"
#include "config.h"
#include "state.h"
#include "settings.h"

unsigned long lastTriggerTime[16] = {0};
static uint8_t activeChannelIndex = 0; // Índice dentro de ACTIVE_MUX_CHANNELS
unsigned long lastEvaluation = 0;
bool previousIR[16] = {0};         // Estado anterior para detectar flancos
unsigned long fanStartTime[4] = {0};
int queuedTriggerCount[4] = {0, 0, 0, 0}; // conteo que originó el disparo en cola, por ÁREA (para el log de evento)

void LED_Operation()
{
  static bool lastState = false;
  bool currentState;

  if (manualLedOverride)
  {
    currentState = manualLedState;
  }
  else
  {
    currentState = operatingWindow_LED() && !chargeOnlyMode; // modo solo carga: sin luz automática
  }
  // Protección por bajo voltaje: bloquea la luz también en modo manual
  currentState = currentState && !lowBatteryLock;

  systemData.ledState = currentState;

  if (currentState != lastState)
  {
    digitalWrite(ledPin, currentState ? HIGH : LOW); // ON-OFF a máxima intensidad
    lastState = currentState;
  }
}

void selectMuxChannel(uint8_t channel)
{
  digitalWrite(MUX_S0, channel & 0x01);
  digitalWrite(MUX_S1, (channel >> 1) & 0x01);
  digitalWrite(MUX_S2, (channel >> 2) & 0x01);
  digitalWrite(MUX_S3, (channel >> 3) & 0x01);
}

bool isAreaBusy(int area)
{
  for (int k = 0; k < 2; k++)
  {
    int fan = AREA_TO_FANS[area][k];
    if (fan >= 0 && systemData.loads[fan])
      return true;
  }
  return false;
}

void readIRSensors()
{
  unsigned long now = millis();
  uint8_t currentChannel = ACTIVE_MUX_CHANNELS[activeChannelIndex];
  selectMuxChannel(currentChannel);
  delayMicroseconds(20); // Tiempo de asentamiento del MUX tras cambiar de canal

  // Filtro anti-ruido: dos lecturas separadas deben coincidir para aceptar el estado.
  // Descarta picos de ruido/flotante transitorios (ver nota de MUX_SIG en setup()).
  bool sample1 = digitalRead(MUX_SIG);
  delayMicroseconds(20);
  bool sample2 = digitalRead(MUX_SIG);

  if (sample1 != sample2)
  {
    // Lectura inconsistente: se descarta esta muestra, se reintenta en el próximo barrido.
    activeChannelIndex = (activeChannelIndex + 1) % ACTIVE_MUX_CHANNEL_COUNT;
    return;
  }
  bool currentState = sample1;
  systemData.irSensors[currentChannel] = currentState;
  int area = currentChannel / 4;
  // Lógica de detección de movimiento (Flanco de subida)
  if (!isAreaBusy(area)) // Solo si ningún ventilador de esta área está encendido
  {
    if (currentState == HIGH && previousIR[currentChannel] == LOW) // Detectar flanco LOW -> HIGH
    {
      // Debounce por tiempo
      if (now - lastTriggerTime[currentChannel] > DEBOUNCE_TIME)
      {
        areaCounter[area]++;
        systemData.areaCounts[area] = areaCounter[area];
        lastTriggerTime[currentChannel] = now;
      }
    }
  }
  else
  {
    areaCounter[area] = 0; // Ya hay un ventilador de esta área encendido, no contamos eventos y reseteamos el contador.
    systemData.areaCounts[area] = 0;
  }
  previousIR[currentChannel] = currentState;
  activeChannelIndex = (activeChannelIndex + 1) % ACTIVE_MUX_CHANNEL_COUNT; // Siguiente canal activo para la próxima llamada
}

int countActiveFans()
{
  int count = 0;
  for (int i = 0; i < 4; i++)
  {
    if (systemData.loads[i])
      count++;
  }
  return count;
}

void activateFan(int i, unsigned long now)
{
  systemData.loads[i] = true;
  fanStartTime[i] = now;
  digitalWrite(FAN_PINS[i], HIGH);
}

// Cuántos ventiladores de esta área se pueden disparar automáticamente ahora
// mismo (excluye los que el operador puso en manual — esos no se tocan).
int getAreaFanCountNeeded(int area)
{
  int count = 0;
  for (int k = 0; k < 2; k++)
  {
    int fan = AREA_TO_FANS[area][k];
    if (fan >= 0 && !manualFanOverride[fan])
      count++;
  }
  return count;
}

// Enciende TODOS los ventiladores automáticos de esta área en el mismo ciclo
// (siempre como par cuando el área controla 2, nunca uno esperando al otro) y
// registra UNA fila de evento con el área real (antes era una fila por ventilador
// con el número de ventilador en la columna "Area").
void activateAreaFans(int area, unsigned long now, int triggerCount)
{
  uint8_t fansMask = 0;
  for (int k = 0; k < 2; k++)
  {
    int fan = AREA_TO_FANS[area][k];
    if (fan >= 0 && !manualFanOverride[fan])
    {
      activateFan(fan, now);
      fansMask |= (1 << fan);
    }
  }
  if (fansMask == 0)
    return;

  // --- REGISTRO DE EVENTO --- (lo escribe taskComm; sin espera para no frenar el escaneo IR)
  FanEvent evt = {area + 1, fansMask, triggerCount,
                  systemData.day, systemData.month, systemData.year,
                  systemData.hour, systemData.minute, systemData.second};
  if (xQueueSend(fanEventQueue, &evt, 0) != pdTRUE)
    Serial.println("Cola de eventos llena: evento de ventilador sin registrar");
}

void evaluateAreas()
{
  if (!operatingWindow_Fan() || lowBatteryLock || chargeOnlyMode)
    return;
  unsigned long now = millis();

  for (int area = 0; area < 4; area++)
  {
    // Si llegamos al umbral y ningún ventilador de esta área está encendido
    if (areaCounter[area] < AREA_THRESHOLD || isAreaBusy(area))
      continue;

    int needed = getAreaFanCountNeeded(area);
    if (needed == 0)
      continue; // todos los ventiladores de esta área están en manual (o el área no tiene ninguno asociado)

    if (countActiveFans() + needed <= MAX_CONCURRENT_FANS)
    {
      // INTERLOCK: hay cupo para encenderlos TODOS juntos de inmediato
      activateAreaFans(area, now, areaCounter[area]);
      Serial.printf("Área %d activó sus %d ventilador(es) juntos. Ignorando nuevas detecciones...\n", area + 1, needed);
    }
    else
    {
      // INTERLOCK: no hay cupo para el par completo todavía, se pone el área en cola
      pendingAreaMask |= (1 << area);
      queuedTriggerCount[area] = areaCounter[area];
      Serial.printf("Área %d en cola: esperando cupo para encender sus %d ventilador(es) juntos...\n", area + 1, needed);
    }

    areaCounter[area] = 0;
    systemData.areaCounts[area] = 0;
  }
  // La ventana de 5 min ahora solo limpia áreas que NO se activaron
  if (now - lastEvaluation >= EVALUATION_WINDOW)
  {
    for (int area = 0; area < 4; area++)
    {
      if (!isAreaBusy(area))
        areaCounter[area] = 0;
      systemData.areaCounts[area] = 0;
    }
    lastEvaluation = now;
  }
}

void controlFans()
{
  unsigned long now = millis();
  // Con la protección por bajo voltaje o el modo solo carga se trata como ventana
  // cerrada (el modo solo carga no bloquea el manual, la protección sí)
  bool fanWindowActive = operatingWindow_Fan() && !lowBatteryLock && !chargeOnlyMode;

  if (!fanWindowActive)
  {
    // La ventana de ventiladores cerró: reseteamos los contadores de las 4 áreas
    // (antes se hacía dentro del for de ventiladores, pero área y ventilador ya
    // no son el mismo índice, así que se hace una sola vez aparte).
    for (int area = 0; area < 4; area++)
    {
      areaCounter[area] = 0;
      systemData.areaCounts[area] = 0;
    }
  }

  for (int i = 0; i < 4; i++)
  {
    if (manualFanOverride[i])
    {
      bool on = manualFanState[i] && !lowBatteryLock; // La protección también bloquea el manual
      digitalWrite(FAN_PINS[i], on ? HIGH : LOW);
      systemData.loads[i] = on;
      continue;
    }
    // Si no está en modo manual
    if (!fanWindowActive)
    {
      digitalWrite(FAN_PINS[i], LOW);
      systemData.loads[i] = false;
      continue;
    }
    // Apagar ventilador cuando cumple tiempo
    if (systemData.loads[i] && (now - fanStartTime[i] >= FAN_ON_TIME))
    {
      digitalWrite(FAN_PINS[i], LOW);
      systemData.loads[i] = false;
    }
  }

  // INTERLOCK: atender la cola de áreas en espera (cada una enciende su par de golpe)
  if (!fanWindowActive)
  {
    // La ventana de ventiladores cerró: no tiene sentido mantener una cola pendiente
    pendingAreaMask = 0;
  }
  else if (pendingAreaMask != 0)
  {
    for (int area = 0; area < 4; area++)
    {
      if (!(pendingAreaMask & (1 << area)))
        continue;

      int needed = getAreaFanCountNeeded(area);
      if (needed == 0)
      {
        // Mientras esperaba, el operador puso en manual todos los ventiladores de esta área
        pendingAreaMask &= ~(1 << area);
        continue;
      }

      if (countActiveFans() + needed > MAX_CONCURRENT_FANS)
        continue; // todavía no hay cupo para encenderlos juntos, se deja en cola

      pendingAreaMask &= ~(1 << area);
      activateAreaFans(area, now, queuedTriggerCount[area]);
      Serial.printf("Área %d activó sus %d ventilador(es) juntos desde la cola.\n", area + 1, needed);
    }
  }
}
