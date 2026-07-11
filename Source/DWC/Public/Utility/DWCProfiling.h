#pragma once

#include "ProfilingDebugging/CpuProfilerTrace.h"

#define DWC_PROFILE_SCOPE(Name) TRACE_CPUPROFILER_EVENT_SCOPE(Name)
