# Informe de traspaso — 24 de octubre de 2026 (cierre)

Documento para retomar el trabajo (o para que el usuario pruebe a mano). Al final
está la lista de comprobaciones manuales con los comandos exactos y qué significa
cada resultado.

## Dónde estamos

| Área | Estado |
|---|---|
| Arranque, logos, vídeos, intro narrativa | Funciona |
| Menús y texturas 2D | Funcionan |
| Audio (pistas SNDP, colas SPU, overlays) | Confirmado bueno por el usuario |
| Teclado / XInput / guardado | Funciona |
| Battle Arena: carga y geometría | Emite 400–600 dibujos por frame; el combate arranca |
| Escena 3D dentro del RT `0xEAB0000` | Se dibuja (hay imagen) pero **deformada** |
| Pase de presentación del invitado (composite + HUD) | **No deja píxeles en pantalla** |
| Efectos `SOUND/*.PAST` (ATRAC3) | Sin sonido |

Consecuencia visible: en batalla la ventana queda **negra**. Con `RX_PASTE=1` se ve
la escena 3D a 1:1 en la esquina superior izquierda, deformada, y sin HUD (el pegado
tapa el 2D: eso explica la regresión "antes se veía el HUD y ahora no").

## Lo que se demostró en esta sesión

Todo con una captura de frames real (`RX_DISPDBG`, `RTT_STAT`) en batalla.

1. **El combate dibuja mucho.** `[RTTSTAT] frame=1800 ops=418 clear=4 offtarget=404
   display=13` — 400+ dibujos fuera de pantalla y 13 en el target de display.

2. **El quad de composición existe y muestrea el RT vivo.** Del pase de display,
   el primer op:
   ```
   [DISPDRAW] d=1 verts=6 pso=guest-fp vp=1280x720 bl=1/122118B at=602  dp=4 texrt=0 tex0=0xDF60000
   ```
   `texrt=0` significa que la textura `0xDF60000` está enlazada como SRV **del RT
   `0xEAB0000` en vivo**, es decir el alias del parche 0007 funciona. `pso=guest-fp`
   dice que se usa el fragment program del invitado, no el fallback.

3. **El estado del dibujo es razonable.** `bl=1/0x122118B` = blend activo con
   `SRC_ALPHA`/`INV_SRC_ALPHA`. `dp=4` y `dp=1C` decodifican **sin test de
   profundidad** (`depth_key`: bit0 = enable). `at=602` = alpha test desactivado;
   `at=10602` = activado con `GEQUAL 2/255`.

4. **El menú usa exactamente el mismo estado y sí se ve.** Mismos FPs
   (`0xD8FFF01`/`0xD8FFF81`), mismo viewport 1280x720, mismo blend, mismo `tex0`.
   La diferencia es el contenido del batch (400+ dibujos offscreen) o el origen de
   la textura.

## Teorías descartadas (no repetirlas)

- **Alias/resolve ausente**: falso, `texrt=0` prueba que el RT vivo está enlazado.
- **Canal alfa del RT**: se forzó el mapeo de componentes del SRV a `alpha=1` para
  los draws a display; la pantalla siguió negra. Revertido.
- **Test de profundidad**: `depth_key` sale desactivado en los ops de display.
- **Canal alfa global / `PS3_FORCE_ALPHA_BLEND`**: sin efecto sobre el problema.
- **cellResc**: importado pero nunca llamado. `cellGcm` 2D no manejado:
  `[GCM2D-UNH]` vacío. No hay blits ocultos ni segundo buffer de display.
- **PSO de fallback**: `pso=guest-fp` en todos los ops de display.
- **Profundidad `depth_key=4`/`0x1C`**: sin test, descartado.

Queda una duda honesta: el pase de display **completo** (quad de composición *y*
sprites del HUD) desaparece, aunque los sprites del HUD tienen su propia textura y
su propio alfa. Eso apunta a algo global del batch o del target, no al contenido
del RT.

## Hipótesis abiertas, en orden de coste

1. **El estado de blend/alpha test esconde el pase.** `RX_OPAQUE=1` dibuja los ops
   de display con blend y alpha test forzados a off (ya está en el binario). Si la
   imagen aparece: el canal alfa de salida del FP (o el alfa del color de vértice)
   llega a 0 y con `SRC_ALPHA` no pinta nada. Si sigue negra: los draws no llegan
   al target.
2. **El alfa del color de vértice del quad es 0.** Los dos quads de display leen un
   atributo de color; si el VP entrega `(r,g,b,0)`, el blend los borra. Mirar el
   buffer de vértices del draw (`vb_byte_offset / 256`) y el `vs_idx` del op.
3. **El binding del backbuffer se pierde en un batch largo.** `RT_REBIND=1`
   reemite `OMSetRenderTargets` antes de cada draw; es una prueba de una línea.
4. **El export del FP pierde el alfa.** Probar `RSX_FP_CTRL_AUTO=1` y comparar el
   HLSL generado para `fp=0xD8FFF01` con y sin la opción.
5. **El present enseña otro buffer.** `GCM_FLIPCOUNT=1` y `FLIP_PC=1` para ver si el
   flip y el `Present` coinciden.

## El otro fallo: vértices deformados dentro del RT

La captura `battle-arena-picture/Screenshot 2026-10-24 175208.png` muestra estelas
radiales desde un punto, bandas verdes y una zona blanca grande: la geometría se
estira, así que el problema está en **posición/constantes del vertex program**, no
en las texturas.

Siguientes pasos, en este orden:

1. `VP_DUMP=1` / `VP_SUBMIT=<n>`: comprobar que el `vp_vs` del draw del combate es
   el esperado y que recibe sus constantes.
2. Revisar el fallback de `vp_fill_cb_slot`: sustituye `c0..c3` según el valor de
   `c00`. Si el juego manda la matriz de otro modo, la proyección sale mal y los
   triángulos se estiran exactamente así.
3. Comparar `c0..c3` de un draw de la arena con la matriz que espera la cámara
   (el manifest de `diagnostics/arena/manual2/frames/*/manifest.txt` ya vuelca los
   slots no nulos de cada op).

## Comprobaciones manuales (para el usuario)

Lanzar desde la raíz del repo, en PowerShell. Cada bloque deja log en
`diagnostics/arena/<carpeta>/`.

```powershell
# 0) Cerrar el proceso antes de reconstruir (si no, lld-link falla)
Get-Process BakuganRecomp -ErrorAction SilentlyContinue | Stop-Process -Force

# 1) (Re)construir
.\.venv\Scripts\python.exe tools\build.py

# 2) Lanzar con una prueba concreta
$env:PS3_VFS_ROOT="$PWD\game"; $env:PS3_TITLE="Bakugan: Battle Brawlers"
$env:PAD_ALWAYS_CHANGE="1"; $env:PS3_NO_SAIL="1"
$env:RX_OPAQUE="1"          # la prueba de esta lista
$env:RX_DISPDBG="8"         # imprime el estado de los 8 primeros ops de display
New-Item -ItemType Directory -Force diagnostics\arena\t1 | Out-Null
Start-Process "$PWD\bakugan-recomp\build\BakuganRecomp.exe" `
  -ArgumentList "$PWD\game\PS3_GAME\USRDIR\EBOOT.elf" `
  -RedirectStandardOutput "$PWD\diagnostics\arena\t1\run.out" `
  -RedirectStandardError  "$PWD\diagnostics\arena\t1\run.err"
```

Llegar a la batalla (unos 2 minutos: **X** o Espacio = Cross, flechas = D-pad,
**Enter** = Start). Después, leer el log:

```powershell
Select-String -Path diagnostics\arena\t1\run.err -Pattern 'DISPDRAW' | ForEach-Object Line
Select-String -Path diagnostics\arena\t1\run.err -Pattern 'RTTSTAT' | Select-Object -Last 3 | ForEach-Object Line
```

**Qué significa cada resultado**

| Resultado en pantalla | Conclusión | Siguiente paso |
|---|---|---|
| Escena + HUD a tamaño completo con `RX_OPAQUE=1` | Es el blend/alfa (hipótesis 1–2) | Medir el alfa de salida del FP del op de display |
| Sigue negra con `RX_OPAQUE=1` | Los draws no llegan al target | Probar `RT_REBIND=1` y `GCM_FLIPCOUNT=1` |
| Se ve sólo con `RX_PASTE=1` | Estado actual del repo | Trabajar la deformación del RT, que es independiente |

## Variables de entorno útiles

| Variable | Efecto |
|---|---|
| `RX_PASTE=1` | Pega 1:1 el último RT dibujado (ver la escena 3D; tapa el HUD) |
| `RX_OPAQUE=1` | Dibuja los ops a display con blend y alpha test a off (A/B) |
| `RX_DISPDBG=<n>` | Log `[DISPDRAW]` de los n primeros ops a display |
| `RTT_STAT=1` | Log `[RTTSTAT]` por frame (offscreen/display/`texDF60000`) |
| `RT_REBIND=1` | Reemite el render target antes de cada draw |
| `VP_SUBMIT=<n>` | Log de los n primeros draws enviados |
| `VP_DUMP=1` | Vuelca constantes y vertex programs |
| `FP_LIST=1` / `FP_OFF=1` | Lista los FPs compilados / fuerza el fallback |
| `PS3_FORCE_ALPHA_BLEND=1` | Sustituye los factores de blend por alfa estándar |
| `NV3089_GATE=<archivo>` | Registra los blits que tocan display |

## Estado del árbol

- `patches/0008-d3d12-display-target-diagnostics.patch` añade `RX_PASTE`,
  `RX_OPAQUE` y `RX_DISPDBG`. Es diagnóstico, no un arreglo: el pipeline lo
  reconoce como ya aplicado.
- Sin cambios pendientes en `bakugan-recomp/`.
- Evidencia local (ignorada por Git): `diagnostics/arena/manual12` (estado de los
  ops de display en batalla), `manual13` (prueba del mapeo alfa, sin efecto) y
  `manual14` (sin pegado: todo negro).
- El repositorio oficial es <https://github.com/Mauryln/Bakugan-Battle-Brawlers-PS3-recomp>.
  No se distribuyen binarios, lifts ni assets: todo se regenera desde el disco del
  usuario con `tools/pipeline.py`.
