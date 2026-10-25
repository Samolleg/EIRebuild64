# Сборка FFmpeg 8.1.3 shared x64 MSVC

P19 готовит только зависимость. Хост не линкуется; SDK Bink и игровые файлы
не меняются. Pin и конфигурация: [PROVENANCE](../../third_party/ffmpeg/PROVENANCE.md),
[source.lock](../../third_party/ffmpeg/source.lock),
[configure.args](../../third_party/ffmpeg/configure.args).

## Окружение

Нужны MSVC Build Tools с C++ x64/Windows SDK, MSYS2 bash/GNU make и NASM.
Для X64FF владелец подготовил MSYS2 в `C:\msys64`, GNU make и NASM в UCRT64;
используется MSVC Build Tools 2022. Системные пакеты рецепт не устанавливает.
Сам рецепт никогда не устанавливает пакеты и не переключается на другой codec.

Владелец может установить MSYS2 официальным установщиком с https://www.msys2.org/.
После установки в его UCRT64 shell выполнить:

```sh
pacman -Syu
# После требуемого MSYS2 перезапуска shell и завершения обновления:
pacman -S --needed make diffutils pkgconf tar xz curl mingw-w64-ucrt-x86_64-nasm
```

Источники команд: [MSYS2 package management](https://www.msys2.org/docs/package-management/),
[пакет NASM](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-nasm).
Штатный MSVC-рецепт FFmpeg описан в `doc/platform.texi` полученного архива,
раздел Microsoft Visual C++ or Intel C++ Compiler for Windows.

## Одна обычная сборка

В обычном **cmd.exe** из корня этой рабочей копии (не из main) выполнить:

```bat
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"
"C:\msys64\usr\bin\bash.exe" --noprofile --norc tools/ffmpeg/build.sh /c/ffmpeg-build/X64P19
```

Если VS/MSYS2 стоят в других каталогах, изменить только пути к инструментам.
Аргумент `/c/ffmpeg-build/X64P19` — отдельный свежий каталог для этой сборки,
без пробелов; рецепт создаст `source`, `build`, `install` и сохранит их.
FFmpeg configure запрещает внешнюю сборку с пробелами в source path
(`configure:4486` в 8.1.3), поэтому исходник распаковывается туда из проверенного
архива. Путь workspace с пробелами не передаётся в generated Makefiles.
`make install` пишет в этот же временный prefix; затем весь результат копируется
в требуемый локальный `dependencies/ffmpeg/8.1.3/x64/{bin,include,lib}`.

При уже существующем `x64` либо занятых `source/build/install` рецепт
останавливается и ничего не удаляет/перезаписывает. Для новой ревизии сначала
сохранить прежний комплект и явно выбрать свежий каталог сборки. Полученные
dependencies и архив игнорируются Git существующим правилом `/dependencies/`.
Программа не интегрирована в MSBuild; каждый link хоста её не запускает.

Обычные compile/link probes внутри upstream configure являются частью сборки.
FATE, checkasm, tests, headless, запуск DLL/декодирования/игры не вызываются.
Выходные notices и фактический configure receipt сохраняются в
`x64/share/ffmpeg/`. Полный pinned source archive сохраняется отдельно для P24;
до сборки/комплектации P24 поставка DLL и её notices не считается готовой.

## Локализованный MSVC и общий комплект X64FF

Если установлен только русский пакет MSVC, `VSLANG=1033` не даёт английского
заголовка: upstream configure ошибочно определяет компилятор. `msvc.sh`
нормализует только запросы идентификации (без аргументов и `-nologo-`), сохраняя
исходный текст/версию. Компиляция и линковка передаются настоящим `cl.exe` и
соседнему `link.exe` с исходными аргументами и кодом возврата. Исходники FFmpeg,
набор компонентов и лицензия не меняются. Копия wrapper в каталоге сборки
исключает пробелы в командах generated Makefiles.

Для X64FF выбран свежий `/c/ffmpeg-build/X64FF-r2`; первая попытка сохранена
в `/c/ffmpeg-build/X64FF` (ошибка распознавания русского MSVC до компиляции DLL).
После успешной сборки скопировать целиком `dependencies/ffmpeg/8.1.3/x64`
в отсутствующий `work/deps64/ffmpeg/8.1.3/x64`, включая `share/ffmpeg`.
Это общий локальный комплект вне Git; существующий комплект не перезаписывать.

`tools/prepare-worktree.ps1` копирует общий комплект, только когда в рабочем
дереве отсутствует `dependencies/ffmpeg/8.1.3/x64`. Параметр `-FFmpegSource`
задаёт другой источник; по умолчанию используется `work/deps64/ffmpeg/8.1.3/x64`
для расположений `work/eirebuild64` и `work/eirebuild64-wt/<ID>`.
Если источника нет, скрипт выдаёт ошибку до подготовки зависимостей.
P20 получает headers из `dependencies/ffmpeg/8.1.3/x64/include`, import libs
из `lib`, runtime DLL из `bin`. Подключение к проекту и player остаётся P20.
Upstream `make install` MSVC помещает import libs в `bin`; рецепт дополнительно
копирует пять `.lib` в `lib`, сохраняя исходный install layout.
