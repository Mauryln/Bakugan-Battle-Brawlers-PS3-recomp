<div align="center">

# Bakugan: Battle Brawlers — Recompilación Nativa de PS3 para Windows

<p><strong>El lanzamiento original de PlayStation 3 (<code>BLES00758</code>), recompilado estáticamente a un ejecutable nativo de Windows x64 — ejecución 100 % nativa sin emuladores.</strong></p>

<p>
<a href="https://github.com/Mauryln/Bakugan-Battle-Brawlers-PS3-recomp/releases/latest"><img src="https://img.shields.io/badge/release-v1.4.0-blue.svg?style=flat-square" alt="Lanzamiento: v1.4.0"></a>
<img src="https://img.shields.io/badge/plataforma-Windows%20x64-0078D6?style=flat-square" alt="Plataforma: Windows x64">
<img src="https://img.shields.io/badge/gr%C3%A1ficos-Direct3D%2012-107C10?style=flat-square" alt="Gráficos: Direct3D 12">
<img src="https://img.shields.io/badge/t%C3%ADtulo-BLES00758%20(Europa)-c0392b?style=flat-square" alt="Título: BLES00758 (Europa)">
<img src="https://img.shields.io/badge/licencia-MIT-2ea44f?style=flat-square" alt="Licencia: MIT">
<img src="https://img.shields.io/badge/estado-jugable%20completo-brightgreen?style=flat-square" alt="Jugable de principio a fin">
</p>

<p>
<a href="README.md"><strong>English Version</strong></a> · 
<a href="#novedades-y-correcciones-v140">Novedades (v1.4.0)</a> · 
<a href="#requisitos-del-sistema">Requisitos del Sistema</a> · 
<a href="#instalación-y-cómo-jugar">Guía de Instalación</a> · 
<a href="#controles-y-sixaxis">Controles y Sixaxis</a> · 
<a href="#agradecimientos-y-créditos">Créditos</a>
</p>

</div>

---

## Galería de Capturas

<div align="center">

### Pantalla Principal
![Pantalla Principal](docs/images/mainscreen.png)

### Arena de Batalla — Pyrus
![Arena Pyrus](docs/images/arena-pyrus.png)

### Arena de Batalla — Ventus
![Arena Ventus](docs/images/arena-ventus.png)

</div>

---

## Descripción del Proyecto

[Bakugan: Battle Brawlers](https://es.wikipedia.org/wiki/Bakugan:_Battle_Brawlers) (2009) se lanzó originalmente de manera exclusiva para consolas y nunca tuvo un port oficial de PC. Este proyecto recompila estáticamente el código binario **PPU (CPU)** y **SPU (DSP)** de la versión de PlayStation 3 a código C++ optimizado para Windows x64 utilizando el toolkit **[PS3Recomp](https://github.com/sp00nznet/ps3recomp)**, enlazado contra un runtime HLE que implementa las librerías del sistema sobre **Direct3D 12**, **WASAPI** e hilos nativos de Windows.

El juego es **completamente jugable de principio a fin**: modo historia, tutoriales, arenas de batalla, minijuegos de cartas, cinemáticas, videos FMV, multijugador (hasta 7 puertos) y sistema de guardado y carga funcionan de forma nativa.

---

## Novedades y Correcciones (v1.4.0)

> Consulta las notas completas en [docs/RELEASE_1.4.0.md](docs/RELEASE_1.4.0.md), [docs/RELEASE_1.3.6.md](docs/RELEASE_1.3.6.md), [docs/RELEASE_1.3.5.md](docs/RELEASE_1.3.5.md) y [docs/RELEASE_1.3.0.md](docs/RELEASE_1.3.0.md).
> **v1.4.0** incorpora vibración SDL3 y calibración Sixaxis, iconos de botones Xbox/Nintendo, sincronización A/V en cinemáticas FMV, soporte de carpetas `PS3_GAME` en el instalador y extracción nativa SELF en C# (`SelfExtract`) dentro de un paquete sin ningún emulador.

### Novedades de la v1.4.0
- **Vibración completa en mandos (SDL3 / DualSense / Switch Pro / XInput)**: Soporte nativo vía `cellPadSetActDirect` para PS5 DualSense, DualShock 4, Switch Pro y Xbox (USB y Bluetooth) + slider de intensidad de vibración (0–100 %) en `BakuganLauncher.exe` (Issues #10 y #8).
- **Iconos de botones de Xbox y Nintendo**: Detección automática del mando conectado (`Auto`) y selector manual (`PlayStation` / `Xbox` / `Nintendo`) en el lanzador, actualizando iconos en juego, diagrama del lanzador y overlay de guardado (Issue #5).
- **Calibración Sixaxis en Modo Híbrido**: Rango real de `±113` cuentas y eje Z (`sensors[2]`) para acelerar/frenar en el Stick Derecho sin saltos accidentales, conservando el Modo Híbrido (Giroscopio + Stick R) junto al perfil **Estándar** por defecto (Issue #8).
- **Sincronización A/V en cinemáticas y pacing FSR 3.1**: Anclaje QPC de alta precisión en `sys_timer_usleep`, transiciones suaves en FSR 3.1 y sincronización de vídeos `.MP4` contra `waveOutGetPosition` (Issue #9).
- **Instalador con soporte de carpetas (`PS3_GAME`) y Desinstalador seguro**: Botón **Carpeta...** (`--folder`) en `BakuganSetup.exe` para instalar desde carpetas descomprimidas además de `.iso` (Issue #8), extracción nativa `SelfExtract` sin binarios de emuladores y nuevo `Uninstall.exe` con GUI/modo silencioso que protege las partidas guardadas y `player.ini` en `%LOCALAPPDATA%\BakuganRecomp`.

### Verificación de Seguridad (VirusTotal — v1.4.0)

| Archivo | Resultado VirusTotal | SHA-256 | Reporte |
| :--- | :--- | :--- | :--- |
| `Bakugan-Battle-Brawlers-Recomp-1.4.0-win64.zip` | **0 / 64** (Limpio) | `770F6375EDBC8833144F2CE247A05E1090D8BF862171A71654DC171FB0E0CCD5` | [Ver reporte](https://www.virustotal.com/gui/file/770f6375edbc8833144f2ce247a05e1090d8bf862171a71654dc171fb0e0ccd5) |
| `BakuganSetup.exe` | **0 / 68** (Limpio) | `2EDE9B140891F884E95244B24A34FBFF8CAE012AF3F7C4EFDEBD19FFFDE42CE4` | [Ver reporte](https://www.virustotal.com/gui/file/2ede9b140891f884e95244b24a34fbff8cae012af3f7c4efdebd19fffde42ce4) |
| `BakuganRecomp.exe` | **0 / 67** (Limpio) | `A455679A8B58C95A1680658805F6B79ED1AADEB2D1110AB42B5DF4F47E8ADC97` | [Ver reporte](https://www.virustotal.com/gui/file/a455679a8b58c95a1680658805f6b79ed1aadeb2d1110ab42b5df4f47e8adc97) |
| `BakuganLauncher.exe` | **0 / 68** (Limpio) | `8988A3AB37F71CDE3CD11982EB9E41BB8E777E62C406C517132F3E176BBC2B96` | [Ver reporte](https://www.virustotal.com/gui/file/8988a3ab37f71cde3cd11982eb9e41bb8e777e62c406c517132f3e176bbc2b96) |
| `Uninstall.exe` | **0 / 68** (Limpio) | `042D1B5EEBD339DCFDBD8AC1DFDA06FB2CAAA6425683D4D2CB188481187D3452` | [Ver reporte](https://www.virustotal.com/gui/file/042d1b5eebd339dcfdbd8ac1dfda06fb2caaa6425683d4d2cb188481187d3452) |

### Rendimiento D3D12 y Gráficos
- **AMD FSR 3.1 Frame Generation (Optical Flow)**: Interpolación de 30 FPS nativos hasta ~60 FPS presentados en arenas compatibles con **separación de HUD (`hudless`)**, manteniendo menús, cartas e interfaz nítidos y sin artefactos.
- **Antialiasing SMAA y Reescalado FSR 1**: Pipeline completo con **SMAA**, **AMD FSR 1.0 (EASU + nitidez RCAS configurable)** en salidas 720p / 1080p / 1440p / 4K, y **Filtrado Anisotrópico 16x** (con protección para texturas 2D).
- **Caché Unificada de Shaders y Optimizaciones de Pipeline**: Caché unificada en disco (`fp_v4` / `vs_v1`) en `%LOCALAPPDATA%\BakuganRecomp\shadercache`, búsquedas por tabla hash, **dirty tracking de SRVs** para omitir escrituras redundantes y **atlas de fuentes persistente** en los overlays.
- **Modos de Pantalla y Diagnóstico**: Modos nativos `windowed`, `borderless` y `fullscreen` DXGI con relación de aspecto **16:9 exacta**, alternancia instantánea entre ventana y sin bordes con **`F11`** y overlay de diagnóstico en tiempo real con **`F12`**.

### Pacing y Jugabilidad a 30 / 60 FPS
- **Corrección de Cuantización en 144 Hz**: Solucionado el fallo que limitaba el ritmo de fotogramas a 28.80 FPS en monitores de 144 Hz, clavando **30.00 FPS estables**.
- **30 FPS Nativos y Modo Opcional de 60 FPS**: Los 30 FPS nativos quedan fijos por defecto (con interpolación opcional FSR 3.1 hasta ~60 FPS visuales), manteniendo además soporte opcional de 60 FPS nativos (`PS3_FPS_CAP=60`) con la corrección del flanco de `Start` para que el **menú de opciones/pausa en batalla** sea totalmente funcional.

### Audio SPU / MultiStream / WASAPI
- **Aceleración SIMD SSSE3 (~12x)**: Núcleos de audio SPU con SSE float y acceso rápido SSSE3 al Local Store para MultiStream.
- **Limitador Dinámico de Picos (Soft-Knee Limiter)**: Evita clipping y distorsión durante combates intensos, acompañado de limpieza de slots huérfanos, de-click y amortiguación adaptativa de jitter.
- **Priorización de ACKs de SPU y Cierre Ordenado de FMV**: La priorización de ACKs del buzón SPU evita bloqueos en ráfagas rápidas de disparos, y los hilos de vídeo/audio se cierran de forma ordenada durante cinemáticas FMV y al salir del juego.

### Sixaxis, Launcher v1.4.0 e Instalador
- **Sixaxis y Controles Analógicos Refinados**: Emulación Sixaxis suavizada con **filtro EMA independiente de FPS**, zona muerta radial 2D, soporte calibrado para mandos con giroscopio real sin doble inclinación, **modo Híbrido** (corrige con el stick derecho incluso inclinando el mando), perfil **Estándar** por defecto y respuesta progresiva del stick izquierdo en diagonales.
- **Nuevo Launcher Oscuro Estilo Pyrus v1.4.0**: Interfaz bilingüe renovada (`BakuganLauncher.exe`) con mapa interactivo del mando (`assets/launcher/Control.png`), control de vibración SDL3, selector de iconos de botones Xbox/Nintendo/PlayStation (`assets/button_prompts/`), persistencia fiel de `player.ini`, gestión inteligente de visibilidad/captura del cursor y diálogo de confirmación de salida con **`Esc`**.
- **Runtime Autocontenido e Instalador**: Usa el runtime dinámico de MSVC (`/MD`) con las DLLs oficiales **firmadas por Microsoft** incluidas junto a los ejecutables (sin necesidad de instalar VC++ Redistributable aparte) y AMD FidelityFX FSR 3.1 en DLLs dedicadas. `BakuganSetup.exe` usa APIs nativas Win32 (`virtdisk.dll` / `IShellLinkW`), admite imágenes `.iso` y carpetas `PS3_GAME`, extrae SELF nativamente en C# mediante `SelfExtract` (con cero binarios de emuladores ni en el instalador ni en la carpeta final y respaldo silencioso `--eboot` por CLI) y protege partidas y `player.ini`.

---

## Requisitos del Sistema

| Componente | Requisitos Mínimos | Configuración Recomendada |
| :--- | :--- | :--- |
| **Sistema Operativo** | Windows 10 de 64 bits (Versión 1909+) o Windows 11 | Windows 10/11 de 64 bits (última actualización) |
| **Procesador (CPU)** | **x86-64 con soporte obligatorio de AVX2 y SSE4.1**<br>*(Intel Core 4ª gen Haswell / AMD Ryzen serie 1000 o superior)* | Procesador de 6 núcleos / 12 hilos con AVX2<br>*(Intel Core i5-10400 / AMD Ryzen 5 3600 o más reciente)* |
| **Memoria (RAM)** | 4 GB de memoria RAM | 8 GB de memoria RAM o superior |
| **Tarjeta Gráfica (GPU)** | Compatible con Direct3D 12 Feature Level 11_0<br>*(NVIDIA GeForce GTX 960 / AMD Radeon RX 460 / Intel Iris Xe)* | Compatible con Direct3D 12 Feature Level 12_0 (para FSR 3.1 Frame Generation)<br>*(NVIDIA GeForce RTX 2060/3050 / AMD Radeon RX 6600 o mejor)* |
| **Almacenamiento** | 6 GB de espacio disponible (compatible con HDD) | 6 GB de espacio disponible en SSD |
| **Sonido** | Dispositivo de audio compatible con Windows / WASAPI | Auriculares o altavoces estéreo / multicanal |
| **Dependencias de Runtime** | **Ninguna** (DLLs `/MD` firmadas por Microsoft incluidas; no requiere VC++ Redistributable) | — |
| **Copia del Juego** | Copia legal del disco europeo original de PS3 (`BLES00758`) | Imagen `.iso` o carpeta `PS3_GAME` extraída |

> [!NOTE]
> Los binarios Release se compilan con **SSE4.1** y **AVX2** activados para los kernels SPU de audio y cómputo. Los procesadores sin soporte para AVX2 o SSE4.1 no son compatibles con los binarios precompilados.

---

## Requisito de Edición del Juego (Versión Europea)

> [!IMPORTANT]
> Esta recompilación requiere exclusivamente los archivos de la **versión europea en disco de Bakugan: Battle Brawlers (`BLES00758`)**.
> Otras ediciones regionales (como la americana `BLUS30409`) tienen direcciones binarias distintas y no son compatibles con esta versión de los lifts.

Debes proporcionar tu propia copia legal del disco europeo en formato **archivo ISO (`.iso`)** o una carpeta extraída que contenga `PS3_GAME/USRDIR/EBOOT.BIN`.

---

## Instalación y Cómo Jugar

### Instalación Nueva
1. Descarga el paquete completo de la versión **`Bakugan-Battle-Brawlers-Recomp-1.4.0-win64.zip`** desde la sección de [Último Lanzamiento](https://github.com/Mauryln/Bakugan-Battle-Brawlers-PS3-recomp/releases/latest).
2. Descomprime completamente el archivo ZIP en la carpeta que prefieras (mantén `payload` junto a `BakuganSetup.exe`).
3. Ejecuta **`BakuganSetup.exe`** desde la carpeta descomprimida.
4. Selecciona tu **archivo ISO europeo (`BLES00758`)** o carpeta **`PS3_GAME`** extraída (mediante **ISO...** o **Carpeta...**).
5. Elige la carpeta de destino y accesos directos (Escritorio / Menú Inicio), y pulsa en **Instalar / Actualizar**. `BakuganSetup.exe` copia los datos del juego y extrae `EBOOT.elf` nativamente con `SelfExtract` (sin binarios de emulador).
6. Abre el juego desde el acceso directo o ejecutando **`BakuganLauncher.exe`**, configura gráficos y controles, y pulsa **Jugar**.

### Actualizar desde una Versión Anterior (v1.2.0 / v1.3.x)
1. Cierra el juego y `BakuganLauncher.exe`.
2. Descomprime el ZIP de la versión v1.4.0 y ejecuta **`BakuganSetup.exe`**.
3. Selecciona la carpeta donde ya lo tienes instalado y **deja el campo de ISO/carpeta en blanco**. El instalador actualizará los binarios y recursos del launcher reutilizando los datos del juego y conservando tus partidas guardadas y `player.ini`.

---

## Controles y Sixaxis

El puente de entrada admite hasta **7 puertos simultáneos** combinando Teclado, Ratón y Mandos (DualShock 4, DualSense, Nintendo Switch Pro, Xbox/XInput y mandos genéricos SDL3). Todos los controles se pueden reasignar visualmente desde **`BakuganLauncher.exe`**.

### Controles por Defecto (Teclado, Ratón y Atajos del Sistema)

| Acción de PS3 / Función | Tecla / Entrada por Defecto | Función en el Juego |
| :--- | :--- | :--- |
| **Cruz (✕)** | `X` *(o Clic Izquierdo del Ratón)* | Confirmar / Saltar / Acción principal |
| **Círculo (○)** | `C` | Cancelar / Retroceder |
| **Cuadrado (□)** | `Z` | Ataque / Carta de habilidad |
| **Triángulo (△)** | `S` | Especial / Carta de inicio |
| **Cruceta y Stick Izquierdo** | Teclas de flecha (`↑`, `↓`, `←`, `→`) | Navegación / Movimiento / Puntería |
| **Stick Derecho** | `I`, `J`, `K`, `L` | Cámara / Inclinación Sixaxis |
| **Impulso Sixaxis** | `F` | Impulso de lanzamiento Sixaxis |
| **Start** | `Enter` | Menú de pausa / opciones |
| **Select** | `Tab` | Estado / Mapa / Detalles |
| **L1 / R1** | `Q` / `E` | Cambiar cartas / Zoom de cámara |
| **L2 / R2** | `1` / `2` | Acciones secundarias / Vista previa |
| **L3 / R3** | `3` / `R` | Pulsación de sticks |
| **Conducción Sixaxis** | **Movimiento del Ratón** / Stick Derecho / Giroscopio | Guiar la esfera Bakugan en la arena |
| **Cambio de Pantalla** | `F11` | Alternar al instante Ventana ↔ Sin bordes |
| **Overlay de Diagnóstico** | `F12` | Mostrar métricas de FPS / frame-time / ajustes en tiempo real |
| **Confirmación de Salida** | `Esc` | Abrir diálogo de confirmación para salir |

### Implementación del Sensor Sixaxis

Tres métodos de control funcionan simultáneamente y pueden ajustarse desde `BakuganLauncher.exe` (con el perfil **Estándar** activo por defecto):

- **Conducción con Ratón**: Inclinación natural con suavizado EMA independiente de FPS, zona muerta radial y retorno elástico al centro. La captura del cursor se adapta automáticamente a menús, overlays y Alt-Tab.
- **Stick Analógico Derecho**: Control suave de 360° en mandos estándar, acompañado de puntería progresiva en el stick izquierdo.
- **Giroscopio Físico y Modo Híbrido**: Soporte calibrado de sensores mediante SDL3 en DualShock 4, DualSense y Switch Pro sin doble inclinación. En modo **Híbrido**, puedes corregir la trayectoria con el stick derecho aunque el mando siga inclinado.

---

## Detalles Técnicos

- **Recompilación Estática AOT**: 15.682 funciones de PPU y 1.331 de SPU compiladas a código máquina x64 nativo con LLVM `clang-cl` `/O2` y runtime CRT dinámico (`/MD`) junto a las DLLs oficiales firmadas por Microsoft. No se distribuye ni ejecuta ningún emulador de PS3.
- **Rutas SPU Aceleradas por SIMD**: Helpers SPU con SSE4.1 / AVX2 y núcleos de audio MultiStream con SSSE3 y SSE float.
- **HLE Direct3D 12**: Traductor de command buffers RSX con subida paralela de vértices, dirty tracking de SRVs, caché unificada de shaders en disco (`fp_v4`/`vs_v1`), SMAA, AMD FSR 1.0 y AMD FSR 3.1 Frame Generation con separación de HUD.
- **Audio WASAPI de Baja Latencia**: Mezcla multicanal con limitador dinámico soft-knee, amortiguación adaptativa de jitter y priorización de ACKs SPU.
- **Ubicación de Partidas y Ajustes**: Partidas en `%LOCALAPPDATA%\BakuganRecomp\dev_hdd0\home\00000001\savedata`; ajustes en `%LOCALAPPDATA%\BakuganRecomp\player.ini`.

---

## Agradecimientos y Créditos

Este port no hubiera sido posible sin la comunidad de emulación y recompilación de código abierto:

- **Equipo de PS3Recomp y Ned Heller ([sp00nznet](https://github.com/sp00nznet))**:  
  Nuestro más sincero agradecimiento a los creadores del proyecto de recompilación estática **[PS3Recomp](https://github.com/sp00nznet/ps3recomp)**. Su trabajo pionero en la traducción PPU/SPU y la arquitectura del runtime HLE hizo viable este proyecto.
- **Desarrolladores de RPCS3 ([RPCS3](https://github.com/rpcs3/rpcs3) / [rpcs3.net](https://rpcs3.net))**:  
  Por su emulador de PS3 y su documentación de ingeniería inversa sobre Cell Broadband Engine, RSX y el software del sistema PlayStation 3.
- **libsdl-org ([SDL3](https://github.com/libsdl-org/SDL))**:  
  Por el soporte multiplataforma de detección de mandos, vibración y lectura de sensores de movimiento.
- **AMD FidelityFX**:  
  Por FSR 1.0 (EASU + RCAS) y FSR 3.1 Frame Generation (Optical Flow).

---

## Aviso Legal y Responsabilidad

Bakugan: Battle Brawlers es © de Activision, Nowpro, Nelvana, Spin Master y Sega Sammy.  
Este proyecto es una recreación independiente de código abierto y **no está afiliado, respaldado ni patrocinado por Sony Interactive Entertainment, Activision ni los titulares de los derechos**.

**NO SE DISTRIBUYEN ARCHIVOS PROTEGIDOS POR COPYRIGHT, CÓDIGO PROPIETARIO, CLAVES NI DATOS DEL JUEGO.**  
Los usuarios deben poseer y proporcionar su propia copia original del disco de PlayStation 3 en su edición europea (`BLES00758`).
