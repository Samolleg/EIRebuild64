#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include <string>
/////////////////////////////////////////////////////////////////////////////////////
class CStructureSaver;
/////////////////////////////////////////////////////////////////////////////////////
struct STPParams
{
public:
    float fPower;
    int nLifetime;
    int nFadeoutTime;

public:
    STPParams()
        : fPower(0)
        , nLifetime(0)
        , nFadeoutTime(0)
    {
    }

    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
struct STPDesc
{
public:
    std::string szName;
    STPParams clear;
    STPParams weather;

public:
    const std::string &GetCode() const { return szName; }

    virtual void Serialize(CStructureSaver &file);
};
struct SFPDesc : public STPDesc
{
public:
    STPParams bloody;

public:
    virtual void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
