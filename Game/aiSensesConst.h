#pragma once
/////////////////////////////////////////////////////////////////////////////////////
// maximal vision distance 64 tiles
// maximal tracking distance 16 tiles
// снижение силы метки при удалении ее на 1 meter
// const float AI_STANDARD_ATMO_DECAY = 0;
// уменьшение заметности следа за один сегмент
// const float AI_FOOTSTEP_TIME_DECAY = 0.1f;
// уровень заметности при переходе через который юнит пропадает с экранов радаров, препятсвия
// при этом не учитываются
// const float AI_MIN_VISIBLE_EDGE = -50;
// среднеквадратическое отклонение нормально распределенной случайной величины,
// используемой в тестах на чувства, при изменении этой константы необходимо создать
// новый файл normal.dat с соответствующими данными
// const float AI_RANDOM_ROLLING = 1;

// multipliers to sound during different states
// bonuses to detect
// sound
const float AI_ATTACK_SOUND = 2;
const float AI_RUN_SOUND = 2;
const float AI_LOOT_SOUND = 1.5f;
const float AI_CRAWL_SOUND = 0.2f;
const float AI_LAY_SOUND = 0.4f;
// vision
const float AI_LAY_VISION = 0.5f;
const float AI_CRAWL_VISION = 0.75f;
const float AI_ATTACK_VISION = 1.5f;
// bonus to sense
const float AI_RUN_SENSE_SOUND = 0.5f;
const float AI_LAY_SENSE_VISION = 1;

// шумность конечных эффектов магии (взрывов fireball`ов итп)
const float AI_MAGIC_EFFECT_SOUND = 4;
// максимальный бонус к восприятиям следов ( снег = *2 )
const float AI_MAX_FOOTSTEP_BONUS = 2;
// максимальный бонус к заметности по зрению ( troll = *2 )
const float AI_MAX_VISION_BONUS = 2;
const float AI_MAX_VISIBILITY_INFO_MULT = 3;

const float AI_DEFAULT_CALL_FOR_HELP_RADIUS = 10;
/////////////////////////////////////////////////////////////////////////////////////
// задержки звуков
const int AI_HEAR_FOOTSTEP_DELAY = 26;
const int AI_HEAR_CALL_FOR_HELP_DELAY = 26;
/////////////////////////////////////////////////////////////////////////////////////
// structure storing information about probabilities for unit to be noted by different feelings
// every field has minimal ratio distance/sense distance for appropriate feeling
// maximal value for each variable is AI_MAX_VISIBILITY_INFO_MULT
struct SUnitVisibilityInfo
{
    float fpSight, fpSmell, fpSound, fpTracking, fpSenseLife;
    //
    bool IsSafe()
    {
        return fpSight == AI_MAX_VISIBILITY_INFO_MULT && fpSmell == AI_MAX_VISIBILITY_INFO_MULT &&
               fpSound == AI_MAX_VISIBILITY_INFO_MULT &&
               fpTracking == AI_MAX_VISIBILITY_INFO_MULT &&
               fpSenseLife == AI_MAX_VISIBILITY_INFO_MULT;
    }
    bool IsRatherSafe(float fpSafetyMulDist = 2)
    {
        return fpSight > fpSafetyMulDist && fpSmell > fpSafetyMulDist &&
               fpSound > fpSafetyMulDist && fpTracking > fpSafetyMulDist &&
               fpSenseLife > fpSafetyMulDist;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// структура для описания масок видимости, для каждого тайла отведено по одному char
// note, data is a square
struct SVisibilityMask
{
    int nShiftX, nShiftY;
    CArray2D<char> data;

    void Clear();
    // расширить область так, чтобы регион [nC-nR,nC+nR] попадал внутрь области
    void Extend(int nCenterX, int nCenterY, int nRadius);
    void AddRound(float fpX, float fpY, float fpRadius);
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
// интересность точек, полученных разным путем (различные чувства), чем больше, тем интереснее
// эти значения завязаны с другими (например они должны быть меньше мотивации страх), power_tracking
// должен быть больше максимального времени жизни следа
// tracking должен иметь приоритет выше остальных для корректной работы tracking`a
const int AI_PPOWER_FIREWORKS = 300; // note, fear motivation of dangerous place is 300 ...
const int AI_PPDEC_FIREWORKS = -1;
const int AI_PPOWER_SEECORPSE = 800;
const int AI_PPDEC_SEECORPSE = -3;
const int AI_PPOWER_SOUND = 900;
const int AI_PPDEC_SOUND = -3;
const int AI_PPOWER_SEEENEMY = 1000;
const int AI_PPDEC_SEEENEMY = -2;
const int AI_PPOWER_HITFROM = 1000;
const int AI_PPDEC_HITFROM = -2;
const int AI_PPOWER_TRACKING = 1100;
/////////////////////////////////////////////////////////////////////////////////////
