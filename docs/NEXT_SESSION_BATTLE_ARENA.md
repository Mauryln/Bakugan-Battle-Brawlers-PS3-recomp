# Próxima sesión — Battle Arena (30/10/2026)

## Leer esto primero

Rama: `codex/battle-arena-rendering`. Port Windows nativo BLES00758. El usuario pidió cerrar con código e informe porque se quedó sin límite de uso. No interpretar este cierre como renderer terminado.

**Confirmado por el usuario:** el gameplay vuelve a mostrarse en una arena compleja; el HUD 2D regresó y se muestra normalmente; pudo lanzar un Bakugan con Sixaxis emulado. **Pendiente:** ruido en cartas/bordes/zonas de efectos, partículas, desenfoque y rendimiento bajo. La **iluminación/sombras** tiene corrección en código (parche 0023, profundidad/comparación de sombras) pero **falta validarla visualmente** — no se probó en juego.

Ejecutable final compilado: `bakugan-recomp/build/BakuganRecomp.exe`. El proceso de diagnóstico quedó cerrado. La ejecución normal NO activa cartas rosas ni profundidad invertida.

```powershell
.	oolsun.ps1
```

## Controles disponibles

| Acción | Teclado / mando |
|---|---|
| Confirmar | X / Cross |
| Apuntar / navegar | Flechas / stick habitual |
| R2 (preparación/carga) | 2 / gatillo derecho |
| Sacudida hacia adelante | F / clic R3 |
| Sixaxis arriba / abajo | I / K |
| Sixaxis izquierda / derecha | J / L |
| Cuatro direcciones con mando | Mantener L3 y mover stick derecho |

El movimiento publica sensores de 10 bits cuando el juego activa SENSOR_ON. La sacudida tiene impulso y frenado, no repite al mantener pulsado. Teclado limitado al puerto 0 y ventana enfocada; respeta PAD_NO_KEYBOARD. El stick derecho conserva su función habitual además del modificador L3.

## Cambios guardados y por qué

Todo cambio reutilizable del toolkit está en `patches/`, no en los lifts generados.

- **0011**: prioridad de RT/aliases explícitos sobre heurísticas por tamaño; constantes FP dinámicas en b1 y cubemaps nativos; caché DXBC identificada por HLSL final. Quitó el negro de la primera arena.
- **0012**: cadena completa de mipmaps y caras de cubemap; footprints y stride BC2/BC3 correctos.
- **0013**: winding RSX→D3D12 corregido. Quitó caras interiores, cielo/suelo ausentes y gran parte del ruido inicial. CULL_FLIP=1 ahora restaura la convención anterior para diagnóstico.
- **0014**: controles Sixaxis anteriores, con pruebas de fases, direcciones, límites, cancelación de opuestos y estado por puerto.
- **Reloj en main.cpp**: recuperación limitada a cuatro ticks y bombeo de mensajes entre ticks. Antes podía ejecutar 240 drenajes seguidos tras una pausa, sin atender la ventana.
- **0015**: comandos de polygon offset A68/A78/A7C y estado por draw/PSO. El juego usa escala 4 y bias 6000 en sombras. Este soporte NO eliminó el ruido de cartas.
- **0016**: un pase de sombras con escritura de color desactivada ya no cuenta como frame visible completo.
- **0017**: buffer de vértices ampliable, preservando ambas mitades de frame y esperando la GPU antes de liberar el antiguo. Las arenas complejas agotaban exactamente 393.216 vértices y perdían el composite/HUD al final. Captura posterior: 446.340 vértices completos, buffer crecido a 786.432 (384 MB entre ambas mitades). El usuario confirmó gameplay visible.
- **0018**: captura bajo demanda con RSX_CAP_TRIGGER, profundidad/culling/bindings en manifest y entradas exactas de dibujos pequeños del display en display_inputs.bin.
- **0019**: caché de texturas 96→512 recursos creados bajo demanda; preparar solo unidades leídas por el FP; quitar SRVs antiguos heap[1+slot] que invadían las ventanas de descriptores por draw desde slot 31. Se observaron 122–123 texturas enlazadas en un frame. Reaparecieron retratos, iconos, mira y botones. El usuario confirmó HUD normal y persistente.
- **0020**: PERF separa tiempo de callbacks de dibujo del tiempo agrupado como guest.
- **0021**: reparar FP_FORCE para el decompilador actual, con filtro FP_FORCE_FP y override RGB que conserva alpha, discard y otras salidas. Es diagnóstico opt-in, no corrección visual.
- **0022 (último cambio)**: copiar atributos constantes una vez por draw y llamar al conversor canónico solo para arrays activos, manteniendo los 16 slots. Reduce trabajo de CPU sin cambiar conversiones ni constantes inmediatas. Prueba compara cada slot byte a byte con la ruta anterior y verifica refresco entre draws. **Compilado y probado por unidad, todavía sin medición de FPS ni validación visual en juego posterior a este cambio.**
- **0023 (REVERTIDO)**: iluminación/sombras por comparación de profundidad. **Se revirtió** porque el mapa de sombras (`0xF100000`, DEPTH24_D8) **nunca se renderiza**: el backend D3D12 usa un único depth buffer (`D24S8`) para todos los pases, y el pase de sombras (depth-only a la zeta `0xF100000`) escribe a ese buffer, no al mapa en VRAM del invitado. Al muestrear `0xF100000` como textura se leían ceros → sombreado incorrecto. Para iluminación real hay que **renderizar el mapa de sombras a una depth texture offscreen** (render target de profundidad) y luego muestrearla — cambio grande, no hecho.

Commits de la segunda parte: 637c297 (reloj), 04bd85d/d68c9b2 (sensores), b2c2296 (polygon offset), ab67cd1 (frame visible), 6951492 (vértices), e657f3f (capturas), 93b700a (HUD/texturas), 7d0f2e9 (perfil), bb34377 (diagnóstico RGB). Ver git log para el commit final de 0022/documentación.

## Evidencia de ruido: no repetir hipótesis descartadas

1. Las fuentes BC1/BC3 de las cartas y retratos se decodifican limpias. Los assets originales no fueron modificados.
2. Reverso de carta 0x0589A480 y frente 0x0586F900 en una carga. El shader de material es FP 0x0D84BB81. VS8 capturado: 99 vértices de un reverso sobre plano Y≈0.003998, Z proyectada≈0.99947–0.99951. Las direcciones cambian entre cargas.
3. La textura 0x058F7700 es el marco luminoso; no confundirla con la textura de la carta.
4. DEPTH_OFF eliminaba ruido en una prueba anterior, pero rompía oclusión: descartado como solución.
5. SV_Position precise no solucionó el ruido: retirado.
6. D32S8 con profundidad invertida tampoco corrigió el defecto: retirado por completo del código y serie de parches. Experimento en diagnostics/arena/session30-capacity-depth/rejected. No habilitar RSX_REVERSED_DEPTH: ya no es una opción del backend actual.
7. **Prueba más reciente y decisiva:** FP_FORCE=1 con FP_FORCE_FP=D84BB81 puso las cartas rosas, manteniendo el alpha y la profundidad originales. El usuario confirmó que **las cartas rosas conservan el ruido**. El cálculo del RGB de iluminación no explica por sí solo las franjas. Revisar cobertura, profundidad, geometría superpuesta, alpha y semántica compartida de pases; no afirmar que faltan únicamente luces.
8. El usuario comparó la arena con un gameplay de YouTube: usa muchos efectos y partículas. No entregó URL. Tratarlo como orientación, no como referencia visual verificada por nosotros.

El diagnóstico rosa se activó solo en el proceso ya cerrado. Para jugar normalmente no definir FP_FORCE ni FP_FORCE_FP. El shader cache usa HLSL final y separa estas variantes, por lo que no hace falta borrar la caché.

## Rendimiento: dato concreto para continuar

Antes de 0022, en la arena verde compleja con HUD corregido:

- Aproximadamente 9.6–10.5 FPS en apuntado; otras escenas 13–22 FPS.
- Por 20 frames, render_frame≈0.27–0.28 s (14%), prepass≈0.15–0.17 s (8%), shaders con 0 MISS una vez calientes.
- PERF submit≈1.265–1.274 s, **66% del tiempo en callbacks de dibujo**. El anterior guest≈86% incluía esta preparación nativa; no atribuirlo todo al PPU.
- La métrica vtx antigua no cubría todos los uploaders de strips; no concluir que la geometría cuesta 2% porque lo diga ese contador.
- 0022 intenta reducir llamadas para atributos desactivados. Medir con la misma arena/cámara y caché caliente antes de añadir más optimizaciones.
- No medir FPS mientras se hace RSX_CAP: el readback bloquea deliberadamente.

Siguientes candidatos si sigue dominando submit: planes de fetch con punteros resueltos por draw, reutilización de vértices indexados dentro del draw, compacción de atributos leídos por VP. Existen rsx_vertex_compact.{h,c} y rsx_vp_analyze_inputs en el toolkit. Conservar semántica de atributos inmediatos, frecuencia, MAIN/LOCAL y reinicios de strips; no copiar conversiones a otra implementación sin verificar equivalencia.

## Iluminación y sombras — causa raíz y corrección (parche 0023)

**Causa raíz confirmada en código** (por qué la iluminación salía plana/sin sombras):

1. **Los shadow maps nunca se subían.** En `d3d12_bind_texture` (rsx_d3d12_backend.c) la lista blanca que registra texturas para el camino VP solo aceptaba `0x81/0x85/0x8B/0x86-0x88/0x9A`. Los formatos de profundidad `0x90` (DEPTH24_D8) y `0x92` (DEPTH16) quedaban fuera, así que el mapa de sombras se muestreaba como SRV nulo → negro → sombras/luces mal.
2. **No se hacía la comparación de profundidad.** El decompilador de fragment programs usaba `rsx_fp_decompile_buffered_ex` (muestreo de color plano), nunca `rsx_fp_decompile_buffered_shadow_ex`. En RSX, muestrear una textura de profundidad con func de comparación activa (bits 28-31 de `SET_TEXTURE_ADDRESS`) devuelve el resultado de la comparación; el camino legacy devolvía el valor bruto, no la comparación.

**Lo que hace 0023:**

- Añade `0x90`/`0x92` a las dos listas blancas (registro de bind y subida de SRV) y mapea `0x90→R32_FLOAT`, `0x92→R16_UNORM` (igual que `rsx_texture_layout` ya decodifica).
- Captura por unidad `filter`, `address` y `control0` en el draw record (antes no se guardaban).
- `dr_shadow_mask()` rellena los descriptores `rsx_fp_shadow_desc` por draw y elige `rsx_fp_decompile_buffered_shadow_ex` cuando hay unidades de sombra soportadas. Añade `shadow_mask` a la clave de la caché de PSO (los shaders con/sin sombra son pipelines distintos).

**Fallback seguro:** si una textura de profundidad muestreada tiene layout no soportado (float-depth `0x91/0x93`, cubo, mips>1, func 0/7, o un programa que mezcla cubo + sombra), `dr_shadow_mask` devuelve 0 y el draw usa exactamente el camino anterior. No hay regresión posible frente al estado previo.

**Lo que NO está verificado y hay que hacer:**

- **No se ejecutó el juego.** Hay que arrancar en arena, confirmar que las sombras/luces se ven y que nada se pone negro. Comparar contra el gameplay de YouTube que usó el usuario.
- Confirmar con `TEXFMTDBG=1` que la textura de profundidad (`fmt=0xB0`) se registra ahora (antes no aparecía), y que su SRV queda `R32_FLOAT`.
- Si la sombra sigue mal, capturar el frame y comparar el HLSL generado (FP D84BB81) contra el esperado: debe contener `_rsx_shadow_cmp` y muestrear `.r` del mapa.

**Partículas (pendiente, sin causa raíz aún):** las partículas no son puntos sueltos (el backend ya tiene `pipeline_state_points`/POINTLIST); suelen ser quads con blend aditivo y alpha-test, que el backend ya soporta. Falta capturar un frame de efectos y mirar si las partículas son quads de textura (formato fuera de lista blanca), sprites de punto con tamaño ignorado (no se trackea `SET_POINT_SIZE`) o un blend concreto. No se tocó en 0023.

## Siguiente sesión: orden recomendado

1. Arrancar normalmente en la misma arena verde compleja. Confirmar que 0022 conserva geometría, retratos, botones y lanzamiento. Comparar PERF y PERF submit durante apuntado con la medición anterior. Si no mejora, medir antes de añadir complejidad.
2. **Validar 0023 (iluminación)**: con `TEXFMTDBG=1` confirmar que `fmt=0xB0` se registra y sube como `R32_FLOAT`; mirar si las sombras/luces ahora se ven. Si algo se puso negro, es un draw que entró en el camino de sombra y hay que capturarlo.
3. Capturar un frame de la carta con ruido y de los efectos. La captura debe contener composite y HUD; un batch de sombras aislado puede tener backbuffer negro sin ser el frame visible final.
4. Separar ruido de cartas de los efectos de arena. El override RGB conservó el ruido: revisar depth test/write, máscaras/orden de pases, posiciones de caras/bordes y alpha. Polygon offset, precise y reversed-Z ya fueron probados sin resolverlo. El ruido **no es solo iluminación**: la corrección de sombras de 0023 no lo soluciona.
5. Revisar desenfoque: el composite combina escena 1120×584, pases 640×360, RT de 1280×720 y una fuente 1120×584 con remap A993 (0x0E1E0000 en una captura). Verificar producer/alias y canales; no sustituir indiscriminadamente todas las texturas del mismo tamaño por pantalla anterior.
6. Revisar partículas: capturar frame de efectos, identificar si son quads de textura (formato fuera de lista blanca), point sprites (falta `SET_POINT_SIZE`) o blend. Añadir a la lista blanca de formatos o trackear point size según corresponda.
7. Verificar una segunda arena y gameplay real después de cambios. No declarar terminada una partida por llegar a CPU2.

## Cómo capturar y perfilar sin adivinar el número de frame

Iniciar el juego desde PowerShell con variables de diagnóstico. Crear la carpeta primero. Mantener una sola ejecución.

```powershell
$diag = Join-Path $pwd 'diagnostics/arena/next-session'
New-Item -ItemType Directory -Force $diag | Out-Null
$env:PERF = '1'
$env:RSX_CAP_DIR = $diag
$env:RSX_CAP_TRIGGER = Join-Path $diag 'trigger'
$env:RSX_DRAW_DUMP = $diag
.	oolsun.ps1
```

En otra consola, estando ya en la escena problemática:

```powershell
Set-Content diagnostics/arena/next-session/trigger 'capture' -Encoding ascii
```

Cada archivo consumido crea frame_100, frame_101, etc. con BMP de RT/backbuffer, manifest y display_inputs.bin. Capturas son datos locales del disco: no versionarlas.

Formato display_inputs.bin (bloques hasta EOF, little endian): 4 uint32 = índice de draw, número de vértices, tamaño CB, tamaño FP-CB; después n×256 bytes de atributos, CB bytes de constantes VP y FP-CB bytes. Constantes VP: 512 float4 y dos float4 de epílogo viewport; FP-CB: cuatro float4 texscale, un float4 alpha y 64 float4 de constantes inline. Las entradas se copian de la mitad de frame que se acaba de ejecutar.

## Diagnósticos útiles conservados

- session30-baseline/fixed/mips/cull: evolución inicial de renderer.
- session30-directions: lanzamiento confirmado por usuario.
- session30-card-source/textures: originales de cartas/materiales y contact.png.
- session30-new-arena/frame_100: batch al límite exacto de 393.216 vértices; escena en RT y backbuffer negro.
- session30-capacity-depth/frame_101: 446.340 vértices y gameplay/HUD parcial; experimento reversed-Z descartado.
- session30-hud/frame_100: HUD incompleto, display_inputs.bin, fuentes de atlas/retratos limpias.
- session30-hud-fixed: HUD restaurado; confirmación del usuario; captura frame_100 y logs previos a optimización final.
- session30-material-profile: rosa conservando ruido, PERF submit≈66%, logs de build y pruebas finales. Proceso cerrado. Esta carpeta usa una arena/personaje que el usuario eligió manualmente.

Algunas utilidades read-*.py leen memoria del PID de una ejecución concreta. No reutilizar un PID antiguo ni tratar direcciones de assets como constantes universales.

## Validación final realizada

- Build Release correcto con tools/build.py.
- Los **23 parches** se aplican en orden a archivos reconstruidos desde HEAD limpio del toolkit (verificado con `git clone` + `git apply` de los 23).
- Pipeline --only patch reconoce los 23 como aplicados.
- **15 pruebas pasan**: strips/restart, root signature de 16 unidades, constantes FP/diagnóstico RGB, identidad de shader cache, elección de fuente/filtrado de unidades, mipmaps/cubemaps, winding, polygon offset, frame visible, crecimiento de vértices, propiedad de descriptores, atributos constantes, Sixaxis, reloj y pipeline.
- El parche **0023 compila** (objeto `rsx_d3d12_backend.c.obj` reconstruido sin errores) y aplica limpio, pero **no se jugó**: la iluminación/sombras correctas necesitan validación visual en arena.
- Logs finales: diagnostics/arena/session30-material-profile/test_*.log y build-final.log.
- **No se midió ni se jugó la última optimización 0022 después del cierre solicitado.** No prometer que el ruido o las caídas de FPS estén solucionados.

## Cambios previos del usuario

Se preservaron README.es.md y el contenido previo de docs/NEXT_SESSION.md, además de docs/OLD_SESSION.md. NEXT_SESSION.md recibe solo un encabezado que apunta aquí; las notas antiguas continúan debajo. El nested toolkit conserva dos diagnósticos locales previos (VTX_COL y contador de superficies indexadas). No hacer git checkout/reset general dentro de ps3recomp-src.
