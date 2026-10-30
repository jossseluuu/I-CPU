# Política de seguridad

I-CPU es una herramienta de cifrado y ocultación. Un fallo de seguridad puede exponer
mensajes que alguien cree protegidos, así que se agradece que se comunique **en privado**.

## Cómo informar de una vulnerabilidad

**No abras un issue público.** Usa el aviso privado de GitHub:
pestaña **Security** → **Report a vulnerability**.

Incluye, si puedes:
- versión (`icpu version`) y sistema operativo;
- descripción del problema y su impacto: qué puede conseguir un atacante;
- pasos para reproducirlo o una prueba de concepto.

**No incluyas contraseñas ni imágenes con mensajes reales.** Genera ejemplos con datos de prueba.

Es un proyecto personal mantenido en el tiempo libre: intentaré responder en un plazo de
dos semanas y publicar la corrección con una nota en el [CHANGELOG](CHANGELOG.md).

## Versiones con soporte

| Versión | Soporte |
|---|---|
| 1.0.x | ✅ |

## Qué se considera vulnerabilidad

- Recuperar o modificar un mensaje sin la contraseña, o distinguir una imagen con mensaje de
  una sin él (con orden aleatorio) mejor de lo que permiten las limitaciones documentadas.
- Fallos de memoria (desbordamientos, lecturas fuera de rango) al procesar imágenes manipuladas.
- Acceso a la API de `icpu-gui` desde otra web o desde otro equipo de la red.

Están documentadas y **no** se consideran vulnerabilidades nuevas (ver
[limitaciones conocidas](docs/arquitectura.md#8-limitaciones-conocidas)):
- la cabecera en claro del formato v1;
- la detectabilidad del orden secuencial, que existe solo para experimentos;
- la detección mediante estegoanálisis moderno (SRM, redes neuronales);
- la pérdida del mensaje al recomprimir o editar la imagen.

> I-CPU no ha pasado ninguna auditoría de seguridad independiente.
