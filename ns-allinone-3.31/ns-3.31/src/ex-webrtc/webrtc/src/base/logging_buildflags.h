// TODO(fqj) use generated ones.
#ifndef BASE_LOGGING_BUILDFLAGS_H_
#define BASE_LOGGING_BUILDFLAGS_H_
#include "build/buildflag.h"
#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
#define BUILDFLAG_INTERNAL_ENABLE_LOG_ERROR_NOT_REACHED() (1)
#else
#define BUILDFLAG_INTERNAL_ENABLE_LOG_ERROR_NOT_REACHED() (0)
#endif
#endif  // BASE_LOGGING_BUILDFLAGS_H_
