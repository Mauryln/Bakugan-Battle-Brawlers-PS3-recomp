# Grabar y reproducir inputs (llegar a la arena sin manos)

> Estado: verificado el 2026-11-06 con `record`/`replay` sobre el build Release
> actual. Complementa `PROJECT_GUIDE.md`; no sustituye a `AGENTS.md`.

## 1. Para qué sirve

El agente necesita depurar la batalla, pero entre el arranque y la arena hay una
secuencia de menús que nadie puede adivinar: el número de pantallas no está
documentado y cualquier guion con tiempos fijos se descoloca en cuanto una carga
tarda distinto de la vez anterior.

La solución es grabar **una** partida manual y reproducirla después:

1. Tú juegas hasta la arena con `record`.
2. Cada ejecución posterior con `replay` conduce el juego sola hasta el mismo
   punto y deja la ventana abierta para que el agente mida, capture o depure.

## 2. Cómo funciona

Dos variables de entorno del HLE de `cellPad`
(`ps3recomp-src/libs/input/cellPad.c`):

| Variable | Efecto |
| --- | --- |
| `PAD_RECORD=<archivo>` | Guarda el paquete que el guest **recibe** (después de teclado, XInput y `PAD_FILE`) cada vez que cambia, más un latido cada 250 ms. |
| `PAD_REPLAY=<archivo>` | Reproduce esa línea de tiempo y anula la entrada del host mientras queden muestras. Al terminar devuelve el control al mando real. |

Formato del archivo (texto, `#` es comentario):

```text
# ps3recomp pad recording v1
# t_ms flips mask lx ly rx ry
0 1 0 128 128 128 128
7203 193 16384 128 128 128 128
```

- `t_ms`: milisegundos desde la primera muestra.
- `flips`: contador de flips del guest (`cellGcm_flip_request_count`) desde la
  primera muestra.
- `mask`: `(DIGITAL2 << 8) | DIGITAL1`, la misma codificación que `PAD_SCRIPT`
  (`START 0x0008`, `UP 0x0010`, `DOWN 0x0040`, `LEFT 0x0080`, `RIGHT 0x0020`,
  `CROSS 0x4000`, `CIRCLE 0x2000`, `TRIANGLE 0x1000`, `SQUARE 0x8000`).
- `lx ly rx ry`: sticks analógicos, 0-255, 128 = centrado.

El replay usa **flips por defecto** (no segundos): un arranque más lento hace
menos flips, así que las pulsaciones caen en las mismas *pantallas* aunque el
equipo vaya cargado. `PAD_CLOCK=wall` fuerza el reloj de pared y `PAD_CLOCK=flips`
lo fuerza en `PAD_SCRIPT`/`PAD_STICK`.

La reproducción avanza como máximo una muestra por poll del guest, de modo que
un tirón de frames no se salta una pulsación corta. Cuando se agotan las
muestras escribe una línea y deja de intervenir:

```text
[cellPad] PAD_REPLAY done: 78 sample(s) played, host pad takes over
```

Ese es el marcador que espera el script.

## 3. Uso

```powershell
# 1) Grabar: juega tú hasta la arena y cierra con Alt+F4
.\tools\pad_autopilot.ps1 record

# 2) Reproducir: el juego llega solo y se queda abierto
.\tools\pad_autopilot.ps1 replay -TimeoutSec 240

# Variantes
.\tools\pad_autopilot.ps1 record -Name torneo
.\tools\pad_autopilot.ps1 replay -Name torneo -CloseWhenDone
```

Las grabaciones y los logs van a `diagnostics/input/` (ignorado por git):

```text
diagnostics/input/arena.padrec       grabación
diagnostics/input/arena.record.log   stdout de la grabación
diagnostics/input/arena.replay.log   stdout de la reproducción
```

Teclado del port (fallback cuando no hay mando XInput): flechas = d-pad,
`X` = cruz, `C` = círculo, `Z` = cuadrado, `S` = triángulo, `Q`/`E` = L1/R1,
`1`/`2` = L2/R2, `Enter` = START, `Tab` = SELECT.

## 4. Reglas y límites

- **`PS3_VERBOSE=0` siempre** al redirigir: sin él, el log verbose deja sin CPU a
  los hilos del guest y el título pierde HUD y cinemáticas. El script ya lo fija.
- Cerrar con Alt+F4 o `CloseMainWindow()`; nunca `Stop-Process -Force`. La
  grabación se vuelca línea a línea, así que un cierre forzado no la corrompe,
  pero el proceso puede quedarse colgado y bloquear el siguiente enlace.
- Solo se graba el **puerto 0**. Es el que usa el juego.
- Una grabación es válida para el mismo lift y el mismo flujo de menús. Si
  cambias el arranque (por ejemplo, aparece un diálogo nuevo), hay que
  regrabar: el replay reproducirá pulsaciones sobre pantallas distintas.
- Si `PAD_RECORD` y `PAD_REPLAY` están definidos a la vez, gana el replay y no se
  graba nada, para no sobrescribir el archivo que se está reproduciendo.
- Para control cerrado de verdad (el agente decide la siguiente pulsación tras
  mirar el log o un frame) sigue estando `PAD_FILE`; ver los comentarios de
  `cellPadGetData`. `PAD_REPLAY` es el caso "ya sé la secuencia".

## 5. Verificación rápida

```powershell
# ¿cuántas muestras y cuánto dura la grabación?
$l = Get-Content diagnostics\input\arena.padrec | Where-Object { $_ -notmatch '^#' }
"muestras: $($l.Count)"; $l[-1]

# ¿llegó el replay al final?
Select-String diagnostics\input\arena.replay.log -Pattern 'PAD_REPLAY'
```


## 6. Perfil de historia/tutorial (2026-11-07)

La ruta limpia del tutorial está guardada localmente como
`diagnostics/input/historia.padrec`, con `historia.profile.json`. Se ejecuta con:

```powershell
.\tools\pad_autopilot.ps1 replay -Name historia -TimeoutSec 600
```

El perfil conserva el slot `BLES00758-PLAYER-DATA-00` y la ubicación de una copia
de las partidas anterior a la grabación. Cada replay crea otra copia de ese
estado inicial en diagnostics/input/historia.replay-*/gamedata y la usa mediante
PS3_GAMEDATA_ROOT. Los cambios del tutorial no se escriben en las partidas reales.
La elección de slot usa PS3_SAVEDATA_CHOICE: el selector host nuevo lee entradas
fuera de cellPad y sus pulsaciones no forman parte de padrec.

El helper comprueba el SHA256 de la grabación frente al perfil. Si cambia la
build, avisa para revisar compatibilidad sin impedir pruebas de una build nueva.
Si falta la copia inicial, se detiene: no sustituye silenciosamente las partidas.
Una regrabación necesita renovar tanto el perfil como el snapshot inicial.
Los archivos de grabación y las partidas son locales e ignorados por git.

### Evitar entradas automáticas mezcladas

PAD_AUTOPRESS alterna X y Start durante 12 de cada 240 lecturas si la variable existe,
aunque su valor esté vacío o sea 0. El helper elimina PAD_RECORD, PAD_REPLAY,
PAD_FILE, PAD_AUTOPRESS, PAD_SCRIPT, PAD_STICK, PAD_SWEEP y los overrides de YDKJ
antes de activar únicamente el modo solicitado; el perfil añade su elección de slot.

Usar `Remove-Item Env:PAD_AUTOPRESS -ErrorAction SilentlyContinue` para eliminarla.
En la invocación comprobada de PowerShell 7.6.9/.NET 10, SetEnvironmentVariable con
$null dejaba una variable presente vacía en el hijo. No usar esa llamada como
prueba de que un flag se desactivó. Se verificó la limpieza ejecutando el bloque
real del helper y consultando el entorno desde un proceso hijo.

La primera toma de esta sesión estaba contaminada por ese flag y no se usa para
el perfil historia. La segunda toma contiene 1456 muestras, 383484 ms y 10429 flips;
termina sin botones ni movimiento de sticks. El usuario confirmó gráficos,
HUD 2D y cinemáticas correctos y pidió conservarlos. No se modificó el renderer.
El formato v1 sólo graba botones y cuatro ejes: no captura directamente los
sensores Sixaxis generados con F/I/J/K/L; no acredita un lanzamiento completo.

Reproducción de historia verificada: las 1456 muestras terminaron y la captura
mostró el tutorial en preparación de lanzamiento, con cartas y HUD. El juego
permanece abierto al finalizar. No se modificaron gráficos para esta comprobación.
