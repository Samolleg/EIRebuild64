# Сборка FFmpeg 8.1.3 shared x64 MSVC

P19 готовит только зависимость. Хост не линкуется; SDK Bink и игровые файлы
не меняются. Pin и конфигурация: [PROVENANCE](../../third_party/ffmpeg/PROVENANCE.md),
[source.lock](../../third_party/ffmpeg/source.lock),
[configure.args](../../third_party/ffmpeg/configure.args).

## Окружение

Нужны MSVC Build Tools с C++ x64/Windows SDK, MSYS2 bash/GNU make и NASM.
На машине P19 есть MSVC Build Tools 2022 и Git bash; GNU make/NASM и обычная
установка MSYS2 не найдены. **Сборка открыта, ничего системно не установлено.**
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
