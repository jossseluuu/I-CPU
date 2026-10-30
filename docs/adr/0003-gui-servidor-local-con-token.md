# 0003 · GUI como servidor web local con token

**Estado:** aceptada (2026-10-30)

## Contexto
Se quería una interfaz gráfica sin añadir un toolkit pesado (Qt, wxWidgets). cpp-httplib ya
estaba incluido. El riesgo de un servidor local es que **cualquier web abierta en el navegador**
o cualquier proceso del equipo podría llamar a su API, que maneja contraseñas y ficheros privados.

## Decisión
- `icpu-gui.exe` escucha **solo en 127.0.0.1**, en un puerto libre que elige el sistema
  (`--port` para fijarlo), y abre el navegador.
- Cada arranque genera un **token aleatorio de 128 bits** que llega a la web en el fragmento de la
  URL (`#t=…`). El fragmento nunca se envía al servidor ni queda en registros. Toda llamada a
  `/api/` exige la cabecera `X-ICPU-Token`, y se compara en tiempo constante. Otra web no puede
  añadir esa cabecera sin una verificación CORS, que no se concede.
- Se rechaza cualquier `Host` distinto de `127.0.0.1:puerto` o `localhost:puerto`, como protección
  frente a *DNS rebinding*.
- El frontend (`apps/gui/web/`) se **embebe en el ejecutable** al compilar (`embed_assets.cmake`).
  Se sirve con CSP estricta (`default-src 'self'`) y sin scripts inline. Nunca se usa `innerHTML`
  con datos.

## Consecuencias
- Un único `.exe` portable y una interfaz con HTML/CSS/JS normales.
- Si se pierde la pestaña, hay que volver a abrir el enlace de la consola, porque el token
  solo dura esa sesión.
- La interfaz depende de un navegador moderno.

## Alternativas descartadas
- Puerto fijo sin token: más simple, pero cualquier web podría usar la API.
- Frontend en una carpeta junto al `.exe`: más fácil de retocar, pero hay que distribuir la carpeta.
