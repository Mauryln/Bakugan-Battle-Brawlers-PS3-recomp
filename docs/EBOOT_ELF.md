# EBOOT.elf: qué necesita el port y cómo obtenerlo

El runner carga un **ELF descifrado**, no el `EBOOT.BIN` de la ISO. Este proyecto
no distribuye el ELF, claves ni lifts: cada usuario lo genera desde su propio
dump.

## Qué hay en una ISO retail

`PS3_GAME\USRDIR\EBOOT.BIN` es un contenedor SELF. Aunque la ISO esté extraída de
una PS3 con CFW/multiman, el contenido puede seguir siendo un SELF con la carga
útil cifrada con la clave del disco. En el dump usado para este port, el payload
del `EBOOT.BIN` no coincide con el ELF (a partir del primer segmento los bytes
son aleatorios), así que no basta con quitarle una cabecera.

El archivo que necesita el proyecto es:

```text
game\PS3_GAME\USRDIR\EBOOT.elf
```

Debe empezar por `7F 45 4C 46` (`\x7fELF`) y no por `SCE\0`. En este dump mide
7.144.736 bytes.

## Cómo generarlo desde tu propio dump

### Opción A: ya tienes el `EBOOT.elf` o `game` extraído

Si el proyecto ya arranca o ya tienes un `EBOOT.elf` descifrado en tu carpeta de trabajo (`game\PS3_GAME\USRDIR\EBOOT.elf`), puedes indicarlo en modo silencioso con `--eboot <ruta>`.

### Opción B: herramientas externas del usuario (respaldo manual)

Si tu `EBOOT.BIN` es un SELF retail cifrado y no usas un módulo local `keys\key_vault.cs`:

1. Descifra el `EBOOT.BIN` de tu propio dump con una herramienta externa del usuario (`ps3dec`, `unself` de ps3tools, etc.) hasta obtener el ELF desencriptado.
2. Verifica que el archivo resultante empieza por `7F 45 4C 46`.
3. Pasa ese archivo al instalador mediante `--eboot <ruta>` o colócalo como `EBOOT.elf` junto a `USRDIR\EBOOT.BIN` antes de instalar.

### Opción C: PS3 con CFW/multiman

Al copiar el juego, activa la opción de descifrado de multiman. Si produce un
`EBOOT.BIN` descifrado, pásalo por `unself` para obtener el ELF. Si tu copia ya
incluye un `EBOOT.elf` válido, úsalo tal cual.

## Comprobar el resultado

```powershell
Format-Hex -Path EBOOT.elf -Count 4
# 7F 45 4C 46
```

Si empieza por `53 43 45 00` (`SCE\0`) todavía es un SELF, no un ELF.

## Instalador

`BakuganSetup.exe` copia los datos de la ISO o carpeta (`PS3_GAME`) e intenta extraer el ELF automáticamente:

- `EBOOT.BIN` que empieza por `7F 45 4C 46`: se copia tal cual.
- fSELF de debug (`key_revision 0x8000`): se desenvuelve sin claves.
- SELF retail con segmentos planos: se desenvuelve si la entropía del primer
  segmento indica texto plano (por debajo de 7,5 bits/byte).
- Cualquier otro SELF: `SelfExtract` intenta el descifrado nativo en C# (AES-256-CBC / AES-128-CTR / zlib) únicamente si se compiló un módulo local `keys\key_vault.cs` (no incluido en el repositorio público); en caso contrario, indica el fallo y en modo CLI acepta `--eboot <ruta>` como respaldo silencioso si ya tienes un `EBOOT.elf`. No se incluye ningún emulador ni ejecutable externo en el instalador.

También se puede probar la extracción sin instalar:

```powershell
BakuganSetup.exe --iso "D:\juego.iso" --extract-elf --out "C:\dump\EBOOT.elf"
BakuganSetup.exe --folder "D:\dump\PS3_GAME" --extract-elf --out "C:\out\EBOOT.elf"
```

El instalador y el juego instalado son **100 % nativos**, sin ningún emulador ni claves distribuidas en el repositorio público.
