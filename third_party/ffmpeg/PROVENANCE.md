# FFmpeg 8.1.3 — источник P19

- Официальный архив: https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz
- Получен 2026-10-24; размер 11 732 036 байт; `RELEASE` внутри архива: `8.1.3`.
- SHA-256: `7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3`.
- Машиночитаемый pin: [source.lock](source.lock). Рецепт принимает только этот digest.
- Подпись: https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz.asc — загрузка не
  завершилась из-за сетевых таймаутов. **Подпись не проверена**; SHA-256 здесь
  идентифицирует полученный архив, но не заменяет проверку подписи. Ключ с
  https://ffmpeg.org/ffmpeg-devel.asc получен, сам по себе архив не удостоверяет.

X64FF, 2026-10-25: `.asc` (520 байт) и ключ (1709 байт) получены с указанных
адресов. Единственная попытка проверки GPG не завершилась: Windows-путь
`--homedir` был неверно обработан MSYS GPG, импорт ключа не состоялся,
результат `Can't check signature: No public key`. Повторов не было;
подпись по-прежнему **не проверена**.

Архив и распакованный неизменённый source находятся локально в игнорируемом
`dependencies/ffmpeg/8.1.3/`: `downloads/ffmpeg-8.1.3.tar.xz` и `ffmpeg-8.1.3/`.
Полный source, архив, полученные headers/import libs/DLL не включены в Git.
В Git сохраняются pin, configure args, рецепт и notices. `COPYING.LGPLv2.1`
и `LICENSE.md` скопированы из этого архива без изменения байтов.

Конфигурация: shared x64 MSVC, LGPL v2.1-or-later, явно отключены GPL,
nonfree, version3 и autodetect сторонних библиотек. Состав и назначение —
[COMPONENTS.md](COMPONENTS.md), команда — [README](../../tools/ffmpeg/README.md).
Рецепт не меняет upstream source. При будущей сборке сохраняет фактические
`config.h`, `config_components.h`, `ffbuild/config.mak` и `ffbuild/config.log`
в `dependencies/ffmpeg/8.1.3/x64/share/ffmpeg/`.

X64FF: сборка MSVC x64 завершена 2026-10-25 после подготовки MSYS2 владельцем
и исправления распознавания локализованного MSVC в рецепте. Получены DLL,
import libs и headers; configure сообщил `LGPL version 2.1 or later`, demuxer
`bink` и decoders `bink`, `binkaudio_dct`, `binkaudio_rdft`.
Общий локальный комплект: `work/deps64/ffmpeg/8.1.3/x64`.
Сохранены исходный архив, recipe и фактический configure receipt;
тесты, декодирование медиа и игровой хост не запускались.

Для будущего распространения DLL сохранять соответствующий полный исходный
архив, этот рецепт/параметры и notices рядом с комплектом поставки по процедуре
P24; один URL в манифесте не объявляется готовой поставкой исходников.
В пользовательский комплект P19 ничего не публикуется. Подключение к хосту,
AVIO, player и удаление Bink SDK относятся к P20/P21.
