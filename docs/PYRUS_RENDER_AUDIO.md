# Pyrus con boost desactivado

Trabajo local en `codex/pyrus-postprocess`, basado en `main` (`aeb708c`).
Las pruebas usan salida 1080p FSR1, render interno 720p y límite de 30 FPS.
El boost sigue desactivado. La optimización arquitectónica de 60 FPS queda
para otra etapa.

## Grabación y evidencia

El usuario controló la partida hasta Pyrus. El guardado se copió a un perfil
aislado y se seleccionó `BLES00758-PLAYER-DATA-00` sin mostrar el selector.
La ventana abrió en el monitor principal con el contador de FPS visible.
`diagnostics/pyrus-heat-20261109/pyrus-reference.padrec` conserva 522 muestras
hasta el flip 3155. SHA256
`6C1B74AA0300D176DC17BC72A1BE2F6E93799681FBF72A76D8E312F7D4A99B88`.
El replay llegó a la vista de apuntado en Pyrus. Los personajes y el instante
de animación pueden variar. Usar el mismo proceso y la misma cámara para medir
el efecto de una sola variable.

Ese replay y `cards.padrec` quedan como rutas de `tools/autopad.ps1`
(`list`, `play pyrus`, `play cards`, `record <nombre>`, `verify`). El script
copia la baseline a un perfil aislado por ejecución y espera
`[cellPad] PAD_REPLAY done`; `-PadFile` toma el control después del replay.

Las imágenes entregadas por el usuario están bajo `bug-pictures/emulador/`
y `bug-pictures/pc/`. En PC se suaviza la escena entera y los bordes del HUD
siguen definidos. En `run-082058452/captures/frame_102/` se conserva la escena
nítida en `rt_0EAB0000.bmp` (1120×584) y el cuadro ya difuminado en
`backbuffer.bmp` (1280×720). El defecto precede al escalado FSR1. El manifiesto
identifica los pases intermedios y la composición antes del HUD.

## Causa del desenfoque

El shader de postproceso reconstruye profundidad con un producto escalar de
los tres bytes RGB y mezcla una escena nítida con muestras filtradas a 640×360.
En el manifiesto de la grabación, el pase `0x028FF101` lee profundidad desde
`0x02E70000` (unidad 2). La textura se declara ARGB8 con remap `0xA993`, aunque
los bytes pertenecen a la superficie D24S8. El backend anterior sólo vinculaba
la profundidad nativa cuando la textura se declaraba en formato de profundidad.

El otro consumidor necesita profundidad de escena en `0x0E1E0000`. El log de
`run-082649848/stdout.log` confirma tres tiles NV3089 de `0x0E460000` hacia
`0x0E1E0000`, con paso horizontal 2 y vertical 1. La copia anterior sólo podía
retener superficies de color. Como la profundidad real estaba en D3D12, los
bytes guest del resolve quedaban vacíos. Las direcciones son ejemplos de la
captura y cambian entre ejecuciones.

La candidata vincula el recurso D24 real en las lecturas ARGB8 puntuales con
ese remap, por dirección exacta o por la transferencia NV3089 observada. El
shader reconstruye los bytes de profundidad en RGB y conserva el postprocesado
del juego. El backend actual no escribe stencil guest. El alfa del caso
soportado queda en cero y los programas inspeccionados leen RGB. No se generaliza
la reinterpretación a otros formatos, remaps o filtros.

## Comparación en la misma partida

En `run-083218907/captures/frame_100/` y `frame_101/` se conserva el A/B.
Mismo proceso, personaje, arena, cámara y configuración. Las animaciones de
lava, partículas y objetos siguieron avanzando. Con el enlace de profundidad
activo se ven definidos el contorno del personaje, la plataforma frontal y
el suelo cercano. El fondo conserva desenfoque y la lava, el humo, las cartas
y el HUD siguen dibujándose. El manifiesto confirma las unidades 0 y 2 en los
tres pases investigados. La referencia visual final se verificó en la ventana.

| Ventana de 17 s | FPS medios | p95 | Draws medianos | SRV por cuadro |
| --- | ---: | ---: | ---: | ---: |
| Enlace anterior | 24,41 | 48,86 ms | 1402 | 6,5 ms |
| Profundidad real | 25,49 | 46,10 ms | 1417 | 4,0 ms |

El `vfetch` se mantuvo en unos 17,3 ms por cuadro y los blits en 1,75 ms por
flip. Se observó una pequeña mejora en este A/B, principalmente en la
preparación de texturas. Pyrus sigue teniendo más trabajo de vértices que la
medición previa en Ventus. No extrapolar la diferencia a otras arenas o equipos.

El arreglo queda activado por defecto. `RSX_PACKED_DEPTH=0` recupera el camino
anterior y `RSX_PACKED_DEPTH_GATE` permite repetir la comparación dentro del
mismo proceso. En el toolkit local, los cambios de audio y profundidad están
archivados respectivamente en `patches/0069-audio-notify-dedup-and-device-diagnostics.patch`
y `patches/0070-d3d12-packed-depth-resolve.patch`. Ya están aplicados y la
comprobación inversa pasó. Mantener `main` como respaldo estable.

## Audio y rendimiento

La grabación manual muestra una pausa de cuadro de 4,13 s al entrar a Pyrus.
La ventana PERF cercana tiene 52 fallos de caché de PSO y 1,24 s de trabajo de
PSO. No atribuir toda la pausa a esos fallos. Durante la arena estable, las
ventanas medianas son 24,54 FPS y 1424 draws, con unos 17 ms de `vfetch` por
cuadro y 6,5 ms de SRV. La mayoría tiene cero fallos de PSO. Las métricas de
submit y vfetch se superponen. El desenfoque se explica por datos de textura
incorrectos. El coste sostenido de CPU y los tirones al cargar son mediciones
distintas.

El mezclador promedió 187,4–187,6 bloques/s y cero muestras descartadas por
falta de espacio. La tasa media no mide interrupciones breves del dispositivo.
El código registraba dos veces la cola de audio cuando Bakugan llamaba Create
y Set con la misma clave. La prueba `tools/test_audio_notify_queue.py` falla
con el código anterior y pasa al hacer Set idempotente. Se conserva el registro
de Create para títulos que no llaman Set. El mezclador copia la lista de claves
bajo el mutex y publica los eventos después de soltarlo.

Las 64 primeras lecturas registradas de la cola siguen dejando 7 de 8 plazas
ocupadas, incluso después de la corrección. Los volcados PCM previos a WASAPI
muestran silencios de transición casi idénticos (12,35 s antes y 12,32 s después
en la transición larga). Esto no demuestra que el corte audible se haya ido.
Queda separar pausas esperadas, huecos en un puerto individual y posibles
subalimentaciones de WASAPI. El audio de películas tiene una ruta independiente.

## Punto de continuación

El usuario confirmó que la imagen quedó corregida y que el stutter de audio
persiste. Los FPS le resultaron aproximadamente iguales. La mejora pequeña
del A/B corresponde a esa muestra controlada. La última ejecución usa la capa
de depuración D3D12 y queda fuera de la comparación de rendimiento.

- Build probada por el usuario en `run-084155085/`. SHA256 del ejecutable
  `D9A9ADF800277C558D8F90F2671957633BFF4C75BEC87B93A05AA74C41873DF3`.
  El replay terminó con 522 muestras y `captures/frame_100/manifest.txt`
  registra 1421 operaciones y los tres pases corregidos (unidades 0, 2 y 2).
  No se registraron fallos de compilación de shaders, EDGE ASSERT ni GPUV.
  `final.ps1` reproduce la entrada a la arena con esta configuración de
  diagnóstico. En el cierre se conservan ejecutable, dependencias y logs en
  `diagnostics/pyrus-heat-20261109/`.
- En `run-083710398/` se validó con la capa D3D12 y se registraron 55 ventanas
  de WASAPI. Intervalo máximo registrado de 16 ms y cero errores de envío.
  El padding fue cero cinco veces en toda la ejecución, pero nunca en las
  ventanas cercanas a los cuadros de 4,19 s y 1,82 s. `GetTickCount64` mide con
  resolución limitada. Estos contadores no garantizan audio continuo.
- Se detectaron bloques de PCM exacto a cero de 5,33–10,67 ms con bordes
  abruptos desde ~59,3 s de audio, antes de WASAPI y tanto antes como después
  de corregir el doble aviso. `pcm-gaps.py` permite repetir ese análisis.
  La siguiente prueba debe medir por puerto y sincronizar con los cambios de
  escena. Comparar el PCM del mezclador con la salida y distinguir las
  transiciones de película. No variar el reloj ni la latencia a ciegas.
- La versión revisada exige todos los tiles válidos del resolve, su productor
  D24 en el mismo lote, textura local y páginas sin escrituras posteriores.
  La prueba `tools/test_packed_depth_resolve.py` cubre tiles incompletos,
  recorte final, orden y sobrescritura de páginas interiores. `Sample`,
  `SampleBias` y `SampleLevel` reconstruyen los bytes antes del shader guest.
- Límites de revisión pendientes antes de integrar. Un segundo resolve al
  mismo destino en el mismo lote puede ocultar el primero a consumidores
  intermedios. Pyrus mostró un resolve completo por destino y cuadro. Para
  generalizar, tomar la identidad del resolve en el momento del consumo.
  Limpiar también el contador de resolves al descartar el lote en un retorno
  temprano y al reiniciar el backend. Esos casos no se modificaron después de
  la prueba que confirmó el usuario.

`main` y `origin/main` siguen en `aeb708c`. Los cambios de esta investigación
están en el árbol de trabajo de `codex/pyrus-postprocess`, sin commit ni push.
Los parches 0069 y 0070 preservan los cambios del toolkit ignorado. No cambiar
de rama, resetear ni reaplicar los parches dentro de `ps3recomp-src`.
