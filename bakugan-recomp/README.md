# Bakugan: Battle Brawlers — port nativo (ps3recomp)

Port de **Bakugan: Battle Brawlers** (`BLES00758`) a Windows x64 mediante
recompilación estática del código PPU y SPU de PS3 con
[ps3recomp](https://github.com/sp00nznet/ps3recomp).

El ejecutable resultante es un programa nativo: no hay emulador ni intérprete en
tiempo de ejecución. Este directorio **no contiene código del juego**: el
código en `recompiled/` y `spu/` se regenera con `../tools/pipeline.py` a partir
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

```powershell
..\.venv\Scripts\python.exe ..\tools\build.py
..\tools\run.ps1
```

`build.py` toma el entorno de MSVC desde `vcvars64.bat` (los headers y librerías
que clang-cl necesita), usa el CMake y el Ninja del venv del proyecto y compila
con `clang-cl` — no `cl`, que carece de `__int128`, `__atomic_*` y
`__builtin_bswap*`.

`run.ps1` fija `PS3_VFS_ROOT=<repo>/game`, de donde `main.cpp` deriva las rutas
`/dev_hdd0/...` del invitado.

## Estado

Ver [`../docs/STATUS.md`](../docs/STATUS.md).
