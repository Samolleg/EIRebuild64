#pragma once
#include "SaveLoader.h"
#include "CheckedSave.h"

// A complete file snapshot owns both its original bytes and its decoded buffer.
// Application and current-directory copies cannot race a second filesystem read.
struct PreparedSave
{
    CString path;
    CMemorySave raw, payload;
    bool Prepare(const CString &filename, int signature, int maximumVersion,
                 int minimumVersion, CheckedCruncher::Error &error)
    {
        path = filename;
        CFileSave file;
        if (!file.OpenRead(path)) { error = {"open_failed", 0}; return false; }
        const unsigned int size = file.GetSize();
        if (size > 512u * 1024 * 1024) { error = {"invalid_file_length", 0}; return false; }
        raw.Clear();
        raw.SetSizeFast(size);
        if (file.Read(raw.GetBufferForWrite(), size) != size)
        { error = {"incomplete_file_read", file.GetPosition()}; return false; }
        std::vector<unsigned char> decoded;
        int version = 0;
        if (!CheckedSave::Decode(reinterpret_cast<const unsigned char *>(raw.GetBuffer()), size,
                                signature, maximumVersion, minimumVersion, decoded, version, error))
            return false;
        payload.Clear();
        payload.SetVersion(version);
        if (!decoded.empty()) payload.Write(decoded.data(), static_cast<UINT>(decoded.size()));
        payload.Seek(0);
        return true;
    }
};
