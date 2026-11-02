# Artefactos y medidas — 02/11/2026

Referencia rápida de todo lo medido ese día. Todos los números salen de
`[PERF]`, `[PERF detail]`, `[PERF primitive]`, `[FRAME]` y de los contadores
`[DRAIN]`, `[FSSTAT]`, `[SPURUN]`, `[PORTTID]`, `[VPCHK]`.
Escena: batalla, quieto, mirando al rival, arena **Aquos** (la más pesada).

## Progresión del día

| Hito | FPS | Frame |
|---|---|---|
| Inicio de sesión | 14,5 | 67-68,5 ms |
| Tras `vp_get_vs` memoizado | 15,0 | ~66 ms |
| Tras caché de vértices validado | 16,2-16,9 | ~60 ms |
| Tras `PS3_KICKWAIT=0` | 17,4-17,6 | ~57 ms |
| Tras batch de 4 (sólo strips sin restart) | **18,0** | **~55 ms** |

Referencia externa: **RPCS3 29,9 FPS** (el juego está bloqueado a 30) con ~2,5
núcleos, misma batalla.

## Reparto del frame a 18 FPS

```
[FRAME] outer/s=6815 due/s=62 pump/s=6877 drain/s=6877 | drain 1.437s/2.05s | wait 0.005s | pres 0.599s (35)
```

| Fase | % | Nota |
|---|---|---|
| drain (FIFO + submit) | **70 %** | ~38 ms/frame |
| dibujo (`render_frame` dentro de `pres`) | **29 %** | ~16 ms |
| `kick_wait` | **0 %** | era 14 % con timeout 4 ms |

`pres` **es** `render_frame` (`rsx_d3d12_backend_present()` lo llama). No hay
coste oculto: drain + dibujo + espera = 100 %.

## `[PERF detail]` (por intervalo de 20 frames)

Valores al final del día (18 FPS):

```
VS 0.007s | CB 0.018s | fence 0.000-0.003s | vertex flush 0.158-0.166s | vfetch 0.384s | def 0.000s | attr 0.276-0.281s
```

Comparación con el baseline de la mañana (14,8 FPS), mismos contadores:

| Contador | Baseline | Final | Cambio |
|---|---|---|---|
| `VS` (lookup vertex program) | 0,110 s | **0,007 s** | memoizado con `vp_ucode_gen` |
| `CB` | 0,019 s | 0,018 s | — |
| `fence` | 0,000 s | 0,000 s | **nunca esperamos a la GPU** |
| `vertex flush` (subida WC) | 0,181 s | 0,158-0,166 s | caché |
| `vfetch` (fetch del strip) | 0,561-0,587 s | **0,384 s** | −33 % |
| `def` (copia 256 B defaults) | 0,212 s | **0,000 s** | relleno por máscara |
| `attr` (bucle de atributos) | 0,355-0,439 s | **0,276 s** | descriptor hoisteado + SIMD |

`vfetch` = `def` + `attr` + copias. A 79 ns por índice, 2,65 M lecturas de
atributo por frame ≈ 8 ns por atributo.

## Volumen real por frame

| Métrica | Valor |
|---|---|
| draws indexados | 1570-1790 |
| vértices canónicos | 400k-530k |
| staging canónico | 126-135 MB/frame |
| subida a la GPU | 37-40 MB/frame |
| draws duplicados en el frame | **18,5k de 42-47k ≈ 44 %** |
| vértices por draw | ~282 |

## Canarias (valor diagnóstico, imagen rota a propósito)

| Canaria | Medida | Conclusión |
|---|---|---|
| `PS3_CANARY_NOVTX=1` (sin strip) | **26,76 FPS** (37,4 ms) | techo del diseño: 30 FPS exige más que los vértices |
| `PS3_CANARY_NOFETCH=1` (sin lecturas) | 489 → 460 ms (72,1 vs 79,2 ns/índice) | las lecturas son sólo ~9 % |
| `PS3_CANARY_COPY64=1` | 76,8 ns/índice | el ancho de escritura no era el coste |
| `VP_STRIP_DIRECT=1` | 76,3 ns/índice, **rompe el render** | descartado (medido, no supuesto) |

## Histórico de ns por índice (`prim=6`)

79,2 baseline → 76,3 directo → 80,7 inline → 83,4 memo índice → 79,2 muestreado
→ 76-80 con caché y batch. La varianza entre escenas es ±5 %, así que sólo son
comparables corridas en el mismo punto de la batalla.

## Fase de carga (diagnóstico cerrado)

| Comprobación | Resultado |
|---|---|
| `PS3_FSSTAT=1` lecturas de fichero | **< 64 en toda la carga** (con umbral 64) |
| `SPURUN` / `SPUINTERP` | **0** durante toda la carga |
| vértices / draws | 0k / ~32 |
| FPS mínimo | **3,88-5,07** |
| frame en el peor momento | ~250 ms con 32 draws y 0 vértices |
| hilo con más CPU | **`spu_exec` al 99 %** (un solo núcleo) |
| `spurs_pm_worker`, `spurs_kernel`, `audio_mix` | 1-2 % (duermen bien) |
| `spu_lifted_thread_run` | **sin locks**: los SPU corren en paralelo |

Conclusión: la carga es **cómputo SPU en serie**. No es disco, no es render, no
es espera (una espera acotada en `spu_rdch` no cambió nada).

## Sondas y variables de entorno

| Variable | Para qué |
|---|---|
| `PERF=1` | `[PERF]`, `[PERF detail]`, `[PERF primitive]`, `[PERF vertex]` |
| `PS3_KICKWAIT=<ms>` | espera del `frame_clock` antes de drenar (0 = no espera). **Medido: 0 gana ~1,2 FPS** |
| `PS3_NOVSYNC=1` | `Present` sin vsync (no cambió nada: el bloqueo no era eso) |
| `VPCHK=1` | compara cada vértice contra una referencia lenta; **0 MISMATCH obligatorio** |
| `VP_CACHE=0` | desactiva el caché de vértices (por defecto está activo) |
| `PS3_CANARY_NOVTX`, `PS3_CANARY_NOFETCH`, `PS3_CANARY_COPY64` | canarias de diagnóstico |
| `PS3_FSSTAT=1` | tamaño medio de lectura de fichero |
| `PS3_SPUSTAT=1` | ejecuciones SPU liftadas y caídas al intérprete |
| `GCM_RATE=1`, `PS3_POLLTOP=<s>` | drains/s y sitios de sondeo de `usleep` |

Los hilos del port imprimen `[PORTTID] nombre=tid` al arrancar.

## Caminos descartados hoy, con su medida

| Hipótesis | Resultado |
|---|---|
| vsync bloquea | `pres` sin cambios con `PS3_NOVSYNC`: **no era** |
| GPU es el cuello | `fence 0.000 s` y GPU ~9 ms: **no** |
| doble búfer de listas + fences | sin espera que solapar: **no aporta nada** |
| compute shader para decodificar | la GPU no lee memoria del guest y subir el buffer cuesta igual: **no compensa** |
| caché de vértices sin validar | rompe render (pelo, parpadeos); **requiere** validación por páginas |
| `usleep` con `SwitchToThread` | neutro |
| `spu_rdch` con espera acotada | neutro |

## Las tres causas del parpadeo del caché (en orden de aparición)

1. **Máscara del VS ausente en la clave** → la pasada de sombras compartía
   entrada con la principal y empaquetaba con otro stride. *Síntoma: parpadeo
   sólo en zonas de sombra.*
2. **Índices de siembra fuera del rango de páginas** (`strip.previous[]`).
3. **Escrituras del DMA de SPU sin estampar** (`spu_dma.h` no pasa por
   `VM_WRITE_COH`).

## Trampas de instrumentación (costaron tres rondas)

- `perf_now()` llamaba a `QueryPerformanceFrequency` **en cada invocación**.
- Las canarias de tamaño usaron `memcpy` de tamaño **variable** → llamada a
  librería que tapa el ahorro.
- `rsx_fetch_attrib_inl` estaba en el header pero se llamaba a
  `rsx_fetch_attrib_base` (externa): inlineado el cuerpo, no la llamada.
- `[PERF] fps` cuenta llamadas a `render_frame`, no frames del guest.
- Muestrear 1 de N y escalar sirve para el perfil, no para el número absoluto.

## Márgenes que quedan, por tamaño

| Bloque | ms | Margen conocido |
|---|---|---|
| `attr` (decode de vértices) | 14 | batch de 4 ya aplicado; más ancho, poco más |
| `vertex flush` (WC) | 8,3 | copias compactas: −4 ms |
| walker FIFO | ~8 | sin desglosar |
| `srv` + `pso` del prepass | 7 | dedup de descriptores |
| relleno de registros por draw | ~8 | sin desglosar |
