#pragma once
#include <Arduino.h>

// Página de mantenimiento, escrita a mano y separada del dashboard: es la forma de
// actualizar el firmware (y con él el dashboard) sin abrir la carcasa en campo, así
// que tiene que funcionar aunque una versión del dashboard venga con errores.
const char ADMIN_PAGE_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mantenimiento - Light Trap</title>
<style>
  body { font-family: 'Segoe UI', 'Helvetica Neue', sans-serif; background:#f3f4f6; color:#111827; padding:24px 16px; max-width:480px; margin:0 auto; }
  h1 { color:#5B6FE8; font-size:20px; margin-bottom:8px; }
  p.note { font-size:12px; color:#6B7280; margin-bottom:16px; line-height:1.5; }
  .card { background:#fff; border-radius:12px; padding:20px; margin-bottom:18px; box-shadow:0 1px 3px rgba(0,0,0,0.1); border-top:3px solid #5B6FE8; }
  .card h2 { font-size:15px; margin-bottom:8px; }
  .card p.note { margin-bottom:12px; }
  input[type=file] { width:100%; margin-bottom:14px; font-size:13px; }
  button { width:100%; background:#5B6FE8; color:#fff; border:none; padding:12px; border-radius:8px; font-weight:700; font-size:14px; cursor:pointer; }
  button:hover { background:#4A5BD1; }
  a.btn { display:block; text-align:center; text-decoration:none; background:#5B6FE8; color:#fff; padding:12px; border-radius:8px; font-weight:700; font-size:14px; margin-bottom:10px; }
  button.danger { background:#EF4444; }
</style>
</head>
<body>
  <h1>🛠️ Mantenimiento del sistema</h1>
  <p class="note">Esta página es independiente del dashboard y de la tarjeta SD: sigue disponible aunque alguno de los dos tenga problemas. Es la vía segura para actualizar el firmware y sacar los datos.</p>

  <div class="card">
    <h2>Actualizar firmware (OTA)</h2>
    <p class="note">Sube el archivo .bin compilado (pio run genera .pio/build/esp32doit-devkit-v1/firmware.bin). El dashboard va dentro del firmware, así que también se actualiza. El equipo se reinicia solo al terminar. Si la subida falla o se corta, el firmware actual sigue funcionando normalmente.</p>
    <form method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="firmware" accept=".bin" required>
      <button type="submit">Actualizar firmware</button>
    </form>
  </div>

  <div class="card">
    <h2>Descargar datos</h2>
    <p class="note">Respaldo completo en un .zip con la misma estructura de carpetas de la SD (BME_Data, INA_Data, BAT_Data, Fans_Data). El respaldo interno vive en la memoria del ESP32 y no depende de la SD. Puede tardar varios segundos en empezar.</p>
    <a class="btn" href="/descargarZip?dir=&amp;src=flash">💾 Todo el respaldo interno (.zip)</a>
    <a class="btn" href="/descargarZip?dir=">📂 Toda la tarjeta SD (.zip)</a>
  </div>

  <div class="card">
    <h2>Formatear respaldo interno</h2>
    <p class="note">Solo funciona si el respaldo interno no se pudo montar (el dashboard lo avisa). Si está funcionando, no hace nada: para liberar espacio se borran meses desde la pestaña Archivos. Antes de formatear se pueden intentar sacar los datos con esptool (ver README).</p>
    <form method="POST" action="/respaldo/formatear" onsubmit="return confirm('¿Formatear el respaldo interno? Se pierde lo que tenga.');">
      <button type="submit" class="danger">Formatear respaldo interno</button>
    </form>
  </div>
</body>
</html>
)rawliteral";
