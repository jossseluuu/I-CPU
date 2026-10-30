## Qué cambia y por qué

<!-- Describe el cambio y el problema que resuelve. Enlaza el issue si existe: "Cierra #12" -->

## Comprobaciones

- [ ] Compila sin avisos (`scripts\build.bat test` o `cmake --preset release && ctest --preset release`)
- [ ] He añadido o actualizado pruebas en `tests/` si cambia el comportamiento
- [ ] He actualizado la documentación (README, `docs/`) y el `CHANGELOG.md` si hace falta
- [ ] **Formato oculto:** el cambio no altera las imágenes que se generan o extraen
      (si lo hace, explícalo y propón un ADR nuevo en `docs/adr/`)
