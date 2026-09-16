#pragma once


class CLeverObject;
class CMapObject;
class CUnit;

bool JMV_RegisterLoot(CMapObject *object, const char *itemSpec);
void JMV_RegisteredLootOnLeverActivated(
    CLeverObject *lever, CUnit *who);
void JMV_RegisteredLootTick();
void JMV_RegisteredLootOnSuccessfulSaveLoad();
void JMV_RegisteredLootReset(const char *reason);



bool JMV_CheckRegisteredLootCodec();
