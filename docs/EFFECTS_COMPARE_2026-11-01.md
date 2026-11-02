> Investigación y comandos de RenderDoc conservados. El usuario confirmó
> después los reflejos y las sombras oscuras corregidos; la prioridad vigente
> es [rendimiento](PERFORMANCE_2026-11-01.md). Retomar desde
> [NEXT_SESSION.md](NEXT_SESSION.md), no desde los pendientes históricos.
> El usuario aclaró que una captura del recomp quedó incompleta y que tomó
> varias de RPCS3 por accidente: no tratarlas como cuadros sincronizados.

# Comparación emulador vs recomp — 01/11/2026

Primera comparación sistemática draw a draw entre RPCS3 (referencia) y el
recomp, con las herramientas `tools/diff_trace.py` (parsers + alineación +
diff) y `tools/analyze_rt.py` (estructura numérica de RTs sin ver imágenes).

## Cómo se obtuvo la referencia del emulador

RPCS3 no registra el stream RSX por método a nivel Trace en la ejecución
normal (`RSX method` solo se emite para métodos inválidos). La vía que
funciona:

1. Jugar hasta el momento deseado en RPCS3 (en ventana; el menú no es
   accesible en fullscreen).
2. **Utilities → Create RSX Capture**: captura el siguiente frame al stream
   `captures/BLES00758_*.rrc.gz` (binario, con bloques de memoria de entrada).
3. **Tools → RSX Debugger**: su timer (~100 ms) escribe
   `command_dump.log` (en la raíz de RPCS3 portable, no en `cache/`), el
   stream del frame capturado pretty-printed: `NOMBRE: 0xarg` por línea.
4. Se recomienda `Disable Vertex Cache: true` durante la sesión de captura
   (restaurado después).

Los nombres/decodificaciones de RPCS3 usados (CONTROL0 enabled=bit31,
factores de blend, comparaciones) se verificaron contra el código fuente del
build exacto del usuario (commit `e53ac18f`, `rsx_decode.h`).

## Resultados

| Aspecto | Resultado |
|---|---|
| Draws por frame | 1.515 emulador, 1.728 recomp (frame_100) |
| Desplazamiento de heaps | +0x700001 en FPs (86–100% confianza); RTs/texturas también cambian de base por sesión |
| Pares alineados | 1.237 (por grupos fp+rt, en orden dentro del grupo) |
| **Estado por pase (alpha/blend/depth/cull/samplers)** | **Idéntico** entre emulador y recomp: los multiconjuntos por pase coinciden exactamente (tras corregir los nombres de RPCS3: `Greater_equal`/`Not_equal`, no `Gequal`/`Notequal`). Las variantes de estado que usa el recomp aparecen también en el emulador (en el mismo pase u otros), y viceversa: ninguna es inventada |
| Pases ausentes | Ninguno: mismos FPs y RTs en ambos lados |
| RT del agua (512×512) | Contenido coherente, estable entre sesiones (mean/std/edge idénticos a la sesión previa) |
| Diffs campo a campo | Ruido de emparejamiento intra-pase (objetos distintos en momentos distintos), no bugs — confirmado por los multiconjuntos |

Alcance: coincide el subconjunto de estado incluido en los parsers; esto no
valida todos los registros RSX. Después se descubrió SET_SHADER_WINDOW
ignorado, que sí afectaba al reflejo. Los síntomas visuales restantes (reflejo deformado, sombras)
viven más abajo: constantes del VP (cámara del pase de reflejo), traducción
FP ucode→HLSL del muestreo, o la arquitectura de profundidad (sombras).

## Herramientas nuevas

- `tools/diff_trace.py <command_dump.log> <manifest.txt>` — parsers de ambos
  formatos, descubrimiento del delta de sesión por rank-pairing de
  histogramas FP, mapeo exclusivo de RTs por solape + tamaño de viewport,
  emparejamiento por grupos (fp,rt), reporte de multiconjuntos por pase.
  `--selftest` valida contra el par de capturas actual.
- `tools/analyze_rt.py <png|bmp>...` — mean/std/negros/colores/bordes de un
  RT para distinguir contenido coherente de basura sin ver la imagen.

## RenderDoc: captura y volcado (listo y probado 01/11/2026)

RenderDoc 1.47 portable vive en `tools\renderdoc\` (git-ignored, 203 MB). No
necesita admin: la capa de Vulkan se activa al lanzar el proceso desde
`renderdoccmd`, así que RPCS3 se captura sin registrar nada en el sistema.

Capturar (F12 dispara la captura; se sale del juego para cerrar el proceso):

```powershell
.\tools\rdoc_capture.ps1 -Tag aquos-ref -Target rpcs3   # emulador (Vulkan)
.\tools\rdoc_capture.ps1 -Tag aquos-recomp              # recomp (D3D12)
```

Los `.rdc` quedan en `diagnostics\arena\renderdoc\<Tag>\`. El de RPCS3 pesa
decenas de MB: es normal.

Volcar un `.rdc` a datos (sin GUI): `index.json` con RTs, texturas y bloques de
constantes por draw, más `tex\<id>.png` y `cb\<evento>_<stage>_<i>.bin`:

```powershell
.\tools\rdoc_dump.ps1 -Capture diagnostics\arena\renderdoc\aquos-ref\rpcs3__frame137.rdc
```

Notas:

- `qrenderdoc.exe` abre un diálogo de "Analytics" la primera vez que se usa la
  GUI; los scripts lo evitan matando el proceso al terminar el volcado. Si
  abres la GUI a mano, responde una vez y no vuelve a salir.
- El script de volcado corre dentro de qrenderdoc (`--python`), que ejecuta
  sin `__file__`: rutas por entorno (`RDOC_CAPTURE`, `RDOC_OUT`).
- Verificado end-to-end con las dos APIs: frame 552 del recomp (D3D12, 2 draws
  del arranque) y frame 137 de RPCS3 (Vulkan, 50 draws).
- En RPCS3 se activó `Renderdoc Compatibility Mode` (backup:
  `config.yml.bak-renderdoc` en su carpeta `config\`).

## Límites conocidos

- El dump del emulador solo incluye el **primer float** (y a veces el índice
  4) de cada `Transform constant load`: no permite diffs de constantes
  completos. Pendiente: RenderDoc en RPCS3 o extraer constantes del
  `.rrc.gz` (bloques de memoria de entrada; el formato de serialización está
  en `Emu/RSX/Capture/rsx_capture.cpp` y `Utils/rsx_serde.hpp`).
- El `.rrc.gz` guarda entradas (texturas, shaders, índices), no el contenido
  de los RTs tras renderizar; para RTs del emulador hace falta RenderDoc o el
  visor del debugger.
- Los conteos de draws por pase difieren entre capturas de momentos
  distintos; no confundir con draws faltantes. Un frame del MISMO momento en
  ambos lados sigue siendo útil pero no imprescindible para estado.

## Resultado RenderDoc: el mapa de reflejo de Aquos (2026-11-01)

Capturas usadas: emulador `rpcs3__frame3242.rdc` (538 draws, Vulkan) y recomp
`recomp__frame4287.rdc` (1721 draws, D3D12). Momentos distintos, así que solo
se comparan propiedades estructurales, no draws uno a uno.

Estructura de pases (RT de destino, draws, eventos):

| Pase | Emulador | Recomp |
| --- | --- | --- |
| reflejo 512x512 | `38776` 88 draws (181-593) | `7976` 350 draws (28409-31559) |
| 512x512 espejado 2 | `38850` 4 draws (634-648) | `7978` 11 draws (31576-31667) |
| copia/escala 512x512 | `37792` 12 draws (692-748) | `7980` 44 draws (31683-32071) |
| escena 1120x584 | `31107` 302 draws (760-2272) | `462` 1285 draws (32088-43677) |
| present | `31725` 1653x904 | `283` 1280x720 |

Lo que **sí** coincide (constantes de vértice, offsets c0..c3 y c16..c31, el
banco del juego está al principio en las dos APIs):

- Pase de reflejo con cámara espejada en ambos: fila Y de la vista con signo
  opuesto (`-1.0`) y ojo reflejado sobre el mismo plano de agua
  (y = -2.9985 en el emulador; el recomp refleja -4.236 a -4.636, mismo plano).
- Misma proyección en los dos pases y en ambos motores: 1.3403 / 2.1445.
- Mismo viewport por pase: `[0,0,512,512]` y `[0,0,1120,584]`, sin altura
  negativa.

Lo que **no** coincide, medido dentro de cada captura (correlación de bandas
del RT de reflejo contra el RT de escena del mismo frame):

| | corr(reflejo, escena) | corr(reflejo, escena volteada) |
| --- | --- | --- |
| Emulador | -0.69 | +0.47 |
| Recomp | +0.29 | -0.10 |

Es decir: en el emulador el mapa de reflejo es la escena vista al revés (lo
esperado de una cámara espejada); en el recomp no lo es, aunque la matriz de
vista sí está espejada. Confirmado a ojo por el usuario sobre
`diagnostics/arena/renderdoc/compare-aquos/` (1..4, mismos RTs).

Conclusión provisional: el problema está entre la matriz espejada y lo que
llega a rasterizarse (o en cómo se muestrea ese RT), no en la matemática del
espejo. Falta un frame del MISMO momento en ambos lados para atribuirlo:
comparar RTs de capturas distintas no da correlación útil (control
escena-vs-escena entre capturas: 0.10, ruido).

Herramienta: `tools/rdoc_dump.py` en modo `RDOC_SAVE_EVENTS` fallaba en Vulkan
porque RPCS3 declara slots de RT nulos; ahora los salta.

## Comandos (copiar y pegar)

Instalación: RenderDoc 1.47 portable en `tools\renderdoc\` (203 MB, ignorado
por git junto con `*.rdc`). No hace falta registrar la capa ni ejecutar como
administrador.

```powershell
# 1. Capturar. Lanza el juego bajo RenderDoc; pulsa F12 UNA vez cuando el
#    efecto esté en pantalla y sal normalmente para que se vuelque el .rdc.
#    El recomp tarda 30-90 s congelado mientras escribe la captura.
.\tools\rdoc_capture.ps1 -Target recomp -Tag aquos-recomp
.\tools\rdoc_capture.ps1 -Target rpcs3  -Tag aquos-ref

# 2. Volcado completo (index.json + RTs + texturas + constantes por draw).
#    ~10-20 min en capturas grandes: un SetFrameEvent por draw.
.\tools\rdoc_dump.ps1 -Capture diagnostics\arena\renderdoc\aquos-recomp\recomp__frame4287.rdc

# 3. Volcado dirigido: solo los RTs activos en esos eventos (segundos).
#    Los eventos son el ÚLTIMO draw de cada pase; se sacan con el comando de
#    estructura de pases de más abajo.
$env:RDOC_SAVE_EVENTS = "31559,31667,32071,43677,43808"
.\tools\rdoc_dump.ps1 -Capture diagnostics\arena\renderdoc\aquos-recomp\recomp__frame4287.rdc `
                      -OutDir diagnostics\arena\renderdoc\aquos-recomp\rt-targeted

# 4. Estadística numérica de un RT (sin mirar la imagen).
.\.venv\Scripts\python.exe -W ignore tools\analyze_rt.py <png> [<png>...]
```

Estructura de pases de un volcado (RT, draws, primer y último evento):

```powershell
.\.venv\Scripts\python.exe -W ignore -c "import json,collections; j=json.load(open(r'<dir>\index.json',encoding='utf-8'))
g=collections.OrderedDict()
for d in j['draws']:
    if d['rts']: g.setdefault(d['rts'][0]['id']+' %dx%d'%(d['rts'][0]['width'],d['rts'][0]['height']),[]).append(d['event'])
for k,v in g.items(): print(k,len(v),min(v),max(v))"
```

Constantes de vértice de un draw (el banco del juego está al principio en
ambas APIs; la proyección se reconoce por `c16 = 1.3403`, `c21 = 2.1445`):

```powershell
.\.venv\Scripts\python.exe -W ignore -c "import json,struct,os; d=r'<dir>'; j=json.load(open(os.path.join(d,'index.json'),encoding='utf-8'))
x=[y for y in j['draws'] if y['event']==<ev>][0]
print([(c['file'],[round(f,4) for f in struct.unpack_from('<16f',open(os.path.join(d,'cb',c['file']),'rb').read(),0)]) for c in x['constant_blocks'] if c['stage']=='ShaderStage.Vertex'])"
```

Correlación de bandas reflejo-vs-escena del mismo frame (la métrica que
delató el problema; valores negativos = el RT es la escena al revés):

```powershell
.\.venv\Scripts\python.exe -W ignore -c "from PIL import Image
def bands(p,n=16):
    im=Image.open(p).convert('L').resize((32,n)); px=im.load()
    r=[sum(px[x,y] for x in range(32))/32 for y in range(n)]; m=sum(r)/len(r) or 1
    return [v/m for v in r]
def corr(a,b):
    ma=sum(a)/len(a); mb=sum(b)/len(b); n=sum((x-ma)*(y-mb) for x,y in zip(a,b))
    da=sum((x-ma)**2 for x in a)**.5; db=sum((y-mb)**2 for y in b)**.5; return n/(da*db)
a=bands(r'<refl.png>'); b=bands(r'<main.png>')
print('as-is %.2f  flip %.2f' % (corr(a,b), corr(a,b[::-1])))"
```

Los PNG de la comparación de Aquos están en
`diagnostics\arena\renderdoc\compare-aquos\` (1-emulador-reflejo,
2-recomp-reflejo, 3-emulador-escena, 4-recomp-escena).

## Registro de la sesión 2026-11-01 (RenderDoc)

1. RenderDoc 1.47 portable en `tools\renderdoc\` y dos wrappers:
   `rdoc_capture.ps1` (F12 sobre recomp D3D12 o RPCS3 Vulkan) y
   `rdoc_dump.ps1` + `rdoc_dump.py` (volcado headless a JSON/PNG/constantes).
   `qrenderdoc --python` es el único host que expone el módulo `renderdoc`; el
   diálogo de "Analytics" del primer arranque se evita matando el proceso en
   cuanto el script escribe `done.txt`.
2. Bugs reales corregidos en el volcador: `GetStructuredFile()` por draw hacía
   el volcado cuadrático; `ResourceId::282` rompía los nombres de PNG (los dos
   puntos son ilegales en Windows y el guardado fallaba en silencio); en D3D12
   los SRV son `Image`, no `ImageSampler`; en Vulkan RPCS3 declara slots de RT
   nulos y el modo dirigido abortaba con `ResourceId::0`.
3. Capturas del usuario: recomp `aquos-recomp\recomp__frame4287.rdc` (288 MB)
   y cinco de RPCS3 en `aquos-ref\` (frames 3096, 3163, 3242, 3313, 3396).
4. Volcados: recomp frame 4287 (1721 draws, 136 texturas) y RPCS3 frame 3242
   (538 draws, 148 texturas). Estructura de pases de las dos (tabla de arriba).
5. Constantes: la cámara del pase de reflejo está espejada en ambos motores,
   sobre el mismo plano de agua, con la misma proyección y el mismo viewport.
6. Contenido: el RT de reflejo del emulador es la escena al revés (-0.69);
   el del recomp no (+0.29), confirmado a ojo por el usuario.
7. Descartado por ruido: comparar RTs de capturas de momentos distintos no
   correlaciona (control escena-vs-escena entre capturas: 0.10), y emparejar
   draws uno a uno es imposible porque el recomp emite ~4x más draws por pase
   (escena: 1285 vs 302; reflejo: 350 vs 88).
8. Aparcado: el volcado de assets de Xbox 360 (`bakugan-nx\`) — `media\movie\*.pam`
   son vídeos y `material_vp/fp.par` es microcódigo Xenos, que no sirve para
   traducir ucode RSX. Solo `media\d00.arc` tendría datos de material
   utilizables.

## Pendientes actualizados

El reflejo se corrigió mediante WPOS y las sombras ya tienen profundidad
nativa. Continúan rendimiento, destello negro ocasional, otras arenas y
validación larga. Los pasos antiguos de comparación sincronizada quedan
como técnica de diagnóstico, no como bloqueo para continuar. El plan actual
está en [NEXT_SESSION.md](NEXT_SESSION.md).

## Continuación: WPOS y SET_SHADER_WINDOW

Se identificó un estado que la comparación anterior no cubría:
`SET_SHADER_WINDOW` (0x1D88). El recomp ignoraba ese método y traducía
WPOS directamente a `SV_Position`. El dump de RPCS3 pide origen inferior
y centro medio; sus constantes de contexto incluyen escala Y -1 y sesgo
584 para la escena de Aquos. El shader del agua usa WPOS.xy y calcula
`1 - y/584`: entregarle Y desde arriba cambia el muestreo del reflejo.

`0038-d3d12-shader-window-coordinates.patch` registra el método, conserva
altura/origen/centro por draw en las componentes libres de b1 y adapta solo
los operandos WPOS. Convierte también WPOS.w a 1/W, sin modificar la ruta
independiente de corrección de perspectiva de texcoords. No cambia matrices,
UVs de los vértices ni invierte globalmente los render targets.

La prueba GPU `tools/test_rsx_window.py` falla con el backend anterior y pasa
con el cambio: ejecuta el FP traducido con los cuatro modos de origen/centro,
una altura invitada distinta de la del target, Z y W no triviales. El cambio
de origen explica un fallo de muestreo verificable; las correlaciones entre
bandas no demuestran por sí solas que el mapa de reflejo carezca de espejo.
El PNG del recomp contiene geometría invertida y no debe clasificarse como
mapa sin espejar solo por esa métrica.

Build inicial correcta y 21/21 pruebas pasan. La corrida
`diagnostics/arena/aquos-window-20261101/frame_100` conserva agua/cascadas y
no registra fallos de PSO; el usuario confirmó que esta build corrigió los reflejos. La build inicial usa la corrección XY; la versión final agrega
la conversión WPOS.w verificada por la prueba GPU. Sombras aún pendientes.

Fuentes del build exacto de RPCS3:
[decodificación de SHADER_WINDOW](https://github.com/RPCS3/rpcs3/blob/e53ac18f/rpcs3/Emu/RSX/rsx_decode.h),
[preparación de WPOS](https://github.com/RPCS3/rpcs3/blob/e53ac18f/rpcs3/Emu/RSX/Core/RSXDrawCommands.cpp),
[consumo en el shader](https://github.com/RPCS3/rpcs3/blob/e53ac18f/rpcs3/Emu/RSX/Program/GLSLSnippets/RSXProg/RSXFragmentPrologue.glsl).

### Sombras: continuación de la prueba

Se restableció el soporte nativo de forma acotada a las superficies D24 que
algún draw realmente muestrea. La profundidad principal conserva la ruta
anterior. El mapa 0x0F100000 de Aquos se escribe como DSV y se muestrea como
SRV; los pases sin color usan cero render targets y un viewport del tamaño
real del mapa. Las capturas usan `GetCopyableFootprints`, incluida su
representación del plano de profundidad, en lugar de inventar un footprint.
La captura `aquos-shadow-map-20261101/frame_100` contiene la silueta del
personaje y termina sin errores de dispositivo.

Se encontró además una inversión de operandos en el helper de comparación:
ZFUNC del RSX compara profundidad almacenada contra referencia. RPCS3 invierte
el comparador al crear el sampler Vulkan (`VKDraw.cpp`,
`get_compare_func(tex.zfunc(), true)`). El helper evaluaba referencia contra
profundidad; se corrigió y la prueba GPU verifica el caso GREATER con
profundidades .25/.75 y referencia .5. El caso falla con el helper original.
La comparación visual de sombra en el suelo sigue en curso.

La captura del usuario `battle-arena-picture/bugs/Screenshot 2026-11-01 151718.png`
muestra cielo negro conservando HUD, personaje, escenario y partículas. Las
40 capturas espaciadas de `aquos-window-20261101` no aislaron ese instante;
no se afirma resuelto el parpadeo. La primera build solo corregía WPOS.xy;
la build actual convierte también WPOS.w según la convención RSX.

### Validación del usuario y separación completa de profundidad

El usuario confirmó la sombra del color correcto en la build del comparador
corregido. Persisten parpadeos; las imágenes `152227` y `152337` de la carpeta
`battle-arena-picture/bugs` muestran además geometría ausente mientras
foliage/partículas/HUD siguen presentes. En la siguiente build se separan
todas las superficies D24 por dirección y tamaño, conservando clears del
invitado en orden y evitando limpiar profundidad por un simple cambio de RT
de color. Corrida: `aquos-depth-isolated-20261101`; validación visual pendiente.

Pruebas: 22/22 tras la corrección del comparador (incluidas GPU WPOS y
comparación de sombras). La separación por superficie tiene prueba D3D12
con dos buffers independientes, clears espaciales no uniformes y readback
mediante footprints reales; prueba de límites de cuadros conserva 241 flips
con clears ordenados. Los 38 parches aplican a HEAD limpio del toolkit.
Quedan únicamente diagnósticos locales anteriores VTX_COL y conteo de draws
indexados fuera de la serie de parches.

### Causa capturada de los destellos completos y cuadros incompletos

`aquos-depth-isolated-20261101/frame_130` tiene backbuffer completamente
negro y exactamente 2048 operaciones, frente a 1637/1635 en sus vecinos
129/131. Sus pases de reflejo 512x512 tienen 702 y 22 draws, exactamente
el doble de 351 y 11 en los vecinos: se concatenó trabajo del cuadro siguiente
y el límite descartó su composición final. No era un fallo del mapa de sombras.

El walker permite seguir más allá de un flip cuando hay >=64 KiB pendientes
(`GCM_FIFO_CATCHUP_BYTES`), política de catch-up del toolkit que mezcla
cuadros en esta arquitectura. `0040` añade `GCM_FULL_FRAMES`: el port lo
activa por defecto y se detiene en cada marcador aunque haya backlog. Otros
usuarios del toolkit conservan su política anterior si no activan la opción.

Además, la rama post-drain del reloj presentaba cuando cambiaba el contador
de flips sin consumir `s_flip_pending`. La siguiente comprobación pre-drain
podía presentar de nuevo el mismo flip, con un lote vacío. Se consume la
notificación también en esa rama. La prueba reproduce por separado el salto
de frontera y la presentación duplicada; ambas fallan antes y pasan después.

La reserva de draws pasa de 2048 a 8192 para no truncar cuadros cargados.
El b0 de doble paridad reserva ahora 132 MiB (antes 33 MiB); los heaps de
samplers se crean por demanda. La prueba GPU recorre las 8192 tablas y
verifica unidad 15, conservación de descriptores y cambios de heap. Otra
prueba conserva clears y presentación única en un cuadro de 4096 operaciones.
No se afirma que ampliar capacidad por sí solo arregle la mezcla de cuadros.

Corrida de comprobación: `aquos-full-frames-20261101`, con reflejos y sombras
conservados. Verificación visual del usuario todavía pendiente. La serie
de 39 parches aplica a HEAD limpio; los únicos diffs locales son los dos
diagnósticos históricos mencionados arriba.

## Cierre y limitaciones

Última prueba del usuario: tras 0040 vio un destello negro aislado luego de
un rato. No marcar los parpadeos como completamente resueltos. El siguiente
perfil y su prototipo están en PERFORMANCE_2026-11-01.md; no se integró el
stream compacto. Los volcados de constantes cachean rangos por recurso:
si una captura actualiza ese mismo rango entre draws, habrá que revisar ese
cache antes de interpretar una aparente igualdad de constantes.

El wrapper rdoc_dump.ps1 conserva directorios de salida existentes y elige
un sufijo nuevo en lugar de borrarlos; imprime la ruta final al terminar.
