#pragma once
#include <windows.h>

namespace OwnedRender {
// CMappedFile owns both handles and the view. These synchronous boundaries
// register only successful, offset-zero, full-file mappings and retire their
// provenance under the same lock as the actual unmap. Queries do not extend
// the caller's view lifetime; the caller must still own its CMappedFile.
void* MapResourceView(HANDLE mapping, DWORD access, DWORD fileSize) noexcept;
BOOL UnmapResourceView(const void* view) noexcept;
}
