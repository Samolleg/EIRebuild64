#pragma once
/////////////////////////////////////////////////////////////////////////////////////
// refcounting with multithread support
/////////////////////////////////////////////////////////////////////////////////////
// специальные функи для refcounts в условиях мульти тредности и мультипроцессорности
inline void MTSInc(unsigned int *pnRefs)
{
    _asm { 
	mov eax, pnRefs
	lock inc dword ptr[eax]
    }
}
inline void MTSDec(unsigned int *pnRefs)
{
    _asm { 
	mov eax, pnRefs
	lock dec dword ptr[eax]
    }
}
inline bool MTSDecTest(unsigned int *pnRefs)
{
    bool bRes;
    _asm {
	mov eax, pnRefs
	lock dec dword ptr[eax]
	setz al
	mov bRes, al
    }
    return bRes;
}
inline void MTSInc(unsigned char *pnRefs)
{
    _asm { 
	mov eax, pnRefs
	lock inc byte ptr[eax]
    }
}
inline bool MTSDecTest(unsigned char *pnRefs)
{
    bool bRes;
    _asm {
	mov eax, pnRefs
	lock dec byte ptr[eax]
	setz al
	mov bRes, al
    }
    return bRes;
}
/////////////////////////////////////////////////////////////////////////////////////
class CMTSLock
{
    unsigned char cLock;

public:
    CMTSLock() { cLock = 1; }
    void Lock()
    {
        while (!MTSDecTest(&cLock))
        {
            MTSInc(&cLock);
            Sleep(0);
        }
    }
    void Unlock() { MTSInc(&cLock); }
};
/////////////////////////////////////////////////////////////////////////////////////
class CMFundament
{
private:
    unsigned int nRefCount;
    //  functions to work with ref count.
    // they are private in order to disable explicit manipulation!
    // use CMPtr templates instead
    void AddRef() { MTSInc(&nRefCount); }
    void Release()
    {
        if (MTSDecTest(&nRefCount))
            delete this;
    }
    // launching member function in the separate thread
    typedef void (CMFundament::*MemberFuncPtr)();
    void RunThread(MemberFuncPtr func, int nStackSize);

protected:
    virtual ~CMFundament() {}

public:
    CMFundament()
        : nRefCount(0)
    {
    }
    // do not copy refcount when copy object
    CMFundament(const CMFundament &a) {}
    CMFundament &operator=(const CMFundament &a) { return *this; }
    //
    bool IsValid() const { return this != 0; } //-V704 this check is needed
    // run member function as a thread, a reference on object will be created for the time of
    // func() execution, do not use exitthread()/terminatethread() in func() for correct refcounting
    template <class T>
    void RunFunctionAsThread(void (T::*func)(), int nStackSize = 102400)
    {
        RunThread(static_cast<MemberFuncPtr>(func), nStackSize);
    }
    // due to absense of template friend classes in vc this class is needed
    friend class CMPtrBase;
};
/////////////////////////////////////////////////////////////////////////////////////
// simple pointer on object, adds reference on assignment and releases on its destruction
// for use as reference on some object owned by someone else or for reference on common object
// ( common object is cleared and deleted only when all references on it are deleted )
class CMPtrBase
{
private:
    CMFundament *ptr;

protected:
    CMFundament *Get() const { return ptr; }

public:
    CMPtrBase()
        : ptr(0)
    {
    }
    CMPtrBase(const CMPtrBase &a)
        : ptr(a.ptr)
    {
        if (ptr)
            ptr->AddRef();
    }
    CMPtrBase(CMFundament *_ptr)
        : ptr(_ptr)
    {
        if (ptr)
            ptr->AddRef();
    }
    ~CMPtrBase()
    {
        if (ptr)
            ptr->Release();
    }
    //
    void SetMFundament(CMFundament *_ptr)
    {
        CMFundament *pOld = ptr;
        ptr = _ptr;
        if (ptr)
            ptr->AddRef();
        if (pOld)
            pOld->Release();
    }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class T>
class CMPtr : public CMPtrBase
{
public:
    CMPtr() = default;
    CMPtr(T *_ptr)
        : CMPtrBase(_ptr)
    {
    }
    CMPtr(const CMPtr &a)
        : CMPtrBase(a)
    {
    }
    // assignment operators
    CMPtr &operator=(T *_ptr)
    {
        SetMFundament(_ptr);
        return *this;
    }
    CMPtr &operator=(const CMPtr &a)
    {
        SetMFundament(a.Get());
        return *this;
    }
    // comparison operators
    bool operator==(const CMPtr &a) const { return Get() == a.Get(); }
    bool operator==(const T *a) const { return static_cast<T *>(Get()) == a; }
    bool operator!=(const CMPtr &a) const { return Get() != a.Get(); }
    bool operator!=(const T *a) const { return static_cast<T *>(Get()) != a; }
    bool operator<(const CMPtr &a) const { return Get() < a.Get(); }
    bool operator>(const CMPtr &a) const { return Get() > a.Get(); }
    bool operator<=(const CMPtr &a) const { return Get() <= a.Get(); }
    bool operator>=(const CMPtr &a) const { return Get() >= a.Get(); }
    // access
    T *operator->() const { return static_cast<T *>(Get()); }
    operator T *() const { return static_cast<T *>(Get()); }
    T *GetPtr() const { return static_cast<T *>(Get()); }
};
/////////////////////////////////////////////////////////////////////////////////////
