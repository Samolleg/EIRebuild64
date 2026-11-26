#pragma once
// Error report package: the renderer logs, settings, newest crash dump,
// newest F11 capture (its final images as PNG, and its json) and a hardware
// summary in %LOCALAPPDATA%\EID3D11\reports\<date-time>[-manual].zip.
#include <windows.h>

#include <string>

namespace owned {

// Game process, the device-lost / host-lost message (message is the existing
// text, reason a one-line cause for the summary). Asks Yes/No; on Yes writes
// the package and shows its path. Blocks the calling thread until answered.
void offer_error_report(const wchar_t* message, const char* reason) noexcept;

// Game process start: when the previous session left a crash dump (the
// addon's Dumps\ei-*.dmp or the render host's host_crash_*.dmp, newer than
// that session's start), asks once on a background thread. Not in harness runs.
void start_error_report_check() noexcept;

// Shift+F11, in the process that takes the capture (the game process, or the
// render host in core mode), right after the F11 capture was requested: a
// worker waits for the capture folder to be written and builds
// <date-time>-manual.zip with it. take_diagnostics_package_result gives the
// on-screen notice text once it is done.
void start_diagnostics_package() noexcept;
// Shift+F11 asks the renderer for one host memory report (read once, then cleared).
bool take_host_memory_report_request() noexcept;
// The renderer calls this when that report has been queued to the event log (or cannot run).
void host_memory_report_done() noexcept;
bool take_diagnostics_package_result(std::string& text) noexcept;

// The renderer's latest memory_status line (video memory against budget,
// private bytes, CPU, frame rate), copied into the package summary.
void set_error_report_status(const char* line) noexcept;

} // namespace owned
