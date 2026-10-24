#pragma once
#include <functional>
////////////////////////////////////////////////////////////////////////////////////////////////////
struct SDefaultPtrHash
{
    size_t operator()(const void *pData) const { return std::hash<const void *>{}(pData); }
};
////////////////////////////////////////////////////////////////////////////////////////////////////
// factory is using RTTI
// objects should inherit T and T must have at least 1 virtual function
template <class T>
class CClassFactory
{
public:
    typedef const type_info *VFT;
    typedef T *(*newFunc)();

private:
    typedef unordered_map<int, newFunc> CTypeNewHash;                // typeID->newFunc()
    typedef unordered_map<VFT, int, SDefaultPtrHash> CTypeIndexHash; // vftable->typeID

    CTypeIndexHash typeIndex;
    CTypeNewHash typeInfo;

    void RegisterTypeBase(int nTypeID, newFunc func, VFT vft);
    static VFT GetObjectType(T *pObject) { return &typeid(*pObject); }
    int VFT2TypeID(VFT t)
    {
        auto i = typeIndex.find(t);
        if (i != typeIndex.end())
            return i->second;
        return -1;
    }

public:
    template <class TT>
    void RegisterType(int nTypeID, newFunc func, TT *)
    {
        RegisterTypeBase(nTypeID, func, &typeid(TT));
    }
    void RegisterTypeSafe(int nTypeID, newFunc func)
    {
        CPtr<T> pObj = func();
        VFT vft = GetObjectType(pObj);
        RegisterTypeBase(nTypeID, func, vft);
    }
    T *CreateObject(int nTypeID)
    {
        newFunc f = typeInfo[nTypeID];
        if (f)
            return f();
        return 0;
    }
    int GetObjectTypeID(T *pObject) { return VFT2TypeID(GetObjectType(pObject)); }
    template <class TT>
    int GetTypeID(TT *p = 0)
    {
        return VFT2TypeID(&typeid(TT));
    }
};
////////////////////////////////////////////////////////////////////////////////////////////////////
template <class T>
void CClassFactory<T>::RegisterTypeBase(int nTypeID, newFunc func, VFT vft)
{
    ASSERT(typeInfo.find(nTypeID) == typeInfo.end());
    ASSERT(typeIndex.find(vft) == typeIndex.end());
    typeIndex[vft] = nTypeID;
    typeInfo[nTypeID] = func;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// macro for registering CFundament derivatives
#define REGISTER_CLASS(factory, N, name) \
    factory.RegisterType(N, (decltype(factory)::newFunc)name## ::New##name, (name *)0);
#define REGISTER_TEMPL_CLASS(factory, N, name, className) \
    factory.RegisterType(N, (decltype(factory)::newFunc)name## ::New##className, (name *)0);
#define REGISTER_CLASS_NM(factory, N, name, nmspace)                                 \
    factory.RegisterType(N, (decltype(factory)::newFunc)nmspace::name## ::New##name, \
                         (nmspace::name *)0);
////////////////////////////////////////////////////////////////////////////////////////////////////
