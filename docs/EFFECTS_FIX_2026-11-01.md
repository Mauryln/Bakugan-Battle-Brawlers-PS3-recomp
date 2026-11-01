# Aquos: reflejos, sombras y fronteras de cuadros

## Correcciones verificadas

- `0038`: decodifica SET_SHADER_WINDOW y adapta WPOS al origen, altura y
  centro de píxel del invitado. WPOS.w expone 1/W sin alterar la corrección
  de perspectiva de texcoords. El usuario confirmó reflejos correctos.
- `0039`: DSV/SRV D24 por dirección y tamaño, clears en orden y cero RTVs
  en pases sin color. ZFUNC compara profundidad contra referencia; antes
  los operandos estaban invertidos. El usuario confirmó sombras oscuras.
- Las capturas de profundidad usan GetCopyableFootprints. La prueba anterior
  construía un footprint incompatible y perdía el dispositivo durante copia.
  Las nuevas capturas guardan BMP y datos crudos con cabecera width/height/pitch.

## Parpadeos: evidencia y corrección en prueba

En `diagnostics/arena/aquos-depth-isolated-20261101/frame_130`, el backbuffer
es completamente negro y el lote llega al límite de 2048 operaciones. Los
cuadros vecinos contienen 1637 y 1635; en el defectuoso hay dos pases de
reflejo completos (702 draws frente a 351). Se acumuló parte del cuadro
siguiente y su composición final se descartó al llenar el lote.

`0040` añade GCM_FULL_FRAMES, activado por defecto por el port. Detiene el
FIFO en cada flip aunque haya >=64 KiB pendientes, conservando la política
anterior del toolkit cuando esa opción está desactivada. El reloj consume
también la notificación pendiente al presentar después del drain, evitando
una segunda presentación del mismo flip con un lote vacío.

La capacidad sube a 8192 operaciones para conservar composición y HUD en
cuadros cargados. El b0 de doble paridad reserva 132 MiB, frente a 33 MiB;
los heaps de samplers se crean según se necesitan. Ampliar capacidad no
sustituye la corrección de fronteras.

Validación: Release compilada, 23/23 pruebas de render/FIFO/pipeline; GPU
WPOS y comparación ZFUNC, dos superficies D24 con patrón no uniforme,
conservación de 8192 tablas de sampler, clears tras 4096 operaciones y
presentación única por flip. Los 39 parches aplican sobre HEAD limpio del
toolkit. Solo quedan fuera los diagnósticos históricos VTX_COL y conteo de
draws indexados.

Corrida actual: `diagnostics/arena/aquos-full-frames-20261101`. En los frames
118–141 (24 capturas del turno), no hay backbuffers negros/blancos ni pases
de reflejo duplicados; se conserva la composición. Los fades de transición
anteriores a ese tramo no cuentan como destellos. Falta confirmar visualmente
varios turnos sin parpadeos con esta última build.
Pyrus, Haos, Subterra y Ventus no están validados en esta sesión; no se dan
por cerrados todos los efectos de arenas. Sigue pendiente el rendimiento
sostenido y la investigación de menús por inactividad.

## Referencia de implementación

RPCS3 del usuario, commit e53ac18f:
[WPOS](https://github.com/RPCS3/rpcs3/blob/e53ac18f/rpcs3/Emu/RSX/Core/RSXDrawCommands.cpp),
[ZFUNC invertido para samplers Vulkan](https://github.com/RPCS3/rpcs3/blob/e53ac18f/rpcs3/Emu/RSX/VK/VKDraw.cpp).
