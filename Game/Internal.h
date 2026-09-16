#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <strstream>
#include <windows.h>
#include <winsock.h>

// Fuckin' windows.h
#ifdef GetObject
#undef GetObject
#endif
#ifdef GetClassName
#undef GetClassName
#endif

#define __EI_JMP_THISCALL(addr)
#define __EI_JMP_STDCALL(addr)
#define __EI_JMP_FASTCALL(addr)
#define __EI_GLOBAL_VAR(addr)

// clang-format off
#define __EI_NOT_CREATABLE(ClassName)        \
private:                                     \
    ClassName();                             \
    ClassName(const ClassName &);            \
    ClassName &operator=(const ClassName &); \
    ClassName(ClassName &&);                 \
    ClassName &operator=(ClassName &&);      \
    ~ClassName()

#define __EI_ONLY_CTOR_DTOR(ClassName)       \
private:                                     \
    ClassName(const ClassName &);            \
    ClassName &operator=(const ClassName &); \
    ClassName(ClassName &&);                 \
    ClassName &operator=(ClassName &&)

#define __EI_MEMBER(offset, type) (*(type *)((uint8_t *)this + (offset)))
#define __EI_MEMBER_PTR(offset, type) ((type)((uint8_t *)this + (offset)))

#define __EI_CHECK_SIZE(struct_name, expected_size)     \
    static_assert(sizeof(struct_name) == expected_size, \
                  "Invalid size of " #struct_name       \
                  ". Expected: " #expected_size);
#define __EI_CHECK_OFFSET(struct_name, member, expected_offset) \
    static_assert(offsetof(struct_name, member) == expected_offset, \
    "Invalid offset of " #struct_name #member \
    ". Expected: " #expected_offset);

#define __EI_MAKE_DUMMY_STRUCT(name, size) \
    struct name                            \
    {                                      \
    private:                               \
        __EI_NOT_CREATABLE(name);          \
        uint8_t _dummy[size];              \
    };
#define __EI_MAKE_DUMMY_CLASS(name, size) \
    class name                            \
    {                                     \
    private:                              \
        __EI_NOT_CREATABLE(name);         \
        uint8_t _dummy[size];             \
    };
// clang-format on

#pragma pack(4)

#ifdef _MSC_VER
#pragma warning(disable : 4624)
#endif

#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
