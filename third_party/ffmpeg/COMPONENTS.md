# Выбранные компоненты FFmpeg 8.1.3

Это состав рецепта [configure.args](configure.args). Фактический configure
ещё не выполнялся, итоговая замкнутая конфигурация будет в его generated files.

| Компонент | Назначение / исходник |
|---|---|
| `avformat`, demuxer `bink` | Чтение контейнера `.bik`; `libavformat/bink.c` |
| `avcodec`, decoder `bink` | Bink video; `libavcodec/bink.c` и его DSP |
| `avcodec`, decoder `binkaudio_rdft` | Bink audio RDFT; `libavcodec/binkaudio.c` |
| `avcodec`, decoder `binkaudio_dct` | Bink audio DCT; тот же исходник |
| `avutil` | Память, кадры, sample/pixel formats, внутренние transform/DSP helpers |
| `swscale` | Преобразование video frames для входов рендера P20 |
| `swresample` | Преобразование sample format/rate/layout в PCM для miniaudio P20 |

`configure` 8.1.3 выбирает для `bink` внутренние `blockdsp` и `hpeldsp`,
для обоих Bink audio — `wma_freqs`. Это helpers, а не включение WMA-декодера.
Необходимые внутренние зависимости и допустимые x64 SIMD реализации выбирает
сам upstream configure. Внешние codec libraries не подключаются.

Все остальные demuxers/decoders/encoders/muxers/parsers/filters/protocols,
программы (`ffmpeg`, `ffplay`, `ffprobe`), docs, сеть, avdevice и avfilter
отключены. Прямого file/network protocol нет: P20 передаёт ресурс через свой
AVIO, как предусмотрено планом x64; P19 не добавляет этот код в хост.

Сборка только DLL и MSVC import libraries: avformat, avcodec, avutil, swscale,
swresample; статические библиотеки FFmpeg отключены. Имена DLL с ABI-суффиксами
задаются pinned source; готовых бинарников пока нет.

Лицензия выбранной конфигурации — LGPL v2.1-or-later, тексты
[COPYING.LGPLv2.1](COPYING.LGPLv2.1) и upstream [LICENSE.md](LICENSE.md).
Заголовки copyright и permissive notices отдельных файлов остаются в полном
source archive. GPL/nonfree/version3 явно отключены; `--disable-autodetect`
не позволяет случайно подключить внешнюю библиотеку с машины сборщика.
Рецепт останавливается, если configure неожиданно включит эти три лицензирующих
режима. Это условие сборки, без пробного запуска DLL или декодера.
