# Launcher, idiomas y controles

## Uso

`tools/run.ps1` sin argumentos abre el launcher. También se puede abrir
`bakugan-recomp/build/BakuganLauncher.exe` directamente. «Guardar ajustes» conserva
la configuración; «Jugar» la guarda y abre el juego. Los cambios se aplican en el
siguiente arranque. La ventana se ajusta al área útil del monitor y a su DPI. El tema usa paneles oscuros,
acentos rojos/dorados y el fondo `PS3_GAME/PIC1.PNG` con la imagen
`PS3_GAME/ICON0.PNG` del dump local. Sin esas imágenes mantiene un tema utilizable.
Las imágenes no se copian al repositorio ni al paquete.

Los ejecutables llevan un icono propio y el juego se compila como aplicación
Windows: no abre consola. La barra de título del juego muestra sólo
«Bakugan: Battle Brawlers»; los contadores de FPS/draws quedan en los logs.
El instalador está documentado en [INSTALLER.md](INSTALLER.md).

- Izquierda: salida 720p/1080p/1440p/2160p/automática/monitor (1080p sin bordes
  predeterminado para nuevas instalaciones); anisotropía 1–16× y nitidez. El selector
  de FSR se eliminó de la interfaz (el reescalado FSR1 funciona de forma automática al superar 720p).
  El límite de FPS es fijo a 30 con pacing uniforme y no se expone en la interfaz.
- Derecha: idioma, mando concreto, modo Sixaxis, sensibilidad de giroscopio,
  zona muerta y editor interactivo gráfico de gamepad con representación visual
  completa del mando y detección de conflictos.
- Entrada simultánea: teclado y mando funcionan en paralelo sin necesidad de
  un selector exclusivo. Se puede alternar entre ambos al instante durante la partida.
- Idiomas manuales de esta edición EU: inglés, español, francés, alemán, italiano,
  neerlandés y sueco. «Automático» deja que el HLE consulte el idioma de Windows.
  La selección se comunica mediante `PS3_LANG`; el launcher no modifica assets.

La resolución interna permanece en 720p, la referencia actual con HUD y escenas
correctos. Los controles de salida reutilizan el renderer existente. Elegir un
límite alto no aumenta por sí mismo los FPS ni cambia el reloj guest de 60 Hz.

## Mapeo

Selecciona «Editar teclado y ratón» o «Editar mando», pulsa el cuadro de una acción,
suelta los controles y pulsa la entrada nueva. Acepta tecla, botón de ratón,
botón del mando o dirección de un eje. Esc cancela, Supr borra y «Restablecer»
restaura sólo el mapa que se está editando. La captura caduca tras 15 segundos.

SDL usa posiciones físicas: `a`/south es el botón inferior (Cruz en PlayStation,
B en Nintendo); `b`/east es el derecho; `x`/west el izquierdo; `y`/north el superior.
Las palancas y gatillos mantienen valores analógicos. La zona muerta se aplica a
los ejes. La selección concreta se conserva por GUID y número de serie cuando
está disponible; mandos idénticos sin serie pueden ser ambiguos.

«Automático» usa el mando elegido si está conectado y, si no, teclado y ratón.
«Mando» sin el dispositivo seleccionado produce entrada neutra; no cambia a otro
mando silenciosamente. «Teclado y ratón» excluye la entrada del mando.

Teclado predeterminado: X/C/Z/S = Cruz/Círculo/Cuadrado/Triángulo; Enter = Start;
Tab = Select; Q/E = L1/R1; 1/2 = L2/R2; 3/R = L3/R3; flechas = D-pad y palanca L;
I/J/K/L = palanca R. Clic izquierdo puede hacer Cruz; mantener clic derecho activa
apuntado con ratón sobre la palanca L. Son opciones independientes. El cursor
sólo se recentra con el juego enfocado y dentro de su área cliente.

## Sixaxis

SDL3 consulta acelerómetro y giroscopio del dispositivo. Si ambos se pueden
activar y leer, los datos físicos se convierten al informe PS3 de 10 bits.
DS4, DualSense y Switch Pro dependen del transporte/controlador que exponga SDL;
un dispositivo virtual XInput normalmente no ofrece sensores. No se fingen
sensores basándose sólo en el nombre del mando.

Sin lecturas válidas, o al elegir «Palanca R», sus direcciones simulan inclinación
y giro. «Impulso Sixaxis» (R3 en mando, F en teclado por defecto) emite un impulso
de lanzamiento y frenado de 200 ms; mantenerlo pulsado no lo repite. También está
disponible como alternativa manual cuando los sensores están activos.

El selector nativo de partidas recibe los botones remapeados del mando, incluyendo
la espera de liberación al salir. Su navegación propia por teclado/ratón se conserva.
Al perder el foco se neutralizan botones, palancas, movimiento y captura de ratón.

## Configuración y desarrollo

Archivo UTF-16 por usuario: `%LOCALAPPDATA%/BakuganRecomp/player.ini`, versión 1.
Se validan opciones, rangos y asignaciones al cargar. No contiene partidas.
El launcher elimina las variables de autocontrol antes de crear el proceso para
que una grabación anterior no pulse X/Start accidentalmente.

Los parámetros explícitos de `tools/run.ps1` o `-Direct` conservan su camino de
pruebas anterior. Un ejecutable iniciado directamente mantiene el input antiguo,
a menos que `PS3_PLAYER_CONFIG` indique un INI; esto conserva la referencia del
autocontrol historia. `docs/INPUT_REPLAY.md` sigue siendo el protocolo de replay.
El formato padrec v1 no graba sensores físicos.

```powershell
.\.venv\Scripts\ninja.exe -C bakugan-recomp/build BakuganRecomp BakuganLauncher
.\.venv\Scripts\ninja.exe -C bakugan-recomp/build PlayerInputTest PlayerPadProviderTest
.\bakugan-recomp\build\PlayerInputTest.exe
.\bakugan-recomp\build\PlayerPadProviderTest.exe
```

SDL 3.4.20 se obtiene del paquete oficial x64 VC, con SHA-256 fijado en
`bakugan-recomp/cmake/SDL3.cmake`; `BAKUGAN_SDL3_ROOT` permite una copia local.
No necesita instalación global. Junto a ambos ejecutables se distribuyen
`SDL3.dll`, `licenses/SDL3.txt` y `shaders/fsr1/`. `cmake --install` incluye esos
componentes. El toolkit necesita el proveedor de `patches/0059-title-host-pad-provider.patch`;
no reaplicar el parche sobre el checkout actual, que ya lo contiene.

## Verificación de esta entrega

- Rama `test`, build Release clang-cl; renderer, lifts y assets no modificados.
- Pruebas: persistencia de mapas/opciones, datos inválidos, limpieza de autopress,
  conversión de sensores, NaN/respaldo, impulso, botones/ejes y presión analógica.
- El test de integración compila el `cellPad.c` real y verifica el informe guest
  big-endian, Cruz, gatillo analógico y cuatro palabras de Sixaxis.
- PS4 Controller físico: sensores detectados y lecturas con gravedad y movimiento;
  el usuario confirmó mapeo y movimiento. DualSense/Switch Pro aún no probados físicamente.
- Ventana inspeccionada completa en el monitor; arranque desde «Jugar» a
  `1280x720 -> 1920x1080`; menú completo observado; usuario confirmó audio,
  cinemáticas y mando normales.
- El usuario completó una partida Bakugan con esta build y confirmó su funcionamiento.
- Tema Bakugan inspeccionado en ventana completa y botón Guardar comprobado.
  Tab hasta Jugar + Enter inicia el proceso; los botones dibujados conservan foco y teclado.
- Bootstrap SDL sin copia local e instalación en staging local comprobados.
- Revisión independiente sin defectos importantes. Quedan por ampliar las pruebas
  de reconexión durante gameplay y recorrido completo con teclado/ratón.

Referencias de API: [SDL_UpdateGamepads](https://wiki.libsdl.org/SDL3/SDL_UpdateGamepads),
[sensores](https://wiki.libsdl.org/SDL3/SDL_GetGamepadSensorData),
[paquete oficial](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20).
Las unidades PS3 siguen la conversión del
[backend SDL de RPCS3](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Input/sdl_pad_handler.cpp).
