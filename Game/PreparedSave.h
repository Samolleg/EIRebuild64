#pragma once
#include "SaveLoader.h"
#include "CheckedSave.h"

// A complete file snapshot owns both its original bytes and its decoded buffer.
// Application and current-directory copies cannot race a second filesystem read.
struct PreparedSave
{
    enum class Result { Ready, Missing, Failed };
    Result PrepareOptional(const CString &filename, int signature, int maximumVersion,
                           int minimumVersion, CheckedCruncher::Error &error)
    {
        path = filename;
        // Only absence is optional: access/sharing errors and corrupt bytes fail.
        auto missing = [&]() {
            const std::string resolved = ModRuntime::MapPath(filename);
            if (GetFileAttributesA(resolved.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
            const DWORD code = GetLastError();
            return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
        };
        if (missing()) return Result::Missing;
        if (Prepare(filename, signature, maximumVersion, minimumVersion, error)) return Result::Ready;
        if (lstrcmpA(error.reason, "open_failed") == 0 && missing()) return Result::Missing;
        return Result::Failed;
    }
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
