> **Estado vigente al 30/10/2026:** continuar con [NEXT_SESSION_BATTLE_ARENA.md](NEXT_SESSION_BATTLE_ARENA.md). Gameplay, HUD y Sixaxis corregidos; efectos/ruido/FPS siguen pendientes. Las notas anteriores se conservan debajo.

Lo que quedó arreglado (commiteado)
Parche	Fix
0009	Deformación 3D — los strips de la arena cortan con 0x7FFF; ya no se lee como vértice.
0010	Cinemáticas negras (RT_REBIND fijo) + congelamientos (caché de shaders en shadercache/).
Build verde, pipeline reconoce los 10 parches, repo limpio. Todo en patches/.
Lo que NO resolví (para mañana)
1. Negro del gameplay (turno del jugador) — la escena 3D sí dibuja a 0xEAB0000 (62% de píxeles), el quad de composición sí se dibuja con alpha=1, pero el backbuffer sale negro. Causa exacta aún sin confirmar.
2. Texturas granuladas (mipmaps/sampling).
3. Caras invisibles (alpha/transparencia).
Dónde está todo
- docs/NEXT_SESSION.md → informe completo: lo arreglado, lo revertido (el alias 1:1 que rompía el HUD), los diagnósticos que dejé, y la prueba concreta para mañana (un comando RSX_CAP+RX_DISPDBG+VP_DUMP listo para copiar/pegar y llegar al turno del jugador).
- En ps3recomp-src quedan sin commitear dos diagnósticos útiles (VTX_COL para ver el alpha del composite, y el conteo de superficie indexada). Están documentados en el informe.
Lo que puedes hacer tú manualmente mañana
Ejecuta el comando del apartado "Prueba concreta" del informe, llega al turno del jugador, muévete 4-5 s y cierra. Con el manifest.txt + [DISPDRAW] sabremos si el composite se dibuja (t0=0xDF60000) y con qué estado, que es la pieza que falta.
No verifiqué: no llegué a confirmar visualmente si la escena 3D del gameplay está fuera de pantalla (cámara/VP) o es un problema del composite — la captura que hice cayó en un frame de transición sin el quad de composición. Riesgo: los dos diagnósticos sin commitear en ps3recomp-src se perderían si haces git checkout . ahí; si los quieres conservar, pídeme que los meta en un parche.