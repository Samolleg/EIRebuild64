#pragma once
class C2DScreenWait {
    static bool bIgnoreSteps;
public:
    static const char* pszProgressName;
    static void Step(int nNumb);
    static void Clear();
    static void Ignore(bool b) { bIgnoreSteps = b; }
};
