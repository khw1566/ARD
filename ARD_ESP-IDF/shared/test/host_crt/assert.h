#pragma once
_Noreturn void host_assert(const char *,const char *,int);
#define assert(x) ((x)?(void)0:host_assert(#x,__FILE__,__LINE__))
