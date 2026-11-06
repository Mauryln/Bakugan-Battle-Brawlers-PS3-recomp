# Continuidad — FSR y salida de pantalla, 06/11/2026

## Resultado conservado

- Rama de trabajo `test`. NV3089 de patch0051 permanece intacto.
- FSR **1.0.2** oficial: EASU + RCAS FP32, render1280×720 → salida1920×1080.
- `tools/run.ps1 -Upscaler FSR1 -WindowMode Borderless` activa la salida.
  `-Upscaler Native` conserva la ruta nativa. No hay generación de cuadros.
- Ventana normal centrada y limitada al área de trabajo; modo sin bordes sobre
  el monitor completo. El hilo de render usa DPI por monitor. La comprobación
  final registró monitor, ventana y cliente1920×1080, DPI120.
- Usuario confirmó imagen correcta y encuadre corregido. El arranque con FSR
  conservó imagen/audio según la validación interactiva. Logs finales sin GPUV.
- El juego se cerró normalmente, liberando audio. Build normal y shaders están
  preparados; el backup anterior NV3089 sigue en `backups/`.

## Rendimiento: no confundir resolución con FPS

Ventana de60s, cámara fija,1682cuadros completos,720p→1080p con FSR1:

| Métrica | Valor |
|---|---:|
| Media |35,6871ms /28,0213FPS|
| p50 |34,7212ms|
| p95 |43,5792ms|
| p99 |49,2648ms|
| Cuadros por encima de33,333ms |69,44%|

No demuestra30FPS estables ni una ganancia por FSR. Usuario indica que FSR
mejora apreciablemente la resolución sin impacto perceptible en rendimiento.
No atribuir un porcentaje de mejora a comparaciones entre personajes/procesos.
Datos: `diagnostics/fsr1-20261106/{windows.json,summary.json,fsr1-check-frames.bin}`.

## Defecto corregido y regresión

`vp_prepare_vertex_fetch` conservaba `base[i]` del draw anterior cuando el
atributo quedaba desactivado o stride0. El descriptor compacto leía ese array
en lugar del registro constante. Se reproduce con la función real y se corrige
asignandoNULL en esa rama: patch0052. Test de los16atributos pasa desde checkout
y desde patch, sin juego. CI ejecuta `tools/test_vertex_fetch_plan.ps1 -FromPatch`.
VPCHK ampliado reveló el fallo también con las optimizaciones desactivadas;
tras corregirlo, >11millones de comprobaciones sin MISMATCH en esa ejecución.

## Experimentos que NO se deben reactivar

- Precarga del staging:26,836FPS original vs26,820FPS candidata. Descartada.
- Memcpy literal64/80:26,966FPS original vs26,558FPS candidata; peores colas.
- Caché de RSX_DBG_PICK: mejor media en algunos ensayos, peoresp99. Descartada.
- Elisión diferida de `last` en tiras:86.016paquetes idénticos en fixture,
  pero NO completó aceptación de rendimiento. Aparcada, no incluida en build.
- No repetir staging/cache de vértices/SIMD/decodeGPU/IO de sesiones anteriores.
- **Modo virtual540p retirado**: el juego consultaba960×540 y creaba esos buffers,
  pero conservaba viewports1280×540 y coordenadas UI incompatibles. El apuntado
  quedaba a la izquierda y se recortaba el juego. Volver a720p corrigió el encuadre.
  No publicar540p como compatible ni aplicar su parche experimental.

Artefactos de investigación: `diagnostics/submit-20261106/`,
`submit-copy-20261106/`, `cpu-waits-20261106/`, `strip-defer-20261106/`,
`fetch-disabled-20261106/`, `fsr1-540p-20261106/` y `fsr1-scale-fix-20261106/`.
Los estados viejos de esas carpetas pueden describir candidatos anteriores;
este documento refleja el resultado final.

## FSR3.1: investigado, pendiente de integración

RTX4060 es compatible con la ruta analítica FSR3.1. FSR4 no ofrece soporte
oficial para estaGPU. El usuario quiere continuar hacia FSR3.1DX12; no está activo.
Faltan profundidad fiable, vectores de movimiento reales y jitter aplicado al
render. No sustituirlos por texturas vacías ni anunciar FSR3 por cargar unaDLL.

Se localizaron headers y DLL oficial firmada del SDK1.1.4 / FSR3.1.4 en
`diagnostics/fsr3-sdk-v1.1.4/`. Firma verificada: Advanced Micro Devices, válida.
No se instaló nada global ni se distribuye esaDLL en git. El SDK2.4 también
ofrece fallback analítico3.1.6 para RTX. Referencias:
[FSR3](https://gpuopen.com/fidelityfx-super-resolution-3/),
[SDK](https://gpuopen.com/amd-fsr-sdk/),
[headers1.1.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4/ffx-api/include/ffx_api).

Siguiente sesión: leer este estado y `docs/FSR.md`; conservar la salida correcta
720p→1080p. Para540p hace falta escalado coherente del renderer, manteniendo
coordenadas lógicas de HUD/cámara; no falsear `cellVideoOutGetResolution`.
Antes de FSR3, definir cómo producir sus entradas a partir del RSX real.

## Medición reutilizable

`FRAME_TIMES=<ruta>` guarda `FTIME01\0` y pares little-endian de doubles:
finQPCenms e intervaloenms. Escritura por lotes120; el último lote parcial no
se guarda. Esperar al menos un lote después de la ventana y comprobar cobertura.
`FRAME_METRICS=1` conserva percentiles por lotes. Ninguna opción se activa sola.
`tools/windows_cpu_sample.cpp` es el muestreador externo con PDB: pausa breve,
contexto/pila congelados, Resume equilibrado y símbolos después de reanudar.
No es un benchmark de FPS. El diagnóstico de CPU sigue pendiente de solución.
