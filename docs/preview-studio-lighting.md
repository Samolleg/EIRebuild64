# Interface preview studio lighting

Camp outfitting, character-panel/info previews and portrait previews use one camera-relative policy in `model_preview_lighting.h`. The embedded x64 host calls it from `Game/OwnedRender/ModelPreview.inl`; body, clothing and weapon groups receive the same light. Standalone head figures opt into the existing preview draw scope; ordinary item icons and world draws retain their policy. Submission uses the existing per-figure lighting scope.

The native diffuse formula has an ambient **floor**: `max(ambient, key * max(dot(normal, direction), 0))`. Ambient is neutral 0.30; the single key is RGB (1, 0.98, 0.95), 35 degrees above and 30 degrees toward screen left. The camera basis comes from the complete clip and figure centre, accounting for perspective/orthographic projection, view rotation and HUD placement. Portraits use the same policy rather than a separate frontal light. No time-of-day input, new setting, additional light/shadow pass, shader or GPU ABI change.

`tools/check-preview-lighting.ps1 -OutputDirectory <task/check>` exercises production direction math across projection, rolled/rotated views, positive clip scaling, HUD scaling/translation, mirrored views and invalid matrices. It also verifies the native light-floor response on front/left/right/top/bottom/back-facing surfaces. `check-model-preview.ps1` covers existing glTF/FIG preview material and submission behaviour.

Changes to this policy still require appearance review in the game: numerical camera checks and offline diffuse illustrations are not evidence of the full renderer's final image or FPS.

The camp formerly forced ambient to 1.0 through ModelPreviewPresentation.full_ambient. That override and the world point-light manager traversal have been removed from the x64 model-preview path. FIG default colors become lit material tints (gray/alpha retained), rather than bypassing lighting. The legacy non-embedded engine_bridge studio is not the active x64 path and is unchanged.
