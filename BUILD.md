> NEWPROJ03: внутри C:/Dev/EvilIslands зависимости находятся в `deps`, локальный NuGet — `deps/nuget`. `tools/prepare-worktree.ps1` находит RULES.md вверх от checkout, в том числе из tasks/<ID>/wt. Глобальные caches и старый work не являются его defaults.
> Выполните `powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare-worktree.ps1`, затем `MSBuild EI_Rebuild.sln /m:4 /nr:false /t:Build /p:Configuration=Release /p:Platform=x64`. Рецепт ниже относится к самостоятельному клону вне нового корня.

# Сборка EIRebuild64 из чистого клона

Проверено 2026-11-06 на `main` `81dbcdd`: обе цели Release x64 собраны,
0 ошибок. Первая сборка заняла 47 минут 10 секунд, из них 43 минуты 46 секунд —
компиляция 638 шейдерных программ без кэша. Это замер конкретной машины.

Нужен доступ к приватному `Samolleg/EIRebuild64`. Игровые данные и готовые
внешние зависимости в репозиторий не входят. Ниже — Windows PowerShell 5.1,
кроме явно отмеченного блока cmd.exe. Пути к инструментам можно заменить.

## Инструменты и клон

- Visual Studio 2022 / Build Tools: C++ v143 для x64/x86, **MFC v143** и Windows SDK
  (включая `rc.exe`); Git. Сборка использует C++17 и статический CRT `/MT` и статическую MFC.
- Для сборки FFmpeg: MSYS2, GNU make, NASM и остальные инструменты из
  [рецепта FFmpeg](tools/ffmpeg/README.md). Установка системных пакетов здесь
  не проверялась; проверен уже установленный набор инструментов.

Клон и рабочий каталог FFmpeg разместите по пути без пробелов:

```powershell
git clone --depth 1 --single-branch --branch main https://github.com/Samolleg/EIRebuild64.git
cd EIRebuild64
git rev-parse HEAD
```

`external/owned-renderer`, miniaudio, JSON, Recast и исходники LuaJIT GC64 уже
включены; копирование файлов из чужого `work/` не требуется.

## Внешние зависимости

**DirectXTex / DirectXTK.** Исходники Microsoft jun2024 включены в
`external/DirectXTex` и `external/DirectXTK` вместе с LICENSE и PROVENANCE.
Решение собирает их статически с `/MT` (Debug `/MTd`); NuGet не требуется.
DirectXTK содержит используемые BasicPostProcess, CommonStates, ScreenGrab и WICTextureLoader
с публичными заголовками, необходимыми хосту. Compute-шейдеры DirectXTex
генерируются Windows SDK fxc при первой сборке.

**FFmpeg 8.1.3 x64.** Соберите по [рецепту](tools/ffmpeg/README.md).
В проверенном окружении MSYS2 curl отказал на цепочке сертификатов. Архив был
получен через Windows HTTPS следующими командами; рецепт затем проверил
его SHA-256 по `third_party/ffmpeg/source.lock`. Проверка GPG-подписи не выполнялась.

```powershell
New-Item -ItemType Directory -Force dependencies/ffmpeg/8.1.3/downloads | Out-Null
Invoke-WebRequest -UseBasicParsing 'https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz' -OutFile 'dependencies/ffmpeg/8.1.3/downloads/ffmpeg-8.1.3.tar.xz'
```

Из корня того же клона в **cmd.exe** (проверен Build Tools 2022 и `C:\msys64`):

```bat
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"
"C:\msys64\usr\bin\bash.exe" --noprofile --norc tools/ffmpeg/build.sh "%CD%/.ffmpeg-build"
```

Рецепт требует свежий `.ffmpeg-build` и отсутствие готового
`dependencies/ffmpeg/8.1.3/x64`; сам ничего не удаляет и не устанавливает в систему.
Результат — `dependencies/ffmpeg/8.1.3/x64/{bin,include,lib,share}`;
исходный install остаётся в `.ffmpeg-build/install`.

**DLSS SDK.** Получите отдельно [официальный тег v310.9.16](https://github.com/NVIDIA/DLSS/tree/v310.9.16),
commit `9981c6350dc58c0ce21f024fd65e31e851c31689`. Нужны шесть заголовков
`include/nvsdk_ngx{,_defs,_params,_helpers,_helpers_d3d,_helpers_cuda}.h`,
`lib/Windows_x86_64/x64/nvsdk_ngx_s.lib` и `nvsdk_ngx_s_dbg.lib`,
`lib/Windows_x86_64/rel/nvngx_dlss.dll` и `LICENSE.txt` с исходной структурой папок.
Тег и этот список проверены удалённо; полная загрузка SDK заново не проверялась.
При сборке использован ранее полученный SDK этого коммита; содержимое нужных
файлов сверено с тегом (для текстовых файлов учтены CRLF/LF).

## Подготовка и одна сборка обеих целей

В PowerShell укажите свой каталог SDK (в корне EvilIslands используются defaults из `deps`):

```powershell
$vsInstall = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$ffmpegSource = Join-Path $PWD '.ffmpeg-build/install'
$dlssSource = 'C:\ei64-deps\DLSS-v310.9.16'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./tools/prepare-worktree.ps1 -VSInstallDir $vsInstall -FFmpegSource $ffmpegSource -DlssSdkSource $dlssSource
if ($LASTEXITCODE -ne 0) { throw 'Preparation failed' }
& "$vsInstall/MSBuild/Current/Bin/MSBuild.exe" EI_Rebuild.sln /m:4 /nr:false /t:Build /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
```

`Bypass` действует только для дочернего процесса. Подготовка копирует зависимости
и собирает LuaJIT; MSBuild собирает рендер, запускает штатный генератор ресурсов
шейдеров и компонует обе цели. Первая генерация шейдеров без кэша длительна
и может не печатать промежуточный прогресс. Отдельный restore
и запуск проверочных проектов не нужны. Результат:

- `out/x64/Release/EI_Rebuild/EIRebuild64.exe`;
- `out/x64/Release/EIRebuild64NavBake/EIRebuild64NavBake.exe`.

## Что требуется для запуска — по исходникам, без игрового прогона

Нужны установленная игра и совместимый мод с `config.reg`, БД и ресурсами.
Размещаются обе EXE в `<игра>/Engine64/`, рядом — `avcodec-62.dll`,
`avformat-62.dll`, `avutil-60.dll`, `swscale-9.dll`, `swresample-6.dll` из
`dependencies/ffmpeg/8.1.3/x64/bin` и release `nvngx_dlss.dll` из SDK.
Visual C++/MFC и LuaJIT линкуются статически; ресурсы шейдеров встроены.
Лицензии и notices сохраняются согласно [THIRD_PARTY.md](THIRD_PARTY.md).

Рабочая папка запуска — `Engine64`; аргумент движку —
`--mod "<абсолютный путь к папке мода>"`. В корне игры используются
`restarter64-settings.xml`, `Config64`, `Logs64`, `Cache64`; сохранения — `Saves64`
в корне сохранений выбранного мода. Старые x86-сохранения не поддерживаются.
Перед входом в карты нужен совместимый Атаман: выбрать собранный NavBake,
запечь Nav64 для актуальных MPR/MOB выбранного мода, передать весь результат
`Maps/Nav64`, включая `Bodies.navbody`. Автоматической перепечки в игре нет.

ReStarter64 не входит в этот Git-репозиторий. Он существует в отдельном локальном
проекте, у которого не настроен remote; доступный соавтору репозиторий не
подтверждён. Его исходники или готовый комплект, совместимый Атаман, игру и мод
нужно получить у владельца отдельно. Сборка стартера, запекание Nav64,
установка и игровой запуск в этой проверке не выполнялись.

## Границы CRT (X64STATICCRT)

`Directory.Build.targets` задаёт `/MT` для Release и `/MTd` для Debug во всех
native-проектах. Основное решение поддерживает Release x64; новых конфигураций
хоста не добавлено. MFC, LuaJIT, Recast/Detour, ScriptCompilerCore, рендер,
DirectXTex и DirectXTK линкуются статически. NGX использует соответствующие
`nvsdk_ngx_s.lib` / `nvsdk_ngx_s_dbg.lib`; подавления LNK2038 нет.

Собственных DLL в комплекте нет. `Game/dx11/RendererDX11.cpp` создаёт
`RendererFacadeCore<owned::Renderer>` внутри EXE; старый `OwnedRendererClient`
не инициализируется в этом пути. Его C API (`ei_renderer_api.h`) также использует
opaque handles с destroy/release у провайдера, POD и COM AddRef/Release,
без передаваемых FILE* и STL-объектов с внешним владельцем.

Внешние DLL: FFmpeg и NVIDIA DLSS; Streamline в комплекте нет.
`Game/VideoPlayer.cpp` выделяет AVIO-буфер через av_malloc, освобождает через
av_free/av_freep; контексты, пакеты и кадры — через соответствующие av*_free.
AVIO callbacks копируют данные в предоставленный буфер и используют opaque
указатель на объект хоста без передачи владения. `Game/VideoAudio.cpp` передаёт
буфер std::vector в swr_convert только для записи, освобождает SwrContext через
swr_free, AVChannelLayout через av_channel_layout_uninit. FILE* через DLL нет.
`external/owned-renderer/Source/dlss_backend.cpp` получает параметры NGX через
GetCapabilityParameters/AllocateParameters и возвращает DestroyParameters;
feature handle освобождается ReleaseFeature, COM-ресурсы — Release. CRT память
и STL не освобождаются чужим модулем. Правки игрового кода не потребовались.
Это аудит исходников; игровой запуск, перенос на ноутбук и Wine не проверены.