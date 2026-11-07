# Walker y subida paralela de vértices — 7 de noviembre de 2026

Estado final: pack2 + caché de pick + copia diferida de last activos. La tabla
A/B siguiente mide sólo pack2, anterior a las dos adiciones. Su incremento
combinado no está medido; las adiciones se conservan por petición explícita del
usuario de acumular también reducciones pequeñas de trabajo.

## Cambio aplicado

El runner activa dos trabajadores para copiar el staging de vértices ya cerrado
al upload D3D12 mientras el dueño prepara SRV/PSO. FIFO, índices, decodificación,
texturas, HLE y llamadas D3D12 conservan su dueño. No se leen datos guest desde
los trabajadores ni se hacen snapshots adicionales.

Se conservan máscara, stride, offsets, orden y cantidad de draws. Los trabajos
usan bloques grandes con fronteras de 64bytes, no un job por draw. Se espera el
fence GPU antes de reservar/escribir y se unen los trabajadores antes de usar el
buffer, cambiar paridad o liberar memoria. Cada escritor ejecuta sfence antes
de publicar completion. `RSX_PACK_WORKERS=0` recupera la ruta síncrona; valores
1..4 seleccionan trabajadores. El toolkit conserva default 0; el port activa 2.

Parche reproducible: `patches/0054-d3d12-parallel-frame-upload.patch`.
No reaplicar la serie al toolkit local ya modificado. No aplicar patch0049.

## Resultado controlado

Release /O2, FSR1 720p->1080p, cámara fija, mismo proceso y personaje. Orden
A1/B1/B2/A2/B3/A3/A4/B4: 30 segundos por ventana, 5 de estabilización, capturas fuera
de los intervalos. PERF=0, VPCHK ausente, FRAME_METRICS=0, PS3_VERBOSE=0; no se
compiló ni ejecutó el sampler durante estas ventanas.

| Métrica | A: síncrono | B: paralelo 2 |
| --- | ---: | ---: |
| FPS medios | 28.714 | 29.297 |
| Cuadro medio | 34.826ms | 34.133ms |
| p95 | 41.513ms | 40.405ms |
| p99 | 46.232ms | 44.971ms |
| CPU frame_clock, un núcleo | 80.07% | 68.98% |
| CPU proceso, núcleos equivalentes |3.005|3.047|
| Cuadros medidos |3443|3515|

El walker baja 11.09 puntos porcentuales, un 13.85% relativo; el proceso mueve el
trabajo a otros hilos, no ahorra ese porcentaje de CPU total. FPS+2.03%, p95 y
p99 mejoran aproximadamente 2.7%. La mejora de FPS es modesta: **30 FPS estables
siguen pendientes**, con variación entre ventanas. No extrapolar a otros equipos,
arenas, personajes o un duelo completo.

| Ventana | FPS | CPU walker/núcleo | p95ms | p99ms |
| --- | ---: | ---: | ---: | ---: |
| A1 | 28.946 | 79.98% | 40.799 | 45.105 |
| B1 | 29.180 | 70.26% | 40.111 | 43.977 |
| B2 | 29.419 | 67.95% | 40.240 | 45.995 |
| A2 | 28.777 | 80.23% | 41.301 | 44.962 |
| B3 | 29.344 | 67.88% | 41.047 | 45.030 |
| A3 | 28.006 | 80.93% | 42.572 | 46.919 |
| A4 | 29.128 | 79.15% | 41.040 | 48.535 |
| B4 | 29.246 | 69.83% | 40.416 | 49.725 |

El muestreo posterior a la medición confirma el desplazamiento: CRT llamado desde
render_frame cae a 0.89% de pared; antes el pack final rondaba 13.35%. El nuevo
perfil vuelve a señalar copias del ensamblado nativo, fetch y esperas del runner.
Esto identifica trabajo pendiente; no justifica repetir direct-strip, staging
compacto, prefetch ni memcpy literal 64/80 ya rechazados.

## Verificación

- Build experimental y build final Release compiladas con clang-cl.
- `tools/tests/rsx_pack_upload_test.cpp`: upload D3D12 real, copia GPU a READBACK
  y referencia independiente;65536 máscaras, counts 1..17, guardas,8 reutilizaciones
  y cierre, con 1/2/3/4trabajadores. CPU nunca lee el upload WC para comprobarlo.
- Replay completo con VPCHK=1 y captura de arena 1763 draws. Sin mensajes de mismatch
  ni GPUV. VPCHK no imprimió conteo de muestras: no afirmar verificación exhaustiva
  por ese log; la comparación exhaustiva corresponde al test GPU anterior.
- Capturas A/B: 1736..1751 draws, HUD, retícula, personaje, agua, estructuras y efectos
  presentes. Las animaciones hacen que no sean imágenes idénticas píxel a píxel.
- Build final: arranque completo, selección de Bakugan/carta y preparación del lanzamiento
  verificados mediante PAD_FILE, capturas accepted-capture/frame_100..105 y sin GPUV.
  El usuario indicó que no hacen falta más pruebas de gameplay; no se completó un duelo.

Compilar el fixture desde un Developer PowerShell con clang-cl, ejecutando desde
la raíz (salidas sólo en diagnostics):

```powershell
clang-cl /nologo /O2 /EHsc /std:c++20 /D_CRT_SECURE_NO_WARNINGS /Ips3recomp-src/libs/video tools/tests/rsx_pack_upload_test.cpp /Fodiagnostics/pack-test.obj /Fediagnostics/pack-test.exe /link d3d12.lib dxgi.lib
.\diagnostics\pack-test.exe 2
```

## Vías descartadas en esta sesión

1. Decode por draw con gather de bytes propios: 28.255 -> 27.293 FPS y más CPU.
2. Decode con snapshots contiguos: A 28.253, B observado 27.163 FPS; B1 también
   empeora sin sampler. B2 fue perfilado y no debe usarse como comparación limpia.

Ambas preservaron bytes en sus pruebas pero añadían trabajo de captura al walker.
Fueron retiradas; no están en el parche ni activadas. El segundo prototipo validó
43845 jobs sin mismatch en 180 cuadros. El primero 2491392 jobs sin mismatch.

## Evidencia local y continuidad

`diagnostics/walker-20261107/`: pack-ab-windows.json, pack-final.json,
pack-ab-frames.bin, pack-ab-capture/frame_100..105 con imágenes y manifest.txt,
pack-clock-B.*, pruebas GPU y logs de compilación. El replay original puede
seleccionar personajes distintos entre procesos: comparar A/B dentro del mismo
proceso. No atribuir a este cambio los 28.02 FPS de FSR medidos en otra sesión.

El usuario confirmó imagen/audio normales en la validación previa y autorizó
continuar las pruebas y aplicar mejoras mientras estaba fuera. La confirmación
no sustituye validación visual del cambio de pack: se revisaron capturas nuevas.

## Contratos D3D12 consultados

[Map y memoria WC](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map),
[sincronización Win32](https://learn.microsoft.com/en-us/windows/win32/sync/synchronization-and-multiprocessor-issues),
[reciclaje por fences](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management).
SFENCE en el escritor es una precaución conservadora frente al orden WC; las docs
no lo nombran como requisito D3D12 explícito.

## Estado de entrega y siguiente sesión

Se conservaron la subida paralela y las mejoras funcionales previas de FSR/NV3089.
Las variantes que regresaban FPS permanecen sólo como evidencia local, desactivadas
y retiradas del toolkit operativo. La rama del port y toolkit continúa siendo test.
La build principal terminó de compilar y el fixture definitivo de tools/tests pasó.

El perfil posterior señala copias del ensamblado nativo (11.17% de pared en la
copia de last), fetch y esperas. Es evidencia para la siguiente investigación,
no autorización para repetir skip-dead-last/direct-strip, staging compacto,
prefetch, memcpy literal o I/O cache ya descartados. Mantener FSR1 720->1080 y
verificar cualquier cambio contra esta referencia. El objetivo de 30 FPS estables
sigue abierto; esta entrega reduce la carga del walker y mejora modestamente FPS.

Backup local del ejecutable y fuentes: `backups/walker-pack-20261107T114209Z`. Incluye manifest SHA256,
parches reproducibles, documentación y fuentes del toolkit; no incluye game data.

## Adición solicitada: acumular también ganancias pequeñas

El usuario cambió el criterio de aceptación al finalizar: conservar también las
mejoras pequeñas de media, aunque antes se hubieran retirado por no mejorar las
colas consistentemente. Se revisaron los resultados archivados; no existe una
colección de mejoras positivas del 1% que sólo haga falta activar.

Se añadió la única candidata recuperable con señal media positiva: caché de
`RSX_DBG_PICK`. Antes, el valor ausente se convertía en -2 y el test `pick<0`
volvía a consultar getenv en cada draw de triángulos. Ahora la inicialización
se representa separadamente del valor; no cambia el filtro, datos ni draws.
Está activa por defecto; `RSX_PICK_CACHE=0` conserva el comportamiento anterior.
Parche 0055, compatible con 0054. Sin evento de A/B ni bloqueo nuevo por draw.

Antecedente medido: último par A2/B3, 27.964 -> 28.090 FPS (+0.45% de media), pero
p99 44.362 -> 48.589 ms. Hubo otras ventanas positivas y variación; no está demostrado
que ese p99 sea causado por la caché ni que la mejora se acumule linealmente con
pack2. Se conserva por la nueva preferencia explícita del usuario y por eliminar
trabajo redundante sin cambiar datos. **El incremento combinado de FPS todavía
no tiene una comparación aislada nueva**; no sumar 0.45% y 2.03% como una medida.

`tools/test_rsx_pick_cache.ps1`: 32 casos de opciones/modos, 20000 llamadas cada uno,
valores y selección idénticos, una consulta en caché, default activo, rollback 0 y
transiciones del helper. Build Release compilada. Se revisa la escena nuevamente;
no se realizan pruebas de gameplay por indicación del usuario.

Las demás propuestas no son ganancias pequeñas acumulables: prefetch, memcpy
literal 64/80, reuse upload y decode workers dieron medias negativas; staging,
SIMD y otras cachés no demostraron más FPS. La variante NV3089 con lock siempre
fue sustituida por el camino condicional actual del mismo trabajo, y no se puede
sumar encima. La copia diferida last se recupera bajo la nueva preferencia del usuario, como se
detalla abajo; su aumento de FPS permanece sin cuantificar. Los ensayos con
regresión medida permanecen desactivados.

### Copia de last recuperada

Patch 0056 conserva el bloque de 256 bytes ya emitido y sólo lo copia a last cuando
aparece un corte que lo consume. Mantiene el ensamblado original, máscaras,
strides, reinicios y continuidad. El canary de copia parcial conserva su camino
anterior. Default activo; `VP_STRIP_DEFER_LAST=0` restaura la copia por vértice.
No modifica ni habilita VP_STRIP_DIRECT.

`tools/test_rsx_strip_defer.ps1` extrae la función actual y la compara con un
snapshot del ensamblador anterior: 86016 paquetes por modo, 172032 en total, bytes
completos, conteos, límites, fetches y rangos de índices idénticos; u16/u32,
reinicios, particiones/continuidad, capacidades y copias 64/256. El fixture y
mocks están versionados en tools/tests/fixtures, sin depender de diagnostics.

La escena se revisó con pack+pick antes de esta última elisión y registró miles
de hitcheck con bad=0; no hubo GPUV. La elisión posterior pasó el fixture exacto
y la compilación final. No se declara una nueva medición de FPS del conjunto
pack+pick+last, ni se extrapolan porcentajes independientes. La siguiente sesión
puede medir esa combinación sobre la referencia d5ab926, sin repetir gameplay.

Backup final del conjunto: `backups/walker-pack-pick-strip-20261107T115149Z`. El backup anterior conserva
la referencia con sólo pack2. Juego cerrado normalmente al terminar la escena.
