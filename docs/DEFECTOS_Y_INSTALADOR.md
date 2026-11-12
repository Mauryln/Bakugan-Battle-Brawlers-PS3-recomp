# Defectos encontrados y secuencia de entrega

Estado al cierre del 8 de noviembre de 2026. El usuario confirma HUD y
cinemáticas corregidos. Causa, código, pruebas y evidencias en
[HUD_START_BUG.md](HUD_START_BUG.md); traspaso en
[CONTINUAR_HUD.md](CONTINUAR_HUD.md). Este registro sustituye conclusiones
previas descartadas; no garantiza ausencia de bugs en todos los recorridos.

## D1/D2: HUD, cartas y cinemáticas — corregidos

La aserción de edgezlib se debía al lookup guest del taskset no publicado.
Se corrigieron además el ledger de consumidores ANY2ANY y el despertar antes
de park. La revisión independiente terminó; sus observaciones se resolvieron
con regresiones. La build final mostró HUD/cartas y escenarios completos
en perfil limpio; el usuario confirma las cinemáticas.

No atribuir el fallo a una completion perdida en el puerto 1. No usar como
criterio WAIT_SIGNAL≈200: el protocolo corregido cambia las esperas. No usar
RSX_CAP como prueba visual del HUD; inspeccionar la ventana real.

## D3: configuración y seguridad del runner — corregidos

main aplica Graphics de player.ini sin sobrescribir el entorno explícito.
Detecta bindings personalizados en modo Auto y conserva rutas Unicode.
ENV_DUMP reserva también el NUL de WideCharToMultiByte, evitando escribir
un byte fuera del buffer. test_player_boot.py verifica configuración y
precedencia en el runner real, sin cargar datos del juego.

## D4: punto de entrada eliminado — corregido

tools/run.ps1 ya no exige el launcher borrado. BakuganBrawlers.cmd llama
a tools/play.ps1. El ejecutable corregido lee la configuración directamente;
no afirmar que el .exe directo pierde necesariamente el HUD. Los wrappers
facilitan rutas y logs, pero no sustituyen las correcciones del runtime.

## D5/D6: CI y referencias obsoletas — corregidos

CI ejecuta compileall y test_repository_contract.py (enlaces locales y puntos
de entrada); ya no llama a scripts borrados. Se actualizaron README,
CONTRIBUTING, AGENTS y README del port. Las pruebas SPURS requieren toolkit
y build locales: no presentarlas como cobertura CI o partida completa.

## D7: launcher e instalador — aplazados

Siguen eliminados por decisión del usuario. No restaurar automáticamente
los archivos de backups/launcher-removed-*/. Primero aceptar el código, luego
reconstruir launcher y después instalador. Las propuestas anteriores sobre
payload, accesos directos y extracción eran un borrador histórico; no obligan
a excluir el futuro launcher ni a usar PowerShell como interfaz definitiva.
El diseño futuro debe respetar perfil, guardados y configuración existentes.
Los extractores SelfExtract/Rpcs3Decrypt antiguos sólo están en copias,
no son componentes activos ya disponibles para empaquetar.

## D8: lecturas nulas del guest — seguimiento

La ejecución final analizada no mostró null-read; el fallo anterior tenía 16.
No se añadió un parche genérico que oculte lecturas inválidas. Si reaparecen,
identificar función y estructura afectada antes de modificar la VM.

## D9: archivos de efectos ausentes — verificado contra dump

En la ejecución final, los 20 nombres con open FAIL estaban ausentes del dump;
no se encontraron fallos de apertura de archivos existentes. No fabricar assets
ni diagnosticar pérdida de instalación sólo por esas líneas. Un futuro paquete
debe conservar USRDIR completo y distinguir ausencia original de error de rutas.

## D10: instrumentación — estado actual

Se conservaron diagnósticos opt-in: BAKUGAN_BOOT_REPORT, BAKUGAN_ENV_DUMP,
BAKUGAN_UI_IO_AUDIT y RSX_CAP_FLIP. El volcado masivo de descriptores de
imagen 3 requiere SPU_BINK_DESC (o la ruta diagnóstica LBP_REAL_POLICY).
Los logs de LFQueue tienen presupuesto reducido. Se retiraron la espera
artificial spurs_port_queue_wait de 2 segundos y la instrumentación temporal
SPU_EVENT_DIAG. No reintroducirlos como corrección.

## D11: logging y concurrencia — causa aclarada

El destino/ritmo de logs exponía carreras del runtime. No era una dependencia
funcional válida de las cinemáticas. PS3_VERBOSE=0 sigue siendo obligatorio al
redirigir para no saturar al guest; tools/play.ps1 lo fuerza. Evitar trazas
masivas en rutas calientes. La build corregida se probó con archivos separados
y perfil aislado sin PS3_* heredadas.

## D12: stop 0x3FFF — interpretación anterior descartada

La afirmación anterior de que image=3 stop 0x3FFF era benigno era incorrecta
para la ruta medida: procedía de EDGE ASSERTION FAILURE en edgezlib. El texto
RETURNED rc=0 / job ran to completion no prueba éxito: el wrapper genérico
puede devolver cero después de un halt. La corrección elimina la aserción
observada; no se cambió a ciegas el contrato del wrapper. Si reaparece el stop,
inspeccionar causa/LS y no ignorarlo por el rc.

## D13: 60 FPS — aplazados expresamente

No hubo cambios de renderer, walker/submit ni decode de vértices en este cierre.
La propuesta de separar productor/consumidor y paralelizar decode requiere su
propia rama, medir antes y validar orden de draws y coherencia guest. Las cifras
históricas de FPS no son un benchmark del binario final de esta sesión.

## D14. Bakugan oscurecidos en el duelo

El 10 de noviembre se reprodujo el defecto en «Use Ability Cards». Los
render targets de 1120×584 `0x09120000` y `0x08C20000` ya contenían ambos
modelos iluminados. Después de componerlos, el pase de pantalla completa
`op313` del frame 6476 usó alfa 0,5 y una textura ARGB8 de memoria principal
(`0x01033D00` en RSX, `0x12133D00` en memoria guest). Se leyeron sus
3.686.400 bytes a cero en varios flips del duelo. Los píxeles de los modelos
perdían la mitad de su RGB antes de dibujar el HUD.

En `render_frame` de `libs/video/rsx_d3d12_backend.c`, el fallback
`SCREEN_AS_TEX` trataba cualquier textura vacía del tamaño de pantalla
como una superficie de reflejos y la sustituía por una copia del
framebuffer. La copia aportaba alfa donde la textura guest era transparente.
El [parche 0076](../patches/0076-d3d12-preserve-main-memory-overlay.patch)
limita esa sustitución a memoria local RSX. Los render targets y resolves
explícitos siguen teniendo prioridad. El pase de la capa se conserva.

La prueba `rsx_texture_source_test.cpp` falló con la selección anterior y
pasó con el cambio. También pasaron el fixture de packed depth y el contrato
del repositorio. En Release con clang-cl, el replay local
`battle-v2-match-125541771` completó 1.080 muestras y el usuario confirmó
el brillo. En el frame 6476, `op313` seguía presente junto al HUD, las cartas
y los efectos. La ventana del duelo entre flips guest 6500 y 6750 dio
29,68 flips/s antes y 29,63 después con límite de 30 FPS. Esta medida
describe ese tramo y no mide capacidad sin límite.

Evidencia local en `diagnostics/battle-v2-match-125541771/` del checkout de
rendimiento. `frame_100` contiene la referencia. El cuadro corregido y su
manifiesto están en `release-candidate/validation-131521829/frame_00`.
`ab-skip-overlay` solo sirvió para aislar el pase durante el diagnóstico.
El defecto del cursor queda como tarea separada.

## Aceptación pendiente antes de empaquetar

Regresiones enfocadas y revisión pasaron; conservar hashes y pruebas indicados
en HUD_START_BUG. No se completó una partida entera ni una prueba final nativa
720p con el último algoritmo; las pruebas finales fueron a 1080p FSR1 con interno
720p. Completar ese repaso antes del launcher. Validar aviso de savedata/audio,
menús, personajes sosteniendo cartas, personajes lanzándolas, HUD, cartas en
campo y escenario completo por separado. Arenas/personajes son aleatorios.
No declarar todo el juego libre de bugs por una sola ejecución.

Después, para launcher e instalador futuros: probar perfil limpio y existente,
configuración, guardados sin sobrescribir, rutas Unicode, doble clic y cierre
normal. Incluir sólo binarios/dependencias/licencias necesarios; nunca assets
o claves del juego en git. No usar contadores rígidos de WAIT_SIGNAL ni exigir
cero ch-block de SPUs de audio como sustitutos de aceptación visual/funcional.

## Entrega v1.2.0 (11 de noviembre de 2026) — retrato, multijugador y sticks 360°

Notas bilingües y resumen de empaquetado en [RELEASE_1.2.0.md](RELEASE_1.2.0.md):

- **Retrato personalizado en Modo Historia (primera sesión)**: el RT offscreen
  `200x200` (`off=0x079C0000`) solo muestreado se desalojaba al crear RTs de
  escena. Se amplió `MAX_OFF_RTS` (`16 -> 64`, `DRAW_SRV_BASE 80`) y
  `MAX_DEPTH_SURFACES` (`16 -> 32`), se fijaron en el pre-pass de frame los RTs
  objetivo y muestreados con desalojo LRU, `off_rt_alias_find` resuelve EA guest
  en `cellGcmResolveOffset`, `NV309E` llama `ps3_guest_stamp` y `vp_upload_tex_slot`
  captura `upload_gen = g_guest_gen` antes de decodificar. Verificado con
  `test_offrt_pin.py`, captura `RSX_CAP` (`frame_00` con `backbuffer.bmp` y
  `rt_079C0000.bmp`) y confirmación del usuario tras prueba nativa.
- **Multijugador habilitado (hasta 7 puertos)**: `cellPad` amplió
  `PAD_MAX_HOST_PORTS` a `CELL_PAD_MAX_PORT_NUM` (7) respetando el tope `4` de
  XInput (`PAD_XINPUT_MAX_PORTS`) y expuso reportes externos vía
  `pad_external_report`/`pad_port_connected`. `input_bridge` asigna puertos
  independientes por mando conectado más un puerto para teclado/ratón incluso
  al abrir directo `BakuganBrawlers.cmd` o `BakuganRecomp.exe`. Validado en
  hardware real con Nintendo Switch Pro Controller (`P1`), PS4 Controller (`P2`)
  y teclado/ratón (`P3`) más `player_pad_provider_test.cpp`.
- **Giro 360° de sticks en minijuegos**: `player::shape_stick_2d` sustituyó el
  recorte axial independiente por zona muerta radial 2D + proyección
  circular-a-cuadrado que conserva el ángulo, validado con
  `player_input_test.cpp` y prueba del usuario.
