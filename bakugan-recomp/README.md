# Bakugan: Battle Brawlers — port nativo (ps3recomp)

Port de **Bakugan: Battle Brawlers** (`BLES00758`) a Windows x64 mediante
recompilación estática del código PPU y SPU de PS3 con
[ps3recomp](https://github.com/sp00nznet/ps3recomp).

El ejecutable resultante es un programa nativo: no hay emulador ni intérprete en
tiempo de ejecución. Este directorio **no contiene código del juego**: el
código en `recompiled/` y `spu/` se genera con las herramientas del toolkit a partir
de tu propio disco.

## Ficheros

| Fichero | Qué es |
|---|---|
| `main.cpp` | Runner: reserva el VM invitado de 4 GB, carga `EBOOT.elf`, registra la tabla de funciones y los handlers HLE, arranca el reloj de frames (vblank/flip sintéticos) y despacha el OPD de entrada. D3D12 en Windows. |
| `stubs.cpp` | Lo específico del juego: overrides de NIDs, parches, carga de PRX. Empieza vacío. |
| `config.toml` | Documentación de la configuración del port (el build no lo lee; los backends se eligen en `main.cpp` y el runtime se ajusta con variables de entorno). |
| `CMakeLists.txt` | Enlaza el lift + la tabla HLE + el scaffold del toolkit (`runtime/ppu/*`, `ps3recomp_runtime`). |
| `recompiled/` | Salida del lifter PPU y `ppu_hle_nids.cpp` (generado). |
| `spu/` | Imágenes SPU levantadas + `spu_workloads.c` (generado). |

## Compilar y ejecutar

Desde la raíz del repositorio, con una build Release ya configurada:

```powershell
.\.venv\Scripts\ninja.exe -C bakugan-recomp\build
.\BakuganBrawlers.cmd
```

Se necesita clang-cl, el SDK de Windows, el toolkit y los lifts generados desde
el dump propio. `cl` no admite las extensiones que requiere el port.
Los antiguos `tools/build.py` y `tools/pipeline.py` ya no existen.

## Estado

Ver la [guía vigente](../docs/PROJECT_GUIDE.md) y los
[defectos y validaciones](../docs/DEFECTOS_Y_INSTALADOR.md).
