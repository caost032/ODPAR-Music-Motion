# ODPAR Authoring Studio — APK de prueba

Esta entrega es un workbench móvil conectado a dos fronteras reales del motor:

- el compositor 2D `Background → Core → Field → HUD`;
- `APP_AUTHORING3D` con importación OBJ, malla editable, material PBR-lite,
  cámara, nodos de superficie, manipulación y raster CPU Scene3D.

La interfaz se recompone para vertical y horizontal y serializa todas las
operaciones nativas en un único executor. No presenta como terminadas las
capacidades que aún no tienen autoridad persistente en el motor.

## Obtener la APK

1. Sube el repositorio a GitHub.
2. Abre **Actions → Android test APK → Run workflow**.
3. Descarga el artefacto `odpar-authoring-studio-apk` e instala
   `app-debug.apk`.

## Lo que se puede probar

- **Core 2D:** fondo, forma central, campo, título/artista, imagen importada,
  intensidad visual y scrub de timeline de presentación.
- **Cámara:** órbita, pan, dolly, enfoque y calidad 1×–4× con presupuesto.
- **Objeto:** importar OBJ, mover/escalar el objeto y transformar su geometría
  alrededor del centro de bounds.
- **Superficie:** presets PBR-lite, roughness y metallic.
- **Nodos:** crear/conectar anchors de superficie, seleccionarlos y ver su
  proyección sobre el viewport.

La pantalla abre un cubo incorporado para comprobar el render sin archivos. El
selector de documentos permite abrir OBJ e imágenes sin permisos amplios de
almacenamiento.

La matriz exacta de capacidades integradas y pendientes está en
`docs/APP_ENGINE_COMPLETION_AUDIT.md`.
