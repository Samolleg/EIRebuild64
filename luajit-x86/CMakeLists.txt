cmake_minimum_required(VERSION 3.5)
project(luajit-x86 C)

if(MSVC)
    string(REGEX REPLACE "/W[1-4]" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /wd4480 /wd4127 /wd4005")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN "-D_CRT_STDIO_INLINE=__declspec(dllexport)__inline")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic -Werror")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "AddonRel")
    if(WIN32 AND CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
        set(ADDON_RELEASE_ENV "ON")
    else()
        message(FATAL_ERROR "AddonRel build type requires MSVC x86")
    endif()
endif()

if(ADDON_RELEASE_ENV)
    string(REGEX REPLACE "/GR-?|/EH[asc-]+" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS_ADDONREL
        "/MT /Zi /O1 /Oy- /Oi- /Gd /GR- /GL /EHsc /DNDEBUG /wd4100 /wd4480 /wd4530 /wd4127 /wd4505 /wd4127")
    set(CMAKE_SHARED_LINKER_FLAGS_ADDONREL
        "/INCREMENTAL:NO /MANIFEST:NO /LTCG /RELEASE /OPT:REF /OPT:ICF /DEBUG /PDBALTPATH:${PROJECT_NAME}.pdb")
endif()

add_library(
    ${PROJECT_NAME} STATIC
    lib_aux.c
    lib_base.c
    lib_bit.c
    lib_buffer.c
    lib_debug.c
    lib_ffi.c
    lib_init.c
    lib_io.c
    lib_jit.c
    lib_math.c
    lib_os.c
    lib_package.c
    lib_string.c
    lib_table.c
    lj_alloc.c
    lj_api.c
    lj_asm.c
    lj_assert.c
    lj_bc.c
    lj_bcread.c
    lj_bcwrite.c
    lj_buf.c
    lj_carith.c
    lj_ccall.c
    lj_ccallback.c
    lj_cconv.c
    lj_cdata.c
    lj_char.c
    lj_clib.c
    lj_cparse.c
    lj_crecord.c
    lj_ctype.c
    lj_debug.c
    lj_dispatch.c
    lj_err.c
    lj_ffrecord.c
    lj_func.c
    lj_gc.c
    lj_gdbjit.c
    lj_ir.c
    lj_lex.c
    lj_lib.c
    lj_load.c
    lj_mcode.c
    lj_meta.c
    lj_obj.c
    lj_opt_dce.c
    lj_opt_fold.c
    lj_opt_loop.c
    lj_opt_mem.c
    lj_opt_narrow.c
    lj_opt_sink.c
    lj_opt_split.c
    lj_parse.c
    lj_prng.c
    lj_profile.c
    lj_record.c
    lj_serialize.c
    lj_snap.c
    lj_state.c
    lj_str.c
    lj_strfmt.c
    lj_strfmt_num.c
    lj_strscan.c
    lj_tab.c
    lj_trace.c
    lj_udata.c
    lj_vmevent.c
    lj_vmmath.c
    lj_vm.obj
)
