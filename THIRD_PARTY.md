# Источники и лицензии EIRebuild64

Манифест P06 описывает унаследованное дерево и план замен. Он не объявляет
все зависимости готовыми для x64. P06 не скачивает пакеты, не собирает DLL
и не удаляет SDK. Исходная история — EIRebuild на `46fd058`.

## Уже в исходном дереве

| Компонент | Источник / версия | Лицензия и происхождение |
|---|---|---|
| miniaudio | `third_party/miniaudio/miniaudio.h`, v0.11.25 (2026-03-04); https://github.com/mackron/miniaudio | Выбор public domain или MIT-0, полный текст `third_party/miniaudio/LICENSE` и в заголовке. Постоянный аудиобэкенд. |
| nlohmann/json | `SDKs/JSON/`, v3.10.5; https://github.com/nlohmann/json | MIT, notice и полный текст в `SDKs/JSON/json.hpp`; это отдельный компонент, не удаляется вместе с dx7sdk. |
| LuaJIT GC64 x64 | `third_party/luajit/`, v2.1, pin `a4c064539ea931274d407e1249b4bb575424a0f4`, 2026-10-21 15:09:24 +02:00, version timestamp `1792588164`; https://luajit.org/git/luajit.git | MIT, полный `COPYRIGHT`; [PROVENANCE](third_party/luajit/PROVENANCE.md), [рецепт](tools/luajit/README.md). Полный неизменённый source; VM генерируется при сборке, статическая библиотека x64 GC64 с `/MD`. P14: хост использует matching публичные заголовки и библиотеку; старые LuaState и luajit-x86 удалены. |
| Рендер автора | `external/owned-renderer/`; https://github.com/Ilufus/evil-islands-owned-renderer | Точные candidate/интеграционные ревизии и дальнейшие изменения перечислены в `external/owned-renderer/PROVENANCE.md`. Общая лицензия рендера в корне vendor-копии не обнаружена; лицензии её third-party не назначают лицензию всему рендеру. Для x64 нужна поставка автора с собственным provenance/notices (P04/P05). |
| Dear ImGui | `external/owned-renderer/ThirdParty/imgui/`; https://github.com/ocornut/imgui, commit `f1cc2ae15e53a861a874c3034aae6798fde194ab` | MIT; `LICENSE.txt` и `PROVENANCE.md` в каталоге компонента. |
| meshoptimizer | `external/owned-renderer/ThirdParty/meshoptimizer/`; https://github.com/zeux/meshoptimizer, v1.1 | MIT; `LICENSE.md` и `PROVENANCE.md` в каталоге компонента. |
| SMAA | `external/owned-renderer/ThirdParty/smaa/`; https://github.com/iryoku/smaa | MIT; `LICENSE.txt` и `PROVENANCE.md`. В provenance исходный upstream commit не указан; не приписывать снимку выдуманный pin. |
| Ресурсы camp UI | `external/owned-renderer/Assets/camp/` | Происхождение отдельных изображений в `PROVENANCE.txt`; это asset provenance, не общая лицензия рендера и не разрешение на распространение игровых данных. |

## Закреплённые NuGet-зависимости

Сами restored packages не включены в исходное дерево. Pins остаются в
`packages.config`, архитектура импорта MSBuild — `x64`; P06 не выполняет restore
и не подтверждает содержимое ещё не полученных x64 libraries.

| Пакет | Версия | Источник / notices |
|---|---|---|
| `directxtex_desktop_2019` | `2024.6.5.1` | DirectXTex, https://github.com/microsoft/DirectXTex, MIT; LICENSE и notices из точного восстановленного пакета сохраняются при подготовке комплекта. |
| `directxtk_desktop_2019` | `2024.6.5.1` | DirectXTK, https://github.com/microsoft/DirectXTK, MIT; сохранить LICENSE/notices точного пакета. Наличие общих helpers не разрешает возвращать старый OpenEI. |
| `Microsoft.XAudio2.Redist` | `1.2.11` | https://www.nuget.org/packages/Microsoft.XAudio2.Redist/1.2.11 ; отдельные условия Microsoft из пакета, не MIT. Необходимость для выбранного miniaudio backend и notices уточняются P22/P24; import пока сохранён. |

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
