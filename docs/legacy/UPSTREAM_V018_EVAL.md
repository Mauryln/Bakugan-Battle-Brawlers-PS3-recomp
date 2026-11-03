# Evaluar PS3Recomp v0.18.0 como base nueva — prompt para sesión aparte

Documento de traspaso. Abrir una **sesión nueva** y pegar el prompt de abajo tal
cual. Todo lo que hay antes es contexto para que esa sesión no tenga que
redescubrirlo.

## Estado verificado el 03/11/2026

| Dato | Valor |
|---|---|
| Rama del toolkit | `test`, 18 commits propios |
| Upstream | `origin/master` = `4e8f3ca`, **v0.18.0** |
| Distancia | **380 commits por detrás** |
| Commits upstream que tocan SPU/syscalls | 131 |
| Commits upstream que tocan `rsx_d3d12_backend.c` | 25 |
| Ficheros tocados por ambos lados | 21 de 30 |

Lo que más se cruza con nuestro trabajo perdido:

- `4eabd92` *drop the texture-slot SRVs that overwrote the SRV heap* — misma
  clase de fallo que sospechábamos para HUD/cartas.
- `#518` *cache converted vertices within a draw*, `#530` *draw indexed triangle
  lists with their indices* — nuestro terreno de vértices.
- `#561` *on-demand RenderDoc capture, resources named in guest terms* — el
  debugger que hoy nos falta para el compute.
- `#579` ring de subida de texturas con LRU; `#583`/PR#486 flip/present;
  `#559` superficies de profundidad por zeta.
- Issue `#345` (*inFAMOUS 2, 7 → 26 fps*, port SPURS-heavy): loops dedicados por
  tipo de atributo, caché del lookup de programa por draw y **un solo recorrido
  para hallar el rango de índices** (2,2 ms/cuadro en su caso).

## Contexto que la sesión nueva necesita

- Raíz del proyecto: `C:\Users\Yemih\Proyects\ps3recomp`. Toolkit anidado en
  `ps3recomp-src/` (repo aparte, ignorado por el repo raíz).
- El port vive en `bakugan-recomp/` (no regenerar `recompiled/` ni `spu/`).
- Docs vigentes: [HUD_CINEMATICS.md](HUD_CINEMATICS.md),
  [AUDIO_MENU_GUARDRAILS.md](AUDIO_MENU_GUARDRAILS.md),
  [NEXT_SESSION.md](NEXT_SESSION.md).
- Build (sin `tools/build.py`, retirado):

```powershell
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin;$env:PATH"
.\.venv\Lib\site-packages\cmake\data\bin\cmake.exe -S bakugan-recomp -B bakugan-recomp\build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=.venv\Scripts\ninja.exe `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build
.\tools\run.ps1
```

- No cerrar el juego con `Stop-Process -Force`: `CloseMainWindow` o Alt+F4.
- Nunca juzgar imagen ni FPS con logs redirigidos sin `PS3_VERBOSE=0`.
- La build buena actual es la rama `test` con estos 18 commits; también hay
  ejecutables de referencia en `backups/`. Nada de eso se toca.

## Prompt para la sesión nueva

```text
Sí, autorizo crear un worktree independiente basado en `origin/master` (`4e8f3ca`)
para evaluar la compatibilidad de Bakugan con PS3Recomp v0.18.0.

Condiciones obligatorias:

1. No modificar, resetear, limpiar ni hacer rebase de la rama `test` o sus 18
   commits locales.
2. No sobrescribir ni mover los backups existentes.
3. Crear el worktree en una carpeta nueva y verificar que está completamente
   separado del checkout actual.
4. Antes de portar código, analizar los cambios de API, CMake, HLE, SPU y RSX
   entre ambas versiones.
5. Conservar todos los parches y correcciones de Bakugan. No eliminarlos
   simplemente porque upstream tenga cambios parecidos.
6. Compilar primero el runtime upstream sin modificaciones.
7. Si compila, intentar integrar el port de Bakugan con los cambios mínimos
   necesarios, documentando cada adaptación.
8. No regenerar las funciones PPU/SPU salvo que se demuestre imprescindible y lo
   autorice.
9. No ejecutar cherry-pick masivo ni fusionar ramas.
10. No modificar la versión estable del juego.

Criterios de evaluación:

- ¿Compila el runtime nuevo?
- ¿Compila Bakugan con la API actual?
- ¿Arranca y llega al menú principal?
- ¿Entra en gameplay?
- ¿Conserva sombras, reflejos, HUD, cartas y audio?
- ¿Mejora los FPS bajo las mismas restricciones de CPU?

Si aparecen errores estructurales importantes, detente y presenta un informe
antes de continuar.

Objetivo: determinar si las mejoras de upstream permiten superar el rendimiento
actual sin perder nuestro trabajo.

No busques nuevas microoptimizaciones todavía. Primero necesitamos comprobar si
actualizar la base tecnológica es viable.
```

## Referencia rápida de lo que no se puede perder

- Caché de shaders a un solo fichero (`shadercache/fp_v2.cache`), ver
  [HUD_CINEMATICS.md](HUD_CINEMATICS.md): es lo que evita recompilar cientos de
  programas al arrancar.
- Arreglos de SPU del 03/11: bloqueo de canales solo para grupos LV2 y admisión
  de cuatro palabras en el buzón (commits `b5e65b4`).
- Parches de rendimiento propios (caché de vértices, relleno compacto, lookup de
  programa por draw): pueden estar superados por `#518`/`#530`, pero se comparan,
  no se descartan de entrada.
- Ruta GPU a medio hacer (`GPU_VTX`, apagada): no está rota por diseño, le falta
  el validador. Si v0.18.0 trae la captura de RenderDoc, se retoma allí.
