# Lo que no hay que tocar, y qué provoca errores gráficos

Reglas con evidencia detrás. Cada una costó una sesión; ninguna es teórica.

## No tocar

| Acción u opción | Por qué |
|---|---|
| Editar `bakugan-recomp/recompiled/` o `bakugan-recomp/spu/` | Es código liftado generado. Se regenera, no se edita. |
| Resetear el toolkit, cambiar de rama o reaplicar la serie de `patches/` sin mirar | La rama `test` tiene 18 commits propios encima de la base; reaplicar parches encima pisa cambios. Para compilar basta ninja. |
| `Stop-Process -Force` / `taskkill /F` | El audio no se libera; cierra con `CloseMainWindow` o Alt+F4 y espera. |
| `PS3_NOVSYNC=1` | Arranques mudos y bloqueo en menú; interfiere con el mezclador. No usarlo para medir FPS. |
| `SPU_CH_BLOCK=1` global | Recupera sonido pero deja la carga de Battle Arena esperando en un contexto SPURS sin productor. |
| `SRV_SHADOW=1` | "cinematics and the 2D HUD stopped loading in battle": reproduce exactamente el síntoma de capas ausentes. |
| `DRAW_KEEP_TEX`, `DRAW_LAST_TEX`, `DRAW_KEEP_BLEND`, `TEX_MARKEMPTY`, `EMPTYTEX`, `DEPTH_OFF`, `TEX_RESOLVE_AUTO` | Palancas de diagnóstico que alteran el cuadro o sus fuentes. `DRAW_LAST_TEX` mueve **todos** los draws de la textura, no solo el HUD. |
| Juzgar imagen o FPS con logs redirigidos *sin* `PS3_VERBOSE=0` | Redirigir stderr activa el log verboso: ~6 000 líneas/s por un lock de `FILE`, los hilos del guest se quedan sin turno y el título pierde inicialización y carga de assets. Con `PS3_VERBOSE=0` la misma build se ve perfecta. |

## Qué provoca errores gráficos

### Pantalla negra por draws que no se registran

Un camino nuevo que deja de registrar draws no da error: el juego sigue a 15 FPS
y la pantalla está negra. **Antes de investigar shaders, mira `[PERF] ... draws`:
en batalla son ~1750.** Si el número se hunde, es registro.

- **Partir una cadena `if/else if/…/else`.** Meter código entre el último
  `else if` y el `else` de "primitiva no soportada" hace que ese `else` (`return;`)
  se coma todos los draws que no van por la ruta nueva. Pasó: 1743 → 51 draws.
- **Cualquier `return` nuevo en `upload_*` que no devuelva vértices** tiene el
  mismo efecto.

### Pantalla negra por desalinear el staging de vértices

Cuatro datos describen el mismo run y deben cambiar **juntos**:

| Dato | Dónde | Quién lo lee |
|---|---|---|
| `s_vertex_fetch_plan.mask` / `slot_off[]` | `vp_fetch_vertex` | escritura de cada atributo |
| `dr->vb_stride` | registro del draw | merge de batches |
| `dr->gpu_input_mask` → `dr->gpu_stride` | `vp_upload_compact_frame` | subida y VBV |
| `s_d3d.vp_vb_offset` | cada `upload_*` | siguiente draw |

Si el relleno escribe compacto y la subida lee canónico (o al revés), cada
vértice se lee desplazado: negro total, sin una línea de error. Un interruptor
que cambie solo un lado del layout es exactamente esto.

### Pantalla negra o blanca por el dispositivo D3D12 eliminado

Un fallo de GPU no perdona: si `Present` devuelve `0x887A0005`, todo lo que se
dibuje después es basura aunque el juego corra y suba contadores. El backend
registra el primer fallo como `[GPUV]`. Casos ya vistos:

- **Recurso usado como UAV sin `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`**:
  la vista UAV y la barrera UAV son llamadas ilegales; el runtime elimina el
  dispositivo con `DXGI_ERROR_INVALID_CALL` (que aparece como `0x887A0001`, no
  como `DEVICE_REMOVED`).
- Un `ByteAddressBuffer.Load` exige dirección múltiplo de 4: un `stride` de
  atributo no múltiplo de 4 tumba el dispositivo en vez de fallar suave.

Para diagnosticarlo hace falta la capa de validación, que **no viene con
Windows**: `D3D12SDKLayers.dll` solo existe tras instalar la característica
opcional *Graphics Tools*:

```powershell
Add-WindowsCapability -Online -Name "Tools.Graphics.DirectX~~~~0.0.1.0"
```

Después, `D3D12_DBG=1` y `D3D12_GPU_VALIDATION=1`: la validación GPU nombra el
acceso exacto del shader (la de CPU no ve dentro del compute).

### Texturas, HUD y cinemáticas

- **HUD 2D, cartas y cinemáticas ausentes a la vez**: ver la regla de
  `PS3_VERBOSE` arriba. Fue la causa real el 03/11, no el render.
- **Atributos half/s16 con componentes cruzadas**: `be16lo`/`be16hi` invertidos
  cambian los componentes por pares; se ve en cinemáticas (half) y no en gameplay
  (float). Si una escena usa precisión media y el resto no, mira aquí.
- **Triángulo suelto un cuadro en transiciones de escena**: un draw sobrevive a la
  transición con datos del cuadro anterior. Cosmético, sin resolver.

### El juego se cierra solo

- **`free()` sobre un puntero del caché de shaders**: el bytecode vive dentro del
  contenedor, no es un `malloc`. Liberarlo corrompe el heap y el proceso muere
  ~1 s después del primer draw, sin mensaje.

## Reglas de validación antes de dar algo por bueno

En este orden, y sin saltarse pasos:

1. Aviso de guardado audible al arrancar.
2. Menú completo y navegable.
3. Primera cinemática con imagen **y** audio.
4. HUD, cartas, sombras y efectos visibles en batalla.
5. Solo entonces, FPS con `PERF=1` en una cámara fija.

Una sesión con más FPS pero sin esas capas no es una mejora válida.
