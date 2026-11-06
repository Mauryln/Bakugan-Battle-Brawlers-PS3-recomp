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
