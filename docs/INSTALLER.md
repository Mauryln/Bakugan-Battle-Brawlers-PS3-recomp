# Instalador de Windows

## Qué genera

```powershell
.\installer\build.ps1
```

Deja en `diagnostics\installer\`:

```text
BakuganSetup.exe     instalador WinForms (sin consola, con icono)
payload\             BakuganLauncher.exe, BakuganRecomp.exe, SDL3.dll,
                     shaders\fsr1, licenses\ y rpcs3\ (extractor EBOOT)
```

Para distribuir, entrega juntos `BakuganSetup.exe` y la carpeta `payload`
(~130 MB, la mayor parte es RPCS3). No se empaqueta ningún dato del juego.

## Shaders

El instalador copia `shadercache/fp_v3.cache`, una caché DXBC ya compilada por
el backend del port. **RPCS3 no compila estos shaders**: sólo se usa para
`--decrypt` del EBOOT. El nombre lleva versión para que una caché antigua
(`fp_v2.cache`) no se reutilice tras cambiar el decompilador.

Si una escena usa un shader que no está en la caché, el juego lo compila en el
primer uso y lo añade. Para regenerar/ampliar la caché con más rutas grabadas:

```powershell
.\tools\warm_shaders.ps1
# o con más replays:
.\tools\warm_shaders.ps1 -Replays historia,arena,mi_arena -Seconds 480,240,240
```


## Uso

1. Selecciona la ISO del disco propio.
2. Elige la carpeta de instalación (por defecto
   `%LOCALAPPDATA%\Programs\BakuganRecomp`, sin UAC).
3. El instalador extrae `EBOOT.elf` automáticamente. Para `EBOOT.BIN` retail
   usa el RPCS3 portable incluido (`rpcs3.exe --decrypt`), que emplea su propia
   base de claves. Si RPCS3 falla, selecciona el `EBOOT.elf` de tu dump como
   respaldo. El instalador no incluye claves ni código del juego. Ver
   [EBOOT_ELF.md](EBOOT_ELF.md).
4. Marca o desmarca el icono de escritorio y el acceso del menú Inicio.
5. Instalar.

El instalador copia los datos `PS3_GAME` desde la ISO y deja:

```text
<carpeta>\BakuganLauncher.exe
<carpeta>\BakuganRecomp.exe
<carpeta>\SDL3.dll
<carpeta>\shaders\...
<carpeta>\game\PS3_GAME\...
```

Los accesos directos apuntan a `BakuganLauncher.exe`. El launcher ya no muestra
consola y su ventana usa el icono del ejecutable.

## Modo automático

Para pruebas o despliegue silencioso:

```powershell
BakuganSetup.exe --iso "D:\juego.iso" --dir "C:\Games\BakuganRecomp" `
  --eboot "C:\dump\EBOOT.elf" --desktop 1 --startmenu 1 --silent
BakuganSetup.exe --iso "D:\juego.iso" --extract-elf --out "C:\dump\EBOOT.elf"
BakuganSetup.exe --iso "D:\juego.iso" --check-iso
```

El resultado se escribe en `installer.log`, junto al ejecutable.

## Límites actuales

- No crea entrada de desinstalación ni desinstalador.
- Requiere Windows 10/11 para `Mount-DiskImage`.
- El `EBOOT.elf` debe provenir del dump del usuario; el proyecto no distribuye
  claves, `EBOOT.BIN` descifrado ni lifts generados.
