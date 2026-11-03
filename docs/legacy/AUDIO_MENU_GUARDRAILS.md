# Audio, menús y cinemáticas: restricciones de trabajo

Actualizado el 03/11/2026. Leer antes de compilar, lanzar o investigar rendimiento
en `test`. Restricciones expresas del usuario: tienen prioridad sobre comandos
de reproducción de documentos anteriores.

## No ejecutar ni activar

| Acción u opción | Motivo y alternativa |
|---|---|
| `tools/pipeline.py --only patch` | Reaplica parches encima de `test` y puede introducir cambios ajenos. Si hace falta regenerar, usar el pipeline completo según la instrucción del usuario y revisar antes los cambios locales. Para recompilar, basta `tools/build.py`. |
| `PS3_NOVSYNC=1` | El usuario observó arranques mudos y bloqueo en el menú; las notas previas registran interferencia con el mezclador. Mantener la presentación normal. No usarlo para medir FPS. |
| `Stop-Process -Force`, `taskkill /F` o terminación abrupta | El audio no se libera limpiamente. Cerrar con `CloseMainWindow` o Alt+F4 y esperar la salida. Si no termina, registrar el bloqueo; no escalar automáticamente a un cierre forzado. |
| `DRAW_KEEP_TEX`, `DRAW_LAST_TEX`, `DRAW_KEEP_BLEND`, `TEX_MARKEMPTY`, `DEPTH_OFF`, `EMPTYTEX`, `TEX_RESOLVE_AUTO` | El usuario las excluye de ejecuciones normales y de validación. Son palancas de diagnóstico que pueden alterar el cuadro o sus fuentes. `DRAW_LAST_TEX` mueve todos los draws de la textura, no solo el HUD: descoloca cartas y sombras. No conservarlas en el entorno. |

No sustituirlas por saltos de draws, audio/SPU omitido, profundidad desactivada
o un reloj acelerado para aparentar una mejora.

## Señal de arranque válido

El usuario identifica un arranque correcto por **el efecto de sonido del aviso
inicial de guardado de datos**. Si falta, considera que el resto puede estar mal
inicializado: menús incompletos, HUD invisible y cinemáticas negras. Este indicador
fue corroborado el 03/11; no demuestra que todos los síntomas tengan una sola causa.

Antes de aceptar medidas o una corrección, comprobar en orden:

1. Suena el aviso y funciona el menú.
2. Battle Arena termina la carga.
3. La primera cinemática muestra imagen mientras suena su audio, sin saltarla.
4. Se ven HUD, cartas, sombras y efectos de la batalla.
5. Solo entonces medir FPS en una cámara fija comparable.

Una sesión con más FPS pero sin esas capas no es una mejora válida.

## Hallazgos del 03/11: distinguir las clases de SPU

La espera real de canales SPU estaba desactivada por defecto. Sin ella, `rdch`
puede continuar antes de recibir una respuesta del PPU. En arranques fallidos
aparecen transferencias DMA con EA `0x00000039`, coincidiendo con respuestas
tardías del servidor printf de Multistream.

La prueba **global** `SPU_CH_BLOCK=1` recuperó el sonido del aviso y el menú,
confirmado por el usuario. Sin embargo, dejó la carga de Battle Arena esperando
en un contexto SPURS sintético:

```text
[ch-wait] spu=0 pc=0x032FC ch=29 ...
```

**No dejar `SPU_CH_BLOCK=1` global como solución final.** Los hilos residentes
de grupos LV2 (`spu_group_id != 0`, como Multistream) y los contextos de trabajo
del scheduler SPURS (`spu_group_id == 0`) tienen modelos de ejecución distintos.
La corrección en verificación debe bloquear las lecturas de los primeros sin
imponer una espera sin productor a los segundos.

También se reprodujo un defecto independiente de envío PPU → SPU: el syscall
esperaba un buzón vacío aunque la estructura ya es FIFO y el hardware admite
cuatro palabras. La corrección de admisión comprueba capacidad y publica bajo el
mismo lock; las pruebas conservan cuatro palabras, orden y contrapresión. **Ese
cambio por sí solo no recuperó sonido ni menú**: no atribuirle todos los síntomas.

## Disciplina de diagnóstico

- Conservar el ejecutable de referencia antes de recompilar y registrar opciones
  efectivas del proceso, no solo las del PowerShell que lo lanza.
- Redirigir stderr activa logs verbosos por defecto; `PS3_VERBOSE=0` los desactiva.
  Registrar cualquier efecto temporal de trazas o capturas.
- No comparar Debug `/Od` directamente con Release `/O2`.
- Mantener cerradas otras instancias del juego durante la comparación.
- No restaurar ZIPs, resetear el toolkit, cambiar de rama ni reaplicar parches
  como sustituto del diagnóstico. El checkpoint antiguo también arrancó mudo
  en esta sesión: su nombre no garantiza un arranque correcto.
- Conservar cambios del usuario. No editar lifts ni datos del juego.

Evidencia local: `diagnostics/performance/session-20261103/`, con logs,
compilaciones, muestreos, captura del menú y pruebas del buzón. No versionar
estos artefactos ni datos del juego.

**Estado final del 03/11:** la corrección selectiva permite terminar la carga de
Battle Arena. Las capas que faltaban (cinemáticas, cartas 3D, HUD 2D) no eran
una regresión del render: desaparecían al lanzar el juego con logs redirigidos,
porque eso activa el log verboso y ahoga los hilos del guest. Con
`PS3_VERBOSE=0` se ven las tres. Ver [HUD_CINEMATICS.md](HUD_CINEMATICS.md).
El ruido de audio en batalla sigue abierto y el trabajo de FPS continúa tras
recuperar esa base.
