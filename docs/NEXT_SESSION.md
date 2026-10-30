> **Estado vigente al 30/10/2026:** continuar con [NEXT_SESSION_BATTLE_ARENA.md](NEXT_SESSION_BATTLE_ARENA.md). Gameplay, HUD y Sixaxis corregidos; efectos/ruido/FPS siguen pendientes. Las notas anteriores se conservan debajo.

# Informe de traspaso — 26 de octubre de 2026 (sesión larga)

Documento para retomar el trabajo mañana. Estado real, qué se arregló, qué
falta y cómo seguir.

## Lo que quedó arreglado (commiteado, parches 0009 y 0010)

| Parche | Problema | Arreglo |
|---|---|---|
| `0009-rsx-strip-restart` | Escena 3D deformada (estelas/triángulos gigantes) | Los strips indexados de la arena cortan con el sentinel **`0x7FFF`** (`NV4097_SET_RESTART_INDEX`). `upload_strip_vp_indexed` trataba ese valor como un vértice. Ahora respeta el corte y la continuidad entre paquetes. |
| `0010-rsx-display-rebind-and-shader-cache` | Cinemáticas negras + congelamientos | (1) `RT_REBIND` va **por defecto activo**: reemite `OMSetRenderTargets` antes de cada draw; el binding del backbuffer se perdía en el batch largo offscreen. (2) **Caché de shaders** en `shadercache/`: guarda el bytecode del fragment program compilado, eliminando el `D3DCompile` (~1s) por shader nuevo. |

Build verde, pipeline reconoce los 10 parches (0001–0010). Test `tools\test_rsx_strip_restart.py` pasa.

## Lo que se descartó / revertí

- **`0011` alias de resolves 1:1**: intenté que el alias del RT se actualizara también en resolves 1:1 (no solo escalados 2:1). **Rompía el HUD** (los buffers de staging de UI caen dentro del rango del RT y se aliasaban mal). Revertido y eliminado del historial.

## Diagnósticos que dejé (sin commitear, en `ps3recomp-src`, inofensivos)

- `VTX_COL=<n>` en `read_vp_vertex`: vuelca el color/alpha (atributo 3) de los primeros vértices. Útil para el negro del gameplay.
- Conteo de superficie en `DRAW_INDEX_ARRAY` (además de `DRAW_ARRAYS`): hace que `RTT_STAT` (`surfEAB`/`surfOther`) cuente también los draws indexados de la escena 3D.

Estos NO están en ningún parche; si quieres, se pueden meter en un `0011` de diagnóstico.

## Lo que FALTA (el negro del gameplay — turno del jugador)

Estado actual, confirmado por el usuario:
- **Cinemáticas**: se ven (escena 3D visible, texturas granuladas, caras invisibles).
- **Turno del jugador**: **todo negro, solo se ve el HUD 2D**. El sonido funciona.

### Evidencia recogida

1. La escena 3D **sí** dibuja a `0xEAB0000` durante el gameplay: `surfEAB` crece hasta 721k (con el conteo indexado). El RT `rt_0EAB0000.bmp` capturado tiene **62% de píxeles no-negros**.
2. El quad de composición **sí** se dibuja: `[VTXCOL] a3=(0 0 0 1) fp=0xD8FFF01` (alpha del color de vértice = 1, no 0).
3. El `backbuffer` sale **negro** (0% no-negro).
4. El fallback de proyección **`VPFB` no se dispara** (0 veces).
5. Las constantes del VP del draw 3D son un **cámara + proyección válidas** (p. ej. `c[3]=(-1.5,-4.4,-54.9,1)`, proyección en `c[4..7]`).
6. El resolve NV3089 es **siempre** `0xEAB0000 → 0xDF60000` escalado 2:1 (no cambia en gameplay).
7. En la captura RSX_CAP (`frame 2400`), los draws a display son solo el **pre-pase de profundidad del HUD** (`cmask=0`, `t0=0x0C489D80`) y **NO aparece el quad de composición** (`t0=0xDF60000`). Puede que `frame 2400` fuera un frame de carga/transición, no el gameplay real.

### Hipótesis abiertas (en orden de coste)

1. **El quad de composición no llega a dibujarse en el frame de gameplay** (o se dibuja con otro `tex0`). El punto 7 apunta aquí, pero la captura pudo ser un frame equivocado. **Siguiente paso: capturar con `RSX_CAP` en un frame LATERAL (más tarde) del turno del jugador y ver si el manifest tiene el quad `t0=0xDF60000` y con qué `cmask`/`blend`/`texrt`.** Si falta, es "el juego no emite el composite en ese estado".
2. **El alpha de salida del fragment program del composite es 0** (el RT se limpia con `cc=(0,0,0,0)` y la escena no escribe alpha). Aunque el alpha del color de vértice es 1, el FP puede muestrear el RT (alpha=0) y con blend `SRC_ALPHA` no pinta. La sesión anterior probó forzar alpha=1 vía mapeo SRV y "no tuvo efecto", pero conviene **reprobar forzando alpha=1 en el SRV del composite SOLO en el turno del jugador** (con `RX_DISPDBG` para ver `texrt`).
3. **Proyección del VP del gameplay** distinta a la de la cinemática (cámara de juego) y mal descompilada/posicionada (escena fuera de pantalla). El RT tiene 62% no-negro, así que no parece vacío, pero podría ser la cinemática congelada. **`VP_DUMP=1` + `RSX_DRAW_DUMP`** para comparar el VP de gameplay vs cinemática.

### Prueba concreta para mañana

```powershell
Remove-Item Env:RX_OPAQUE, Env:RT_REBIND, Env:RSX_FP_CTRL_AUTO, Env:GCM_FLIPCOUNT, Env:RX_PASTE, Env:RSX_CAP, Env:RTT_STAT, Env:VTX_COL, Env:VP_DUMP -ErrorAction SilentlyContinue
$env:PS3_VFS_ROOT="$PWD\game"; $env:PS3_TITLE="Bakugan: Battle Brawlers"
$env:PAD_ALWAYS_CHANGE="1"; $env:PS3_NO_SAIL="1"
$env:RSX_CAP="3000:8:60"; $env:RSX_CAP_DIR="$PWD\diagnostics\arena\manual-next"
$env:RX_DISPDBG="32"; $env:VP_DUMP="1"
New-Item -ItemType Directory -Force diagnostics\arena\manual-next | Out-Null
Start-Process "$PWD\bakugan-recomp\build\BakuganRecomp.exe" `
  -ArgumentList "$PWD\game\PS3_GAME\USRDIR\EBOOT.elf" `
  -RedirectStandardOutput "$PWD\diagnostics\arena\manual-next\run.out" `
  -RedirectStandardError  "$PWD\diagnostics\arena\manual-next\run.err"
```

Llegar al turno del jugador, moverse 4-5 s, cerrar. Luego mirar en
`manual-next/frames/*/manifest.txt` si el quad de composición (`t0=0xDF60000`)
está, con qué `cmask`/`blend`, y en `run.err` el `[DISPDRAW]` (`texrt`) y el
`vp_dump.hlsl`.

## Otros pendientes (visuales, después del negro)

- **Texturas granuladas** en los modelos: probablemente mipmaps/sampling (el RT/atlas no genera mips, o el filtro es puntual). Mirar `vp_upload_tex_slot` y el control de mip/filtro.
- **Caras invisibles**: probablemente alpha/transparencia (la textura de cara tiene alpha=0 o el blend la descarta). Relacionado con el alpha del punto 2.

## Cómo se reproduce el build

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py   # aplica parches 0001-0010
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

Navegación a Battle Arena: X (aviso guardado) → X (título) → New Game X →
flecha abajo (Battle Arena) → X (confirmar PJ) → X (confirmar escenario).

## Estado del árbol

- `patches/0009`, `0010` commiteados (los arreglos). `0001`–`0008` ya existían.
- `shadercache/` gitignoreado.
- `diagnostics/arena/manual-*` contienen la evidencia de esta sesión (ignorados por Git).
- En `ps3recomp-src` quedan sin commitear: los patches aplicados (normal) + los
  diagnósticos `VTX_COL` y el conteo de superficie indexada.
