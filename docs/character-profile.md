# Профиль человека human.v1

Редакция 1, glTF 2.0, контракт для следующих этапов универсального загрузчика и экспортёра. Нормативные значения находятся в [human.v1.json](../Data/profiles/human.v1.json). Поля `gltf` и `materials` задают контракт формата. X64GLTF02 добавляет загрузку и статическую позу GLB через retained GPU-путь фигур; границы текущего среза приведены ниже. Версия профиля меняется при изменении имён, иерархии, bind, осей, обязательных клипов или смысла metadata.

## Источники и решения v1

Скелет прочитан headless Blender 5.2.2 из `tasks/NALA378/armed-key-draft.blend`, объект `Nala Armature 378`, datablock `Nala — complete author skeleton (378)`. SHA-256 файла сохранён в `provenance.blendSha256`. Прочитаны `Armature.data.bones[].matrix_local` и родители, которые задают rest/bind. Текущий кадр, pose constraints и активный action в bind не включаются. Это сохранённая Blender-арматура переноса bahonec, исходная ревизия автора `66a6036da4b365da3761f4268aa4f10509bc75cd`.

Сверены `tasks/NALA378/wt/pz_skn/nala_rig.py` и `all-animation-contract.json`. В переносе ортогональная Blender-база B отличается от полного авторского bind A со stretch. Авторский evaluator использует `P_B = P_author × inverse(A) × B`. Экспорт v1 получает B и запечённую позу в этой базе. Повторное применение A, контроллеров автора или персональной коррекции Налы при загрузке изменит результат. Остаточные post-skin corrections должны быть предварительно запечены в представимые glTF-каналы/морфы с сохранением результата. Их перенос относится к отдельной задаче ассета.

Игровая семантика взята из `Game/adFormat.h`, `Unit.cpp`, `UnitServer.cpp`, `UnitClientSpecific.cpp`, `ModAnimationRules.h` и GLTF00-MAP §1.5. Конкретные ADB-записи прочитаны из сохранённого `tasks/FIGSKN02A/wt/models/assets/unhufe-skn-prototype-r1/source/source-info.json`, используемого NALA378. Его SHA-256 указан в JSON. Каталог содержит 325 исходных имён, из них 253 имеют ADB-записи. Клипы без записи доступны через явную привязку сценария.

Новые решения v1 включают минимальный обязательный скелет, сокеты, базовые имена клипов, extras и реестр ресурсов. Их геометрия и художественная пригодность требуют последующей приёмки модели и вещей владельцем.

## Оси, единицы и bind

Все значения профиля в метрах. Правосторонняя модель имеет +Y вверх, +Z вперёд и −X вправо персонажа. Исходная Blender-арматура имеет +Z вверх, −Y вперёд, что согласуется с направлением от стопы к пальцам. Преобразование `C` переводит `(x,y,z)` в `(x,z,−y)`. Сцена использует METRIC и scale_length=1.

Сохранённый `object.matrix_world` содержит положение и масштаб сцены около 0.931. Нормализация v1 использует локальную rest-арматуру и единичный контейнер модели. Позиция исходного корня `FemaleAvatar` вычитается из всех global bind. Корень профиля находится в начале координат. Эта точка является origin скелета, опорная плоскость сетки задаётся при подготовке ассета. Персональный scene scale Налы в профиль не переносится.

Для каждой исходной глобальной матрицы B определяется `G = C × T(−sourceRootPosition) × B × inverse(C)`. Для ребёнка `L = inverse(Gparent) × G`, для корня `L = G`. Именно L хранится как translation, rotation и scale в JSON. Родительская поза составляется `Gchild = Gparent × Lchild`. Матрицы хранятся column-major и умножают векторы-столбцы. Quaternion имеет порядок x,y,z,w. `inverseBindMatrix = inverse(G)` задаёт skin palette `Gpose × inverseBindMatrix` в model space. World placement применяется единожды после skinning.

Экспортёр преобразует mesh positions, normals, tangents, morph deltas, bind и animation одной согласованной системой. Обычный glTF Y-up export Blender требуется настроить с учётом уже выполненной нормализации. Двойная конверсия C недопустима. Для движка Z-up обратное преобразование `(x,y,z) → (x,−z,y)` выполняется на границе model/world.

Эталон содержит 378 узлов. Обязательны 26 анатомических узлов с полной цепочкой предков. Остальные 352 узла, включая пальцы, лицо и дополнительные корректоры, опциональны. Нала сохраняет полный авторский набор. Упрощённый новый человек может исключить опциональные ветви. Если узел присутствует, сохраняются его точное имя, исходный родитель и bind. Предки любого присутствующего узла тоже присутствуют. Ссылки weights и animation на исключённые узлы запрещены. Ненулевые влияния исключаемой кости должны быть явно перераспределены при авторинге. Неизвестные имена joints требуют следующей версии профиля. Сокеты являются отдельными узлами metadata.

Точность bind составляет 1e-4 на компоненту матрицы в метрах, норма quaternion отличается от 1 максимум на 1e-4. Сравнение rotations выполняется через матрицы, поскольку q и −q эквивалентны. Все числа конечны, scale положителен, матрицы обратимы. `G × inverseBindMatrix` должен совпадать с единичной матрицей в пределах 1e-4. Узлы имеют уникальные имена, иерархия ациклична, корень один. Отсутствующие animation channels используют bind TRS.

## Сокеты и вещи

Пять сокетов обязательны для тела. Их TRS относительно родительской кости перечислены в JSON. Эти численные значения определяют единственную нормативную рамку. Приведённые ниже offsets выражены в осях модели в bind и пересчитаны в локальные оси кости при создании профиля.

| Сокет | Родитель | Offset в bind, м |
|---|---|---|
| socket_hand_r | CC_Base_Hand.R | (0, 0.06, 0) |
| socket_hand_l | CC_Base_Hand.L | (0, 0.06, 0) |
| socket_back | CC_Base_Spine02 | (0, 0, −0.15) |
| socket_belt | CC_Base_Pelvis | (0.16, 0, 0) |
| socket_head | CC_Base_Head | (0, 0.18, 0) |

В bind оси всех сокетов совпадают с осями модели. Локальное вращение сокета компенсирует bind-вращение его кости. В движении сокет следует полной текущей матрице родителя. Для кистей +Y означает направление от рукояти к острию, +Z задаёт направление режущей кромки/прицела, +X дополняет правостороннюю рамку. Левая кисть использует тот же договор без отрицательного масштаба.

Rigid item задаёт `asset.extras.ei.kind="rigid-item"`, `profile`, `socket` и `grip` TRS. Grip представляет рамку хвата в координатах предмета. Матрица предмета равна `Wbody × GbonePose × Lsocket × inverse(Ggrip)`. Identity grip помещает origin предмета в центр сокета. Щит, лук и предмет на поясе задают собственный grip. Для двуручного хвата вторая кисть управляется анимацией. Автоматическое решение IK выходит за контракт v1. Сокеты являются предложенными стандартными рамками, прежние fit-offsets NALA378 из legacy attachment-пути в них не копируются.

## Выбор клипа и ADB

Обязательные уникальные `animations[].name` приведены в таблице. Все девять клипов присутствуют в каждом body GLB.

| Имя v1 | Цикл | Исходный ориентир | События |
|---|---|---|---|
| idle | да | cidle01 | по authored motion |
| walk | да | cwalk01 | минимум один footstep |
| run | да | crun01 | минимум один footstep |
| attack_unarmed | нет | uattack11 | hit |
| death | нет | udeath01 | по authored motion |
| suffer | нет | usuffer01 | по authored motion |
| cast | нет | выбранный ADB_ACTION_CAST | release |
| cross | нет | ucross01 | по authored motion |
| special | нет | uspecial19 | по authored motion |

Это примеры семантики. Импортёр определяет допустимость соответствия по flags конкретной записи. `uattack11` имеет нулевую weapon mask, `uattack04` содержит SWORD. Перенос оружейного uattack04 на базовый безоружный удар требует отдельного авторинга. `legacyBindings` содержит все 253 исходных ADB-записи с именем, индексом, flags, flags2, длительностью, вероятностью, скоростью и полями событий. `legacyClipCatalog` перечисляет все 325 имён, включая сценарные клипы без ADB. Будущий экспортёр добавляет нужные overrides в `asset.extras.ei.bindings`.

Запись binding имеет `state`, `action`, `stage`, `weaponMask`, `modifierBits`, `clip`. Поля запроса совпадают с декодированными значениями JSON `animation.adbConstants`. Все пять полей обязательны, mask сравнивается по действующим Match/MatchNoWeapon. Альтернативная запись `{ "legacyClip": "uattack04", "clip": "attack_sword" }` связывает уже выбранное игрой имя. Несколько совпадающих правил одного приоритета являются ошибкой данных.

Состояния включают neutral, rest, attack, warry, lie. Действия включают special, attack_unarmed (базовое имя для ACTION_ATTACK при любом оружии), cast, run, walk, idle, death, suffer, cross. Стадии включают start, cycle, end, unique. Оружейные биты включают sword, axe, dagger, spear, hammer, bow, crossbow. Неименованные биты сохраняются как mask. Модификаторы атаки/колдовства задают горизонталь left/middle/right и вертикаль up/middle/down. Для suffer/death используются MM/FM/BM/ML/MR и run/walk/stay. Для walk сохраняются lame1/lame2, для idle stay/look, для cross целевая поза, для special числовой код. Полная побитовая карта хранится в JSON.

Порядок разрешения после игрового выбора ADB следующий.

1. Точный override выбранного legacyClip.
2. Полный tuple state/action/stage/weapon/modifiers.
3. Среднее направление атаки middle/middle, как в GetAttackAnimation.
4. Безоружный вариант того же действия, состояния, стадии и модификаторов.
5. Базовый клип действия.

Базовый клип разрешён для основных девяти действий с unique/cycle семантикой. Для отсутствующих start/end допускается нулевой визуальный переход к найденному основному клипу при сохранении host duration. Cross с конкретной целевой позой и специальные действия с конкретным кодом требуют явного binding для этого кода. Это позволяет сохранить переходы lie/rest, навыки, сценарные и briefing-анимации. Сборка персонажа с запрошенной сценарной возможностью отклоняется при отсутствии её binding. Конечный неразрешённый запрос возвращает диагностическую ошибку с полным tuple. Для lie/rest движения должны иметь явные варианты, чтобы лежащий персонаж сохранял позу. Базовый fallback для этих состояний запрещён, кроме death. JSON содержит эти ограничения отдельно.

Игровой выбор ADB, вероятность и модовые 2hs/2ha/dat, staff remaps сохраняются в host. Они предшествуют таблице визуальных клипов. Время берётся из ADB/сохранения. При source frames N и фазе f нормализованная фаза равна f/N, а визуальное время равно этой фазе, умноженной на длительность glTF-клипа. Cycle использует остаток по единице, unique зажимает фазу на конце. Восстановление из сохранения устанавливает текущую фазу без повторного проигрывания прошлых событий. Анимации in-place, движение по миру и скорость остаются у игровой логики. Death удерживает последний кадр.

## События

`animations[i].extras.ei.events` содержит отсортированный массив событий. Каждая запись имеет уникальный внутри клипа `id`, `name`, `time` в секундах и `payload` object. Время конечно и принадлежит [0,duration]. Пример приведён ниже.

```json
{"ei":{"events":[{"id":"step-1","name":"footstep","time":0.2,"payload":{"foot":"left"}},{"id":"impact-1","name":"hit","time":0.4,"payload":{}}]}}
```

Маркеры Blender служат авторским вводом. В GLB сохраняется именно extras, поскольку glTF 2.0 не определяет игровые markers. Допускается несколько событий с одинаковым временем, порядок определяется id.

| Имя | Источник ADB | Payload / назначение |
|---|---|---|
| hit | dwHit при IsHaveHit | момент контакта, пустой payload |
| release | dwHit у ranged attack/cast | выпуск снаряда/заклинания |
| footstep | dwSteps до первого нуля при IsHaveSteps | foot left/right/unspecified |
| sound | dwSFXFrame, dwSFXIndices при соответствующих flags | indices с исходными кодами звуков |
| attachment_visibility | dwHides[0], dwHides[1] при IsHaveUnhide | target projectile, visible true/false |
| action_end | duration | конец unique-действия |

Время исходного frame k переводится в секунды k/15, затем при иной длительности в `k/N × durationGLTF`. Нулевые sentinel в dwSteps и dwSFXFrame не создают events. Для dwHit и dwHides допустимость нулевого кадра определяется flags и действием. Исходные source values сохраняются в legacyBindings для аудита. ADB не определяет левую/правую стопу, поэтому при отсутствии authored указания используется unspecified. DwHit для death/suffer сам по себе не означает нанесение урона. Его смысл определяется действием и host-контекстом.

Host остаётся единственным источником урона, снарядов, звука и видимости стрелы. Импортёр согласует фазу event с соответствующим выбранным ADB, расхождение более 1/15 секунды исходной длительности отклоняет binding. Визуальный sampler отдаёт фазу и events адаптеру host, который дедуплицирует событие по action-instance/cycle/event-id. Интервал доставки (previous,current], loop разбивается на конец и начало цикла, time=0 доставляется один раз при начале действия/цикла. Seek/load/reverse sampling не воспроизводят исторические события. Эти правила должны быть реализованы на этапе анимационной интеграции.

## Зоны тела, одежда и morphs

Каждый body primitive имеет `extras.ei.zone` из torso, arms, legs, feet, head. Primitive целиком принадлежит одной зоне. Границы зон разрезаются при экспорте с дублированием вершин по необходимости. Torso включает шею до стыка головы, arms включают кисти, legs заканчиваются у щиколоток, feet включают пальцы, head включает лицо и волосы. Для волос можно выделить отдельный primitive той же зоны head.

Одежда содержит `asset.extras.ei` с profile, kind=garment, hides (уникальные имена зон) и morphSet. Итоговая маска скрытых primitives тела равна объединению hides всех надетых вещей. После снятия вещи маска пересчитывается по оставшимся. Невидимые вещи не скрывают тело. Маска применяется только к body. Слои одежды определяются слотами предметов в данных игры. Частичное скрытие внутри primitive требует разбиения сетки при авторинге.

Skin одежды использует те же имена/иерархию/bind. Rigid items могут иметь ноль skins. Максимум четыре ненулевых влияния на вершину, JOINTS_1/WEIGHTS_1 отклоняются, веса конечны, неотрицательны и нормированы с допуском 1e-4. Морфы POSITION/NORMAL/TANGENT применяются до linear skinning. `mesh.extras.targetNames` задаёт уникальные имена morphs. Body и garment с morphSet должны иметь одинаковый упорядоченный набор имён, диапазоны и defaults из `asset.extras.ei.morphs` (записи name/min/max/default), общие weights передаются по имени. Различие morphSet отклоняет экипировку. Для модели без морфов используется пустой набор и одинаковый morphSet. Название human.default.v1 в JSON является примером идентификатора набора, его фактические shape-каналы определяются отдельными данными ассета.

## Материалы и гамма

Используется core glTF metallic-roughness. POSITION и NORMAL обязательны. Для текстур обязателен соответствующий TEXCOORD_0/1. BaseColor factor и остальные factors берутся с glTF defaults. Карты baseColor, normal, metallicRoughness, occlusion и emissive опциональны. Normal map требует tangent VEC4 с handedness, при отсутствии tangent импортёр строит MikkTSpace с разделением швов. Нормали единичны, преобразуются inverse-transpose с нормализацией. Normal texture использует glTF tangent-space +Y. Occlusion читается из R, roughness из G, metallic из B. Раздельные textures/UV для occlusion и metallicRoughness сохраняются, импортёр может собрать ORM только с корректным пересэмплированием.

BaseColor RGB и emissive RGB textures имеют sRGB encoding, alpha линейна. Normal/ORM линейны. BaseColorFactor и emissiveFactor являются линейными множителями. Цветовые операции glTF выполняются после sRGB decode. На границе текущего display-encoded renderer результат кодируется sRGB ровно один раз. Исправление `4edc1a2`/`skn_runtime.h` читает BC7 sRGB DDS через UNORM SRV, сохраняя display-encoded цвет игры. Будущий glTF import должен учитывать эту границу и исключить дополнительный SRV decode уже подготовленного цвета. Реализация полностью linear lighting потребует согласованного изменения границы. Прямой перенос glTF texture в нынешний SRV без согласования factors и lighting не подтверждает сохранность цвета.

OPAQUE игнорирует alpha при расчёте покрытия. MASK использует alpha texture × baseColorFactor.a и cutoff=0.5 по умолчанию. BLEND использует стандартную прозрачность и соответствующую сортировку. Alpha не кодирует класс материала, раны или свечение. doubleSided следует glTF, при двустороннем shading учитывается ориентация нормали. Цвет кожи и волосы требуют последующей визуальной приёмки на D3D11/D3D12.

## Подмножество glTF v1

Контейнер GLB 2.0 содержит buffers и PNG/JPEG images внутри файла. Абсолютные пути, внешние URI, data URI и сетевые ресурсы отклоняются. Mesh primitives используют TRIANGLES. Поддерживаются core accessors, включая normalized integers и sparse, node TRS либо matrix по правилам glTF, skins с inverseBindMatrices и morph targets. Анимируемые узлы задают TRS, локальный shear при экспорте должен быть устранён с сохранением результата либо экспорт завершается ошибкой. Для тела используется один skin. Допускаются joints подмножеством эталона при выполнении правил обязательности.

Каналы animations включают translation, rotation, scale, weights. Поддерживаются STEP, LINEAR и CUBICSPLINE с glTF tangent/time scaling, quaternion normalization и shortest-path LINEAR rotation. Один animation содержит не более одного канала на node/path. Cameras/lights не используются в персонаже, активная сцена и body root однозначны.

Allowlist extensions состоит из KHR_materials_unlit и KHR_texture_transform (offset/rotation/scale/texCoord, UV0/1). Draco, meshopt, BasisU/KTX2, material variants, transmission, volume, clearcoat и прочие extensions в v1 не реализуются. Любое неизвестное имя в extensionsRequired означает отказ. Optional extension допускается игнорировать только при наличии полноценного core fallback. Отсутствие доступного core buffer/image/material является ошибкой. Поддержка morphs и extensions в этом разделе является требованием к будущему загрузчику.

## Привязка и ресурсы

Пак публикует `profiles/human.v1.json`, `models/registry.v1.json`, `models/characters/<id>.glb` и `models/items/<id>.glb`. Репозиторный источник профиля находится в Data/profiles, при упаковке префикс Data удаляется. Идентификаторы lowercase ASCII соответствуют `^[a-z0-9][a-z0-9_-]*$`, resource keys используют `/`, без drive, абсолютного корня и `..`. GLB самодостаточен.

Версионированный реестр имеет словари characters и items. Их ключи совпадают с устойчивыми visual/model identifiers прототипа или shape предмета в данных игры. При выборе visualId host разрешает запись реестра и её resource/profile. Это новая спецификация data binding для следующих этапов. Пример содержится в JSON resources.example. При замене существующего visualId запись реестра задаёт новый формат. При отсутствии записи сохраняется существующий выбор legacy-ресурса. Для записи с GLB ошибочный ресурс или profile даёт диагностический отказ.

Metadata `asset.extras.ei.profile` обязана совпадать с записью реестра. `kind` принимает body, garment или rigid-item. Body содержит skeleton/sockets, garment задаёт hides и morphSet, rigid-item задаёт socket/grip. Числа TRS имеют фиксированные размеры 3/4/3, default grip identity допустим только при явно указанном grip. Неизвестные зоны, sockets, clip names в bindings, версии и неоднозначные записи отклоняются до публикации визуала. Registry/profile/GLB публикуются одной транзакцией PAK. ResourceStore сохраняет identity и mount generation, которые входят в ключи кэша вместе с identity зависимого профиля.

## Граница готовности

Подготовлены документ и численный профиль по прочитанным исходникам. Headless Blender использован для чтения сохранённой арматуры и формирования представления bind. Сборки и отдельные тесты заданием запрещены либо не назначены. Runtime загрузка, визуальная проверка гаммы/хвата/деформации, точность экспорта и игровое воспроизведение ещё требуют следующих этапов GLTF00-MAP. Владельцу остаётся художественно принять предложенные сокеты и базовые движения на реальных ассетах.

## Имена и иерархия эталона

Таблица перечисляет все исходные кости в сохранённом порядке. Полные численные translation/rotation/scale и inverse bind находятся в JSON skeleton.bones.

| Кость | Родитель | Обязательна |
|---|---|---|
| FemaleAvatar | — | да |
| CC_Base_BoneRoot | FemaleAvatar | да |
| CC_Base_Hip | CC_Base_BoneRoot | да |
| CC_Base_Waist | CC_Base_Hip | да |
| CC_Base_Spine01 | CC_Base_Waist | да |
| CC_Base_Spine02 | CC_Base_Spine01 | да |
| CC_Base_Clavicle.R | CC_Base_Spine02 | да |
| CC_Base_Upperarm.R | CC_Base_Clavicle.R | да |
| CC_Base_UpperarmTwist01.R | CC_Base_Upperarm.R | нет |
| CC_Base_UpperarmTwist02.R | CC_Base_UpperarmTwist01.R | нет |
| CC_Base_Forearm.R | CC_Base_Upperarm.R | да |
| CC_Base_Elbow.R | CC_Base_Forearm.R | нет |
| CC_Base_ForearmTwist01.R | CC_Base_Forearm.R | нет |
| CC_Base_ForearmTwist02.R | CC_Base_ForearmTwist01.R | нет |
| CC_Base_Hand.R | CC_Base_Forearm.R | да |
| CC_Base_Finger00.R | CC_Base_Hand.R | нет |
| CC_Base_Finger01.R | CC_Base_Finger00.R | нет |
| CC_Base_Finger02.R | CC_Base_Finger01.R | нет |
| CC_Base_Finger02Nub.R | CC_Base_Finger02.R | нет |
| CC_Base_Finger10.R | CC_Base_Hand.R | нет |
| CC_Base_Finger11.R | CC_Base_Finger10.R | нет |
| CC_Base_Finger12.R | CC_Base_Finger11.R | нет |
| CC_Base_Finger12.R.001 | CC_Base_Finger12.R | нет |
| CC_Base_Finger20.R | CC_Base_Hand.R | нет |
| CC_Base_Finger21.R | CC_Base_Finger20.R | нет |
| CC_Base_Finger22.R | CC_Base_Finger21.R | нет |
| CC_Base_Finger22.R.001 | CC_Base_Finger22.R | нет |
| CC_Base_Finger30.R | CC_Base_Hand.R | нет |
| CC_Base_Finger31.R | CC_Base_Finger30.R | нет |
| CC_Base_Finger32.R | CC_Base_Finger31.R | нет |
| CC_Base_Finger32.R.001 | CC_Base_Finger32.R | нет |
| CC_Base_Finger40.R | CC_Base_Hand.R | нет |
| CC_Base_Finger41.R | CC_Base_Finger40.R | нет |
| CC_Base_Finger42.R | CC_Base_Finger41.R | нет |
| CC_Base_Finger42.R.001 | CC_Base_Finger42.R | нет |
| DEF_Clavicle.R | CC_Base_Clavicle.R | нет |
| CC_Base_Clavicle.L | CC_Base_Spine02 | да |
| CC_Base_Upperarm.L | CC_Base_Clavicle.L | да |
| CC_Base_UpperarmTwist01.L | CC_Base_Upperarm.L | нет |
| CC_Base_UpperarmTwist02.L | CC_Base_UpperarmTwist01.L | нет |
| CC_Base_Forearm.L | CC_Base_Upperarm.L | да |
| CC_Base_Elbow.L | CC_Base_Forearm.L | нет |
| CC_Base_ForearmTwist01.L | CC_Base_Forearm.L | нет |
| CC_Base_ForearmTwist02.L | CC_Base_ForearmTwist01.L | нет |
| CC_Base_Hand.L | CC_Base_Forearm.L | да |
| CC_Base_Finger00.L | CC_Base_Hand.L | нет |
| CC_Base_Finger01.L | CC_Base_Finger00.L | нет |
| CC_Base_Finger02.L | CC_Base_Finger01.L | нет |
| CC_Base_Finger02Nub.L | CC_Base_Finger02.L | нет |
| CC_Base_Finger10.L | CC_Base_Hand.L | нет |
| CC_Base_Finger11.L | CC_Base_Finger10.L | нет |
| CC_Base_Finger12.L | CC_Base_Finger11.L | нет |
| CC_Base_Finger12.L.001 | CC_Base_Finger12.L | нет |
| CC_Base_Finger20.L | CC_Base_Hand.L | нет |
| CC_Base_Finger21.L | CC_Base_Finger20.L | нет |
| CC_Base_Finger22.L | CC_Base_Finger21.L | нет |
| CC_Base_Finger22.L.001 | CC_Base_Finger22.L | нет |
| CC_Base_Finger30.L | CC_Base_Hand.L | нет |
| CC_Base_Finger31.L | CC_Base_Finger30.L | нет |
| CC_Base_Finger32.L | CC_Base_Finger31.L | нет |
| CC_Base_Finger32.L.001 | CC_Base_Finger32.L | нет |
| CC_Base_Finger40.L | CC_Base_Hand.L | нет |
| CC_Base_Finger41.L | CC_Base_Finger40.L | нет |
| CC_Base_Finger42.L | CC_Base_Finger41.L | нет |
| CC_Base_Finger42.L.001 | CC_Base_Finger42.L | нет |
| DEF_Clavicle.L | CC_Base_Clavicle.L | нет |
| CC_Base_Ribs.R | CC_Base_Spine02 | нет |
| CC_Base_Ribs.L | CC_Base_Spine02 | нет |
| PeChoBase.R | CC_Base_Spine02 | нет |
| DEF_PechoBase.R | PeChoBase.R | нет |
| PeChoBase.L | CC_Base_Spine02 | нет |
| DEF_PechoBase.L | PeChoBase.L | нет |
| DEF_Clavicle.L.001 | CC_Base_Spine02 | нет |
| DEF_Clavicle.R.001 | CC_Base_Spine02 | нет |
| DEF_Spine02 | CC_Base_Spine02 | нет |
| CC_Base_NeckTwist01 | CC_Base_Spine02 | да |
| DEF_NeckTwist01 | CC_Base_NeckTwist01 | нет |
| CC_Base_NeckTwist02 | CC_Base_NeckTwist01 | да |
| CC_Base_Head | CC_Base_NeckTwist02 | да |
| CC_Base_FacialBone | CC_Base_Head | нет |
| CC_Base_JawRoot | CC_Base_FacialBone | нет |
| CC_Base_Tongue01 | CC_Base_JawRoot | нет |
| CC_Base_Tongue02 | CC_Base_Tongue01 | нет |
| CC_Base_Tongue03 | CC_Base_Tongue02 | нет |
| CC_Base_Teeth02 | CC_Base_JawRoot | нет |
| CC_Base_UpperJaw | CC_Base_FacialBone | нет |
| CC_Base_EyeBall.R | CC_Base_FacialBone | нет |
| DEF_SkinOjoCara.R | CC_Base_FacialBone | нет |
| DEF_SkinOjoParpado_Inf.R | DEF_SkinOjoCara.R | нет |
| DEF_SkinOjoParpado_Sup.R | DEF_SkinOjoCara.R | нет |
| CC_Base_EyeBall.L | CC_Base_FacialBone | нет |
| DEF_SkinOjoCara.L | CC_Base_FacialBone | нет |
| DEF_SkinOjoParpado_Inf.L | DEF_SkinOjoCara.L | нет |
| DEF_SkinOjoParpado_Sup.L | DEF_SkinOjoCara.L | нет |
| TValle_NarizSkin_DEF_SCALER | CC_Base_FacialBone | нет |
| TValle_NarizSkin_POINT | TValle_NarizSkin_DEF_SCALER | нет |
| TValle_NarizSkin_Tip | TValle_NarizSkin_POINT | нет |
| TValle_NarizSkin_DEF_STRETCHED | TValle_NarizSkin_POINT | нет |
| LabiosRoot | CC_Base_FacialBone | нет |
| Labio_Up.Center | LabiosRoot | нет |
| Labio_Up_DEF.L | Labio_Up.Center | нет |
| Labio_Up_DEF.R | Labio_Up.Center | нет |
| Labio_Down.Center | LabiosRoot | нет |
| Labio_Down_DEF.L | Labio_Down.Center | нет |
| Labio_Down_DEF.R | Labio_Down.Center | нет |
| Labio_Up.L | LabiosRoot | нет |
| Labio_Up_DEF.L.001 | Labio_Up.L | нет |
| Labio_Up.R | LabiosRoot | нет |
| Labio_Up_DEF.R.001 | Labio_Up.R | нет |
| Labio_Down.L | LabiosRoot | нет |
| Labio_Down_DEF.L.001 | Labio_Down.L | нет |
| Labio_Down.R | LabiosRoot | нет |
| Labio_Down_DEF.R.001 | Labio_Down.R | нет |
| Labio.L | LabiosRoot | нет |
| Labio.R | LabiosRoot | нет |
| Labios.Closing | LabiosRoot | нет |
| Labio_Up_Out.R | LabiosRoot | нет |
| Labio_Up_Out_STRE.R | Labio_Up_Out.R | нет |
| Labio_Up_Out_DEF.R | Labio_Up_Out_STRE.R | нет |
| Labio_Up_Out.L | LabiosRoot | нет |
| Labio_Up_Out_STRE.L | Labio_Up_Out.L | нет |
| Labio_Up_Out_DEF.L | Labio_Up_Out_STRE.L | нет |
| Labio_Out.L | LabiosRoot | нет |
| Labio_Out_STRE.L | Labio_Out.L | нет |
| Labio_Out_DEF.L | Labio_Out_STRE.L | нет |
| Labio_Out.R | LabiosRoot | нет |
| Labio_Out_STRE.R | Labio_Out.R | нет |
| Labio_Out_DEF.R | Labio_Out_STRE.R | нет |
| Labio_Down_Out.R | LabiosRoot | нет |
| Labio_Down_Out_STRE.R | Labio_Down_Out.R | нет |
| Labio_Down_Out_DEF.R | Labio_Down_Out_STRE.R | нет |
| Labio_Down_Out.L | LabiosRoot | нет |
| Labio_Down_Out_STRE.L | Labio_Down_Out.L | нет |
| Labio_Down_Out_DEF.L | Labio_Down_Out_STRE.L | нет |
| Labio_Down_Out.Center | LabiosRoot | нет |
| Labio_Down_Out_STRE.Center | Labio_Down_Out.Center | нет |
| Labio_Down_Out_DEF.Center | Labio_Down_Out_STRE.Center | нет |
| Labio_Up_Out.Center | LabiosRoot | нет |
| Labio_Up_Out_STRE.Center | Labio_Up_Out.Center | нет |
| Labio_Up_Out_DEF.Center | Labio_Up_Out_STRE.Center | нет |
| DEF_NeckTwist02 | CC_Base_NeckTwist02 | нет |
| PHYSICS_PechoTrack.R | CC_Base_Spine02 | нет |
| PHYSICS_PechoTrack.L | CC_Base_Spine02 | нет |
| PHYSICS_SCALER_Pecho.R | CC_Base_Spine02 | нет |
| PHYSICS_Pecho.R | PHYSICS_SCALER_Pecho.R | нет |
| DEF_Pecho.R | PHYSICS_Pecho.R | нет |
| PHYSICS_Pecho.R.001 | PHYSICS_SCALER_Pecho.R | нет |
| DEF_Pecho.R.001 | PHYSICS_Pecho.R.001 | нет |
| PHYSICS_Pecho.R.002 | PHYSICS_SCALER_Pecho.R | нет |
| DEF_Pezon.R | PHYSICS_Pecho.R.002 | нет |
| PHYSICS_SCALER_Pecho.L | CC_Base_Spine02 | нет |
| PHYSICS_Pecho.L | PHYSICS_SCALER_Pecho.L | нет |
| DEF_Pecho.L | PHYSICS_Pecho.L | нет |
| PHYSICS_Pecho.L.001 | PHYSICS_SCALER_Pecho.L | нет |
| DEF_Pecho.L.001 | PHYSICS_Pecho.L.001 | нет |
| PHYSICS_Pecho.L.002 | PHYSICS_SCALER_Pecho.L | нет |
| DEF_Pezon.L | PHYSICS_Pecho.L.002 | нет |
| DEF_Spine01 | CC_Base_Spine01 | нет |
| CC_Base_Pelvis | CC_Base_Hip | да |
| CC_Base_Thigh.R | CC_Base_Pelvis | да |
| CC_Base_ThighTwist01.R | CC_Base_Thigh.R | нет |
| CC_Base_ThighTwist02.R | CC_Base_ThighTwist01.R | нет |
| CC_Base_Calf.R | CC_Base_Thigh.R | да |
| CC_Base_Knee.R | CC_Base_Calf.R | нет |
| CC_Base_CalfTwist01.R | CC_Base_Calf.R | нет |
| CC_Base_CalfTwist02.R | CC_Base_CalfTwist01.R | нет |
| CC_Base_Foot.R | CC_Base_Calf.R | да |
| CC_Base_ToeBase.R | CC_Base_Foot.R | да |
| CC_Base_Toe00.R | CC_Base_ToeBase.R | нет |
| CC_Base_Toe10.R | CC_Base_ToeBase.R | нет |
| CC_Base_Toe20.R | CC_Base_ToeBase.R | нет |
| CC_Base_Toe30.R | CC_Base_ToeBase.R | нет |
| CC_Base_Toe40.R | CC_Base_ToeBase.R | нет |
| TValle_Knee_Fix.R | CC_Base_Thigh.R | нет |
| TValle_UpLegFix.R | CC_Base_Pelvis | нет |
| CC_Base_Thigh.L | CC_Base_Pelvis | да |
| CC_Base_ThighTwist01.L | CC_Base_Thigh.L | нет |
| CC_Base_ThighTwist02.L | CC_Base_ThighTwist01.L | нет |
| CC_Base_Calf.L | CC_Base_Thigh.L | да |
| CC_Base_Knee.L | CC_Base_Calf.L | нет |
| CC_Base_CalfTwist01.L | CC_Base_Calf.L | нет |
| CC_Base_CalfTwist02.L | CC_Base_CalfTwist01.L | нет |
| CC_Base_Foot.L | CC_Base_Calf.L | да |
| CC_Base_ToeBase.L | CC_Base_Foot.L | да |
| CC_Base_Toe40.L | CC_Base_ToeBase.L | нет |
| CC_Base_Toe30.L | CC_Base_ToeBase.L | нет |
| CC_Base_Toe20.L | CC_Base_ToeBase.L | нет |
| CC_Base_Toe10.L | CC_Base_ToeBase.L | нет |
| CC_Base_Toe00.L | CC_Base_ToeBase.L | нет |
| TValle_Knee_Fix.L | CC_Base_Thigh.L | нет |
| TValle_UpLegFix.L | CC_Base_Pelvis | нет |
| DEF_CC_Base_Pelvis | CC_Base_Pelvis | нет |
| DEF_PelvisLat.L | CC_Base_Pelvis | нет |
| DEF_PelvisLat.R | CC_Base_Pelvis | нет |
| Gluten_Aperture.R | CC_Base_Pelvis | нет |
| Gluten_Aperture.L | CC_Base_Pelvis | нет |
| Anus_Root | CC_Base_Pelvis | нет |
| Anus_Hole.F | Anus_Root | нет |
| DEF_Anus_Hole.R.000 | Anus_Hole.F | нет |
| DEF_Anus_Hole.L.000 | Anus_Hole.F | нет |
| Anus_Hole.B | Anus_Root | нет |
| Anus_Hole.R.002 | Anus_Root | нет |
| DEF_Anus_Hole.R.002 | Anus_Hole.R.002 | нет |
| Anus_Hole.L.002 | Anus_Root | нет |
| DEF_Anus_Hole.L.002 | Anus_Hole.L.002 | нет |
| Anus_Hole.R.001 | Anus_Root | нет |
| DEF_Anus_Hole.R.001 | Anus_Hole.R.001 | нет |
| Anus_Hole.L.001 | Anus_Root | нет |
| DEF_Anus_Hole.L.001 | Anus_Hole.L.001 | нет |
| Anus_Hole.R.003 | Anus_Root | нет |
| DEF_Anus_Hole.R.003 | Anus_Hole.R.003 | нет |
| Anus_Hole.L.003 | Anus_Root | нет |
| DEF_Anus_Hole.L.003 | Anus_Hole.L.003 | нет |
| Anus_Inner_Hole_Root | Anus_Root | нет |
| DEF_Anus_Inner_Hole.000 | Anus_Inner_Hole_Root | нет |
| Anus_Hole_Out_Stretch_Target.F | Anus_Root | нет |
| STRETCH_Anus_Hole_Out.F | Anus_Hole_Out_Stretch_Target.F | нет |
| DEF_Anus_Hole_Out.F | STRETCH_Anus_Hole_Out.F | нет |
| Anus_Hole_Out_Stretch_Target.B | Anus_Root | нет |
| STRETCH_Anus_Hole_Out.B | Anus_Hole_Out_Stretch_Target.B | нет |
| DEF_Anus_Hole_Out.B.001 | STRETCH_Anus_Hole_Out.B | нет |
| Anus_Hole_Out_Stretch_Target.R.002 | Anus_Root | нет |
| STRETCH_Anus_Hole_Out.R.002 | Anus_Hole_Out_Stretch_Target.R.002 | нет |
| DEF_Anus_Hole_Out.R.002 | STRETCH_Anus_Hole_Out.R.002 | нет |
| Anus_Hole_Out_Stretch_Target.R.001 | Anus_Root | нет |
| STRETCH_Anus_Hole_Out.R.001 | Anus_Hole_Out_Stretch_Target.R.001 | нет |
| DEF_Anus_Hole_Out.R.001 | STRETCH_Anus_Hole_Out.R.001 | нет |
| Anus_Hole_Out_Stretch_Target.R.003 | Anus_Root | нет |
| STRETCH_Anus_Hole_Out.R.003 | Anus_Hole_Out_Stretch_Target.R.003 | нет |
| DEF_Anus_Hole_Out.R.003 | STRETCH_Anus_Hole_Out.R.003 | нет |
| Anus_Hole_Out_Stretch_Target.L.002 | Anus_Root | нет |
| STRETCH_Anus_Hole_Out.L.002 | Anus_Hole_Out_Stretch_Target.L.002 | нет |
| DEF_Anus_Hole_Out.L.002 | STRETCH_Anus_Hole_Out.L.002 | нет |
| Anus_Hole_Out_Stretch_Target.L.001 | Anus_Root | нет |
| STRETCH_Anus_Hole_Out.L.001 | Anus_Hole_Out_Stretch_Target.L.001 | нет |
| DEF_Anus_Hole_Out.L.001 | STRETCH_Anus_Hole_Out.L.001 | нет |
| Anus_Hole_Out_Stretch_Target.L.003 | Anus_Root | нет |
| STRETCH_Anus_Hole_Out.L.003 | Anus_Hole_Out_Stretch_Target.L.003 | нет |
| DEF_Anus_Hole_Out.L.003 | STRETCH_Anus_Hole_Out.L.003 | нет |
| Vag_Root | CC_Base_Pelvis | нет |
| Vag_Labia_Root | Vag_Root | нет |
| Vag_Lip.L | Vag_Labia_Root | нет |
| DEF_Vag_Lip.L | Vag_Lip.L | нет |
| Vag_Lip.R | Vag_Labia_Root | нет |
| DEF_Vag_Lip.R | Vag_Lip.R | нет |
| Vag_Lip.L.001 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.L.001 | Vag_Lip.L.001 | нет |
| Vag_Lip.R.001 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.R.001 | Vag_Lip.R.001 | нет |
| Vag_Lip.L.002 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.L.002 | Vag_Lip.L.002 | нет |
| Vag_Lip.R.002 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.R.002 | Vag_Lip.R.002 | нет |
| Vag_Lip.L.003 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.L.003 | Vag_Lip.L.003 | нет |
| Vag_Lip.R.003 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.R.003 | Vag_Lip.R.003 | нет |
| Vag_Lip.L.004 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.L.004 | Vag_Lip.L.004 | нет |
| Vag_Lip.R.004 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.R.004 | Vag_Lip.R.004 | нет |
| Vag_Lip.L.005 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.L.005 | Vag_Lip.L.005 | нет |
| Vag_Lip.R.005 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.R.005 | Vag_Lip.R.005 | нет |
| Vag_Lip.L.006 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.L.006 | Vag_Lip.L.006 | нет |
| Vag_Lip.R.006 | Vag_Labia_Root | нет |
| DEF_Vag_Lip.R.006 | Vag_Lip.R.006 | нет |
| Vag_Lip.B | Vag_Labia_Root | нет |
| Vag_Lip_Out.L | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.L | Vag_Lip_Out.L | нет |
| DEF_Vag_Lip_Out.L | STRETCH_Vag_Lip_Out.L | нет |
| Vag_Lip_Out.R | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.R | Vag_Lip_Out.R | нет |
| DEF_Vag_Lip_Out.R | STRETCH_Vag_Lip_Out.R | нет |
| Vag_Lip_Out.L.001 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.L.001 | Vag_Lip_Out.L.001 | нет |
| DEF_Vag_Lip_Out.L.001 | STRETCH_Vag_Lip_Out.L.001 | нет |
| Vag_Lip_Out.R.001 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.R.001 | Vag_Lip_Out.R.001 | нет |
| DEF_Vag_Lip_Out.R.001 | STRETCH_Vag_Lip_Out.R.001 | нет |
| Vag_Lip_Out.L.002 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.L.002 | Vag_Lip_Out.L.002 | нет |
| DEF_Vag_Lip_Out.L.002 | STRETCH_Vag_Lip_Out.L.002 | нет |
| Vag_Lip_Out.R.002 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.R.002 | Vag_Lip_Out.R.002 | нет |
| DEF_Vag_Lip_Out.R.002 | STRETCH_Vag_Lip_Out.R.002 | нет |
| Vag_Lip_Out.L.003 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.L.003 | Vag_Lip_Out.L.003 | нет |
| DEF_Vag_Lip_Out.L.003 | STRETCH_Vag_Lip_Out.L.003 | нет |
| Vag_Lip_Out.R.003 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.R.003 | Vag_Lip_Out.R.003 | нет |
| DEF_Vag_Lip_Out.R.003 | STRETCH_Vag_Lip_Out.R.003 | нет |
| Vag_Lip_Out.L.004 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.L.004 | Vag_Lip_Out.L.004 | нет |
| DEF_Vag_Lip_Out.L.004 | STRETCH_Vag_Lip_Out.L.004 | нет |
| Vag_Lip_Out.R.004 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.R.004 | Vag_Lip_Out.R.004 | нет |
| DEF_Vag_Lip_Out.R.004 | STRETCH_Vag_Lip_Out.R.004 | нет |
| Vag_Lip_Out.L.005 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.L.005 | Vag_Lip_Out.L.005 | нет |
| DEF_Vag_Lip_Out.L.005 | STRETCH_Vag_Lip_Out.L.005 | нет |
| Vag_Lip_Out.R.005 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.R.005 | Vag_Lip_Out.R.005 | нет |
| DEF_Vag_Lip_Out.R.005 | STRETCH_Vag_Lip_Out.R.005 | нет |
| Vag_Lip_Out.L.006 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.L.006 | Vag_Lip_Out.L.006 | нет |
| DEF_Vag_Lip_Out.L.006 | STRETCH_Vag_Lip_Out.L.006 | нет |
| Vag_Lip_Out.R.006 | Vag_Labia_Root | нет |
| STRETCH_Vag_Lip_Out.R.006 | Vag_Lip_Out.R.006 | нет |
| DEF_Vag_Lip_Out.R.006 | STRETCH_Vag_Lip_Out.R.006 | нет |
| DEF_Vag_ClitLipOut.L | Vag_Labia_Root | нет |
| DEF_Vag_ClitLipOut.R | Vag_Labia_Root | нет |
| Vag_Hole_Root | Vag_Root | нет |
| Vag_Hole.F | Vag_Hole_Root | нет |
| DEF_Vag_Hole.R.000 | Vag_Hole.F | нет |
| DEF_Vag_Hole.L.000 | Vag_Hole.F | нет |
| Vag_Hole.B | Vag_Hole_Root | нет |
| Vag_Hole.R.001 | Vag_Hole_Root | нет |
| DEF_Vag_Hole.R.001 | Vag_Hole.R.001 | нет |
| Vag_Hole.L.001 | Vag_Hole_Root | нет |
| DEF_Vag_Hole.L.001 | Vag_Hole.L.001 | нет |
| Vag_Hole.R.002 | Vag_Hole_Root | нет |
| DEF_Vag_Hole.R.002 | Vag_Hole.R.002 | нет |
| Vag_Hole.L.002 | Vag_Hole_Root | нет |
| DEF_Vag_Hole.L.002 | Vag_Hole.L.002 | нет |
| Vag_Hole.R.003 | Vag_Hole_Root | нет |
| DEF_Vag_Hole.R.003 | Vag_Hole.R.003 | нет |
| Vag_Hole.L.003 | Vag_Hole_Root | нет |
| DEF_Vag_Hole.L.003 | Vag_Hole.L.003 | нет |
| Vag_InnerHole_Root | Vag_Hole_Root | нет |
| DEF_Vag_InnerHole.001 | Vag_InnerHole_Root | нет |
| Vag_Middle.001 | Vag_Root | нет |
| DEF_Vag_Middle.001 | Vag_Middle.001 | нет |
| DEF_Vag_Uretra | Vag_Middle.001 | нет |
| Vag_Middle.000 | Vag_Root | нет |
| DEF_Vag_Middle.000 | Vag_Middle.000 | нет |
| Vag_Middle.002 | Vag_Root | нет |
| DEF_Vag_Middle.002 | Vag_Middle.002 | нет |
| Vag_ClitBase | Vag_Root | нет |
| Vag_ClitBase.001 | Vag_ClitBase | нет |
| DEF_Vag_ClitBaseToMiddle | Vag_ClitBase.001 | нет |
| DEF_Vag_Clit | Vag_ClitBase.001 | нет |
| DEF_Vag_Clit.001 | Vag_ClitBase | нет |
| Gluten_Root | CC_Base_Pelvis | нет |
| GluteControl.L | Gluten_Root | нет |
| CENTER_OF_MASS_Glute.L | GluteControl.L | нет |
| DAMPER_Glute.L | GluteControl.L | нет |
| SCALER_Glute.L | DAMPER_Glute.L | нет |
| DEF_Glute.L | SCALER_Glute.L | нет |
| PHYSICS_TrackGlute.L | GluteControl.L | нет |
| GluteControl.R | Gluten_Root | нет |
| CENTER_OF_MASS_Glute.R | GluteControl.R | нет |
| DAMPER_Glute.R | GluteControl.R | нет |
| SCALER_Glute.R | DAMPER_Glute.R | нет |
| DEF_Glute.R | SCALER_Glute.R | нет |
| PHYSICS_TrackGlute.R | GluteControl.R | нет |
| Guias | CC_Base_BoneRoot | нет |
| Labio_Up.Tip.Center | Guias | нет |
| Labio_Down.Tip.Center | Guias | нет |
| Labio_Up.Tip.L | Guias | нет |
| Labio_Up.Tip.R | Guias | нет |
| Labio.Tip.L | Guias | нет |
| Labio.Tip.R | Guias | нет |
| Labio_Down.Tip.L | Guias | нет |
| Labio_Down.Tip.R | Guias | нет |
| Nariz_Guia.R | Guias | нет |
| Nariz_Guia.L | Guias | нет |
| Labio_Up.Botton.Center | Guias | нет |
| Labio_Up.Botton.R | Guias | нет |
| Labio.Botton.R | Guias | нет |
| Labio_Down.Botton.Center | Guias | нет |
| Labio_Down.Botton.R | Guias | нет |
| Labio_Up.Botton.L | Guias | нет |
| Labio_Down.Botton.L | Guias | нет |
| Labio.Botton.L | Guias | нет |
| Nariz_Guia.Center | Guias | нет |


## Загрузчик и срез X64GLTF02

Необязательное поле реестра `heroCharacter` содержит ID записи из `characters`.
Host применяет его при уже установленном `main_hero_visual`. Это позволяет
назначить визуал Human Hero данными без персональных условий в загрузчике.
Остальные персонажи выбирают запись по своему ASCII-lowercase visualId.
Отсутствующее поле сохраняет обычный поиск по visualId. Назначенный GLB с ошибкой
отклоняется с причиной в журнале. Namespace и приоритеты задаёт ResourceStore.

Срез рисует первый ключ `idle`, при отсутствии клипа использует bind pose.
Для этого этапа допустим один клип idle. Полный набор действий и событий из v1
остаётся контрактом следующих шагов. Метаданные морфов и каналы всех клипов
сохраняются вместе с неизменяемым документом cgltf. Начальные веса морфов
применяются к геометрии. Текстуры декодируются WIC в BGRA, цветовые mip строятся
через linear sRGB с однократным кодированием в дисплейную гамму игры.
Срез использует одну UV-развёртку материала. TEXCOORD_0/1 и общий для его карт
KHR_texture_transform применяются к примитиву. Разные UV-наборы/преобразования
карт одного материала диагностируются до публикации. Для их исполнения
потребуется расширить потоки/параметры GPU-материала.
