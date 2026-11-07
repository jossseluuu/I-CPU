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
| 2.0.x | ✅ |
| 1.5.x | ❌ (actualiza a 2.0: sal aleatoria y modo adaptativo) |
| 1.0.x | ❌ (actualiza a 2.0) |

## Qué se considera vulnerabilidad

- Recuperar o modificar un mensaje sin la contraseña, o distinguir una imagen con mensaje de
  una sin él (en modo adaptativo y orden aleatorio) mejor de lo que permiten las limitaciones documentadas.
- Fallos de memoria o consumo desproporcionado de recursos al procesar imágenes manipuladas.
- Acceso a la API de `icpu-gui` desde otra web o desde otro equipo de la red.

Están documentadas y **no** se consideran vulnerabilidades nuevas (ver
[limitaciones conocidas](docs/tecnico/arquitectura.md#8-limitaciones-conocidas)):
- las debilidades de los formatos v1 y v2, que la 2.0 solo lee: vuelve a ocultar esos mensajes;
- la detectabilidad de `--mode replace`, `--mode match` y el orden secuencial, pensados para experimentos;
- la detección mediante estegoanálisis moderno (SRM, redes neuronales);
- la pérdida del mensaje al recomprimir o editar la imagen.

> I-CPU no ha pasado ninguna auditoría de seguridad independiente.
