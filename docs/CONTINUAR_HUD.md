# Continuación: HUD y cinemáticas corregidos

Cierre de contexto: 8 de noviembre de 2026. El usuario confirma que HUD y
cinemáticas están corregidos y pide guardar todo en docs. No iniciar más cambios
en este cierre. Leer primero [HUD_START_BUG.md](HUD_START_BUG.md), después
[DEFECTOS_Y_INSTALADOR.md](DEFECTOS_Y_INSTALADOR.md) y la guía general
[PROJECT_GUIDE.md](PROJECT_GUIDE.md).

## Decisión y causa confirmada

La aserción edgezlib_inflate_task.cpp(272), retorno 0x80410911 y stop 0x3FFF,
no era una notificación de fin de job perdida. CreateTaskset no publicaba su
workload en la tabla guest; el consumidor no podía resolver el taskset. También
había defectos en el registro de consumidores ANY2ANY y un despertar perdido
entre pre-check y park. Los tres mecanismos se corrigieron. No reintroducir
la espera artificial del puerto ni una cola diferida de supuestas completions.

## Código y revisión

- Toolkit: libs/spurs/cellSpurs.c (tabla guest, IDs, ciclo de vida y ledger
  individual), runtime/spu/spu_workload.c (latch/CV),
  runtime/syscalls/lv2_register.c (ack correcto del evento).
- Reproducción: patches/0066-spurs-taskset-lfqueue-signals.patch. Está aplicado;
  comprobación inversa pasa. No aplicarlo otra vez.
- Port: main.cpp, player_config.cpp/.h: Graphics desde ini, prioridad del entorno,
  controles Auto personalizados, Unicode y buffer ENV_DUMP.
- Entrada actual: BakuganBrawlers.cmd → tools/play.ps1. PS3_VERBOSE=0 obligatorio
  al registrar logs. El ejecutable corregido lee player.ini directamente.
- Revisión independiente terminada; se corrigieron fugas de IDs, Join repetido,
  Remove activo y protocolo de consumidores múltiples. Pruebas cubren 32 ciclos,
  wrap módulo 30, dos destinos, señales antes/después de park y FIFO real.
- Token ANY2ANY 0xFFFF de notificación PPU no implementado: STAT sin publicación.
  No apareció en el título probado. No prometer soporte general de SPURS.

## Validación preservada

Sesión local: diagnostics/code-repair-20261108-171723/.
Referencia final: clean-175828/ (perfil aislado, sin PS3_* heredadas;
1080p FSR1 e interno 720p). Capturas/logs y límites en HUD_START_BUG.
SHA256 del exe final: 2D3FF4D00C07F29966AC95ABAEE9529BE2190460341F4B52C9F124424E65BE02.
No usar los runs intermedios como binario final; no comparar arenas aleatorias.

Pasaron test_spurs_taskset_workload.py, test_spu_user_event_ack.py,
test_player_boot.py, test_repository_contract.py, PlayerInputTest,
PlayerPadProviderTest y compileall. Build Release con clang-cl. Las dos pruebas
SPU necesitan --compiler si clang-cl no está en PATH; compilador local:
C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe.
Logs red/green y build-final.log en la carpeta de sesión.

Grabación válida: diagnostics/record-cards-20261108-111621/cards.padrec
(420 muestras, ~104.7 s, reloj flips). Copiar baseline/ a un gamedata nuevo
por prueba. No usar uncapped.padrec ni ab.padrec. Partida original sin tocar:
gamedata/dev_hdd0/home/00000001/savedata. Los replays requieren observar
el menú: no asumir que siempre llegan a la misma escena. PAD_FILE permite
correcciones observadas; usar pulsos como '0x4000 2' para Cross y '0x0008 2'
para Start. Sin contador, mantiene 40 polls y puede repetir en menús.

## Siguiente sesión

1. Revisar el diff actual, sin resetear ni restaurar los componentes borrados.
2. Cerrar el repaso de defectos y aceptación del código: si hace falta, revisar
   imágenes finales 14–22 de lanzamiento, repetir prueba final a salida 720p
   y comprobar interacción/partida más larga. No se probó el juego completo.
3. Cuando el código esté aceptado, reconstruir launcher; después instalador.
   El esquema antiguo de instalador es histórico, no una especificación aprobada.
4. 60 FPS para después, en trabajo separado. No tocar arquitectura ahora.

## Salvaguardas y estado de trabajo

Port HEAD 6f8cfa4b66298dce360bf794db7713ebd82291df. Toolkit HEAD
2c0539f3702e4934021ea7a4f9b883ca53876e74, rama test. Ambos tenían cambios
previos: port-before.patch/toolkit-before.patch y exe anterior en la carpeta
de sesión. No reset, rebase, cambio de rama ni aplicación masiva de parches.
No editar recompiled/ ni spu/. Launcher e instalador siguen eliminados.
Sin commits ni push. Cerrar juego normalmente, nunca forzar el proceso.
Mantener render interno 720p y respetar flags prohibidos de AGENTS.md.
