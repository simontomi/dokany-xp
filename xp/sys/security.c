// XP build wrapper (WDK 7.1 build.exe only finds sources in its own folder)
// The WDK warning.h (forced after xp_compat.h) raises C4242 (possible loss of
// data) to level 3; the upstream build does not report it, nor C4018
// (signed/unsigned comparison, x64 only).
#pragma warning(disable : 4242 4018)
#include "../../sys/security.c"
