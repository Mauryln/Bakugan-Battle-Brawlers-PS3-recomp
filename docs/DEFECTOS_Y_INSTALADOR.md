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
