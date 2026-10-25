#pragma once
class C2DScreenWait {
    static bool bIgnoreSteps;
public:
    static const char* pszProgressName;
    static void Step(int nNumb);
    static void Clear();
    static void Update(); // Host idle/message checkpoints, without starting a movie.
    static void Ignore(bool b) { bIgnoreSteps = b; }
};
