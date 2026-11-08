#include "StdAfx.h"
#include "ScriptCompiler.h"
#include "SaveLoader.h"
#include "SimpleCryptographer.h"
#include "MapEditorChunkIDs.h"
#include "CompilerVersion.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <stdexcept>
#include <vector>

namespace {
// Bound allocations before passing sizes to CString/chunk APIs.
constexpr size_t MaxInput = 256u * 1024u * 1024u;

std::vector<char> ReadBytes(FILE* input)
{
    std::vector<char> bytes;
    char buffer[16384];
    for (;;) {
        const size_t count = fread(buffer, 1, sizeof(buffer), input);
        if (count > MaxInput - bytes.size())
            throw std::runtime_error("Input exceeds 256 MiB");
        bytes.insert(bytes.end(), buffer, buffer + count);
        if (count != sizeof(buffer)) {
            if (ferror(input)) throw std::runtime_error("Cannot read input");
            return bytes;
        }
    }
}

std::vector<char> ReadFile(const wchar_t* path)
{
    FILE* file = _wfopen(path, L"rb");
    if (!file) throw std::runtime_error("Cannot open input file");
    try {
        auto bytes = ReadBytes(file);
        fclose(file);
        return bytes;
    } catch (...) {
        fclose(file);
        throw;
    }
}

CString Text(const char* data, size_t size)
{
    // Do not silently compile just the prefix of a malformed text input.
    if (size && memchr(data, 0, size))
        throw std::runtime_error("Embedded NUL in script text");
    if (!size) return CString();
    return CString(data, static_cast<int>(size));
}

CString ReadMob(const std::vector<char>& bytes)
{
    if (bytes.empty()) throw std::runtime_error("Empty MOB file");
    CMemorySave file, objectDb, chunk;
    file.StartSaving();
    file.Write(bytes.data(), static_cast<UINT>(bytes.size()));
    file.SetStrictRead(true);
    if (!file.GetChunk(ID_OBJECTDBFILE, objectDb))
        throw std::runtime_error("MOB has no object database chunk");
    if (objectDb.GetChunk(ID_SS_TEXT, chunk)) {
        // The game's decoder requires a DWORD seed and a nonempty buffer.
        if (chunk.GetSize() < sizeof(DWORD))
            throw std::runtime_error("Truncated encrypted Script chunk");
        if (chunk.GetSize() == sizeof(DWORD)) return CString();
        CSimpleCryptographer crypt;
        crypt.SetCodetData(chunk.GetBuffer(), static_cast<int>(chunk.GetSize()));
        CString text;
        crypt.GetString(text);
        // Decode uses CString::ReleaseBuffer: reject hidden bytes after a NUL,
        // but permit a single legacy trailing terminator.
        const size_t decodedSize = chunk.GetSize() - sizeof(DWORD);
        if (decodedSize != static_cast<size_t>(text.GetLength()) &&
            decodedSize != static_cast<size_t>(text.GetLength()) + 1)
            throw std::runtime_error("Embedded NUL in Script chunk");
        return text;
    }
    if (objectDb.GetChunk(ID_SS_TEXT_OLD, chunk)) {
        size_t size = chunk.GetSize();
        if (size && chunk.GetBuffer()[size - 1] == 0) --size;
        return Text(chunk.GetBuffer(), size);
    }
    throw std::runtime_error("MOB has no Script chunk");
}

std::string Utf8(const char* data, int size)
{
    if (!size) return {};
    const int count = MultiByteToWideChar(1251, 0, data, size, nullptr, 0);
    if (!count) throw std::runtime_error("Cannot decode compiler message");
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(1251, 0, data, size, wide.data(), count);
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("Cannot encode compiler message");
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, result.data(), length, nullptr, nullptr);
    return result;
}

int Reply(int exitCode, int line, int code, const std::string& error)
{
    const nlohmann::ordered_json response = {
        {"ok", exitCode == 0}, {"line", line}, {"code", code},
        {"error", error}, {"compiler", "EIRebuild " SCRIPT_CHECK_COMMIT}
    };
    const std::string output = response.dump() + "\n";
    if (fwrite(output.data(), 1, output.size(), stdout) != output.size() || fflush(stdout))
        return 2;
    return exitCode;
}

void Invariant(const char* message)
{
    throw std::runtime_error(message);
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    // Binary streams preserve CP1251 input (including Ctrl-Z), UTF-8 output.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    try {
        if (!AfxWinInit(GetModuleHandleW(nullptr), nullptr, GetCommandLineA(), 0))
            throw std::runtime_error("Cannot initialize MFC");
        CString text;
        if (argc == 2 && !wcscmp(argv[1], L"--stdin")) {
            const auto bytes = ReadBytes(stdin);
            text = Text(bytes.data(), bytes.size());
        } else if (argc == 3 && !wcscmp(argv[1], L"--text")) {
            const auto bytes = ReadFile(argv[2]);
            text = Text(bytes.data(), bytes.size());
        } else if (argc == 2 && argv[1][0] != L'-') {
            text = ReadMob(ReadFile(argv[1]));
        } else {
            throw std::runtime_error("Usage: ScriptCheck <file.mob> | --text <file.txt> | --stdin");
        }
        const auto language = ScriptLanguage::Supported();
        CScriptCompiler compiler(language, Invariant);
        SScriptThread* worldThread = nullptr;
        const bool ok = compiler.CompileText(text, language, worldThread) != FALSE;
        const auto error = compiler.GetLastError();
        // worldThread is owned by compiler. Never schedule or execute it.
        return Reply(ok ? 0 : 1, error.nLine, error.eError,
                     Utf8(error.text, error.text.GetLength()));
    } catch (CException* exception) {
        exception->Delete();
        return Reply(2, 0, -1, "MFC input/compiler failure");
    } catch (const CSaveReadError& error) {
        return Reply(2, 0, -1, std::string("Invalid MOB: ") + error.reason);
    } catch (const std::exception& error) {
        return Reply(2, 0, -1, error.what());
    } catch (...) {
        return Reply(2, 0, -1, "Unexpected utility failure");
    }
}
