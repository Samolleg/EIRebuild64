# Сборка игровых DLL FFmpeg 9.0.2

Нужны MSVC v143 x64 с Windows SDK, MSYS2 bash/make и NASM. Из рабочего дерева rebuild64 откройте x64 Developer Command Prompt, добавьте в PATH каталоги MSYS2 ucrt64/bin и usr/bin. Запустите bash --noprofile --norc tools/ffmpeg/build.sh с одним аргументом — полным путём MSYS2 к новому рабочему каталогу без пробелов.

Рецепт сверяет SHA-256 архива из [source.lock](../../third_party/ffmpeg/source.lock). Используется собственный AVIO, демультиплексор Bink и декодеры Bink video, RDFT и DCT. Пять shared DLL собраны с MSVC, без CLI, сети и внешних кодеков. Выход — dependencies/ffmpeg/9.0.2/x64 плюс исходный архив в downloads. Для штатной сборки движка скопируйте x64 и downloads в deps/ffmpeg/9.0.2 проекта.

Сборка движка добавляет в комплект исходный архив, лицензию и точный рецепт. Программа проверки может передавать Bink через AVIO так же, как Game/VideoPlayer.cpp. Полная информация об архиве — в [PROVENANCE](../../third_party/ffmpeg/PROVENANCE.md).
