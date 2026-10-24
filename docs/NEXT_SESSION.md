# Reporte de cierre — 24 de octubre de 2026

## Estado confirmado por el usuario

- El **audio quedó casi perfecto**; queda revisar cualquier corte residual.
- **Battle Arena tarda en cargar, pero inicia el combate.**
- Las texturas **2D cargan normalmente**.
- El renderizado **3D sigue pendiente**: personajes y escenario todavía no se ven correctamente.
- Se detuvo el trabajo a petición del usuario para conservar su cuota. No iniciar más pruebas hoy.

## Cambios de esta sesión

### Audio y ejecución SPU

Los menús abren pistas `SNDP` con PCM de 16 bits, estéreo, a 44,1 kHz. La nota
anterior que atribuía todo el silencio a ATRAC3 era incorrecta para estas pistas.

Se implementaron las colas de entrada SPU y el servicio `stop 0x110`: vinculación,
recepción del evento y continuación del programa. Sus claves son valores de
32 bits, distintos de los puertos de salida 0–63. Los handlers publican también
el resultado en el registro PPU `r3`.

Multistream copia dos kernels a **LS `0x32D80`**, con entrada `0x32D98`. Sus lifts
originales estaban en `0x80`. `tools/build_audio_overlays.py` genera los lifts
reubicados y los registra como imágenes 6/7. Se reconocen por firma porque el
juego copia sus bytes al heap antes de transferirlos al SPU. Los archivos
generados permanecen en `bakugan-recomp/spu/`, ignorados por Git.

También se corrigieron los retornos entre intérprete y código compilado, se
eliminó una consulta `getenv` por cada acceso MFC y se habilitaron las rutas SIMD
existentes del toolkit. El mixer mantiene un pulso de 256/48000 segundos y las
esperas PPU solicitan temporizadores de alta resolución.

**No todas esas modificaciones resolvieron por sí solas los cortes.** Las trazas
mostraron entregas de unos 47 ms para 10,7 ms de audio; quitar el bloqueo de
configuración redujo el intervalo a aproximadamente 16–30 ms. El usuario confirmó
la mejora importante después de activar SSE4.1.

### Renderer

- La firma raíz, los samplers y las texturas por dibujo pasaron de **4 a 16 unidades**.
- Los pipelines inválidos se guardan en caché para evitar recompilar el mismo
  shader en cada dibujo. Antes se observaron errores `0x80070057` repetidos y
  caídas hasta una fracción de FPS.
- Battle Arena sí emite geometría: capturas con **438–600 dibujos** y contenido
  en `0x0EAB0000`. La conclusión antigua de «cero geometría» correspondía al menú,
  y no describe el combate.

## Validación y límites

- Build final correcta: `diagnostics/arena/build16.log`.
- Pruebas puntuales pasan: colas/STOP SPU, retornos dinámicos, reloj de audio,
  temporizadores PPU, configuración MFC y creación de PSO usando la textura 15.
- SIMD: 200.000 casos de shuffle, ocho millones de casos de coma flotante y
  70 pruebas de helpers, sin diferencias respecto a las referencias.
- Ambos kernels de audio coincidieron **bit por bit en registros y memoria**
  con el intérprete en estados capturados del juego.
- La última build incorpora **AVX2 después de la última confirmación visual del
  usuario**; falta validar ese ejecutable en juego. Después solo se retiró un
  logger temporal del código fuente: recompilar al retomar para sincronizarlo.

Opciones CMake: `PORT_SPU_AVX2=OFF` conserva SSE4.1; `PORT_SPU_SSE41=OFF` desactiva
ambas optimizaciones. El ejecutable comprueba las capacidades de CPU al arrancar.

## Prioridades para mañana

1. Recompilar y confirmar audio, aviso de guardado, título y entrada en Battle
   Arena con los vídeos normales y sin variables de diagnóstico heredadas.
2. Reproducir un combate con personaje/escenario fijos. Guardar captura, FPS y
   logs; separar tiempo de carga de rendimiento durante el combate.
3. Seguir la imagen desde el target 3D hasta la pantalla. Verificar especialmente
   el resolve NV3089 `0x0EAB0000 → 0x0DF60000`: el backend busca texturas por offset
   exacto y podría estar leyendo la copia CPU en lugar del resultado GPU.
4. Revisar entradas de vértices, constantes y shaders antes de cambiar texturas.
   Otra hipótesis pendiente es el fallback de proyección de `vp_fill_cb_slot`,
   que puede sustituir `c0..c3` según `c00`; comprobar si afecta al juego.
5. Revisar los shaders que todavía fallen, formatos comprimidos y samplers.
   **No dar por corregido el 3D por el hecho de que el combate inicie.**

## Archivos y reproducción

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py --only patch
.\.venv\Scripts\python.exe tools\pipeline.py --only spu-lift
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

Los cambios reutilizables están en `0005-audio-spu-events-and-timing.patch` y
`0006-d3d12-texture-units-and-pso-cache.patch`. Se comprobó que el pipeline reconoce
los seis parches ya aplicados.

Evidencia local, ignorada por Git: `diagnostics/arena/`. Especialmente
`fixed5_frames/frame_06/` (escena corrupta dentro del RT), `kernel6_test.log`,
`kernel7_test.log`, `simd_avx2.log`, `texture_units_green.log` y `audio_timeline.txt`.
Las mediciones de FPS pertenecen a ejecuciones distintas, algunas con capturas;
no constituyen un benchmark comparable.

Repositorio oficial: <https://github.com/Mauryln/bakugan-ps3-recomp>.
Los cambios de esta sesión quedan **sin commit ni push**. No se añadieron assets
del juego. Antes de publicar, revisar el diff, ejecutar las regresiones y actualizar
el estado con el resultado real del 3D.
