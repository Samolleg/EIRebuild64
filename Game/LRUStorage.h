#pragma once

#include <map>
#include <utility>
/////////////////////////////////////////////////////////////////////////////////////
template <class T, class M>
class CLRUStorage
{
private:
    std::map<std::string, std::pair<T *, DWORD>> loadedObjects;
    int nMaxSize;

private:
    void RemoveExcess();

public:
    CLRUStorage()
        : nMaxSize(50)
    {
    }
    ~CLRUStorage() { FreeObjects(); }

    void SetMaxSize(int nSize)
    {
        nMaxSize = nSize;
        RemoveExcess();
    }
    bool Contains(const char* name) const { return loadedObjects.find(name) != loadedObjects.end(); }
    T *GetObject(const char *pszName) {
        return GetObject(pszName, [](T*) {});
    }
    // The draw must acquire its reference before RemoveExcess can evict a
    // newly loaded figure (including equal native millisecond timestamps).
    template<class Pin> T *GetObject(const char *pszName, Pin&& pin);
    void FreeObjects();
};
/////////////////////////////////////////////////////////////////////////////////////
template <class T, class M>
template<class Pin>
T *CLRUStorage<T, M>::GetObject(const char *pszName, Pin&& pin)
{
    auto it = loadedObjects.find(pszName);
    if (it != loadedObjects.end())
    {
        pin(it->second.first);
        it->second.second = timeGetTime();
        return it->second.first;
    }
    T *pObj = M::CreateObject(pszName);
    if (!pObj)
        return 0;
    pin(pObj);
    loadedObjects[pszName] = std::make_pair(pObj, timeGetTime());
    RemoveExcess();
    return pObj;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class T, class M>
void CLRUStorage<T, M>::FreeObjects()
{
    for (auto it = loadedObjects.begin(); it != loadedObjects.end(); ++it)
        M::DestroyObject(it->second.first);
    loadedObjects.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
template <class T, class M>
void CLRUStorage<T, M>::RemoveExcess()
{
    while (loadedObjects.size() > nMaxSize)
    {
        typename std::map<std::string, std::pair<T *, DWORD>>::iterator it, it_old;
        DWORD dwOldTime = timeGetTime();
        it_old = loadedObjects.end();
        for (it = loadedObjects.begin(); it != loadedObjects.end(); ++it)
            if (it->second.second < dwOldTime)
            {
                dwOldTime = it->second.second;
                it_old = it;
            }
        if (it_old != loadedObjects.end())
        {
            M::DestroyObject(it_old->second.first);
            loadedObjects.erase(it_old);
        }
    }
}
