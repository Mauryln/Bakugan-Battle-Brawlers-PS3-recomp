# Salida 1080p con FSR 1

El port puede conservar el render interno de PS3 a **1280×720** y presentar a
**1920×1080** mediante los shaders oficiales FSR 1.0.2 de AMD: EASU para
reescalado y RCAS para nitidez. Los formatos, pitches y resoluciones guest
permanecen iguales; el procesado ocurre al terminar el cuadro.

El checkout del toolkit debe contener los cambios reproducibles de
`patches/0052-clear-disabled-vertex-descriptors.patch` y
`patches/0053-d3d12-output-upscale-pass.patch`. La copia de trabajo validada ya
los contiene: no reaplicar la serie sobre ella.

```powershell
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build
.\tools\run.ps1 -Upscaler FSR1
.\tools\run.ps1 -Upscaler FSR1 -WindowMode Borderless
```

Para volver a la salida nativa:

```powershell
.\tools\run.ps1 -Upscaler Native
```

Al ejecutar el binario directamente, `PS3_FSR=fsr1` activa el procesado;
`PS3_FSR=0` o la variable ausente lo desactiva. Los archivos de
`shaders/fsr1/` generados por CMake deben acompañar al ejecutable.

## Launcher y presets

`BakuganLauncher.exe` (misma carpeta que `BakuganRecomp.exe`) ofrece:

| Preset | Salida | FPS |
| --- | --- | --- |
| 720p / 30 FPS | 720p | 30 |
| 1080p / 30 FPS (default) | 1080p | 30 |
| 1080p / 60 FPS | 1080p | 60 |
| 1080p / monitor | 1080p | refresco del monitor |
| 1440p / 60 FPS | 1440p | 60 |

El launcher fija `PS3_INTERNAL=720`, `PS3_OUTPUT`, `PS3_FSR=auto`,
`PS3_FPS_CAP` y `PS3_WINDOW_MODE=borderless`, busca `EBOOT.elf` y arranca el
juego. `--preset 720p30|1080p30|1080p60|1080pmonitor|1440p60` permite probarlo
sin GUI. Las variables equivalentes siguen disponibles en `tools/run.ps1`:
`-RenderHeight`, `-Output`, `-FSR`, `-Sharpness`, `-FpsCap`, `-TickHz`.

`PS3_FPS_CAP` acepta `30`, `60`, `90`, `120`, `monitor` o `0` (sin tope). Es un
techo de presentación, no una garantía: en batalla el coste CPU sigue limitando
a ~28-29 FPS. Para 90/120 el launcher fija también `PS3_TICK_HZ` al mismo valor
(experimental: el guest puede avanzar su lógica más rápido). El valor validado
para jugar es 30 o 60 con `PS3_TICK_HZ=60`.

## Por qué el render interno sigue siendo 1280x720

El título construye sus viewports, su buffer de display y su composición final
para 1280x720. Subir solo el backbuffer del host (o falsear
`cellVideoOutGetResolution`) deja la imagen real en un rectángulo de 1280x720
en la esquina superior izquierda; escalar el clip-space la amplía pero recorta.
La ruta validada es conservar el render interno a 720p y usar FSR 1 para la
salida. Aumentar de verdad la resolución interna exige escalar las superficies
y viewports del guest de forma coherente (trabajo pendiente, no activado).

`PS3_INTERNAL` acepta 720/900/1080/1440 y existe para experimentos; el valor
por defecto y el que usa el launcher es 720. Un valor mayor no mejora la imagen
con este título.

La ventana normal se ajusta al área de trabajo del monitor. `-WindowMode
Borderless` ocupa el monitor sin cambiar el modo de pantalla. Ambas opciones
conservan el tamaño de salida a 1080p. La variable equivalente es
`PS3_WINDOW_MODE=windowed` o `borderless`.

El modo virtual 960×540 se retiró durante la validación: el título conservaba
viewports de anchura1280 y desplazaba HUD y apuntado. No se considera un modo
compatible. `PS3_RENDER_HEIGHT` admite720; bajar el render de forma correcta
requiere mantener coherentes las coordenadas lógicas del juego y las físicas.

La barra de título distingue resolución interna y salida. Los FPS mostrados
son cuadros renderizados por el juego: FSR 1 **no genera cuadros** ni elimina
un cuello de CPU. El objetivo de 30 FPS debe verificarse por tiempos de cuadro,
además de comprobar audio, HUD, geometría y efectos.

`RSX_CAP_TRIGGER` conserva `backbuffer.bmp` a resolución interna y añade
`output.bmp` a 1080p cuando el procesador está activo. Ambos pertenecen al
mismo cuadro. El HUD se reescala junto con la escena; no existe todavía una
capa de UI nativa a 1080p ni integración de FSR 3 Frame Generation.

Dependencia: [FSR v1.0.2 oficial](https://github.com/GPUOpen-Effects/FidelityFX-FSR/releases/tag/v1.0.2),
licencia MIT incluida en `bakugan-recomp/third_party/fsr1/LICENSE.txt`.
