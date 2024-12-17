#ifndef COMPILER_H_INCLUDED
#define COMPILER_H_INCLUDED

#include <stddef.h>

#if __STDC_VERSION__ < 202311L
#define unreachable() (__builtin_unreachable())
#endif

#endif
