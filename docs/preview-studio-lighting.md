# Interface preview studio lighting

Camp outfitting, character-panel/info previews and portrait previews use one camera-relative policy in `model_preview_lighting.h`. Both direct and queued bridge paths call it; body, clothing and weapon groups receive the same light. The existing preview transaction restores world lighting afterwards.

The native diffuse formula has an ambient **floor**: `max(ambient, key * max(dot(normal, direction), 0))`. Ambient is neutral 0.30; the single key is RGB (1, 0.98, 0.95), 35 degrees above and 30 degrees toward screen left. The camera basis comes from the complete clip and figure centre, accounting for perspective/orthographic projection, view rotation and HUD placement. Portraits use the same policy rather than a separate frontal light. No time-of-day input, new setting, additional light/shadow pass, shader or GPU ABI change.

`tools/check-preview-lighting.ps1 -OutputDirectory <task/check>` exercises production direction math across projection, rolled/rotated views, positive clip scaling, HUD scaling/translation, mirrored views and invalid matrices. It also verifies the native light-floor response on front/left/right/top/bottom/back-facing surfaces. `check-model-preview.ps1` covers existing glTF/FIG preview material and submission behaviour.

Changes to this policy still require appearance review in the game: numerical camera checks and offline diffuse illustrations are not evidence of the full renderer's final image or FPS.
