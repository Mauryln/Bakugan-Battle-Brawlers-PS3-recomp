# Estado del port — Bakugan: Battle Brawlers (BLES00758)

Actualizado el 02/11/2026. La guía única para retomar trabajo es
[NEXT_SESSION.md](NEXT_SESSION.md). Build principal Windows x64 / D3D12:
`bakugan-recomp/build/BakuganRecomp.exe`.

## Confirmado

- Arranque, menús, películas mediante Media Foundation, teclado / XInput.
- Battle Arena jugable: turnos del jugador y CPU, campo y cartas corregidos.
- Aquos: cascadas y lluvia, reflejos corregidos con WPOS y sombras oscuras
  mediante mapas D24 nativos y comparación correcta. Confirmado por el usuario.
- Fronteras de cuadros, notificación de flip y capacidad corregidas tras
  capturar un cuadro truncado; no se omiten efectos ni trabajo SPU.
- Stream GPU compacto integrado (0042), con fallback completo.
- Reciclaje FIFO con dueño único (0043): el usuario confirmó menú sin atasco,
  audio de menús sin ruido y ausencia de pantallazos en la prueba de Aquos.
- Música PCM / mezclador SPU y audio de películas disponibles; no se da por
  cerrado el audio ATRAC3 / efectos .PAST.

## Prioridades pendientes

1. **30 FPS sostenidos.** Es lo único que falta para cerrar el proyecto. No es
   un problema difuso: el desglose medido está en la sección siguiente, y el
   plan por fases en [NEXT_SESSION.md](NEXT_SESSION.md). El upload GPU compacto
   quedó en 384 MiB en la corrida medida, frente a 1536 MiB anteriores.
2. **Estabilidad prolongada.** El usuario ya no vio pantallazos tras 0043;
   comprobar que no reaparezcan después de jugar más tiempo.
3. Validar Pyrus, Haos, Subterra, Ventus, tutorial e historia y sesiones largas.
4. Verificar sesiones largas de menús tras corregir el doble reciclaje FIFO (0043).
5. Audio ATRAC3 / efectos sonoros y defectos restantes del selector de modo.

## Dónde se va el frame (medido el 02/11/2026)

Perfil con `PERF=1`, sin capturas. Cifras de los últimos logs de
`diagnostics/performance/` (escena de perfil, no la vista Aquos cargada):
16,6–17,4 FPS, ~60 ms/cuadro. Reparto de 20 cuadros:

| Bloque | ms/cuadro | % | Qué es |
|---|---|---|---|
| `submit` | ~30 | 50 % | Decodificar atributos y escribir el stream de vértices |
| `render_frame` | ~14 | 23 % | Grabar comandos D3D12, incluido el empaquetado (~5 ms) |
| resto | ~16 | 27 % | Present, drain FIFO, walker de métodos, guest |

Dentro de `submit`, `[PERF primitive]` (0048) señala un solo culpable:

```
indexed prim=6 (TRIANGLE_STRIP)  calls=1418/cuadro  ms=23,3/cuadro
indexed prim=5 (TRIANGLES)       calls= 385/cuadro  ms= 6,1/cuadro
arrays  prim=8 (QUADS)           calls=  12/cuadro  ms= 0,1/cuadro
```

El 79 % del coste de `submit` es un bucle: triangle strips indexados. La causa
es tráfico de memoria, no ALU: cada vértice se escribe a 256 B canónicos
(~121 MB/cuadro), `render_frame` los vuelve a leer (~121 MB) para empaquetarlos
a ~35 MB, y `vp_fetch_vertex` hace 512 B de `memcpy` por vértice. ~207 MB por
cuadro de tráfico evitable. Las variantes SIMD/`restrict` ya se probaron y no
ayudaron: el problema son los bytes, no las operaciones.

El tercio "resto" no está medido por separado todavía. `PS3_FTRACE` ya emite
`PS3_FT_DRAIN` en `cellGcmSys.c:1808` y `[PERF submit]` ya separa la geometría
dentro del drain, así que `drain − submit` da el walker sin escribir código.
Es la primera tarea de la sesión siguiente porque decide el techo alcanzable.

## Informes vigentes

- [Rendimiento e integración compacta](PERFORMANCE_2026-11-01.md).
- [Correcciones de reflejos, sombras y cuadros](EFFECTS_FIX_2026-11-01.md).
- [Comparación y herramientas RenderDoc](EFFECTS_COMPARE_2026-11-01.md).
- [Arquitectura](ARCHITECTURE.md), [vídeo](VIDEO.md).

Los informes BATTLE_ARENA_FIXED, EFFECTS_AQUOS y PERFORMANCE_2026-10-31 se
conservan como evidencia histórica, no como lista vigente de pendientes.
La serie de main tiene 47 parches (0001–0048, sin 0023). La validación incluye
Release, las 42 suites de `tools/test_*.py` y aplicación sobre toolkit
limpio; la CI solo ejecuta `compileall` y `tools/test_pipeline_patches.py`, el
resto es local porque necesita el toolkit y el disco. El stream compacto ya
forma parte de la build; conserva RAM canónica pero solo para respaldo y
capturas — preparar esa RAM canónica es justamente el coste pendiente.

El experimento de vertex pulling GPU está guardado en `codex/gpu-vertex-pull`
(0049–0051), fuera de main. Sus pruebas de equivalencia pasan, pero solo cubre
QUADS no indexados: ~32 draws y 1116 vértices por cuadro, mientras el coste
real está en STRIP indexados. No se retoma sin resolver antes ese bucle.
El desglose por primitiva de 0048 ya se hizo y está arriba. Ver NEXT_SESSION.

Los informes con fecha anterior al 02/11/2026 son historia, no estado: sus
listas de "próximo trabajo" incluyen cosas ya hechas (stream compacto 0042,
separación de capacidad CPU/GPU 0046, caché de índices 0047) y cifras de
escenas distintas. `PERFORMANCE_2026-11-01.md` lleva una sección al principio
marcada como vigente; lo que hay debajo es la evidencia de cómo se llegó aquí.

## Construir y ejecutar

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

Usar una copia propia del disco. Assets, binarios del juego, lifts, capturas
y salidas de build no se versionan. Las correcciones del toolkit se conservan
en patches; no editar código generado para solucionar bugs.

## Rendimiento: medidas del 02/11/2026

Reparto completo del frame, referencia del emulador (29,9 FPS en la misma batalla), hipotesis descartadas con dato y los dos intentos que rompieron geometria: ver [PERFORMANCE_2026-11-02.md](PERFORMANCE_2026-11-02.md).

Short version: 67 ms/frame = 26,7 ms triangle strips + 14,5 ms prepass PSO/SRV + 8,5 ms subida write-combined + ~11 ms walker. 1704 draws y 488k vertices por frame, sin duplicacion. El juego esta bloqueado a 30 FPS.
