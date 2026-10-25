#pragma once

// The source commit this build was made from (build_version.cpp; the build
// passes it to that one file, so a new commit recompiles nothing else).
// "unknown" where the build does not set it (x64 host, shader generator).
namespace owned {
extern const char kBuildCommit[];
} // namespace owned
