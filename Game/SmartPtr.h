#pragma once
// ******************************************************************************** //
// **                            class CRefCount                                 ** //
// **          this class have only one goal - track ref counter                 ** //
// **           and self-destruct, then ref counter reaches zero                 ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CRefCount
{
private:
    mutable int nRefCount;

protected:
    virtual ~CRefCount() {} // this object cannot be deleted directly - only through 'Release'
public:
    CRefCount()
        : nRefCount(0)
    {
    } // by default, object created with ref count = '1'
    // ref count methods
    void AddRef() const { ++nRefCount; }
    void Release() const
    {
        if (!(--nRefCount))
            delete this;
    }
    int GetRef() const { return nRefCount; }
    // ref count checks
    bool IsReferenced() const { return nRefCount != 0; }
    bool IsUnreferenced() const { return nRefCount == 0; }
    // unique ID of the object
    DWORD GetRTID() const { return reinterpret_cast<DWORD>(this); }
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                            class CSmartPtr                                 ** //
// **       smart pointer, which calls AddRef() and Release() for stored         ** //
// **           object in storing/releasing operations respectivelly             ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
class CSmartPtr
{
private:
    TYPE *pObject;

protected:
    // set new object and remove old
    void Set(TYPE *pNewObject)
    {
        TYPE *pOldPtr = pObject;
        if ((pObject = pNewObject))
            pObject->AddRef();
        if (pOldPtr)
            pOldPtr->Release();
    }

public:
    CSmartPtr()
        : pObject(0)
    {
    }
    CSmartPtr(TYPE *pNewObject)
        : pObject(pNewObject)
    {
        if (pObject)
            pObject->AddRef();
    }
    CSmartPtr(const CSmartPtr &ptr)
        : pObject(ptr.pObject)
    {
        if (pObject)
            pObject->AddRef();
    }
    ~CSmartPtr()
    {
        if (pObject)
            pObject->Release();
    }
    // assignment operators
    CSmartPtr &operator=(TYPE *pNewObject)
    {
        Set(pNewObject);
        return *this;
    }
    CSmartPtr &operator=(const CSmartPtr &ptr)
    {
        Set(ptr.pObject);
        return *this;
    }
    // object access operators (dereference and pointer access)
    operator TYPE *() const { return pObject; }
    TYPE *operator->() const { return pObject; }
    // comparison operators
    bool operator==(const CSmartPtr &ptr) const { return (pObject == ptr.pObject); }
    bool operator==(TYPE *pNewObject) const { return (pObject == pNewObject); }
    bool operator!=(const CSmartPtr &ptr) const { return (pObject != ptr.pObject); }
    bool operator!=(TYPE *pNewObject) const { return (pObject != pNewObject); }
    // check for empty object
    bool IsEmpty() const { return (pObject == 0); }
    // direct acces to the object... ugly functions, but it is neccessary,
    // because C++ can't cast from smartptr to polymorphics of the stored data
    TYPE *GetPtr() { return pObject; }
    const TYPE *GetPtr() const { return pObject; }
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
