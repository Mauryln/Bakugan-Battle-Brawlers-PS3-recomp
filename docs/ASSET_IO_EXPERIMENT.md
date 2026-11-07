# Experimento de entrada/salida archivado

`patches/0049-asset-io-bounded-cache.patch` y las herramientas
`measure_game_io.py`, `inspect_io_trace.py`, `test_asset_io.py` y fixtures
`asset_io_*` conservan el experimento local de caché y trazas de assets.
**No es una optimización aprobada para la build de referencia.** No reaplicar
este parche al toolkit sano ni activar variables PS3_IO como parte del launcher.

Los resultados anteriores de 29–30 FPS con geometría ausente fueron rechazados.
Las pruebas del backend comprueban operaciones de archivos, no la fidelidad de la
escena ni una ganancia de FPS. Para reproducirlas hace falta el backend experimental
indicado por `--backend` (o el checkout local que ya lo contiene). El launcher y
el proveedor de controles no dependen de este experimento.

El toolkit local es un checkout separado con cambios del usuario: no resetear,
rebasear, cambiar de rama ni reaplicar la serie completa para reproducir el launcher.

En el cierre de esta entrega, `runtime/io/asset_io.cpp` no está presente en el
checkout sano. Por eso el test experimental no pudo compilarse; las pruebas de
entrada y las dos regresiones de CI sí pasan. Esto no bloquea el launcher ni
justifica restaurar el backend experimental en producción.
