# Continuar desde aquí — 01/11/2026

## Estado vigente

Build principal: `bakugan-recomp/build/BakuganRecomp.exe`, Release Windows
D3D12, BLES00758. El usuario confirmó cascadas, lluvia, reflejos correctos y
sombras oscuras en Aquos. El objetivo prioritario ahora es rendimiento:
gameplay <=15 FPS, tirones desde loading y audio adelantado a las primeras
presentaciones. NO se dan por alcanzados 30 FPS sostenidos.

0043 evita que la recuperación automática rebobine el anillo que ya tiene
el callback HLE de reciclaje. El usuario confirmó menú destrabado, audio de
menús sin ruido y ausencia de pantallazos en la última prueba de Aquos.
Falta validación prolongada; persisten carga lenta y ruido de audio en batalla.
No considerar cerrados los efectos de todas las arenas: faltan pruebas
completas de Pyrus, Haos, Subterra y Ventus, tutorial e historia.

## Qué cambió

| Parche | Resultado |
|---|---|
| 0036 | Samplers reales por draw: wrap/filtros/LOD/borde; cascadas y lluvia confirmadas |
| 0037 | Vértices en RAM cacheada; mejora parcial de recuperación de carga, mayor RAM |
| 0038 | SET_SHADER_WINDOW / WPOS: origen, centro, altura y 1/W; reflejos confirmados |
| 0039 | D24 DSV/SRV por superficie, clears ordenados, pases sin color y ZFUNC correcto; sombras oscuras confirmadas |
| 0040 | GCM_FULL_FRAMES por defecto en el port, notificación de flip consumida una vez y 8192 draws; corrige mezcla/truncamiento capturados |
| 0041 | PERF separa VS lookup, snapshots de constantes, fence y copia de vértices; diagnóstico desactivado sin PERF=1 |
| 0042 | Stream GPU compacto por inputs del VP, fallback completo y capacidad GPU independiente |
| 0043 | Dueño único del reciclaje FIFO: corrige las esperas de 2000 ms y los pantallazos de la última prueba |

Hay 42 parches en la serie 0001–0043: 0023 sigue retirado. No editar lifts.
Los dos diagnósticos históricos locales VTX_COL y conteo de draws indexados
no están en parches. Evitar resets generales dentro de ps3recomp-src.

## Primera tarea al retomar: rendimiento

Leer [PERFORMANCE_2026-11-01.md](PERFORMANCE_2026-11-01.md): contiene cifras,
limitaciones del perfil y la integración compacta 0042. Gameplay de
Aquos ronda 12 FPS; preparación de draws ~49%, render_frame ~24%, y copiar
vértices consumía ~11 ms/cuadro antes de 0042, ahora ~8–9 ms. Loading a 4–5 FPS tiene casi todo el
tiempo fuera del renderizador; después aparecen crecimientos de VBO hasta
1536 MiB antes de compactar. El upload GPU quedó en 384 MiB; CPU y GPU ya crecen por separado.

1. El stream GPU compacto ya está integrado (0042). Falta optimizar la
   preparación CPU con evidencia; conservar fallback y los atributos canónicos.
2. Medir idéntica cámara antes/después; conservar solo mejoras demostradas.
3. Perfilar loading PPU/SPU y esperas; no asumir que todo es asignación GPU.
4. Validar estabilidad prolongada tras 0043, sin recurrencias de pantallazos.
5. Probar efectos de las otras arenas y sesiones largas de menús tras 0043.
6. Diagnosticar ruido de audio en batalla y tirones desde loading.

## Reproducir / medir

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py --only patch
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
# Para medir, antes de lanzar:
$env:PERF = "1"
```

PERF no debe combinarse con capturas para comparar FPS. Los scripts de
RenderDoc y análisis están documentados en
[EFFECTS_COMPARE_2026-11-01.md](EFFECTS_COMPARE_2026-11-01.md). Las capturas
de RPCS3 fueron de momentos distintos y una del recomp quedó incompleta;
la correlación de bandas no prueba por sí sola que falte el espejo.

## Evidencia local útil

- `diagnostics/arena/aquos-window-20261101`: primera corrección de reflejos.
- `aquos-shadow-compare-20261101`: sombras oscuras; readback nativo correcto.
- `aquos-depth-isolated-20261101/frame_130`: negro, 2048 ops y pase de reflejo
  duplicado (702 frente a 351 draws en vecinos), cola truncada.
- `aquos-full-frames-20261101/frame_118` a `frame_141`: 24 capturas sin ese
  patrón de duplicación / cuadro vacío; no prueba estabilidad prolongada.
- `aquos-perf-20261101`: perfil sin capturas, CPU y fases de carga/gameplay.
- `compact-prototype-20261101`: prototipo histórico; la versión integrada es 0042.
- `shadow-experiment-withdrawn-20261101`: primera prueba fallida, solo histórica.
- `battle-arena-picture/bugs`: capturas aportadas por el usuario.

Todas esas rutas son relativas a `diagnostics/arena/` salvo la última. Los
dumps, símbolos, capturas y assets son locales e ignorados. No subirlos a Git.

## Invariantes

Conservar límite original 30 FPS, cuadros completos, strips nativos, estado
por draw, todas las texturas/partículas, transparencia, profundidad y audio.
No activar NODEPTH/DEPTH_OFF, no simular sombras, no saltar trabajo SPU, no
alterar el reloj para inflar contadores. Conservar backpressure del FIFO y
comparación de alfa UNORM. Mantener parches reproducibles y pruebas GPU.

Los antiguos handoffs de 26–30/10 se retiraron por estar sustituidos. Sus
versiones siguen en Git; una copia local anterior a la limpieza queda en
`diagnostics/arena/docs-before-close-20261101/`. Los informes de hitos
con evidencia útil se conservan, marcados como históricos.
