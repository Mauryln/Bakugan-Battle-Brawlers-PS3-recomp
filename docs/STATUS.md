# Estado del port — 03/11/2026

**Build de referencia:** `bakugan-recomp/build/BakuganRecomp.exe`, rama `test`
del toolkit. Release, D3D12, BLES00758. Sin flags de diagnóstico.

## Qué funciona

- Arranque completo, aviso de guardado audible, menú navegable.
- Partida guardada: carga y continúa.
- Historia y batallas jugables de principio a fin, con HUD 2D, cartas 3D,
  cinemáticas de presentación de personajes, sombras, reflejos y efectos.
- Audio y vídeo, con el ruido descrito abajo bajo carga.
- Teclado y XInput.

**16-17 FPS en batalla**, 15,6-16,0 medidos con `PERF=1` (1 754 draws, 399k
vértices). La GPU está al 7,5 %: el límite es el hilo de render.

## Fallos conocidos

| Fallo | Impacto |
|---|---|
| Ruido de audio ("brrr") en batalla | Coincide con las caídas de FPS: el juego no entrega bloques PCM a tiempo. No es el reloj del mixer. |
| 16-17 FPS en batalla, con caídas puntuales | Ver [PERFORMANCE.md](PERFORMANCE.md). No es jugable a 30 FPS todavía. |
| Carga lenta (Battle Arena) | Un `spu_exec` al 100 % durante la carga. |
| Triángulo suelto un cuadro en transiciones de escena | Cosmético, poco frecuente. |
| Faltan pruebas completas de Pyrus, Haos, Subterra, Ventus, tutorial e historia | No dar por cerrado lo que no se ha jugado. |

Nada de esto impide jugar la historia ni las batallas.

## Cómo se ejecuta

```powershell
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build   # solo si hay cambios
.\tools\run.ps1
```

Cierra con Alt+F4 o `CloseMainWindow` (nunca `Stop-Process -Force`). Para medir:
`$env:PERF="1"` y **`$env:PS3_VERBOSE="0"`** en la misma sesión.

Caché de shaders: `shadercache/fp_v2.cache` (un fichero). Se puede borrar; la
primera batalla recompilará programas y dará tirones de ~1 s por shader nuevo.

## Documentos

- [ARCHITECTURE.md](ARCHITECTURE.md) — cómo funciona el código hoy.
- [GUARDRAILS.md](GUARDRAILS.md) — qué no tocar y qué provoca errores gráficos.
- [PERFORMANCE.md](PERFORMANCE.md) — medido: lo que no ayuda y lo que podría.
- `legacy/` — documentos anteriores. Se conservan por su evidencia, pero sus
  comandos y conclusiones pueden estar superados por los de arriba.

## Siguiente paso acordado

Comprobar si PS3Recomp **v0.18.0** (380 commits por delante) permite superar el
rendimiento actual sin perder este trabajo. Instrucciones y condiciones en
[legacy/UPSTREAM_V018_EVAL.md](legacy/UPSTREAM_V018_EVAL.md). Esta build queda
como referencia jugable al 80 % y como base para portar.
