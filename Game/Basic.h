#pragma once
#include <cstddef>
/////////////////////////////////////////////////////////////////////////////////////
class CStructureSaver;
/////////////////////////////////////////////////////////////////////////////////////
// single thread version
// базовые классы для системы подсчета ссылок на объекты с целью упрощения memory
// management
//   Поддерживаются 2 типа объектов - некопируемые (наследуются от CFundament) и
// копируемые, наследуются от CObjectBase, для копируемых подразумевается, что корректные
// конструктор копирования и оператор присваивания генерируются автоматически, для того,
// чтобы это происходило все members производных классов должны иметь корректные конструкторы
// копирования и операторы присваивания. Кроме того наследники от CObjectBase получают
// корректные методы Clear() и Duplicate() без каких либо усилий.
//   При наследовании от CObjectBase настойчиво рекомендуется использовать макрос
// OBJECT_BASIC_METHODS для определения всех методов, необходимых для функционирования
// системы подсчета ссылок.
//   Есть два шаблона указателей - простые указатели - CPtr с добавлением ссылки и
// указатели, обладающие правом собственности - CObj. После уничтожения CObj объект, на который
// он ссылался очищается и помечается как невалидный. При присваивании или копировании CObj
// они копируют объект, на который ссылаются
//   Ограничения - множественное наследование не поддерживается из-за реализации
// CFundament::operator new. Наследование от CObj/CPtr рассматривается как читерство,
// так как позволяет обойти подсчет ссылок. Нельзя переопределять operator new, так
// как удалятся они будут с помощью стандартного operator`a delete (из-за delete this)
// и так как в operator new надо позвать InitRefCount()
/////////////////////////////////////////////////////////////////////////////////////
// forward declaration
template <class T>
class CPtr;
template <class T>
class CObj;
//#define FORCE_ALLOC_CHECK
/////////////////////////////////////////////////////////////////////////////////////
class CFundament
{
private:
    union
    {
        struct
        {
            unsigned short nRefCount;
            unsigned short nObjMask;
        };
        int nRefData; // highest bit is set when object is invalid
    };
    //  functions to work with ref count.
    // they are private in order to disable explicit manipulation!
    // use CObj/CPtr templates instead
    void AddRef() { nRefCount++; }
    void DecRef() { nRefCount--; }
    void Release()
    {
        if ((--nRefCount) == 0)
            delete this;
    }
    void InitRefCount() { nRefData = 0; }
    bool HasSingleReference() const { return nRefCount == 1; }

protected:
    virtual ~CFundament() {}

public:
    typedef CPtr<CFundament> CSimplePtr;
    CFundament() {}
    // do not copy refcount when copy object
    CFundament(const CFundament &a) { ASSERT(0); }
    CFundament &operator=(const CFundament &a)
    {
        ASSERT(0);
        return *this;
    }
    void *operator new(size_t nSize)
    {
        void *pTemp = dbgnew char[nSize];
        ((CFundament *)pTemp)->InitRefCount();
        return pTemp;
    }
    void *operator new[](size_t nSize)
    {
        ASSERT(0);
        return 0;
    }
#if (defined(_DEBUG) && defined(_AFX)) || defined(FORCE_ALLOC_CHECK)
    void *operator new(size_t nSize, const char *pszFile, int nLine)
    {
        void *pTemp = new (pszFile, nLine) char[nSize];
        ((CFundament *)pTemp)->InitRefCount();
        return pTemp;
    }
#endif
    // highest bit is used as invalid flag
    bool IsValid() const { return this != 0 && (nRefData & 0x80000000) == 0; }
    // for serialization purposes
    virtual void Serialize(CStructureSaver &file) {}
    // due to absense of template friend classes in vc this class is needed
    friend class CPtrBase;
    friend class CObjBase;
    friend class CObjectBase;
};
/////////////////////////////////////////////////////////////////////////////////////
class CObjectBase : public CFundament
{
private:
    void Invalidate()
    {
        nObjMask |= 0x8000;
        Clear();
    }
    void SetObj() { nObjMask |= 0x1; }
    void ResetObj() { nObjMask &= ~0x1; }
    unsigned short IsObj() const { return nObjMask & 1; }

protected:
    // function should clear contents of object, easy to implement via consequent calls to
    // destructor and constructor, this function should not be called directly, use Clear()
    virtual void DestroyContents() = 0;

public:
    CObjectBase() {}
    // do not copy refcount when copy object
    CObjectBase(const CObjectBase &a) {} // CFundament() is called
    CObjectBase &operator=(const CObjectBase &a) { return *this; }
    // create copy of an object
    virtual CObjectBase *Duplicate() const = 0;
    // reset data in class to default values, saves RefCount from destruction
    void Clear()
    {
        AddRef();
        DestroyContents();
        DecRef();
    }
    //
    friend class CObjBase;
};
/////////////////////////////////////////////////////////////////////////////////////
// macro that helps to make CFundament derivative to be serializable
#define FUNDAMENT_BASIC_METHODS(classname) \
public:                                    \
    static classname *New##classname()     \
    {                                      \
        return new classname();            \
    }                                      \
                                           \
private:
/////////////////////////////////////////////////////////////////////////////////////
// macro that helps to create neccessary members for proper operation of ref/ptr system
// if class needs special destructor, use CFundament
#define OBJECT_BASIC_METHODS(classname)           \
public:                                           \
    static classname *New##classname()            \
    {                                             \
        return new classname();                   \
    }                                             \
                                                  \
protected:                                        \
    virtual CObjectBase *Duplicate() const        \
    {                                             \
        CObjectBase *pRes = new classname(*this); \
        return pRes;                              \
    }                                             \
    virtual void DestroyContents()                \
    {                                             \
        classname::~classname();                  \
        ::new (this) classname;                   \
    }                                             \
    virtual ~classname()                          \
    {                                             \
    }                                             \
                                                  \
private:
/////////////////////////////////////////////////////////////////////////////////////
// simple pointer on object, adds reference on assignment and releases on its destruction
// for use as reference on some object owned by someone else or for reference on common object
// ( common object is cleared and deleted only when all references on it are deleted )
class CPtrBase
{
private:
    CFundament *ptr;
    template <class T> friend struct CPtrFaker;

protected:
    CFundament *Get() const { return ptr; }

public:
    CPtrBase()
        : ptr(0)
    {
    }
    CPtrBase(const CPtrBase &a)
        : ptr(a.ptr)
    {
        if (ptr)
            ptr->AddRef();
    }
    CPtrBase(CFundament *_ptr)
        : ptr(_ptr)
    {
        if (ptr)
            ptr->AddRef();
    }
    ~CPtrBase()
    {
        if (ptr)
            ptr->Release();
    }
    //
    void SetFundament(CFundament *_ptr)
    {
        CFundament *pOld = ptr;
        ptr = _ptr;
        if (ptr)
            ptr->AddRef();
        if (pOld)
            pOld->Release();
    }
    CFundament *GetFundament() const { return ptr; }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class T>
class CPtr : public CPtrBase
{
public:
    CPtr() {}
    CPtr(T *_ptr)
        : CPtrBase(_ptr)
    {
    }
    CPtr(const CPtr &a)
        : CPtrBase(a)
    {
    }
    CPtr(const CObj<T> &a);
    // assignment operators
    CPtr &operator=(T *_ptr)
    {
        SetFundament(_ptr);
        return *this;
    }
    CPtr &operator=(const CPtr &a)
    {
        SetFundament(a.Get());
        return *this;
    }
    CPtr &operator=(const CObj<T> &a);
    // comparison operators
    // bool operator==( const CPtr &a ) const { return Get() == a.Get(); }
    // bool operator==( const T *a    ) const { return static_cast<T*>(Get()) == a; }
    bool operator!=(const CPtr &a) const { return Get() != a.Get(); }
    bool operator!=(const T *a) const { return static_cast<T *>(Get()) != a; }
    bool operator<(const CPtr &a) const { return Get() < a.Get(); }
    bool operator>(const CPtr &a) const { return Get() > a.Get(); }
    bool operator<=(const CPtr &a) const { return Get() <= a.Get(); }
    bool operator>=(const CPtr &a) const { return Get() >= a.Get(); }
    // access
    T *operator->() const { return static_cast<T *>(Get()); }
    operator T *() const { return static_cast<T *>(Get()); }
    T *GetPtr() const { return static_cast<T *>(Get()); }
};
/////////////////////////////////////////////////////////////////////////////////////
// "owning" pointer
class CObjBase
{
    CObjectBase *ptr;
    template <class T> friend struct CObjFaker;

protected:
    void Free()
    {
        if (ptr)
        {
            if (!ptr->HasSingleReference())
                ptr->Invalidate();
            ptr->Release();
        }
    }
    void Assign(CObjectBase *_ptr)
    {
        if (!_ptr->IsValid())
        {
            ptr = 0;
            return;
        }
        if (_ptr->IsObj())
            ptr = _ptr->Duplicate();
        else
            ptr = _ptr;
        ptr->AddRef();
        ptr->SetObj();
    }
    CObjectBase *ExtractPtr()
    {
        if (!ptr)
        {
            return 0;
        }
        CObjectBase *pRes = ptr;
        ptr->DecRef();
        ptr->ResetObj();
        ptr = 0;
        return pRes;
    }
    CObjectBase *Get() const { return ptr; }

public:
    CObjBase()
        : ptr(0)
    {
    }
    CObjBase(CObjectBase *_ptr) { Assign(_ptr); }
    CObjBase(const CObjBase &a) { Assign(a.ptr); }
    ~CObjBase() { Free(); }
    //
    bool IsValid() const { return ptr != 0; }
    //
    void SetObjectBase(CObjectBase *_ptr)
    {
        Free();
        Assign(_ptr);
    }
    CObjectBase *GetObjectBase() const { return ptr; }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class T>
class CObj : public CObjBase
{
public:
    CObj() {}
    CObj(T *_ptr)
        : CObjBase(_ptr)
    {
    }
    CObj(const CObj &a)
        : CObjBase(a)
    {
    }
    T *Extract() { return (T *)ExtractPtr(); }
    // assignment operators
    CObj &operator=(T *_ptr)
    {
        if (_ptr == Get())
            return *this;
        Free();
        Assign(_ptr);
        return *this;
    }
    CObj &operator=(const CObj &a)
    {
        if (a.GetPtr() == GetPtr())
            return *this;
        Free();
        Assign(a.Get());
        return *this;
    }
    // comparison operators
    bool operator==(const CObj &a) const { return Get() == a.Get(); }
    bool operator==(const T *a) const { return static_cast<T *>(Get()) == a; }
    bool operator!=(const CObj &a) const { return Get() != a.Get(); }
    bool operator!=(const T *a) const { return static_cast<T *>(Get()) != a; }
    bool operator<(const CObj &a) const { return Get() < a.Get(); }
    bool operator>(const CObj &a) const { return Get() > a.Get(); }
    bool operator<=(const CObj &a) const { return Get() <= a.Get(); }
    bool operator>=(const CObj &a) const { return Get() >= a.Get(); }
    // access
    T *operator->() const { return static_cast<T *>(Get()); }
    operator T *() const { return static_cast<T *>(Get()); }
    T *GetPtr() const { return static_cast<T *>(Get()); }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class T>
CPtr<T>::CPtr(const CObj<T> &a)
    : CPtrBase(a.GetPtr())
{
}
/////////////////////////////////////////////////////////////////////////////////////
template <class T>
CPtr<T> &CPtr<T>::operator=(const CObj<T> &a)
{
    SetFundament(a.GetPtr());
    return *this;
}
/////////////////////////////////////////////////////////////////////////////////////
// для использования только в MakeObj, нужен для реализации операций с STL структурами
// данных типа list<CObj<T>>, например, push_back( new T ) без копирования T, с
// использованием этого метода запись будет такой: push_back( MakeObj( new T ) )
template <class T>
struct CObjFaker : public CObj<T>
{
    CObjFaker(T *_pData)
    {
        // Borrow only until the destination wrapper performs its normal copy.
        this->ptr = _pData;
    }
    ~CObjFaker() { this->ptr = nullptr; }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class T>
inline CObjFaker<T> MakeObj(T *o)
{
    return CObjFaker<T>(o);
}
/////////////////////////////////////////////////////////////////////////////////////
// для использования только в MakePtr, нужен для реализации операций с STL структурами
// данных типа vector<CPtr<T>>, например, push_back( pItem ) без излишних AddRef/Release,
// с использованием этого метода запись будет такой: push_back( MakePtr( pItem ) )
template <class T>
struct CPtrFaker : public CPtr<T>
{
    CPtrFaker(T *_pData)
    {
        // Borrow only until the destination wrapper performs its normal copy.
        this->ptr = _pData;
    }
    ~CPtrFaker() { this->ptr = nullptr; }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class T>
inline CPtrFaker<T> MakePtr(T *o)
{
    return CPtrFaker<T>(o);
}
/////////////////////////////////////////////////////////////////////////////////////
struct SObjTest
{
    CFundament *pTest;
    SObjTest(CFundament *_pTest)
        : pTest(_pTest)
    {
    }
    template <class T>
    bool operator()(CObj<T> &a)
    {
        return a == pTest;
    }
    template <class T>
    bool operator()(CPtr<T> &a)
    {
        return a == pTest;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
