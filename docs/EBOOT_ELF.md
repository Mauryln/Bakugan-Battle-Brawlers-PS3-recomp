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

### Opción A: ya tienes el `game` extraído

Si el proyecto ya arranca, usa directamente
`game\PS3_GAME\USRDIR\EBOOT.elf`. El instalador tiene un campo para ese archivo.

### Opción B: RPCS3 una sola vez

1. Abre tu ISO/dump con el RPCS3 portable de `tools/rpcs3/rpcs3.exe`.
2. Arranca el juego una vez y deja que RPCS3 descifre el EBOOT.
3. Usa la opción de RPCS3 para volcar el `EBOOT.BIN` descifrado (o toma el
   `EBOOT.BIN` que deja en su `dev_bdvd`).
4. Si el resultado sigue siendo un SELF (`SCE\0`), quita el contenedor:

```powershell
unself EBOOT.BIN EBOOT.elf
```

`unself` forma parte de ps3tools. Si avisa que el archivo está cifrado, primero
hay que descifrarlo con la clave del disco (IRD de tu propio dump) mediante
`ps3dec` u otra herramienta equivalente; el proyecto no incluye claves.

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

`BakuganSetup.exe` copia los datos de la ISO e intenta extraer el ELF
automáticamente:

- `EBOOT.BIN` que empieza por `7F 45 4C 46`: se copia tal cual.
- fSELF de debug (`key_revision 0x8000`): se desenvuelve sin claves.
- SELF retail con segmentos planos: se desenvuelve si la entropía del primer
  segmento indica texto plano (por debajo de 7,5 bits/byte).
- Cualquier otro SELF: el instalador incluye un **RPCS3 portable mínimo** y
  ejecuta `rpcs3.exe --decrypt EBOOT.BIN`. RPCS3 usa su propia base de claves, así
  que una ISO retail normal no pide claves al usuario. Probado con la ISO de este
  proyecto: el ELF generado es idéntico al `EBOOT.elf` ya existente (mismo
  SHA-256).
- Si RPCS3 falla, pide el `EBOOT.elf` de respaldo.

También se puede probar la extracción sin instalar:

```powershell
BakuganSetup.exe --iso "D:\juego.iso" --extract-elf --out "C:\dump\EBOOT.elf"
```

RPCS3 se distribuye bajo GPLv2; el instalador sólo incluye el ejecutable y sus
DLLs, sin firmware, juegos ni claves del proyecto. Fuente:
<https://github.com/RPCS3/rpcs3>.
