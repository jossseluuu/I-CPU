# ¿Para quién es I-CPU?

> Cada perfil tiene sus preguntas y su ruta de lectura. Elige la tuya.

## Persona que quiere enviar algo en privado

**Qué te ofrece:** esconder un texto o un fichero dentro de una foto, cifrado con tu contraseña,
sin instalar nada ni subir nada a Internet.

**Ruta de lectura**
1. [Instalación](instalacion.md) (opción A: ejecutables ya compilados)
2. [Primeros pasos](primeros-pasos.md)
3. [Buenas prácticas](buenas-practicas.md), sobre todo *Enviar la imagen* y *La contraseña*

**Ten presente:** I-CPU es un proyecto de aprendizaje sin auditoría. Para proteger a personas en
situaciones de riesgo, usa herramientas auditadas y asesórate.

## Estudiante de Informática, Telecomunicaciones o Matemáticas

**Qué te ofrece:** un proyecto completo y legible para ver aplicados conceptos de criptografía
(KDF, AEAD, separación de dominios), teoría de códigos (codificación por síndrome, Viterbi),
procesamiento de imagen (DCT, paletas, filtros de paso alto), estadística (chi-cuadrado, RS) e
ingeniería del software (capas, pruebas, CI, fuzzing).

**Ruta de lectura**
1. [Sobre el proyecto](../sobre-el-proyecto.md): objetivos y qué se aprende
2. [Arquitectura](../tecnico/arquitectura.md) → [Esteganografía](../tecnico/esteganografia.md) → [Criptografía](../tecnico/criptografia.md)
3. [Experimentos](experimentos.md): ideas para trabajos de clase o un TFG
4. [Decisiones de diseño](../tecnico/decisiones-de-diseno.md): por qué cada cosa es como es, con los errores que se corrigieron

## Docente

**Qué te ofrece:** una herramienta visual para clases de seguridad, multimedia o estadística. La GUI
enseña en directo que una imagen con mensaje «parece la misma» (comparador, lupa, diferencias ×64)
y que los ataques estadísticos funcionan contra unos modos y no contra otros.

**Propuesta de práctica (2 horas)**
1. Ocultar el mismo mensaje con `replace`, `match` y `adaptive` en una foto ([GUI](interfaz-grafica.md#ocultar)).
2. Analizar cada resultado con la portadora original ([Analizar](interfaz-grafica.md#analizar)).
3. Ejecutar un experimento y explicar por qué RS detecta la sustitución y no las otras
   ([estegoanálisis](../tecnico/estegoanalisis.md)).
4. Debate: ¿un test que no salta demuestra que la imagen está limpia?

Las demos de `samples/` (contraseña `icpu-demo`) sirven para empezar sin preparar nada.

## Investigador en esteganografía o estegoanálisis

**Qué te ofrece:** STC con costes HILL, UERD y de paleta, un formato documentado, un banco de
pruebas con salida CSV/JSON y una biblioteca C++ para generar conjuntos de imágenes.

**Ruta de lectura**
1. [Especificación del formato v3](../tecnico/formato.md)
2. [Esteganografía](../tecnico/esteganografia.md), con detalles de STC y referencias
3. [Biblioteca C++](../tecnico/biblioteca.md) y [experimentos](experimentos.md)

**Ten presente:** STC binario de altura 8 (no ternario); la carga prudente es 0,25 bpnzac/bpp
aproximadamente; no se ha evaluado frente a SRM ni frente a redes. Se agradecen resultados.

## Desarrollador

**Qué te ofrece:** C++20 moderno sin dependencias externas en ejecución, una API de dos cabeceras,
una GUI web embebida en el ejecutable y una base de pruebas con CI, sanitizers y fuzzing.

**Ruta de lectura**
1. [Instalación](instalacion.md) (opción B o C)
2. [Arquitectura](../tecnico/arquitectura.md) y [biblioteca C++](../tecnico/biblioteca.md)
3. [Interfaz web y API HTTP](../tecnico/interfaz-web.md)
4. [Calidad, pruebas y desarrollo](../tecnico/calidad-y-pruebas.md) y [CONTRIBUTING](../../CONTRIBUTING.md)

## Auditor o revisor de seguridad

**Ruta de lectura**
1. [Criptografía y modelo de amenazas](../tecnico/criptografia.md)
2. [Especificación del formato](../tecnico/formato.md)
3. [Seguridad de la GUI](../tecnico/interfaz-web.md#seguridad)
4. [Política de seguridad](../../SECURITY.md) para informar en privado

Puntos de interés: separación de claves con BLAKE2b, uso del nonce en cabecera y cuerpo, oráculo de
errores, nombres de fichero al extraer, límites de los decodificadores, token y comprobación de `Host`.
