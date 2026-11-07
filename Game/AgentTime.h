#pragma once

// Transient policy of an explicitly launched --background process. The ordinary
// CountTime path never reads these values. All access is on the game thread.
namespace AgentTime {
inline int scale=1;
inline double debtMilliseconds=0;
inline double clientDebtMilliseconds=0;
inline double excludedMilliseconds=0;
inline void Set(int value) noexcept {scale=value;}
}
