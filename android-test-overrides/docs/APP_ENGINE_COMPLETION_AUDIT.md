# ODPAR — Auditoría de paridad Motor ↔ App

Estado: 2026-08-18. Este documento separa capacidad real, integración pendiente y
dirección de producto. Su propósito es impedir que una interfaz aparente una
capacidad que el estado autoritativo del motor todavía no puede conservar.

## Regla de producto

"Usar todo el motor" no significa enlazar cada función C al proceso de UI. La app
debe alcanzar todas las capacidades mediante fronteras apropiadas: edición y
preview interactivos en móvil; análisis, master y export pesado como jobs
reproducibles; un único proyecto persistente como fuente de verdad.

## Estado verificable

El motor registra 194 módulos y tres fronteras app-facing separadas:

- `APP_AUTHORING3D`: sesión de un modelo para importación OBJ, malla editable,
  cámara de autoría, material PBR-lite, nodos de superficie, manipulación y raster.
- `PROJECT_SESSION`: apertura verificada, catálogo, leases, lectura por blobs y
  guardado exacto/firmado; aún sin adopción de ediciones ni historial.
- `odm_ffi`: ABI mínimo de preview/jobs/diagnóstico legado.

La app Android de esta entrega es un workbench real, no la app total: expone el
compositor Layered y `APP_AUTHORING3D`. No debe describirse como fachada de los
194 módulos hasta completar las uniones siguientes.

## Matriz de paridad

| Dominio | Motor real | Falta para la app completa |
| --- | --- | --- |
| Proyecto | ODPARMS, Bundle, Links, Root, Derivation, Transaction, ProjectSession | Slice B: adopción, revisiones, undo/redo, autosave y creación mutable |
| Composición 2D | Background/Core/Field/HUD, estilos, reacciones y Overlay2D draw plan | Raster genérico de shapes/imágenes/texto y autoridad Presentation única |
| Timeline | Tiempo 48 kHz, waveform, snap, regiones, lanes, automation y mapping | Transport, edición de clips/tracks y binding Android whole-project |
| Música | Music Map determinista, 96 lanes de reacción, eventos y procedencia | Importación→análisis→timeline→preview desde el proyecto abierto |
| Media | WAV/PCM/PNG, facts, límites, resample, PCM Store y adaptador FFmpeg | Reproducción Android, mixer, video runtime y adopción transaccional de assets |
| 3D | Scene3D, PBR-lite, texturas, luces, sombras, cámara, mesh authoring, paths, nodes, physics | Escena multiobjeto de proyecto, cámaras/luces/physics/reaction/timeline y bindings completos |
| Preview | Reference renderer, RenderIR preview, AppAuthoring3D raster y presupuestos | Preview único por sample que combine 2D + 3D + música + timeline |
| Export | Master, receipts, export Layered, Delivery y FFmpeg sin shell | Compilador Presentation→Master y job iniciado desde el mismo proyecto |
| UI | Schema de 13 familias y catálogos tipados | Inspectores generados por schema, outliner global, selección y transacciones |

## ODPAR: Texture Editor

La visión del Texture Editor es consistente y suficientemente diferenciada. Su
invariante central debe ser `2D ↔ surface binding ↔ 3D`, nunca posición de
pantalla. `SurfacePatch` es el agregado correcto: fuente, máscara, proyección,
región ligada, canales, mezcla, orden y metadatos permanecen editables; bake es
una compilación, no la destrucción del proyecto.

Ya existen bases reutilizables:

- `MeshSurfacePoint3D` y `MeshSurfacePath3D` con IDs y baricéntricas;
- AuthoringNode3D con roles `SURFACE_ANCHOR`, `MASK_ANCHOR` y
  `PROJECTION_CORNER`;
- manipulación directa sobre superficie y Camera Follow;
- texturas verificadas por SHA, UV perspective-correct, PBR-lite y slots;
- Bundle/Transaction y MeshAuthoringProgram para publicación transaccional.

Brechas que no deben simularse en UI:

1. MeshAuthoring no conserva UV/normales importadas; hoy compila `u=v=0`.
2. No existe el agregado persistente `SurfacePatch` ni una región/interior de patch.
3. No existen `AuthoringSheet`/`.odtex`, canvas 2D persistente ni reproyección segura.
4. No existe stack de layers/masks/channel targets de textura ni raster de máscara.
5. `APP_AUTHORING3D` todavía no publica una textura como asset del modelo.
6. No hay wire/codecs para patch, sheet, capas, masks, node graph y surface paths.
7. ProjectSession aún no puede adoptar estos cambios ni producir undo/redo.

Orden de implementación Texture Editor:

1. Atributos UV/normales persistentes y reglas de invalidación/remapeo.
2. `SurfacePatch` + `AuthoringSheet` + `.odtex` versionados e íntegros.
3. Compositor no destructivo de capas/máscaras/canales.
4. Preview/render de patches en Scene3D y `APP_AUTHORING3D`.
5. Registros wire, Bundle Links y transacciones.
6. ProjectSession history/adoption y undo/redo semántico.
7. UI 2D/3D compartiendo las mismas identidades, seguido de bake/export.

## Secuencia para paridad total

1. Terminar ProjectSession Slice B.
2. Crear una fachada móvil `AppProjectSession` para new/open/save/import,
   transaction, undo/redo, preview y export.
3. Compilar Presentation a runtime 2D/3D y luego al mismo Master.
4. Unificar importación/materialización de audio, imagen, video, font, mesh y texture.
5. Añadir transport Android y timeline conectado a Music Map.
6. Generar inspectores y herramientas desde el schema del motor.
7. Integrar Texture Editor según el orden anterior.
8. Ejecutar export headless y delivery como jobs verificables.

Hasta cerrar esos pasos, cada entrega de app debe declarar con precisión qué
frontera real usa. La amplitud visual nunca sustituye autoridad, persistencia,
undo, determinismo o exportación reproducible.
