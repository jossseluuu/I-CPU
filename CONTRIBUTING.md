# Cómo contribuir

Gracias por el interés en I-CPU. Es un proyecto personal de aprendizaje, pero las contribuciones,
los informes de fallos y las ideas son bienvenidos.

## Antes de empezar

- **¿Un problema de seguridad?** No abras un issue: sigue [SECURITY.md](SECURITY.md).
- **¿Un fallo?** Abre un issue con la plantilla *Fallo*: versión (`icpu version`), sistema, pasos para
  reproducirlo y, si puedes, una imagen de prueba **sin datos reales**.
- **¿Una idea?** Abre un issue con la plantilla *Mejora* antes de escribir código grande, para
  acordar el enfoque.

## Preparar el entorno

Sigue la [guía de instalación](docs/guias/instalacion.md) (opción B o C) y comprueba que todo pasa:

```bat
scripts\build.bat test
```

Lee la [arquitectura](docs/tecnico/arquitectura.md) y [calidad, pruebas y desarrollo](docs/tecnico/calidad-y-pruebas.md).

## Reglas del proyecto

1. **Las interfaces no tienen lógica.** Lo nuevo va en la biblioteca (`src/`, `include/icpu/`), y la
   CLI y la GUI solo lo presentan.
2. **El formato es un contrato.** Cambiar cómo se escriben los bits, la sal, la cabecera, las
   claves o el empaquetado exige una **versión nueva del formato** y una decisión documentada en
   [decisiones-de-diseno.md](docs/tecnico/decisiones-de-diseno.md). Las imágenes antiguas se tienen
   que seguir leyendo (`tests/golden`).
3. **Lo seguro, por defecto.** Ninguna opción nueva puede hacer que el comportamiento por defecto sea
   más detectable o menos seguro.
4. **Cada cambio de comportamiento lleva su prueba**, y cada fallo corregido, una prueba que lo reproduzca.
5. **Sin avisos** del compilador (`/W4` en MSVC, `-Wall -Wextra -Wpedantic -Wconversion` en GCC/Clang).
6. **Estilo**: código, comentarios y mensajes de consola en español sin tildes; la web y la
   documentación, con tildes. Sigue el formato del código que tengas alrededor (`.editorconfig`).
7. **Documentación**: si cambia algo visible, actualiza la guía correspondiente en `docs/` y el
   [CHANGELOG](CHANGELOG.md) (en la entrada de la próxima versión).
8. **Nada de dependencias nuevas** sin hablarlo antes: todo va en `third_party/` y se compila con el proyecto.

## Pull requests

- Una rama por cambio, con commits pequeños y descriptivos.
- Rellena la plantilla del PR, incluida la casilla del **formato oculto**.
- La CI (Windows, Linux, sanitizers y fuzzing) debe pasar.
- Si cambias la GUI, adjunta una captura; si cambias resultados, regenera las figuras con
  `node scripts/generar_figuras.mjs`.

Al contribuir, aceptas que tu código se publique con la [licencia MIT](LICENSE) del proyecto.
