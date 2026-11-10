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
| Рендер автора | `external/owned-renderer/`; upstream `ef7e12467db17ef673f8ce3b105063b4b3b16417`; https://github.com/Ilufus/evil-islands-owned-renderer | PolyForm Noncommercial 1.0.0; полные `LICENSE.md`, `NOTICE.md` и все Required Notice сохранены в vendor-копии. EID3D11 renderer by Ilufus. Это изменённая embedded-версия EIRebuild64; локальные отличия перечислены в `PROVENANCE.md`. Лицензии third-party сохраняют собственные условия. |
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

FFmpeg 8.1.3 получен из https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz;
SHA-256 `7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3`.
[Pin/provenance](third_party/ffmpeg/PROVENANCE.md),
[состав](third_party/ffmpeg/COMPONENTS.md),
[рецепт](tools/ffmpeg/README.md). LGPL v2.1-or-later, shared x64 MSVC;
GPL/nonfree/version3 и внешняя autodetection отключены. Включаются avformat/bink,
avcodec/bink/binkaudio_rdft/binkaudio_dct, avutil, swscale, swresample.
Полные `COPYING.LGPLv2.1` и upstream `LICENSE.md` сохранены рядом с lock/config.
Архив/полный source и выход `dependencies/ffmpeg/8.1.3/x64/{bin,include,lib}`
вне Git. Подпись архива не проверена: её загрузка завершалась таймаутами.
Сборка остаётся открытой: не найдены MSYS2 make и NASM; DLL/import libs/headers
ещё не получены. Это фактический source pin и рецепт, не готовая x64 dependency.
Подключение к хосту, AVIO и удаление Bink SDK — P20/P21.

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
