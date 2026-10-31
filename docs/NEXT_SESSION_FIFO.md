> **ACTUALIZACIÓN 31/10/2026:** gameplay Battle Arena, campo y cartas corregidos.
> Cierre y validación: [BATTLE_ARENA_FIXED.md](BATTLE_ARENA_FIXED.md).
> Continuar con [NEXT_SESSION_EFFECTS_SHADERS.md](NEXT_SESSION_EFFECTS_SHADERS.md).
> El contenido siguiente se conserva como informe histórico.

# Próxima sesión — FIFO desync y bugs visuales (30/10/2026, cierre)

Rama: `codex/battle-arena-rendering` (pusheado a origin). Port Windows nativo BLES00758.

**Estado:** el juego **se cuelga al entrar al turno del jugador (y del CPU)** por un desync del
FIFO. La partida es jugable hasta ese punto. Hay además corrupción visual en cartas y terreno.

---

## 1. Estado git y parches

Commits de esta sesión (de abajo hacia arriba):

```
370c332 FIFO: resync on unknown packet type instead of skipping data words
b25dd21 Document shadow-map never-rendered root cause
9e45d93 Revert shadow comparison: shadow map is never rendered by D3D12 backend
85f3c26 Throttle on-demand captures to avoid FIFO desync; add debug launcher
e7b2c47 Add depth-compare shadow support for arena lighting   (REVERTIDO en 9e45d93)
71799e6 battle arena gameplay fixed
```

Parches en `patches/` (24, `0023` fue eliminado al revertirlo):

- `0001`–`0022`: estado previo (gameplay/HUD/Sixaxis/3D/rendimiento).
- `0024-d3d12-capture-throttle.patch`: **captura on-demand limitada a 1/500ms** (evita el
  desync por readbacks en ráfaga). **Conservar.**
- `0025-fifo-desync-recovery.patch`: **resync en "unknown packet type"** en vez de `+4`, más
  el dump del trace en el caso `unmapped-CALL`. **Conservar, pero NO resuelve el cuelgue** (ver §2).

`tools/debug_run.ps1` es el lanzador de diagnóstico (log a `diagnostics/arena/<tag>/run.err`,
`PS3_VERBOSE=0`, TEXFMTDBG/PSOMISSDBG/CUBEKEYDBG/PERF/BLENDDBG/TEX_BINDDBG/FP_LIST/FP_DUMP/
RSX_DRAW_DUMP, y `-Capture` para RSX_CAP_TRIGGER).

---

## 2. EL BLOQUEO (PRIORIDAD 1) — desync del FIFO

### Síntoma
Al entrar al turno del jugador (y del CPU), el walker del push buffer se desalinea y lee
**datos de vértices/constantes (floats) como si fueran comandos**. El juego queda esperando
un fence que nunca llega → cuelgue.

### Evidencia (log `diagnostics/arena/fifotrace/run.err`)
El dump de los últimos 64 comandos (`[FIFOSTEP]`) que añadí muestra que el walker **avanza
correctamente** por todos los métodos de setup y luego cae en datos crudos:

```
... blend(0x0314/0x0320/0x0308) → depth(0x0A6C) → stencil(0x0330) → ...
io=000785A4 w=00041D94  CLEAR_SURFACE count=1   (avanza +8 → 0x785AC, correcto)
io=000785AC w=00000000  NOP                      (avanza +4 → 0x785B0)
io=000785B0 w=BFE38E39  float -1.777 → type=5 (desconocido)
io=000785C4 w=3F884211  float 1.066  → type=1 (¡JUMP!) → target 0x1F884210 no mapeado → resync
```

La corrida `fifofix` (con el parche 0025) reprodujo lo mismo con otra palabra:

```
FIFO JUMP to unmapped IO 0x1F000000 (word 0x3F000000)   ← float 0.5 decodificado como JUMP
FIFO resync (unmapped-JUMP) 0x00076DAC -> put 0x00000000
```

### Por qué el parche 0025 NO arregla el cuelgue
El fix 0025 solo resincroniza en "unknown packet type" (bits 29-31 = 3..7). Pero **cualquier
float >= 0.5 tiene el bit 29 = 1** (IEEE-754), o sea decodifica como **JUMP (type 1)**, no como
"unknown". Ejemplos que caen en JUMP: `0x3F000000`=0.5, `0x3F800000`=1.0, `0x3F884211`=1.066.
Por eso el walker sigue llegando al `unmapped-JUMP`, resincroniza a `put=0`, y el juego
**aun así se queda colgado**: está esperando un fence (NV406E_SET_REFERENCE) que el resync
descartó. El resync rescata algunos fences (`gcm_fifo_resync_why`), pero no todos.

### Causa raíz pendiente de confirmar
El walker se desalinea **justo después de la secuencia de clear**, en el arranque del frame.
Dos hipótesis (no excluyentes):

1. **Reciclado del anillo (más probable).** El juego recicla su propio push buffer por callback
   (`gcmReserve`). Si recicla **antes de que el walker consuma** la cabecera del método que
   precede a los datos (el que debería decir "vienen N palabras"), el walker lee la cabecera
   sobrescrita (cero → NOP) y luego los floats viejos. Evidencia: justo antes de los floats hay
   un `NOP` en `0x785AC`, sospechosamente en el sitio exacto de una cabecera de método.
   **Dónde mirar:** `cellGcmSys.c` — `gcm_fifo_recycle` / la heurística de reciclado (~línea
   2500), y `gcmReserve`/`cellGcmAddressToOffset`. El walker publica `get` al final de cada
   pasada (`vm_write32(GCM_CONTROL_GUEST_ADDR+4, ...)`), que es lo que el callback del juego
   usa para saber qué puede reciclar.

2. **Mis-count de un método con payload grande.** Algún método (p.ej. subida de constantes de
   transformación o el inline array) se decodifica con un `count` menor al real, y el walker
   avanza de menos. Los métodos de setup del trace avanzan bien, así que sería uno de los
   métodos **después** de `CLEAR_SURFACE` que el trace ya no alcanza a mostrar.

### Cómo avanzar mañana (en este orden)
1. Relanzar con `GCM_FIFO_SNAP=20` + `GCM_WORD_STATS=60` + `GCM_DRAINDBG=1` (ya existen en el
   código) para ver **cómo deriva `get` respecto a `put`** y qué clase de palabras hay entre la
   última cabecera válida y el primer float.
2. Buscar en `[SNAP]` la deriva `get`/`put` y en `[words]` el conteo nop/hdr/jump/call/ret.
3. Localizar el método que debería consumir los floats (buscar `0x1800`/`0x1808`/`0x1814`/
   constantes de vértice) y ver si su `count` llega mal o si su cabecera fue sobrescrita.
4. Si es el reciclado: hacer que el walker **no publique `get` por delante de lo realmente
   consumido**, o que el reciclado espere a `get == put` (o un margen seguro) antes de sobrescribir.
   **No** añadir saltos artificiales para ignorar comandos inválidos.

Diagnósticos ya disponibles en `cellGcmSys.c`: `GCM_DRAINDBG`, `GCM_FIFO_SNAP`, `GCM_WORD_STATS`,
`GCM_RECDBG`, `GCM_CTXDBG`, `GCM_SCANREF`, `GCM_SUBCH_GATE`, `GCM_VAL_WATCH`. El trace de 64
comandos (`s_tr_off/s_tr_w` + `gcm_fifo_dump_around`) ya se vuelca en JUMP/CALL/unknown.

---

## 3. Corrupción visual (después del FIFO)

### 3.1 Cartas BC3/DXT5 — fragmentación horizontal
- Síntoma: las cartas mantienen borde y perspectiva pero la ilustración interior tiene
  fragmentación horizontal negra/blanca/multicolor.
- Cartas = DXT5 (BC3), fmt `0x88`/`0xA8`, 204×120 y 228×300, mips=1.
- El payload DXT se copia **crudo** (bit-idéntico, `rsx_texture_decode` rama `compressed`), así
  que la decodificación CPU no es el problema: es **offset/stride del dato comprimido**, el
  subresource footprint, o el sampling.
- **Pendiente:** volcar una carta con `BAKUGAN_DUMP_TEX` (dirección de guest de la carta, ver
  `[TEXFMT] fmt=0xA8`) y comparar el dato crudo contra lo que se sube a D3D12. Verificar
  `DXGI_FORMAT_BC3_UNORM` (o sRGB), row pitch, subresource footprints, offsets de mip, SRV y
  sampling. **No reescribir el decodificador sin evidencia.**

### 3.2 Transparencias incorrectas (región derecha del escenario)
- Región parcialmente transparente con bandas verticales oscuras.
- Revisar alpha blending (`0x122118B` estándar / `0x122108B` aditivo, ambos mapeados bien),
  alpha testing, depth write, render targets y orden de composición. (Los blend ya se
  verificaron correctos; falta el orden/RT.)

### 3.3 Sombras / mapa de sombras `0xF100000`
- **No implementado y correctamente revertido (0023).** El backend D3D12 usa **un único depth
  buffer** (`D24S8`), así que el pase de sombras (depth-only a la zeta `0xF100000`) escribe al
  buffer de pantalla y **nunca rellena `0xF100000`**. Muestrearlo da ceros → sombreado falso.
- La implementación correcta (renderizar a una depth texture offscreen y muestrearla) debe
  planificarse por separado. **No** reintroducir la lectura de `0xF100000` sin ese soporte.

### 3.4 FPS
- Sin capturas ~21-22 fps; con capturas ~10.5 (el readback bloquea). El cuello de botella es
  `guest ~84-92%` (PPU recompilado + preparación de draw); `PERF submit` llegó a 64-65% en
  arenas complejas. El parche `0022` ya intentó reducir el empaquetado de vértices; falta medir
  si ayudó.

---

## 4. Dónde está todo

- Logs de esta sesión: `diagnostics/arena/lighting`, `lighting2`, `fifotrace`, `fifofix`.
- Capturas de frame (del run `lighting`, con RSX_CAP): `diagnostics/arena/lighting/frame_100`..
  `frame_109` (manifest.txt, backbuffer.bmp, rt_*.bmp, display_inputs.bin). Útiles para §3.
- `fp_dump.hlsl` en la raíz: HLSL de los FPs (incluye el de carta `0x0D84BB81` con sombra PCF y
  alpha test). `vs_*.hlsl/vertices/constants/state` en `diagnostics/arena/lighting/`.
- `tools/debug_run.ps1`: lanzador de diagnóstico.

## 5. Validación obligatoria (no declarar nada arreglado sin esto)
1. Compilar (`tools/build.py`). 2. `git clone` + `git apply` de los parches (24). 3. Entrar al
turno del jugador **sin cuelgue**. 4. Turno del CPU **sin cuelgue**. 5. Capturar una carta y
comparar. 6. Confirmar que `0024` sigue funcionando. 7. Documentar.

**Resumen de una línea:** el bloqueo es un desync del FIFO que lee floats como comandos; el
fix real está en el reciclado del anillo o en un mis-count de método, no en el resync.
