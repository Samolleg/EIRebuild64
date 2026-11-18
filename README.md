# EIRebuild64

Отдельная версия движка «Проклятых Земель» для Windows x64 с общей историей
[EIRebuild x86](https://github.com/Samolleg/EIRebuild). Исходная принятая база —
`46fd05803086300c2d3f225d291bf9a56c6f19d7` (включает подготовку X64P01).
x86 сохранён в отдельном проекте и заморожен до прохождения кампании на x64.

## Решения проекта

- Только native x64. Единственная конфигурация основного графа — `Release|x64`.
- Один встроенный рендер: исходники включены в `external/owned-renderer`. Хост передаёт
  типизированные входы; запасного OpenEI, ddraw или x86-рендера нет.
- Новая игра и собственная схема сохранений. Старые сохранения и совместимость
  x86 ↔ x64 не поддерживаются.
- Первая версия — одиночная игра. Сохраняется локальный обмен клиент ↔ сервер
  внутри процесса; TCP, LMP/MMP, лобби, чат-сервер и адресная книга не переносятся.
- Звук — miniaudio. Видео `.bik` — FFmpeg 9.0.2, LGPL DLL вместо Bink SDK.
- Lua — зафиксированный LuaJIT v2.1 с GC64; старый готовый x86 VM object не
  используется как вход x64. Версия и источник указаны в [THIRD_PARTY.md](THIRD_PARTY.md).
- Движок устанавливается отдельно в `Engine64/`; используются `Saves64/`,
  `Config64/`, `Logs64/` и профиль `restarter64-settings.xml`.
  ReStarter64 — отдельный проект, в этот репозиторий не входит.
- Различия бывших x87-формул принимает владелец по игровому результату;
  изменение баланса само по себе переносом не разрешается.

## Сборка

Инструкция для чистого клона: **[BUILD.md](BUILD.md)**. Она описывает получение
внешних зависимостей, подготовку и сборку обеих целей `Release|x64`:
`EIRebuild64.exe` и `EIRebuild64NavBake.exe`. LuaJIT GC64, встроенный рендер
и генератор ресурсов шейдеров собираются из исходников репозитория.
NuGet-пакеты, FFmpeg и DLSS SDK нужно получить отдельно; готовые зависимости
и игровые данные в Git не входят. `tools/prepare-worktree.ps1` сам их не скачивает.

Унаследованные отдельные `*Check.vcxproj` и `Game/Game.sln` не входят в
основной граф `EI_Rebuild.sln`; их старые конфигурации не являются x86-целью
EIRebuild64. Исторические `build-*.py`, `build-local.py` и handoff-скрипты
не являются инструкцией сборки или упаковки x64.

Проектные пути выходов (от корня репозитория):

| Проект | Выход | Промежуточные файлы |
|---|---|---|
| Хост | `out/x64/Release/EI_Rebuild/EIRebuild64.exe` | `out/obj/x64/Release/EI_Rebuild/` |
| NavBake | `out/x64/Release/EIRebuild64NavBake/EIRebuild64NavBake.exe` | `out/obj/x64/Release/EIRebuild64NavBake/` |
| LuaJIT | `out/x64/Release/luajit/luajit.lib` | `out/obj/x64/Release/luajit/` |
| OwnedRenderer | `out/x64/Release/OwnedRenderer/OwnedRenderer.lib` | `out/obj/x64/Release/OwnedRenderer/` |
| RendererShaders | `out/x64/Release/RendererShaders/` | `out/obj/x64/Release/RendererShaders/` |

В хосте сохранены действующие Release-параметры C++17, MBCS/MFC и `/MD`;
NuGet imports выбирают x64 по `Platform=x64`, версии `packages.config` сохранены.
Shader/camp `.res` берутся из выхода `RendererShaders`, а библиотека LuaJIT —
из отдельного выхода её проекта. Подготовка генерирует x64 VM из закреплённого
source LuaJIT. Успешная сборка не заменяет игровую приёмку; условия запуска
и ограничения проверки приведены в BUILD.md.

Правила работы с историей — [CONTRIBUTING.md](CONTRIBUTING.md).
Происхождение и лицензии зависимостей — [THIRD_PARTY.md](THIRD_PARTY.md).

X64NET: сетевая игра в x64 v1 удалена: TCP, лобби, чат, адресная книга и передача файлов. Сохранён только обмен клиента и сервера внутри процесса для одиночной игры.
