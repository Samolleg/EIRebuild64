#include "StdAfx.h"
#include "SaveLoader.h"
#include "BasicChunk.h"
#include <iostream>
#include <stdexcept>

static unsigned checks = 0;
static void Check(bool value, const char *why)
{ ++checks; if (!value) throw std::runtime_error(why); }

// Some non-save users implement their own short-read contract. CSave::Read
// must not mark these streams failed merely because the caller reached EOF.
class OptionalStream : public CMemorySave
{
public:
    UINT VRead(void *buffer, UINT size) override
    { if (size) memset(buffer, 0, size); return 0; }
};

template<class F> static void Reject(F operation, const char *stream)
{
    try { operation(); Check(false, "strict read did not reject"); }
    catch (const CSaveReadError &error)
    {
        Check(error.stream == stream, "error lost source identity");
        Check(!error.at.empty(), "error lost reader stage");
        Check(error.frames > 0, "error did not capture throw stack");
    }
}

static void Contracts()
{
    CMemorySave payload;
    payload.SetStrictRead(true, "save.sav", "CWorldServer::LoadSave");
    CMemorySave resource;
    int value = 123;
    Check(resource.Read(&value, sizeof(value)) == 0 && value == 0 && !resource.IsOk(),
          "memory EOF contract changed");
    OptionalStream optional;
    Check(optional.Read(&value, sizeof(value)) == 0 && optional.IsOk(),
          "base class changed optional short-read status");
    Check(optional.ReadString().IsEmpty() && optional.IsOk(), "optional string EOF changed");
    Reject([&] { payload.Read(value); }, "save.sav");
    payload.Clear();
    Check(!payload.IsStrictRead(), "Clear leaked old save policy");

    // Network update history moves its reference position past the current size.
    resource.Clear(); resource.SetSizeFast(32); resource.SetSize(0);
    resource.Seek(4);
    Check(resource.GetPosition() == 4 && resource.IsOk(), "network reference Seek changed");
    resource.SetSize(0); resource.Write(value);
    resource.SetStrictRead(true, "save.sav", "strict_seek");
    Reject([&] { resource.Seek(5); }, "save.sav");

    CSaveReadError owned("initial", 0);
    try {
        CMemorySave temporary;
        temporary.SetStrictRead(true, "temporary.sav", "temporary_reader");
        temporary.Read(value);
    } catch (const CSaveReadError &error) { owned = error; }
    Check(owned.stream == "temporary.sav" && owned.at == "temporary_reader",
          "error identity dangles after source destruction");
}

static void Children()
{
    CMemorySave embedded, parent;
    embedded.Write((char)7);
    parent.Write(embedded); parent.Seek(0);
    parent.SetStrictRead(true, "embedded.sav", "Read/interfaceState");
    CMemorySave child;
    parent.Read(child);
    child.Seek(0);
    Check(child.IsStrictRead(), "embedded policy not propagated");
    int value = 0;
    Reject([&] { child.Read(value); }, "embedded.sav");
    parent.Clear(); parent.Write((DWORD)7); parent.Write((DWORD)9); parent.Write((char)8);
    parent.Seek(0); parent.SetStrictRead(true, "chunk.sav", "ReadChunk");
    DWORD id = 0;
    Check(parent.ReadChunk(id, child) && id == 7 && child.IsStrictRead(), "chunk policy lost");
    Reject([&] { child.Read(value); }, "chunk.sav");
    child.SetOk();
    CMemorySave copy;
    copy.CopyFrom(child);
    Check(copy.IsStrictRead(), "CopyFrom policy lost");
    Reject([&] { copy.Read(value); }, "chunk.sav");
    CMemorySave blob, encoded;
    blob.Write((char)9);
    { CStructureSaver writer; writer.Start(encoded, false, 117); writer.Add(7, blob); writer.Finish(encoded); }
    encoded.SetStrictRead(true, "blob.sav", "CStructureSaver");
    { CStructureSaver reader; reader.Start(encoded, true, 117); reader.Add(7, child); reader.Finish(encoded); }
    Check(child.IsStrictRead() && child.GetSize() == 1, "BLOB policy lost");
    Reject([&] { child.Read(value); }, "blob.sav");
    parent.Clear(); parent.Write(embedded); parent.Seek(0); parent.Read(child); child.Seek(0);
    Check(!child.IsStrictRead(), "embedded resource inherited stale strict flag");
    Check(child.Read(&value, sizeof(value)) == 1 && !child.IsOk(), "resource short read throws");
    parent.Clear(); parent.Write((char)0); parent.Write((char)2); parent.Write((char)0);
    parent.Seek(0); parent.SetStrictRead(true, "structure.sav", "CStructureSaver");
    Reject([&] { CStructureSaver reader; reader.Start(parent, true, 117); }, "structure.sav");
}

int main()
{
    try {
        Contracts();
        Children();
        std::cout << "STREAM_SUMMARY checks=" << checks << " errors=0\n";
        return 0;
    } catch (const CSaveReadError &e) { std::cerr << e.reason << '\n'; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; }
    catch (CException *e) { e->Delete(); std::cerr << "MFC exception\n"; }
    return 1;
}
