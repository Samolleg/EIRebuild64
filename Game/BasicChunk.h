#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include <list>
#include <vector>
#include <map>
#include <unordered_map>
#include "SaveLoader.h"
#include "Basic.h"
#include "BasicFactory.h"
/////////////////////////////////////////////////////////////////////////////////////
extern CClassFactory<CFundament> SSClasses;
/////////////////////////////////////////////////////////////////////////////////////
#define REGISTER_SAVELOAD_CLASS(N, name) REGISTER_CLASS(SSClasses, N, name)
/////////////////////////////////////////////////////////////////////////////////////
// a) chunk structure
// b) ptr/ref storage
// system is able to store ref/ptr only for objectbase ancestors
// Fixed resource file structure (dynamic saves: SaveSchema.h)
// -header section list of object types with opaque uint32 labels
// -object data separated in chunks one chunk per object
// c)replace memorysave with specialized objects to increase perfomance

// chunk with index 0 is used for system and should not be used in user code
typedef char chunk_id;
class CStructureSaver
{
public:
    typedef std::string stdString;

private:
    struct CChunkLevel
    {
        chunk_id idChunk;
        int nStart, nLength;
        int nChunkNumber; // номер чанка по порядку для считывания - используется при
                          // записи/считывании vector/list

        void Clear()
        {
            idChunk = (chunk_id)0xff;
            nChunkNumber = 1;
            nStart = 0;
            nLength = 0;
        }
        CChunkLevel() { Clear(); }
    };
    // objects descriptors
    CMemorySave obj;
    // objects data
    CMemorySave data;
    std::list<CChunkLevel> chunks;
    typedef std::list<CChunkLevel>::iterator CChunkLevelIterator;
    typedef std::list<CChunkLevel>::reverse_iterator CChunkLevelReverseIterator;
    int nVersion;
    bool bIsReading;
    // Asset labels are opaque uint32 file IDs, never native addresses.
    // Dynamic saves use SaveSchema, not this fixed resource format.
    std::map<std::uint32_t, CPtr<CFundament>> resourceObjects;
    std::unordered_map<CFundament *, std::uint32_t, SDefaultPtrHash> resourceLabels;
    std::list<CFundament *> toStore;
    void ReleaseObjects();

    bool ReadShortChunk(CChunkLevel &src, int &nPos, CChunkLevel &res);
    bool WriteShortChunk(CChunkLevel &dst, chunk_id dwID, const char *pData, int nLength);
    bool GetShortChunk(CChunkLevel &src, chunk_id dwID, CChunkLevel &res, int nNumber = 1);
    int CountShortChunks(CChunkLevel &src, chunk_id dwID);
    //
    bool StartChunk(const chunk_id idChunk);
    void FinishChunk();
    void AlignDataFileSize();
    int CountChunks(const chunk_id idChunk);
    void SetChunkCounter(int nCount) { chunks.back().nChunkNumber = nCount; }
    //
    void DataChunk(const chunk_id idChunk, void *pData, int nSize);
    void DataChunkBLOB(const chunk_id idChunk, CMemorySave &file);
    void DataChunkString(const chunk_id idChunk, CString &data);
    void DataChunkString(const chunk_id idChunk, stdString &data);
    // storing/loading pointers to objects
    void StoreObject(const chunk_id idChunk, CFundament *pObject);
    CFundament *LoadObject(const chunk_id idChunk);
    //
public:
    CStructureSaver() { nVersion = 1; bIsReading = false; }
    ~CStructureSaver()
    {
        if (bIsReading)
            ReleaseObjects();
    }
    bool IsReading() { return bIsReading; }
    int GetVersion() { return nVersion; }

    //
    void Start(CSave &res, bool bRead, int _nVersion = 1);
    void Finish(CSave &res);
    //
    void AddRaw(const chunk_id idChunk, void *pBuffer, int nBufferSize)
    {
        DataChunk(idChunk, pBuffer, nBufferSize);
    }

    // main function for serializing fields of objects / objects itself
    template <class T>
    void Add(const chunk_id idChunk, T &data)
    {
        if (!StartChunk(idChunk))
            return;
        data.Serialize(*this);
        FinishChunk();
    }
    template <>
    void Add<int>(const chunk_id idChunk, int &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<long>(const chunk_id idChunk, long &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<bool>(const chunk_id idChunk, bool &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<char>(const chunk_id idChunk, char &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<short>(const chunk_id idChunk, short &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    // template <> void Add<signed int>( const chunk_id idChunk, signed int &data ) { DataChunk(
    // idChunk, &data, sizeof( data ) ); } template <> void Add<signed long>( const chunk_id
    // idChunk, signed long &data ) { DataChunk( idChunk, &data, sizeof( data ) ); } template <>
    // void Add<signed char>( const chunk_id idChunk, signed char &data ) { DataChunk( idChunk,
    // &data, sizeof( data ) ); } template <> void Add<signed short>( const chunk_id idChunk, signed
    // short &data ) { DataChunk( idChunk, &data, sizeof( data ) ); }
    template <>
    void Add<unsigned int>(const chunk_id idChunk, unsigned int &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<unsigned long>(const chunk_id idChunk, unsigned long &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<unsigned char>(const chunk_id idChunk, unsigned char &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<unsigned short>(const chunk_id idChunk, unsigned short &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<float>(const chunk_id idChunk, float &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<double>(const chunk_id idChunk, double &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<long double>(const chunk_id idChunk, long double &data)
    {
        DataChunk(idChunk, &data, sizeof(data));
    }
    template <>
    void Add<CString>(const chunk_id idChunk, CString &data)
    {
        DataChunkString(idChunk, data);
    }
    template <>
    void Add<stdString>(const chunk_id idChunk, stdString &data)
    {
        DataChunkString(idChunk, data);
    }
    template <>
    void Add<CMemorySave>(const chunk_id idChunk, CMemorySave &file)
    {
        DataChunkBLOB(idChunk, file);
    }
    template <>
    void Add<CPtrBase>(const chunk_id idChunk, CPtrBase &data)
    {
        if (IsReading())
            data.SetFundament(LoadObject(idChunk));
        else
            StoreObject(idChunk, data.GetFundament());
    }
    template <>
    void Add<CObjBase>(const chunk_id idChunk, CObjBase &data)
    {
        if (IsReading())
            data.SetObjectBase((CObjectBase *)LoadObject(idChunk));
        else
            StoreObject(idChunk, data.GetObjectBase());
    }
    // structs for adding objects as templates and as ordinary objects
    template <class T>
    struct SAdd
    {
        void operator()(CStructureSaver *s, const chunk_id idChunk, T &data)
        {
            s->Add(idChunk, data);
        }
    };
    template <class T>
    struct SAddT
    {
        void operator()(CStructureSaver *s, const chunk_id idChunk, T &data)
        {
            s->AddT(idChunk, data);
        }
    };
    //
    template <class T>
    void AddT(const chunk_id idChunk, CPtr<T> &data)
    {
        Add(idChunk, (CPtrBase &)data);
    }
    template <class T>
    void AddT(const chunk_id idChunk, CObj<T> &data)
    {
        Add(idChunk, (CObjBase &)data);
    }
    // serializnig several standard containers
    // vector
    template <class T, class Adder>
    void AddVector(const chunk_id idChunk, std::vector<T> &data, Adder adder)
    {
        int i, nSize;
        if (!StartChunk(idChunk))
            return;
        if (IsReading())
        {
            data.clear();
            data.resize(nSize = CountChunks(1));
        }
        else
            nSize = data.size();
        for (i = 0; i < nSize; i++)
        {
            SetChunkCounter(i + 1);
            adder(this, 1, data[i]);
        }
        FinishChunk();
    }
    template <class T>
    void AddT(const chunk_id idChunk, std::vector<T> &data)
    {
        AddVector(idChunk, data, SAdd<T>());
    }
    template <class T>
    void AddTT(const chunk_id idChunk, std::vector<T> &data)
    {
        AddVector(idChunk, data, SAddT<T>());
    }
    // list
    template <class T, class Adder>
    void AddList(const chunk_id idChunk, std::list<T> &data, Adder adder)
    {
        if (!StartChunk(idChunk))
            return;
        if (IsReading())
        {
            data.clear();
            data.Insert(data.begin(), CountChunks(1), T());
        }
        int i = 1;
        for (auto k = data.begin(); k != data.end(); ++k, ++i)
        {
            SetChunkCounter(i);
            adder(this, 1, *k);
        }
        FinishChunk();
    }
    template <class T>
    void AddT(const chunk_id idChunk, std::list<T> &data)
    {
        AddList(idChunk, data, SAdd<T>());
    }
    template <class T>
    void AddTT(const chunk_id idChunk, std::list<T> &data)
    {
        AddList(idChunk, data, SAddT<T>());
    }
    // map
    template <class T1, class T2>
    void AddT(const chunk_id idChunk, std::map<T1, T2> &data)
    {
        if (!StartChunk(idChunk))
            return;
        // TODO...
        FinishChunk();
    }
    // unordered_map
    template <class T1, class T2, class T3, class T4, class Adder>
    void AddHashMap(const chunk_id idChunk, std::unordered_map<T1, T2, T3, T4> &data, Adder adder)
    {
        if (!StartChunk(idChunk))
            return;
        if (IsReading())
        {
            data.clear();
            for (int nSize = CountChunks(1), i = 1; nSize > 0; nSize--, i++)
            {
                T1 idx;
                SetChunkCounter(i);
                Add(1, idx);
                adder(this, 2, data[idx]);
            }
        }
        else
        {
            for (auto pos = data.begin(); pos != data.end(); ++pos)
            {
                T1 idx = pos->first;
                Add(1, idx);
                adder(this, 2, pos->second);
            }
        }
        FinishChunk();
    }
    template <class T1, class T2, class T3, class T4>
    void AddT(const chunk_id idChunk, std::unordered_map<T1, T2, T3, T4> &data)
    {
        AddHashMap(idChunk, data, SAdd<T2>());
    }
    template <class T1, class T2, class T3, class T4>
    void AddTT(const chunk_id idChunk, std::unordered_map<T1, T2, T3, T4> &data)
    {
        AddHashMap(idChunk, data, SAddT<T2>());
    }
};
/////////////////////////////////////////////////////////////////////////////////////
