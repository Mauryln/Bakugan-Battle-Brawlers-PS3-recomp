# Estado del port — Bakugan: Battle Brawlers (BLES00758)

Actualizado el 01/11/2026. La guía única para retomar trabajo es
[NEXT_SESSION.md](NEXT_SESSION.md). Build principal Windows x64 / D3D12:
`bakugan-recomp/build/BakuganRecomp.exe`.

## Confirmado

- Arranque, menús, películas mediante Media Foundation, teclado / XInput.
- Battle Arena jugable: turnos del jugador y CPU, campo y cartas corregidos.
- Aquos: cascadas y lluvia, reflejos corregidos con WPOS y sombras oscuras
  mediante mapas D24 nativos y comparación correcta. Confirmado por el usuario.
- Fronteras de cuadros, notificación de flip y capacidad corregidas tras
  capturar un cuadro truncado; no se omiten efectos ni trabajo SPU.
- Stream GPU compacto integrado (0042), con fallback completo.
- Reciclaje FIFO con dueño único (0043): el usuario confirmó menú sin atasco,
  audio de menús sin ruido y ausencia de pantallazos en la prueba de Aquos.
- Música PCM / mezclador SPU y audio de películas disponibles; no se da por
  cerrado el audio ATRAC3 / efectos .PAST.

## Prioridades pendientes

1. **30 FPS sostenidos.** Aquos ronda 12 FPS en el perfil de gameplay; el
   usuario reporta <=15 FPS. Hay tirones a 4–5 FPS desde loading, audio antes
   que imagen en presentaciones y ruido de audio en batalla. El upload GPU compacto quedó en 384 MiB
   en la corrida medida, frente a 1536 MiB anteriores.
2. **Estabilidad prolongada.** El usuario ya no vio pantallazos tras 0043;
   comprobar que no reaparezcan después de jugar más tiempo.
3. Validar Pyrus, Haos, Subterra, Ventus, tutorial e historia y sesiones largas.
4. Verificar sesiones largas de menús tras corregir el doble reciclaje FIFO (0043).
5. Audio ATRAC3 / efectos sonoros y defectos restantes del selector de modo.

## Informes vigentes

- [Rendimiento e integración compacta](PERFORMANCE_2026-11-01.md).
- [Correcciones de reflejos, sombras y cuadros](EFFECTS_FIX_2026-11-01.md).
- [Comparación y herramientas RenderDoc](EFFECTS_COMPARE_2026-11-01.md).
- [Arquitectura](ARCHITECTURE.md), [vídeo](VIDEO.md).

Los informes BATTLE_ARENA_FIXED, EFFECTS_AQUOS y PERFORMANCE_2026-10-31 se
conservan como evidencia histórica, no como lista vigente de pendientes.
La serie tiene 46 parches (0001–0047, sin 0023). La validación incluye Release,
25 pruebas de render/FIFO/pipeline y dos de audio/SPU, más aplicación
sobre toolkit limpio. El stream compacto ya forma parte de la build; conserva RAM canónica para respaldo y capturas.

## Construir y ejecutar

```powershell
.\.venv\Scripts\python.exe tools\pipeline.py
.\.venv\Scripts\python.exe tools\build.py
.\tools\run.ps1
```

Usar una copia propia del disco. Assets, binarios del juego, lifts, capturas
y salidas de build no se versionan. Las correcciones del toolkit se conservan
en patches; no editar código generado para solucionar bugs.
