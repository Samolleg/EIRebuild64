#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "basic.h"
#include <unordered_map>
#include <list>
/////////////////////////////////////////////////////////////////////////////////////
// Классы, обеспечивающие разделение общих ресурсов. Каждый ресурс представляет из
// себя объект. Объект идентифицируется дескриптором. Дескриптор должен уметь вычислять
// свое hash значение (для быстрого поиска) и иметь operator == для сравнения.
// CSharedManager - простой класс, обеспечивающий однократное создание класса с одним
// дескриптором. Создание происходит посредством виртуальной функции NewObject(),
// единственный метод, нуждающийся в перекрытии.
// CLRUSharedManager - аналогичен CSharedManager, но имеет возможность освобождать
// ресурсы посредством вызова FreeData(), предполагается, что освобождать нужно при
// недостатке места для создания затребованного в NewObject объекта, поэтому FreeData()
// protected. Функция NextFrame() служит для обозначения момента после которого будет
// повторное использование объектов. Повторное использование возможно и раньше, но в
// случае недостатка ресурсов на один кадр более выгодной становится стратегия MRU
// discard. Для обнаружения этой ситуации и введен NextFrame().
// В CSharedManager в качестве объектов разделения должны выступать наследники CObj,
// так как дескрипторы запоминают указатели на них, а при Clear() они должны быть уничтожены
// C CLRUSharedManager могут быть использованы наследники CFundament, в этом случае
// в дескрипторах хранится указатель на внутреннюю структуру CLRUSharedManager`a. Для
// предотвращения разрушения объекта из CLRUSharedManager`a во время использования
// рекомендуется прибавлять ему refcount на время использования посредством CPtr, иначе
// при следующем вызове GetData() объект, возвращенный предыдущим вызовом может быть
// разрушен
/////////////////////////////////////////////////////////////////////////////////////
template <class TData>
class CSharedDesc
{
    // caching
    void *pManager;
    CPtr<TData> pData;
    //
    int nHashValue;

protected:
    void SetHashValue(int nHash) { nHashValue = nHash; }

public:
    CSharedDesc()
    {
        pManager = 0;
        nHashValue = -1;
    }
    int GetHashValue() const { return nHashValue; }
    friend class CSharedDescAccess;
};
/////////////////////////////////////////////////////////////////////////////////////
// no friend templates :(
class CSharedDescAccess
{
public:
    template <class TData>
    static void SetCache(CSharedDesc<TData> &res, void *_pManager, TData *_pData)
    {
        res.pManager = _pManager;
        res.pData = _pData;
    }
    template <class TData>
    static bool CheckCache(CSharedDesc<TData> &res, void *_pManager)
    {
        return res.pManager == _pManager && res.pData->IsValid();
    }
    template <class TData>
    static TData *GetCache(CSharedDesc<TData> &res)
    {
        return res.pData;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// simple sharing
/////////////////////////////////////////////////////////////////////////////////////
// objects are erased only on Clear()
template <class TDesc, class TData>
class CSharedManager
{
    struct SSharedHash
    {
        int operator()(const TDesc &a) const { return a.GetHashValue(); }
    };
    CSharedDescAccess descAccess;
    typedef std::unordered_map<TDesc, CObj<TData>, SSharedHash> CSharedHash;
    CSharedHash objects;

protected:
    virtual TData *NewObject(TDesc &desc) = 0;

public:
    void Clear() { objects.clear(); }
    TData *GetData(TDesc &desc);
};
/////////////////////////////////////////////////////////////////////////////////////
template <class TDesc, class TData>
TData *CSharedManager<TDesc, TData>::GetData(TDesc &desc)
{
    if (descAccess.CheckCache(desc, this))
        return descAccess.GetCache(desc);
    TData *pRes;
    auto p = objects.find(desc);
    if (p != objects.end())
    {
        pRes = p->second;
        descAccess.SetCache(desc, this, pRes);
        return pRes;
    }
    pRes = NewObject(desc);
    objects[desc] = pRes;
    descAccess.SetCache(desc, this, pRes);
    return pRes;
}
/////////////////////////////////////////////////////////////////////////////////////
// sharing with limited resources (LRU or MRU depending on load)
/////////////////////////////////////////////////////////////////////////////////////
template <class TDesc, class TData>
class CLRUSharedManager
{
    class CObjProxy;
    struct SSharedHash
    {
        int operator()(const TDesc &a) const { return a.GetHashValue(); }
    };
    typedef std::unordered_map<TDesc, CPtr<CObjProxy>, SSharedHash> CSharedHash;
    typedef std::list<CObj<CObjProxy>> CObjList;
    //
    class CObjProxy : public CObjectBase
    {
        OBJECT_BASIC_METHODS(CObjProxy);

    public:
        typename CObjList::iterator listPlace;
        typename CSharedHash::iterator hashPlace;
        CPtr<TData> pData;
        int nLastFrame;
    };
    //
    CObjList objects;
    CSharedHash index;
    CSharedDescAccess descAccess;
    int nFrameCounter; // no need to initialize, only its change matters
protected:
    // the only method to be overloaded
    virtual TData *NewObject(TDesc &desc) = 0;
    bool FreeData(); // return true if something was freed, use when lack of resources occured
public:
    void Clear()
    {
        index.clear();
        objects.clear();
    }
    void NextFrame() { nFrameCounter++; }
    //
    TData *
    GetData(TDesc &desc); // to make sure that object will not be deleted use CPtr for returned ptr
};
/////////////////////////////////////////////////////////////////////////////////////
template <class TDesc, class TData>
bool CLRUSharedManager<TDesc, TData>::FreeData()
{
    if (objects.empty())
        return false;
    CObjProxy *pFProxy = objects.front();
    if (pFProxy->nLastFrame == nFrameCounter)
    {
        // we have encountered severe space limitation,
        // space is not enough for one frame data
        // using MRU discard strategy
        index.erase(pFProxy->hashPlace);
        objects.pop_front();
    }
    else
    {
        // using LRU discard strategy
        CObjProxy *pLProxy = objects.back();
        index.erase(pFProxy->hashPlace);
        objects.pop_back();
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TDesc, class TData>
TData *CLRUSharedManager<TDesc, TData>::GetData(TDesc &desc)
{
    CObjProxy *pProxy;
    if (descAccess.CheckCache(desc, this))
        pProxy = (CObjProxy *)descAccess.GetCache(desc); // pProxy is guaranteed to be valid
    else
    {
        auto p = index.find(desc);
        if (p == index.end())
        {
            TData *pRes;
            pRes = NewObject(desc);
            if (pRes->IsValid())
            {
                pProxy = new CObjProxy;
                pProxy->listPlace = objects.insert(objects.end(), MakeObj(pProxy));
                pProxy->hashPlace = index.insert(CSharedHash::value_type(desc, MakePtr(pProxy)))
                                        .first;
                pProxy->pData = pRes;
                pProxy->nLastFrame = nFrameCounter;
                descAccess.SetCache(desc, this, (TData *)pProxy);
            }
            return pRes;
        }
        pProxy = p->second;
        descAccess.SetCache(desc, this, (TData *)pProxy);
    }
    pProxy->nLastFrame = nFrameCounter;
    objects.splice(objects.end(), objects, pProxy->listPlace);
    return pProxy->pData;
}
/////////////////////////////////////////////////////////////////////////////////////
// String hash
/////////////////////////////////////////////////////////////////////////////////////
// fast string hashing function
struct SStringHashFunctional
{
    int operator()(const char *__s) const
    {
        int nRes = 0x76128732;
        for (;;)
        {
            int nStr = ((const int *)__s)[0];
            int n1 = ((nStr & 0xf0f0f0f0) >> 4) | nStr;
            n1 = n1 & (n1 >> 8);
            if (((n1 & (n1 >> 16)) & 0xff) == 0)
            {
                if ((nStr & 0xff) == 0)
                    break;
                if ((nStr & 0xff00) == 0)
                {
                    nRes ^= nStr & 0xff;
                    break;
                }
                if ((nStr & 0xff0000) == 0)
                {
                    nRes ^= nStr & 0xffff;
                    break;
                }
                if ((nStr & 0xff000000) == 0)
                {
                    nRes ^= nStr & 0xffffff;
                    break;
                }
            }
            nRes += nStr;
            nRes ^= (nRes << 17);
            __s += 4;
        }
        return nRes;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class TData>
class CStringDesc : public CSharedDesc<TData>
{
    std::string szName;
    void RecalcHash()
    {
        SStringHashFunctional f;
        this->SetHashValue(f(szName.c_str()));
    }

public:
    void SetName(const char *pszName)
    {
        szName = pszName;
        RecalcHash();
    }
    const char *GetName() { return szName.c_str(); }
    bool operator==(const CStringDesc &a) const { return szName == a.szName; }
};
/////////////////////////////////////////////////////////////////////////////////////
