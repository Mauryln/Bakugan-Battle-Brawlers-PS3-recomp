# Instalador de Windows

## Qué genera

```powershell
.\installer\build.ps1
```

Deja en `diagnostics\installer\`:

```text
BakuganSetup.exe     instalador WinForms (sin consola, con icono)
payload\             BakuganLauncher.exe, BakuganRecomp.exe, Uninstall.exe, SDL3.dll,
                     ffx_*.dll, shaders\fsr1, assets\ y licenses\
```

Para distribuir, entrega juntos `BakuganSetup.exe` y `payload\` (~42 MB comprimido en ZIP).
El instalador no incluye emuladores ni herramientas externas: el juego y su
instalador son 100 % nativos.

## Shaders

El runtime compila y guarda su caché de shaders en `%LOCALAPPDATA%\BakuganRecomp\shadercache` (`fp_v4` / `vs_v1`). Para regenerar/ampliar cachés de desarrollo con replays grabados:

```powershell
.\tools\warm_shaders.ps1
# o con más replays:
.\tools\warm_shaders.ps1 -Replays historia,arena,mi_arena -Seconds 480,240,240
```


## Uso

1. Selecciona tu ISO (**ISO...**) o carpeta extraída (**Carpeta...**, aceptando `PS3_GAME` o la raíz del dump) del disco propio (`BLES00758`).
2. Elige la carpeta de instalación (por defecto
   `%LOCALAPPDATA%\Programs\BakuganRecomp`, sin UAC).
3. El instalador extrae `EBOOT.elf` nativamente en C# mediante `SelfExtract` (con cero emuladores incluidos; en modo silencioso acepta además `--eboot <ruta>` como respaldo opcional si ya tienes un `EBOOT.elf`). Ver [EBOOT_ELF.md](EBOOT_ELF.md).
4. Marca o desmarca el icono de escritorio y el acceso del menú Inicio.
5. Instalar / Actualizar.

El instalador copia los datos `PS3_GAME` desde la ISO o carpeta y deja únicamente el juego nativo:

```text
<carpeta>\BakuganLauncher.exe
<carpeta>\BakuganRecomp.exe
<carpeta>\Uninstall.exe
<carpeta>\SDL3.dll
<carpeta>\ffx_*.dll
<carpeta>\shaders\...
<carpeta>\assets\...
<carpeta>\game\PS3_GAME\...
```

Los accesos directos apuntan a `BakuganLauncher.exe`. El launcher ya no muestra
consola y su ventana usa el icono del ejecutable.

## Modo automático

Para pruebas o despliegue silencioso:

```powershell
BakuganSetup.exe --iso "D:\juego.iso" --dir "C:\Games\BakuganRecomp" `
  --desktop 1 --startmenu 1 --silent
BakuganSetup.exe --folder "D:\dump\PS3_GAME" --dir "C:\Games\BakuganRecomp" --silent
BakuganSetup.exe --folder "D:\dump" --extract-elf --out "C:\dump\EBOOT.elf"
BakuganSetup.exe --iso "D:\juego.iso" --check-iso
```

El resultado se escribe en `installer.log`, junto al ejecutable.

## Límites actuales

- Registra `Uninstall.exe` en `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\BakuganRecomp` y conserva intactas las partidas en `%LOCALAPPDATA%\BakuganRecomp`.
- Montar imágenes `.iso` requiere Windows 10/11 (API nativa `virtdisk.dll`). Los dumps en carpeta (`--folder` / **Carpeta...**) no requieren montaje virtual.
- El juego (`EBOOT.BIN` / datos del disco) debe provenir del dump del usuario; el proyecto no distribuye claves ni lifts generados.
