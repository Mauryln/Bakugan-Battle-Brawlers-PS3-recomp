# HUD, cartas y cinemáticas: investigación vigente

Actualizado el 8 de noviembre de 2026. HUD y cinemáticas corregidos y confirmados por el usuario. Revisión del runtime
completada; regresiones enfocadas pasan. Esto no certifica todas las partidas
ni todos los equipos. Estado de continuación: [CONTINUAR_HUD.md](CONTINUAR_HUD.md).

## Causa observada

La explicación anterior de «evento de fin perdido antes del Attach» era
incorrecta para el fallo medido. La instrumentación del puerto 1 encontró:

```
EDGE ASSERTION FAILURE: %s(%d)
edgezlib_inflate_task.cpp(272)
retorno de la cola: 0x80410911
```

El evento transportaba el diagnóstico de una aserción. Su llamada termina en
`stop 0x3FFF`; responder CELL_OK o aplazar el evento no arregla el aborto.

La ruta SPU que completa la extracción de la LFQueue resuelve un ID de workload
mediante `CellSpurs + 0xB00 + 32*wid + 8`. CreateTaskset sólo conservaba el
taskset en tablas del host y escribía wid=0: el lookup guest devolvía NULL y
SendSignal fallaba. El ritmo de arranque determinaba si se alcanzaba esa ruta.

## Correcciones implementadas

- CreateTaskset reserva un ID y publica el taskset en la tabla guest. Los
  workers HLE conservan su ejecución; el scheduler de policy modules los omite.
  JoinTaskset retira el workload después de esperar el fin de los tasks.
  DestroyTaskset libera el ID; Destroy/Remove rechazan tasks activos con BUSY.
  Se comprueba la identidad del taskset al liberar para que un Join repetido no
  retire otro taskset que reutilizó el ID. Crear dos veces el mismo EA falla.
- El productor PPU de LFQueue publica bajo el lock de coherencia. En ANY2ANY
  respeta el registro individual de consumidores: q+4 contiene los contadores
  registrados/confirmados/notificados (5 bits cada uno, módulo 30); q+0x32
  contiene los tokens de workload/task (15 entradas). Sólo avanza notificados
  por los elementos disponibles, descontando notificaciones pendientes.
  Resuelve cada destino y publica antes de señalizarlo fuera del lock. No
  resetea el registro completo ni despierta consumidores indiscriminadamente.
  Sin esta segunda parte, la carga quedaba parada en pop=254/push=280.
  El wake genérico de ANY2ANY se suprime porque alteraba las confirmaciones.
- La señal pendiente y la entrada en WAIT_SIGNAL usan el mismo lock/CV. Se
  volvió a comprobar la señal dentro de la espera: había una ventana entre
  el pre-check y el registro del task dormido.
- Un send_event sin puerto devuelve ENOTCONN en el buzón; throw_event consume
  el dato sin ack. Se retiró la espera artificial de 2 segundos. Los códigos
  128/192 de flags también se procesan para tasks sin grupo LV2.
- El volcado masivo de descriptores de imagen 3 requiere SPU_BINK_DESC=1;
  los logs de colas tienen presupuesto reducido. Sin cambios de renderer.

La revisión independiente finalizó y sus observaciones de ciclo de vida y
consumidores múltiples se corrigieron con pruebas de regresión. El token especial
0xFFFF (notificación PPU) sigue sin estar implementado: devuelve STAT antes de
publicar, sin éxito ficticio ni publicación parcial. No apareció en el caso de
Bakugan probado. No ampliar ese protocolo sin evidencia y pruebas propias.

Archivos del toolkit: libs/spurs/cellSpurs.c, runtime/spu/spu_workload.c y
runtime/syscalls/lv2_register.c. El parche reproducible de los cambios de esta
sesión es patches/0066-spurs-taskset-lfqueue-signals.patch; ya está aplicado en
el checkout actual. NO reaplicarlo ni resetear el toolkit. No se editaron lifts.

## Evidencia local

Todo bajo `diagnostics/code-repair-20261108-171723/` (no versionado):

- `port-before.patch`, `toolkit-before.patch`, `BakuganRecomp-before.exe`:
  estado previo preservado. `docs-before/`: documentación previa.
- `before-files-171746/`: reproducción por cmd.exe con ficheros separados,
  sin HUD/cartas. `before-screen.png`: captura real.
- `eventdiag-files-172007/` y `pathdiag-files-172243/`: aserción, LS y ruta.
- `workload-fixed-files-172810/`: desaparece el aborto, aparece carga bloqueada.
- `wake-fixed-files-173301/`: prueba intermedia; no usar como build final.
- `ledger-fixed-files-173708/` y `ledger-fixed-hud-cards.png`: batalla con
  retrato, mira, cartas y escenario en 720p nativo por el arranque que fallaba.
- `quiet-1080-files-174032/scene-10.png`, `scene-11.png`: ambos personajes
  sosteniendo cartas; `scene-14.png` a `scene-19.png`: secuencia de lanzamiento;
  `scene-24.png` en adelante: HUD y cartas. Revisar el escenario antes de
  aceptar fidelidad completa: en esta arena aleatoria se ve suelo liso/cielo.
- `clean-174547/`: perfil limpio, resultado positivo intermedio; no es el
  binario final.
- **`clean-175828/`: binario final**, perfil LOCALAPPDATA aislado, sin PS3_*
  heredadas, configuración de player.ini (1080p FSR1, interno 720p), baseline
  copiada. `battle-hud-cards.png`: escenario Haos completo, HUD y cartas.
  `final-scene-00.png` a `final-scene-53.png`: nueva presentación y batalla
  Aquos con agua/escenario completo; `final-scene-12.png`: personajes
  sosteniendo cartas; `final-scene-24.png`, `36`, `53`: cartas y HUD.
  La última selección de imágenes de lanzamiento quedó sin inspección final
  individual al agotarse el contexto; el usuario confirma HUD y cinemáticas
  corregidos. No presentar la captura intermedia como prueba del binario final.
  Cero EDGE ASSERT/stop 0x3FFF, GPUV y eventos sin puerto en el análisis de logs.
  Los 20 nombres con open FAIL no existen en el dump; no se encontraron fallos
  de apertura de archivos presentes. No crear assets ficticios.
- `clean-175628/`: entró en historia con otra partida; no comparar con batalla.

Binario final `bakugan-recomp/build/BakuganRecomp.exe`, SHA256:
`2D3FF4D00C07F29966AC95ABAEE9529BE2190460341F4B52C9F124424E65BE02`.
Las arenas/personajes cambian aleatoriamente: no comparar draws o FPS entre
esas capturas. Los runs anteriores al protocolo individual son intermedios.

Las capturas son de la ventana real con Computer Use. RSX_CAP sirve para
diagnóstico del backend, no demuestra por sí mismo que el HUD sea visible.
Los ~200 WAIT_SIGNAL no son una condición universal: tras corregir el protocolo
puede haber menos esperas. Los ch-block de SPUs raw de audio deben distinguirse
de un aborto del task de assets; no exigir cero global sin identificar origen.

## Arranque actual

`BakuganBrawlers.cmd` llama a `tools/play.ps1`; conserva logs separados en
`%LOCALAPPDATA%/BakuganRecomp`. Launcher e instalador siguen eliminados y su
reconstrucción está aplazada por el usuario. Mantener PS3_VERBOSE=0 al registrar
logs. El ejecutable lee Graphics, idioma y controles de player.ini; el entorno
explícito tiene prioridad. Render interno 720p.

## Regresiones enfocadas

```
.venv/Scripts/python.exe tools/test_spurs_taskset_workload.py
.venv/Scripts/python.exe tools/test_spu_user_event_ack.py
.venv/Scripts/python.exe tools/test_player_boot.py
.venv/Scripts/python.exe tools/test_repository_contract.py
```

Las dos primeras requieren clang-cl y el toolkit local; la de workload requiere
la librería del build actual. Fallaron antes de sus cambios y pasan después.
PlayerInputTest y PlayerPadProviderTest también pasaron. No se han probado todas
las arenas ni una partida completa después de estos cambios.

## Otras correcciones de código

main.cpp aplica Graphics de player.ini antes del runtime, con prioridad para
el entorno explícito. has_input_overrides detecta bindings personalizados en
modo Auto. Las rutas de configuración conservan Unicode. Se corrigió además
un desbordamiento de un byte en el buffer de WideCharToMultiByte del ENV_DUMP
(reservar también el NUL). Tests de arranque verifican configuración/precedencia.
CI y enlaces documentales se actualizaron; componentes borrados siguen borrados.

## Alcance pendiente

No se modificó renderer, walker, submit ni decode de vértices. Los 60 FPS quedan
aplazados por el usuario; productor/consumidor y paralelización necesitan otra
rama y validación de orden de draws y coherencia guest. Antes de pasar al launcher,
completar el repaso de defectos y aceptación funcional de la build final; después
reconstruir launcher y finalmente instalador, con aprobación del alcance futuro.
No se completó una partida entera ni una nueva prueba final a salida 720p nativa
con el último algoritmo (las pruebas finales de perfil limpio fueron a 1080p FSR1).
