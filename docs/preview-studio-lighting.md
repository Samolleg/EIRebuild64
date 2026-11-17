# Interface preview studio lighting

Camp outfitting, character-panel/info previews and portrait previews use one camera-relative policy in `model_preview_lighting.h`. The embedded x64 host calls it from `Game/OwnedRender/ModelPreview.inl`; body, clothing and weapon groups receive the same light. Standalone head figures opt into the existing preview draw scope; ordinary item icons and world draws retain their policy. Submission uses the existing per-figure lighting scope.

The native diffuse formula has an ambient **floor**: `max(ambient, key * max(dot(normal, direction), 0))`. Ambient is neutral 0.30; the single key is RGB (1, 0.98, 0.95), 35 degrees above and 30 degrees toward screen left. The camera basis comes from the complete clip and figure centre, accounting for perspective/orthographic projection, view rotation and HUD placement. Portraits use the same policy rather than a separate frontal light. No time-of-day input, new setting, additional light/shadow pass, shader or GPU ABI change.

`tools/check-preview-lighting.ps1 -OutputDirectory <task/check>` exercises production direction math across projection, rolled/rotated views, positive clip scaling, HUD scaling/translation, mirrored views and invalid matrices. It also verifies the native light-floor response on front/left/right/top/bottom/back-facing surfaces. `check-model-preview.ps1` covers existing glTF/FIG preview material and submission behaviour.

Changes to this policy still require appearance review in the game: numerical camera checks and offline diffuse illustrations are not evidence of the full renderer's final image or FPS.

The camp formerly forced ambient to 1.0 through ModelPreviewPresentation.full_ambient. Character previews now omit that override and world point-light traversal. Their FIG default colors become lit material tints (gray/alpha retained), rather than bypassing lighting. The legacy non-embedded engine_bridge studio is not the active x64 path and is unchanged.


X64GLTF13 separates decorative UI models from characters with ModelDrawDomain::interface_decoration, set explicitly by CI3DMapObjTranslation for ESC, options and save/load. Both domains retain private preview poses and exclude world picking/retained-world submission. Only the character preview domain uses studio lighting, including standalone heads. Nested scopes restore the previous domain.

Decorations restore the policy before X64GLTF12: white full ambient, the existing sun and admitted nearby world point lights, and FIG default colors without conversion to studio-lit tints. No model-name, CUnit-type or object-ID heuristic is involved. Character lighting coefficients, direction and point-light exclusion remain unchanged; no shader or extra rendering pass is added. check-model-preview.ps1 covers this common adapter policy, FIG colors, scope restoration and world/picking isolation. The three screen call sites are inspected separately; these checks do not execute the live UI.
