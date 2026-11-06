# Сохранённая геометрия голема, GAV1

Источник G02–G04: Dejavu-plugin, коммит 421604cc78b4259db8be4ad5698525164a70e847, src/generated/golem_damage_data.h, golem_death_data.h, golem_spell_data.h. Перенесены 30 отлетающих частей, два ядра, 71 часть полной смерти и 12 вариантов лезвий. Исходные генераторы неизвестны и не восстанавливались. Модели/RES не меняются; сцена использует действующие djwarden и primary/secondary djwbody/djwblade → djwcrack/djwbreak, в hard rghbody/rghblade → rghcrack/rghbreak.

Импорт: Tools/import-golem-resources.mjs, importGolem(repository,destination,git). Выполняется только наш парсер литералов git show, код reference не запускается. Порядок, XYZ, UV, stage/hand/texture/bone сохранены. Нормали для G02–G04 рассчитывает стандартный GPU-путь по граням, CPU-освещения нет.

Little-endian: magic GAV1, uint32 version=1, uint32 part_count. Для каждой части 7 uint32 (kind,stage,texture,hand,actor,core,vertex_count), char bone[32], float offset[3], затем vertex_count записей XYZUV/NXYZ (8 float32) и uint32 kind. kind: 0 detach, 1 core, 2 full death, 3 blade, 4 ruby. Кратность трём, предел 60000 вершин, конечность чисел и все метаданные проверяет потребитель; неверный ресурс даёт ошибку и пропуск draw, без другой оболочки/рендера.

Голем: 115 частей, 1111620 байт. Позы костей и birth ticks событий сохраняются сервером в существующих party vars; клиент/renderer читает их без изменения симуляции. Земля запрашивается в точках эффекта, клиентская абсолютная высота не прибавляется повторно.

G05: ruby-v1.bin — 26 частей, 412716 байт, src/generated/ruby_actor_data.h на том же коммите. Сохранены XYZ/UV, нормали, kind, кости и authored offsets. Рост обелиска использует этот ресурс и текущий atlas 994520; живые/мёртвые актёры рисуются одним штатным retained model pass, их core получает отдельные параметры материала. Хост не дублирует тела и не рассчитывает CPU-освещение. Потоки используют secondary 994520; пыль — primary 994521.
