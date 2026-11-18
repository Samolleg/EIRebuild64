# Источники и лицензии EIRebuild64

Манифест P06 описывает унаследованное дерево и план замен. Он не объявляет
все зависимости готовыми для x64. P06 не скачивает пакеты, не собирает DLL
и не удаляет SDK. Исходная история — EIRebuild на `46fd058`.

## Уже в исходном дереве

| Компонент | Источник / версия | Лицензия и происхождение |
|---|---|---|
| miniaudio | `third_party/miniaudio/miniaudio.h`, v0.11.25 (2026-03-04); https://github.com/mackron/miniaudio | Выбор public domain или MIT-0, полный текст `third_party/miniaudio/LICENSE` и в заголовке. Постоянный аудиобэкенд. |
| nlohmann/json | `SDKs/JSON/`, v3.10.5; https://github.com/nlohmann/json | MIT, notice и полный текст в `SDKs/JSON/json.hpp`; это отдельный компонент, не удаляется вместе с dx7sdk. |
| LuaJIT GC64 x64 | `third_party/luajit/`, v2.1, pin `a4c064539ea931274d407e1249b4bb575424a0f4`, 2026-10-21 15:09:24 +02:00, version timestamp `1792588164`; https://luajit.org/git/luajit.git | MIT, полный `COPYRIGHT`; [PROVENANCE](third_party/luajit/PROVENANCE.md), [рецепт](tools/luajit/README.md). Полный неизменённый source; VM генерируется при сборке, статическая библиотека x64 GC64 с `/MT`. P14: хост использует matching публичные заголовки и библиотеку; старые LuaState и luajit-x86 удалены. |
| Рендер автора | `external/owned-renderer/`; candidate-1126 `6bdebfa9be990b717b8dc667bbbb9405494f6be4`; https://github.com/Ilufus/evil-islands-owned-renderer | PolyForm Noncommercial 1.0.0; полные `LICENSE.md`, `NOTICE.md` и все Required Notice сохранены в vendor-копии. EID3D11 renderer by Ilufus. Это изменённая embedded-версия EIRebuild64; локальные отличия перечислены в `PROVENANCE.md`. Лицензии third-party сохраняют собственные условия. |
| Dear ImGui | `external/owned-renderer/ThirdParty/imgui/`; https://github.com/ocornut/imgui, commit `f1cc2ae15e53a861a874c3034aae6798fde194ab` | MIT; `LICENSE.txt` и `PROVENANCE.md` в каталоге компонента. |
| meshoptimizer | `external/owned-renderer/ThirdParty/meshoptimizer/`; https://github.com/zeux/meshoptimizer, v1.1 | MIT; `LICENSE.md` и `PROVENANCE.md` в каталоге компонента. |
| SMAA | `external/owned-renderer/ThirdParty/smaa/`; https://github.com/iryoku/smaa | MIT; `LICENSE.txt` и `PROVENANCE.md`. В provenance исходный upstream commit не указан; не приписывать снимку выдуманный pin. |
| Ресурсы camp UI | `external/owned-renderer/Assets/camp/` | Происхождение отдельных изображений в `PROVENANCE.txt`; это asset provenance, не общая лицензия рендера и не разрешение на распространение игровых данных. |

## DirectXTex и DirectXTK

Microsoft DirectXTex / DirectXTK, tag jun2024 (DirectXTex соответствует NuGet
2024.6.5.1), MIT. Нужные исходники, LICENSE и точные SHA256 исходных архивов
сохранены в `external/DirectXTex` и `external/DirectXTK` (PROVENANCE.md).
Локальные проекты собирают статические библиотеки с /MT, Debug /MTd.
NuGet DirectXTex/DirectXTK и XAudio2.Redist не используются.

## Закреплённый source и рецепт FFmpeg (P19)

FFmpeg 9.0.2 получен из https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz;
SHA-256 `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`.
[Pin/provenance](third_party/ffmpeg/PROVENANCE.md),
[состав](third_party/ffmpeg/COMPONENTS.md),
[рецепт](tools/ffmpeg/README.md). LGPL v2.1-or-later, shared x64 MSVC;
GPL/nonfree/version3 и внешняя autodetection отключены. Включаются avformat/bink,
avcodec/bink/binkaudio_rdft/binkaudio_dct, avutil, swscale, swresample.
Полные `COPYING.LGPLv2.1` и upstream `LICENSE.md` сохранены рядом с lock/config.
Архив/полный source и выход `dependencies/ffmpeg/9.0.2/x64/{bin,include,lib}`
вне Git. Подпись проверена ключом FFmpeg. В комплект входят DLL, исходный архив и рецепт сборки в sources/ffmpeg/9.0.2/.

## Планируемые зависимости

- **Рендер x64 и shader tool (P04/P05):** поставка автора с полным
  source/headers/build recipe, shaders/assets и лицензиями всех third-party.
  Унаследованный снимок содержит x86 asm/layout dependencies; снять платформенный
  guard в наших проектах недостаточно для готовности native x64.

## Что исключается из x64-цели последующими пакетами

| Унаследованные файлы | Дальнейшее действие |
|---|---|
| `SDKs/BinkW32/`, `binkw32.lib`, Bink API и экраны | P19–P21: FFmpeg и переключение всех видеоэкранов; P21 удаляет Bink SDK/header/link references. В P06 файлы и Bink-ссылки сохранены, они остаются блокером. |
| `SDKs/dx7sdk/`, `SDKs/MilesWin/`, старые device imports | P22 после переключения видео/аудио: убрать SDK-зависимости, использовать нужные Windows SDK GUID/imports и реальные device inputs. Исторические notices не дают права включать SDK в пользовательский x64-комплект. |
| `luajit-x86/lj_vm.obj` и старые LuaJIT generated inputs | P12/P14: полный pinned GC64 source и генерация; старый каталог удалён из дерева и сохранён только в истории. |
| Старые outputs, EXE/DLL/PDB, caches, `packages/`, `dependencies/`, `out/` | Не переносить как входы порта или tracked пользовательский комплект. P24 формирует отдельный чистый комплект с нужными DLL/notices; PDB хранится отдельно. P06 ничего массово не удаляет. |
| Saves, config пользователя, logs, dumps, данные мода | Приватные runtime-данные, не third-party source и не часть репозитория/пакета. Будущие собственные каталоги x64 не заменяют файлы x86. |

Ссылки фиксируют происхождение, а не факт получения или сборки dependencies.
Полный x64 граф, итоговый набор notices и игровая приёмка остаются открытыми.

## NVIDIA DLSS / NGX (X64DLSS01)

NVIDIA DLSS SDK v310.9.16, NVIDIA/DLSS tag v310.9.16, commit
`9981c6350dc58c0ce21f024fd65e31e851c31689`.
Source: https://github.com/NVIDIA/DLSS/tree/v310.9.16 .
This product uses NVIDIA DLSS and NVIDIA NGX. NVIDIA and GeForce RTX are
trademarks and/or registered trademarks of NVIDIA Corporation in the U.S. and
other countries. Copyright NVIDIA Corporation. All rights reserved.

The SDK is external, not vendored. Run tools/prepare-worktree.ps1; its
-DlssSdkSource parameter or EIREBUILD64_DLSS_SDK environment variable overrides
the default deps/dlss-sdk/v310.9.16 source. Build property DlssSdkRoot
can override the prepared dependencies/dlss/v310.9.16 location. The host links
the Windows x64 nvsdk_ngx_s.lib (/MT), or nvsdk_ngx_s_dbg.lib (/MTd) for Debug. Only the
release nvngx_dlss.dll is shipped beside EIRebuild64.exe, with the unchanged
SDK LICENSE.txt at licenses/NVIDIA-DLSS-LICENSE.txt. Headers/import libraries
and development DLLs are excluded from the handoff and Git.

Release obligations remain with the owner: NVIDIA RTX SDK License plus RTX
Supplement (March 14, 2024), especially base sections 1(c), 2 and supplement
1, 4, 6.2, 7.1(b), 7.2(c). Notify NVIDIA before commercial release; provide
the required application attribution and NVIDIA Marks on splash screens,
About (if present) and game credits; obtain applicable prior approval for
mark usage. This documentation notice does not replace those application
requirements. No notification, trademark approval or acceptance on behalf
of the owner is performed by the build. Review distribution/EULA conditions
and resolve these obligations before public release.

## RecastNavigation (X64NAV01)

Recast/Detour, official tag v1.6.0, commit 6dc1667f580357e8a2154c28b7867bea7e8ad3a7.
Source: https://github.com/recastnavigation/recastnavigation/releases/tag/v1.6.0 .
Archive SHA-256: 8b50c62177249554b226514b307e6c529934a62b3926822777bf6abb8906a155.
Unmodified Recast and Detour modules only, statically compiled x64 /MD, DT_POLYREF64.
zlib license: external/recastnavigation/License.txt. Pin: third_party/recastnavigation.lock.json.

## Direct3D 12 dependencies (X64D3D12, runtime integration pending)

The coordinator supplied these packages offline in tasks/X64D3D12/downloads.
They were copied into project deps. tools/prepare-worktree.ps1 materializes
only the needed files under ignored dependencies/ for the build. No network
restore is used. OwnedRenderer/D3D12Sdk.props defines the build paths.

- AMD FidelityFX SDK 2.3.0, Kits/FidelityFX, from
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v2.3.0 .
  Local deps/fidelityfx-sdk/2.3.0. API, framegeneration and upscalers headers;
  signedbin/amd_fidelityfx_loader_dx12.lib. Runtime files are
  amd_fidelityfx_loader_dx12.dll and amd_fidelityfx_framegeneration_dx12.dll.
  Complete supplied license.md is third_party/d3d12/AMD-FidelityFX-LICENSE.md
  and is copied to application output licenses/AMD-FidelityFX-LICENSE.md.
  Preserve this combined license and its component notices on distribution.
  FSR FG requires Microsoft VC++ Redistributable 2015+ x64. If it is missing,
  FSR FG is unavailable; the game and DLSS FG do not require these AMD DLLs.
- NVIDIA NVAPI commit 70d337db9186e968eab622f7e786de7e437faf3d, from
  https://github.com/NVIDIA/nvapi/tree/70d337db9186e968eab622f7e786de7e437faf3d .
  Local deps/nvapi/70d337db/nvapi-70d337db9186e968eab622f7e786de7e437faf3d.
  Headers and static amd64/nvapi64.lib, used for the author's Reflex code.
  MIT license preserved in third_party/d3d12/NVIDIA-NVAPI-LICENSE.txt and
  application output licenses/NVIDIA-NVAPI-LICENSE.txt. No NVAPI SDK DLL ships.
- Microsoft DirectXShaderCompiler v1.9.2609, release archive
  dxc_2026_09_29.zip from
  https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609 .
  Local deps/dxc/1.9.2609. Build inputs bin/x64/dxc.exe, dxcompiler.dll,
  dxil.dll. Supplied LICENCE-MIT.txt, LICENSE-LLVM.txt and LICENSE-MS.txt
  are preserved unchanged in third_party/d3d12 and the prepared dependency.
  Only compiled DXIL resources enter the application. Compiler executables
  and DLLs are excluded from the runtime kit.
- NVIDIA DLSS SDK v310.9.16, same tag/commit/license as X64DLSS01 above.
  The existing deps/dlss-sdk/v310.9.16 now includes the four supplied
  *_dlssg*.h headers and release nvngx_dlssg.dll. Preparation and runtime
  packaging include DLSS-G beside nvngx_dlss.dll. The unchanged SDK
  LICENSE.txt and the existing NVIDIA attribution/distribution obligations
  apply to both. The author calls NGX directly, with no Streamline dependency.

The build-task runtime DLL list includes both FidelityFX DLLs and DLSS-G.
Application output contains SDK licenses. A distributed kit must also carry
the complete licenses above, including the existing NVIDIA DLSS license.
Source/build integration is staged. Host executor and NGX transport adaptation
remain open. No build, shader compilation, kit or game execution is validated.

## zstd 1.5.7 / xxHash

Local official zstd 1.5.7 in deps/zstd/v1.5.7, prepared under dependencies/zstd/v1.5.7.
PAK reader/writer uses the static C library at compression level 3 and its bundled XXH64 implementation.
BSD-3-Clause option selected. Copyright (c) Meta Platforms, Inc. and affiliates.
Full license is dependencies/zstd/v1.5.7/LICENSE; COPYING retains the alternative GPLv2 license.
Bundled xxHash uses the same BSD-style option; Copyright (c) Yann Collet - Meta Platforms, Inc. See lib/common/xxhash.h.

```text
BSD License

For Zstandard software

Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name Facebook, nor Meta, nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

```

## cgltf v1.15

`dependencies/cgltf/v1.15/cgltf.h` is prepared from the coordinator-provided
`deps/cgltf/v1.15`, jkuhlmann/cgltf tag v1.15. Header-only MIT dependency.
The archive SHA-256 is `84E352092E5CD6AAB7F66DE62DDB66BEB5E6F18D412CA9D12950D7A55BFEF25A`.
The complete MIT text is copied alongside the header as `LICENSE` by
`tools/prepare-worktree.ps1`. The source archive is already available locally.

## Renderer 1126 dependencies (2026-11-16)

The current x64 kit additionally ships NVIDIA DLSS Ray Reconstruction
nvngx_dlssd.dll from the existing v310.9.16 SDK, AMD FSR 3.1 upscaler runtime
amd_fidelityfx_upscaler_dx12.dll from FidelityFX SDK 2.3.0, and Microsoft
Direct3D Agility SDK 1.620.2 D3D12/D3D12Core.dll (SDK version 620).
RTXDI-Library 3.2.0, commit b67f61cd0bfa6792acbf5429bb29a68ee5cdd808,
is a build-time shader dependency. Its MIT license and the Agility SDK's
unchanged LICENSE.txt are included in the kit's licenses directory.
Existing DXC 1.9.2609, NVAPI 70d337db, DLSS SR/FG and AMD FG remain pinned.
Official download URLs, lengths and SHA-256 are recorded in
third_party/d3d12/renderer1126.lock.json. Dependencies are prepared locally;
the build does not restore anything from the network.

Sources: https://github.com/NVIDIA-RTX/RTXDI-Library/tree/b67f61cd0bfa6792acbf5429bb29a68ee5cdd808
and https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.620.2 .
## NVIDIA PhysX 5 GPU

PhysX 5.6.1, tag 107.3-physx-5.6.1, commit 5ca9f472105a90d70d957c243cb0ef36fe251a9f.
BSD-3-Clause. Official source: https://github.com/NVIDIA-Omniverse/PhysX/tree/5ca9f472105a90d70d957c243cb0ef36fe251a9f
Pinned source and CUDA 12.8.1 download hashes: tools/physx/fetch-dependencies.ps1.
Offline build and packaging: tools/physx/build.ps1; details: tools/physx/README.md.
Static x64 /MT host libraries and PhysXGpu_64.dll, built together from source.
CUDA libraries retain NVIDIA license terms; kit licenses include original SDK notices.
GPU dynamics/broadphase for client-only stone debris; no authoritative combat changes.