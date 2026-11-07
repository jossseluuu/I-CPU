# Preguntas frecuentes

## Uso

**¿Se nota que la imagen lleva algo?**
A simple vista, no: solo cambia ±1 en algunas muestras. Con herramientas estadísticas clásicas,
tampoco si usas el modo adaptativo con poca carga. Con detectores modernos o con la imagen
original al lado, sí podría notarse ([qué protege y qué no](tecnico/criptografia.md#qué-protege-y-qué-no)).

**¿Cuánto puedo ocultar?**
En modo adaptativo, aproximadamente un 3 % del tamaño de la imagen sin comprimir en PNG: 13 KB
en una de 480 × 320 y unos 1,1 MB en una foto de 12 megapíxeles. En JPEG, mucho menos.
`icpu info foto.png` o el recuadro de la GUI te lo dicen exactamente.

**Me dice que no cabe. ¿Qué hago?**
Por orden: usa una imagen más grande, comprime el fichero, reparte el contenido en varias imágenes
o, si te da igual que se detecte, usa `--mode replace` (*Sustitución* en la GUI).

**He enviado la imagen por WhatsApp y ya no se puede extraer.**
WhatsApp (y casi todas las redes) recomprimen las imágenes y eso borra el mensaje. Envíala como
*Documento* ([cómo enviarla](guias/buenas-practicas.md#enviar-la-imagen)).

**He olvidado la contraseña.**
El mensaje no se puede recuperar. No hay puerta trasera: es justo lo que hace que sea seguro.

**¿Por qué el error es el mismo si la contraseña está mal y si no hay mensaje?**
Si I-CPU dijera «contraseña incorrecta», confirmaría que la imagen lleva algo. Es deliberado.

**¿Funciona con fotos del móvil?**
Sí. Los JPEG del móvil se procesan en el dominio DCT sin recomprimirlos. Los HEIC (iPhone) no son
compatibles: expórtalos a JPEG antes.

**¿Puedo usar un GIF animado?**
Sí, el mensaje se reparte entre todos los fotogramas y se conserva la animación.

**¿Puedo ocultar un mensaje en una imagen que ya lleva otro?**
Puedes, pero el primero se pierde: la segunda inserción reescribe los mismos bits.

**¿Funciona sin Internet?**
Sí, todo. La GUI no carga nada de fuera.

## GUI

**Al cerrar la pestaña, ¿se queda el programa abierto?**
No: si no queda ninguna pestaña de I-CPU, el servidor se cierra solo a los ~10 segundos. Recargar
la página no lo cierra.

**Ejecuté `icpu-gui` dos veces y solo hay un programa.**
Es a propósito (en Windows): la segunda vez abre otra pestaña del que ya estaba abierto.

**La web dice «Falta el token de sesión».**
Has abierto la dirección sin el `#t=…` final. Usa el enlace completo de la consola o vuelve a abrir `icpu-gui`.

**¿Se guarda algo en el navegador?**
Solo el tema (claro u oscuro) y, mientras dura la sesión de la pestaña, el token y la pestaña
activa (`sessionStorage`, que el navegador borra al cerrarla). El historial vive en memoria y
desaparece al recargar.

## Seguridad

**¿Es seguro?**
El cifrado usa primitivas sólidas y conocidas (Argon2id, XChaCha20-Poly1305). Pero I-CPU es un
proyecto personal **sin auditoría**, y la esteganografía no es indetectable frente a detectores modernos.

**¿Qué pasa si alguien modifica la imagen?**
La extracción falla: el MAC detecta cualquier cambio, incluso de un bit. Nunca recibirás un
mensaje alterado.

**¿Alguien de mi red puede usar mi I-CPU abierto?**
No. El servidor solo escucha en 127.0.0.1 (tu propio equipo) y exige un token por sesión.

**¿Y las imágenes de la versión 1.0 o 1.5?**
Se siguen leyendo, pero sus formatos tenían debilidades. Si el mensaje sigue siendo sensible,
extráelo y vuelve a ocultarlo con la 2.0.

**He encontrado un fallo de seguridad.**
No abras un issue público: sigue la [política de seguridad](../SECURITY.md).

## Técnica

**¿Por qué el modo adaptativo cabe tan poco?**
Porque cuanto más se oculta, más se detecta. 0,25 bits por ranura es una carga prudente; el modo
de sustitución admite hasta 16 veces más, pero los tests clásicos lo detectan.

**¿Por qué tarda unos segundos en fotos grandes?**
STC recorre 256 estados por cada muestra con el algoritmo de Viterbi (unos 24 s con 12 MP), y
Argon2id tarda a propósito. Los modos `replace` y `match` son casi instantáneos.

**¿Puedo usar I-CPU desde mi programa?**
Sí, es una biblioteca C++20 ([guía](tecnico/biblioteca.md)), o mediante la CLI con `--json`.

**¿Por qué una GUI en el navegador y no una ventana normal?**
Un solo ejecutable portable sin dependencias pesadas, con una interfaz moderna en HTML. Las
razones y los riesgos están en [D3](tecnico/decisiones-de-diseno.md#d3-la-gui-es-un-servidor-web-local-con-token).
