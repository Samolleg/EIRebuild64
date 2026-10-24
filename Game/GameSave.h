#pragma once
#include "SaveSchema.h"
#include "BaseManager.h"
#include "MapObject.h"
#include "GSVars.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <type_traits>

namespace SaveSchema {
// The slot owner creates an isolated world and its managers BEFORE Load().
// Bind the saved manager identities, not the live world's managers. Registry
// captures this manifest so owners outlive Graph's rejected-load cleanup.
// If CWorld/CParty owns a manager, pass an aliasing shared_ptr whose control
// block owns that staged world/party; never give its raw manager a second deleter.
enum class OwnerKind : std::uint32_t { Map = 1, ServerSpecific, ClientSpecific, Item, Spell, Effect, Player };
struct OwnerDescription
{
    ObjectId id = 0;
    OwnerKind kind{};
    int first = 0, last = 0, previous = -1;
    void Store(Writer &) const;
    void Load(Reader &);
};
class SavedOwners
{
    struct Entry { OwnerKind kind; std::shared_ptr<CBaseManager> manager; };
    std::map<ObjectId, Entry> entries;
public:
    void Add(OwnerKind, std::shared_ptr<CBaseManager>);
    std::vector<OwnerDescription> Describe() const;
    CBaseManager &Find(ObjectId, OwnerKind) const;
    CGameBase *Map(int) const;
    std::map<ObjectId, CSmartPtr<CGameBase>> prepared;
    bool omitPlayers = false;
    // Serializes every member as a graph reference, including unreferenced ones.
    // Reading validates exact owner membership; factories have already run.
    void StoreMembers(Writer &) const;
    void LoadMembers(Reader &) const;
};
void RegisterGameTypes(Registry &, std::shared_ptr<SavedOwners>);
}

// Typed aggregate fields. No native-layout path and no CSave calls exist here.
// The fixed-width scalar overloads deliberately reject 64-bit native counts;
// collection sizes pass through Count, references through the graph registries.
namespace SaveSchema::Fields {
inline void Count(Writer &w, std::uint64_t n)
{
    if (n > INT32_MAX) throw Error(ErrorCode::Limit, "aggregate count overflow");
    w.Count(static_cast<std::uint32_t>(n));
}
template<class T, class = void> struct HasStore : std::false_type {};
template<class T> struct HasStore<T, std::void_t<decltype(std::declval<const T &>().Store(std::declval<Writer &>()))>> : std::true_type {};
template<class T, class = void> struct HasDiscard : std::false_type {};
template<class T> struct HasDiscard<T, std::void_t<decltype(std::declval<T &>().DiscardSave())>> : std::true_type {};
template<class T> struct Codec;
template<class T> void Field(Writer &w, const T &v) { Codec<T>::Store(w, v); }
template<class T> void Field(Reader &r, T &v) { Codec<T>::Load(r, v); }
template<class T> void ClearField(T &v) { Codec<T>::Clear(v); }
template<class T> struct Codec
{
    static void Store(Writer &w, const T &v)
    {
        if constexpr (std::is_same_v<T, bool>) w.Bool(v);
        else if constexpr (std::is_enum_v<T>) w.Int32(static_cast<std::int32_t>(v));
        else if constexpr (std::is_integral_v<T>)
        {
            static_assert(sizeof(T) <= 4, "use an explicit wire width for 64-bit fields");
            if constexpr (sizeof(T) == 1) w.UInt8(static_cast<std::uint8_t>(v));
            else if constexpr (sizeof(T) == 2) w.UInt16(static_cast<std::uint16_t>(v));
            else if constexpr (std::is_signed_v<T>) w.Int32(static_cast<std::int32_t>(v));
            else w.UInt32(static_cast<std::uint32_t>(v));
        }
        else if constexpr (std::is_same_v<T, float>) w.Float32(v);
        else if constexpr (HasStore<T>::value) v.Store(w);
        else w.Value(v); // only a registered P16 value codec, never Serialize
    }
    static void Load(Reader &r, T &v)
    {
        if constexpr (std::is_same_v<T, bool>) v = r.Bool();
        else if constexpr (std::is_enum_v<T>) v = static_cast<T>(r.Int32());
        else if constexpr (std::is_integral_v<T>)
        {
            static_assert(sizeof(T) <= 4, "use an explicit wire width for 64-bit fields");
            if constexpr (sizeof(T) == 1) v = static_cast<T>(r.UInt8());
            else if constexpr (sizeof(T) == 2) v = static_cast<T>(r.UInt16());
            else if constexpr (std::is_signed_v<T>) v = static_cast<T>(r.Int32());
            else v = static_cast<T>(r.UInt32());
        }
        else if constexpr (std::is_same_v<T, float>) v = r.Float32();
        else if constexpr (HasStore<T>::value) v.Load(r);
        else r.Value(v);
    }
    static void Clear(T &v) { if constexpr (HasDiscard<T>::value) v.DiscardSave(); }
};
template<> struct Codec<CString>
{
    static void Store(Writer &w, const CString &v) { w.String(std::string((const char *)v, v.GetLength())); }
    static void Load(Reader &r, CString &v)
    {
        auto s = r.String();
        if (s.find('\0') != std::string::npos) throw Error(ErrorCode::Invalid, "NUL in saved text");
        v = CString(s.data(), static_cast<int>(s.size()));
    }
    static void Clear(CString &) {}
};
template<> struct Codec<std::string>
{
    static void Store(Writer &w, const std::string &v) { w.String(v); }
    static void Load(Reader &r, std::string &v) { v = r.String(); }
    static void Clear(std::string &) {}
};
template<class T> struct Codec<T *>
{
    static void Store(Writer &w, T *v) { w.GameReference(v); }
    static void Load(Reader &r, T *&v)
    {
        T *next = r.GameReferenceAs<T>();
        if (next) next->AddRef();
        if (v) v->Release();
        v = next;
    }
    static void Clear(T *&v) { T *old = v; v = nullptr; if (old) old->Release(); }
};
template<class T> struct Codec<CSmartPtr<T>>
{
    static void Store(Writer &w, const CSmartPtr<T> &v) { w.GameReference(v); }
    static void Load(Reader &r, CSmartPtr<T> &v) { v = r.GameReferenceAs<T>(); }
    static void Clear(CSmartPtr<T> &v) { v = nullptr; }
};
template<> struct Codec<CMapObjectPtr>
{
    static void Store(Writer &w, const CMapObjectPtr &v) { w.GameReference(static_cast<CMapObject *>(v)); }
    static void Load(Reader &r, CMapObjectPtr &v) { v = r.GameReferenceAs<CMapObject>(); }
    static void Clear(CMapObjectPtr &v) { v.Clear(); }
};
template<class T> struct Codec<CPtr<T>>
{
    static void Store(Writer &w, const CPtr<T> &v) { w.Reference(v.GetPtr()); }
    static void Load(Reader &r, CPtr<T> &v) { v = r.ReferenceAs<T>(); }
    static void Clear(CPtr<T> &v) { v = nullptr; }
};
template<class T> struct Codec<CObj<T>>
{
    static void Store(Writer &w, const CObj<T> &v)
    { if (!v.GetPtr()) throw Error(ErrorCode::Invalid,"null owned graph node"); w.Reference(v.GetPtr()); }
    static void Load(Reader &r, CObj<T> &v)
    {
        T *next=r.ReferenceAs<T>();
        if (!next) throw Error(ErrorCode::Invalid,"null owned graph node");
        v=next;
        if (v.GetPtr()!=next) throw Error(ErrorCode::Identity,"graph node has two exclusive owners");
    }
    static void Clear(CObj<T> &v) { CPtr<T> pin(v.GetPtr()); v.Extract(); }
};
template<class T, size_t N> struct Codec<T[N]>
{
    static void Store(Writer &w, const T (&v)[N]) { for (const auto &x : v) Field(w, x); }
    static void Load(Reader &r, T (&v)[N]) { for (auto &x : v) Field(r, x); }
    static void Clear(T (&v)[N]) { for (auto &x : v) ClearField(x); }
};
template<size_t N> struct Codec<char[N]>
{
    static void Store(Writer &w, const char (&v)[N])
    {
        const auto end=static_cast<const char *>(std::memchr(v,0,N));
        if (!end) throw Error(ErrorCode::Invalid,"unterminated saved sound name");
        w.String(std::string(v,end));
    }
    static void Load(Reader &r, char (&v)[N])
    {
        auto text=r.String();
        if (text.size()>=N || text.find('\0')!=std::string::npos)
            throw Error(ErrorCode::Invalid,"invalid saved sound name");
        std::fill(std::begin(v),std::end(v),0);
        std::copy(text.begin(),text.end(),v);
    }
    static void Clear(char (&)[N]) {}
};
template<class T, class A> struct Codec<CArray<T, A>>
{
    static void Store(Writer &w, const CArray<T, A> &v)
    { Count(w, v.GetSize()); for (INT_PTR i = 0; i < v.GetSize(); ++i) Field(w, v[i]); }
    static void Load(Reader &r, CArray<T, A> &v)
    {
        Clear(v); v.SetSize(r.Count());
        // MFC constructs pointer slots as null, so partial-load cleanup is safe.
        for (INT_PTR i = 0; i < v.GetSize(); ++i) Field(r, v[i]);
    }
    static void Clear(CArray<T, A> &v)
    { for (INT_PTR i = 0; i < v.GetSize(); ++i) ClearField(v[i]); v.RemoveAll(); }
};
template<class T, class A> struct Codec<CAArray<T, A>> : Codec<CArray<T, A>> {};
// CSimplyArray is a macro alias for CAArray in the current tree.
template<class T, class A> struct Codec<CList<T, A>>
{
    static void Store(Writer &w, const CList<T, A> &v)
    { Count(w, v.GetCount()); for (POSITION p = v.GetHeadPosition(); p;) Field(w, v.GetNext(p)); }
    static void Load(Reader &r, CList<T, A> &v)
    {
        Clear(v); const auto n = r.Count();
        for (std::uint32_t i = 0; i < n; ++i)
        {
            // MFC requires an initial element; an empty owner converts to its null ARG_TYPE.
            T empty{};
            POSITION p=v.AddTail(empty);
            Field(r,v.GetAt(p));
        }
    }
    static void Clear(CList<T, A> &v)
    { for (POSITION p = v.GetHeadPosition(); p;) ClearField(v.GetNext(p)); v.RemoveAll(); }
};
template<> struct Codec<CGSVariableSet>
{
    static void Store(Writer &w, const CGSVariableSet &v)
    {
        std::map<std::string, float> ordered(v.begin(), v.end());
        Count(w, ordered.size());
        for (const auto &x : ordered) { w.String(x.first); w.Float32(x.second); }
    }
    static void Load(Reader &r, CGSVariableSet &v)
    {
        v.clear(); const auto n = r.Count();
        for (std::uint32_t i = 0; i < n; ++i)
        {
            auto key = r.String(); auto value = r.Float32();
            if (key.find('\0') != std::string::npos || !v.emplace(key, value).second)
                throw Error(ErrorCode::Invalid, "invalid/duplicate saved variable");
        }
    }
    static void Clear(CGSVariableSet &) {}
};
} // namespace SaveSchema::Fields
