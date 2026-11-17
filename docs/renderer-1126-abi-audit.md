# Аудит CPU/HLSL после renderer 1126

Проверено 17.11.2026: rebuild64 `e8b65173d904369460178d7b27eca14827ba206a`, включая RTAO1126 (`77f3f1f9`) и RSHADOW1126. Авторский 1126: `6bdebfa9be990b717b8dc667bbbb9405494f6be4` в отдельном checkout RENDERREVIEW01.

**Новых функциональных несовпадений в проверенных контрактах не найдено. Runtime-код не изменён.**

## Метод и охват

- Опись выполнена по 56 HLSL-источникам, указанным в актуальном `dxil.mak`, а не по всем оставшимся файлам кэша: 49 типов элементов StructuredBuffer (включая примитивы и RTXDI), 72 имени блоков констант / 75 вариантов объявлений. Дополнительно прочитаны root constants и vertex input layout служебных D3D12 overlay/bringup-программ.
- Для 33 пар структур CPU/HLSL: MSVC вычислил `sizeof`/`offsetof`/размер каждого поля из извлечённых C++-объявлений; DXC 1.9.2609 скомпилировал HLSL-объявления и вывел рефлексию. Сопоставлены общий размер и тип каждого 32-битного участка с его смещением. Группировки `float4`/`float[4]`, `uint4`/четыре UINT и вложенные структуры раскрыты. Порядок семантически соответствующих полей и места передачи прочитаны отдельно.
- Для 35 вариантов крупных constant buffers выполнено такое же компиляторное сравнение. Остальные 40 вариантов проверены по рефлексии DXC и местам формирования массивов/малых управляющих записей; меньшие используемые префиксы учтены отдельно.
- Остальные структурированные буферы: семь GPU-only/упакованных контрактов сопоставлены с фактическим stride выделения; RTXDI DI/GI — с подключёнными заголовками 3.2.0; семь примитивных типов — с типом/шагом элементов. Raw-контракты рельефа, packed geometry и SKN/glTF проверены отдельно ниже.
- Отрицательный контроль: компиляция прежних RT-объявлений возвращает 64/24 байта при CPU 80/28; оба нарушения обнаружены. Это проверка способности метода замечать именно прошлый дефект.
- Копий игры, игровых запусков, установки и полной сборки игры не было. Компилировались только автономные измерители размещения и извлечённые объявления шейдеров. GPU-прогоны для этого аудита не понадобились.

## Структурированные буферы: компиляторная сверка

| HLSL | CPU | Байт | Результат |
|---|---|---:|---|
| `NativeInstance` | `NativeInstanceGpu` | 432 | Совпало |
| `NativeDraw` | `NativeDrawGpu` | 100 | Совпало |
| `NativeDrawWork` | `NativeDrawWorkGpu` | 40 | Совпало |
| `NativePreparedVertex` | `NativePreparedVertexGpu` | 64 | Совпало |
| `NativeWorldVertex` | `NativeWorldVertexGpu` | 80 | Совпало |
| `NativeWorldState` | `NativeWorldStateGpu` | 28 | Совпало |
| `NativePose` | `NativePoseGpu` | 140 | Совпало |
| `NativePoseNode` | `Node` | 104 | Совпало |
| `NativeInstanceUpdate` | `NativeInstanceUpdate` | 544 | Совпало |
| `NativePointLight` | `NativePointLightValue` | 64 | Совпало |
| `RtWorldVertex` | `NativeWorldVertexGpu` | 80 | Совпало |
| `RtWorldState` | `NativeWorldStateGpu` | 28 | Совпало |
| `RtJob` | `RaytracingRepackJob` | 128 | Совпало |
| `RtPointLight` | `NativePointLightValue` | 64 | Совпало |
| `RdLight` | `RtDirectLight` | 64 | Совпало |
| `RfParticle` | `RtReflectParticleGpu` | 48 | Совпало |
| `RiCaveExit` | `CaveExitGpu` | 64 | Совпало |
| `PcLamp` | `NativePointLightValue` | 64 | Совпало |
| `CaveLight` | `NativePointLightValue` | 64 | Совпало |
| `CaveExit` | `CaveExitGpu` | 64 | Совпало |
| `CaveVpl` | `CaveVirtualLight` | 48 | Совпало |
| `CrVertex` | `CrVertex` | 32 | Совпало |
| `CrInstance` | `CrInstance` | 96 | Совпало |
| `CrBone` | `Bone` | 48 | Совпало |
| `GameParticle` | `GameParticleGpu` | 48 | Совпало |
| `GcVertex` | `GcVertex` | 56 | Совпало |
| `GcInstance` | `GcInstanceGpu` | 64 | Совпало |
| `GcRule` | `GroundCoverRule` | 160 | Совпало |
| `GcFootprint` | `FootprintSplat` | 32 | Совпало |
| `GcTile` | `GcTileGpu` | 48 | Совпало |
| `GcSnowSegment` | `Segment` | 48 | Совпало |
| `GcZonePiece` | `ZonePiece` | 32 | Совпало |
| `GcHosted` | `HostedPlantGpu` | 96 | Совпало |

Дополнительные контракты:

| Элемент | Байт | Источник/проверка |
|---|---:|---|
| `GcCandidate` / `GcSurvivor` | 48 / 80 | `kGcCandidateBytes` / `kGcSurvivorBytes`, D3D11 и D3D12 allocation |
| `GcCellJob` / `GcDrawInput` | 16 / 32 | `gc_ensure_cell_jobs`, pool draw-input allocation |
| `ImpostorBakeVertex` | 32 | `finish_buffer(..., 8 * sizeof(float), ...)` |
| `RtProbe` | 16 | Четыре UINT: state, generation low/high, padding; allocation и запись по 16 |
| `NativeTerrainFrameValues` | 1200 | 300 float-слов: формулы `kTerrain*First`, таблицы/ветер/материалы; один structured element |
| `RTXDI_PackedDIReservoir` / `RTXDI_PackedGIReservoir` | 24 / 32 | Заголовки SDK и `kRtReservoirBytes` / `kRtIndirectReservoirBytes` |
| `float`, `float2`, `float4`, `int`, `uint`, `uint2`, `uint4` | 4/8/16/4/4/8/16 | Индексы, offsets, terrain tables, fur nodes/list, masks и owner records |

Для NativeInstance/Draw/Update, Pose/Node, WorldVertex/State проверены также `create_buffer`, `StructureByteStride`, upload/copy и смещения DXR vertex input. Используются актуальные `sizeof`, а не старые 64/24. Среди других объявлений шейдеров не найдено оставшейся короткой копии этих расширенных структур.

## Крупные блоки констант

| HLSL-вариант | CPU байт | HLSL байт | Результат |
|---|---:|---:|---|
| `NativeRasterConstants_0` | 368 | 368 | Совпало |
| `NativeWorldLightingConstants_0` | 48 | 48 | Совпало |
| `NativeShadowConstants_0` | 912 | 912 | Совпало |
| `FogFrame_0` | 464 | 464 | Совпало |
| `FogFrame_1` | 464 | 144 | Совместимый префикс |
| `Precipitation_0` | 608 | 608 | Совпало |
| `RtDirectConstants_0` | 400 | 400 | Совпало |
| `RtIndirectConstants_0` | 512 | 512 | Совпало |
| `RtIndirectDenoise_0` | 144 | 144 | Совпало |
| `RtReflectConstants_0` | 256 | 256 | Совпало |
| `CrFrame_0` | 256 | 256 | Совпало |
| `GameParticleFrame_0` | 208 | 208 | Совпало |
| `GcFrame_0` | 1360 | 1360 | Совпало |
| `SkyFrame_0` | 816 | 816 | Совпало |
| `LiquidReflectionFrame_0` | 304 | 304 | Совпало |
| `RtWaterFrame_0` | 304 | 304 | Совпало |
| `CaveInject_0` | 160 | 160 | Совпало |
| `CaveCompose_0` | 64 | 64 | Совпало |
| `CaveGlow_0` | 16 | 16 | Совпало |
| `AmbientOcclusionConstants_0` | 192 | 192 | Совпало |
| `FurDraw_0` | 496 | 496 | Совпало |
| `Ambient_0` | 512 | 512 | Совпало |
| `WindTrails_0` | 3312 | 3312 | Совпало |
| `SurfaceEffect_0` | 32 | 32 | Только неиспользуемое padding; см. ниже |
| `ClearValues_0` | 32 | 32 | Совпало |
| `Terrain2Cover_0` | 656 | 656 | Совпало |
| `Terrain2Bake_0` | 96 | 96 | Совпало |
| `LiquidWaveStep_0` | 544 | 544 | Совпало |
| `ImpostorBake_0` | 32 | 32 | Совпало |
| `C_0` | 368 | 256 | Совместимый префикс |
| `C_1` | 368 | 352 | Совместимый префикс |
| `CaveSunShadow_0` | 912 | 880 | Совместимый префикс |
| `LiquidRippleFrame_0` | 992 | 912 | Совместимый префикс |
| `DebugViewConstants_0` | 128 | 128 | Совпало |
| `cbFSR2_0` | 124 | 124 | Совпало |

## Малые/составные блоки и управляющие массивы

| Блоки | Передаваемый контракт |
|---|---|
| `RtDebugConstants` / `RtHitDebugConstants` | Общая запись 144 байта; debug читает её первые 112, hit-debug все 144 |
| `NativePointLightConstants`, `RtPointLightConstants`, `PcLamps`, `RtDirectLightGrid` | Заголовок света 32 байта + CaveSampleConstants 80; первые два читают 112, lamp/grid читают префикс 32 |
| `NativeWorkRange` | 8 UINT / 32 байта для build; pose/shadow этапы читают используемый префикс из 4 UINT |
| `NativeWorldClip` | 2 матрицы + 5 float4 = 208; `native_world_clip_values` пишет смещения 0/64/128/144/160/176/192 |
| `NativeShadowHistoryConstants`, `NativeMotionConstants`, `GcMotion`, `CameraMotion` | 96 / 96 / 80 / 144 используемых байт; GcMotion берёт префикс motion, camera-motion загружает запас 192 с используемыми первыми 144 |
| `NativeTerrainMissList` | 8 UINT / 32 байта; согласованы region/list/args slots |
| `Terrain2CacheBake` | Варианты 32 и 48: int4 offset, float4 texel, при соответствующем варианте int4 target |
| `FaceCheck` / `FaceFeedback` | По 160 байт: две матрицы и два float4 с разным, явно соответствующим порядком |
| `GcPlace` / `GcCull` | 384 / 528 байт; размеры массивов получаются из одних CPU-констант и передаются в shader source |
| `DepthOfField` / `DepthOfFieldDebug` | Массивы float[20] / float[36] = 80 / 144 байта |
| `SunRays` / `Bloom` / `Metrics` | float[12] / float[8] / float[4] = 48 / 32 / 16 байт |
| `ShelterFilter` / `SnowSurface` | int[4]+float[4] / 8 float = 32 / 32 байта |
| `CrDraw`, `GcDraw`, `GcPaint`, `GcRun`, `GcOwner` | Четыре 32-битных управляющих слова / 16 байт |
| `RtTermView`, `RtProbeConstants`, `RtRepack`, `ShadowRangeConstants`, `ShadowScroll`, `UnitMips`, `RainRings` | По 16 байт; типы и порядок значащих слов совпадают с вызывающим кодом |
| `cbSPD` / `cbRCAS` | SPD использует первые 6 UINT из массива 8 (24/32); RCAS — 4 UINT / 16 |
| Host D3D12 diagnostic/overlay | Root — 8 DWORD/32 байта, Frame — 48, Bar — 32; ImGui input задаётся через offsetof/sizeof ImDrawVert |

Основные места передачи: `renderer_native_d3d12.cpp`, `renderer_native_queue.cpp`, `renderer_ground_cover_d3d12.inl`, `renderer_terrain2_cache.cpp`, `renderer_terrain_face_cache.cpp`, `renderer_precipitation.cpp`, `fsr2_upscaler.h`, Host/d3d12.

## Raw-адресация

MSVC подтвердил:

| CPU запись | Размер | HLSL/caller контракт |
|---|---:|---|
| `NativePackedGroup` | 64 | Четыре lane по x/y/z/w, смещения 0/16/32/48 |
| `NativeTextureCoordinate` | 8 | float2 UV |
| `NativeGpuComponent` | 16 | uint4 position/normal/texture/extra |
| `NativeTerrainVertex` | 120 | Совпадают поля и смещения `loadTerrainVertex`; RT repack использует stride 120 и flags на +24 |
| `skn::GpuVertex` | 244 | joints +0, weights +16, четыре inverse-матрицы 3x4 с +32 и шагом 48, tangent +224, correction_node +240 |
| `WeatherWindGpu` | 32 | Два float4 в terrain-frame начиная с float-слова 20 |
| `CaveSampleConstants` | 80 | Продолжение светового блока с байта 32 |

Дополнительно проверены RT vertex arena 20 байт (xyz + uv), index triangle 12 байт, record/shade 16 байт и header 32, размеры и адресация NativeInstanceUpdate и world-vertex input для BLAS. Исправленные RT объявления не оставляют старого шага в этих местах.

## Безопасные различия

1. `SurfaceEffect`: C++ `UINT padding[3]` против HLSL `float3 padding`, байты 20–31. Поля viewport/fade, размер 32 и все смещения совпадают. Padding инициализируется нулями и не читается шейдером. Это формальное различие типов, а не функциональный дефект; runtime не менялся.
2. Несколько шейдеров намеренно читают только начало более длинного constant buffer: fog 144/464, fixed-function C 256 или 352/368, cave-shadow 880/912, ripple 912/992 (после 912 — CPU-only id/reach), lamp/grid 32/112, SPD 24/32. Совпадает весь используемый префикс. Это не случай StructuredBuffer с неправильным шагом.
3. CPU-массивы и HLSL-векторы иногда группируют одни и те же байты по-разному; это учтено при сравнении. Матрицы для этих контрактов объявлены row_major либо явно записываются строками.

## Ограничения вывода

Это проверка ABI/упаковки/адресации и её мест передачи в данном снимке кода. Она не доказывает корректность значений на каждом игровом кадре, синхронизации GPU, выбора ресурса или визуальных алгоритмов; RT-картинку и FPS по поручению проверяет владелец. Новых runtime-исправлений и новой сборки для установки по результатам аудита не требуется.

Сырые описи, MSVC offsets, DXC reflection и отрицательный контроль сохранены в `tasks/RABI1126`: `hlsl-inventory.json`, `structured-results.json`, `cb-results.json`, `raw-layouts.txt`, `negative-control.json`, каталоги `layouts`, `constants`, `raw`. В `cb-results.json` строка SurfaceEffect намеренно остаётся FAIL по строгому сравнению типов; выше дано её разрешение как неиспользуемого padding, а не скрытая замена результата на PASS.
