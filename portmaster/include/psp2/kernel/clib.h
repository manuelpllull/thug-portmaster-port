#ifndef _PSP2_KERNEL_CLIB_H_
#define _PSP2_KERNEL_CLIB_H_

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define sceClibPrintf printf
#define sceClibVsnprintf vsnprintf
#define sceClibSnprintf snprintf

#ifdef __cplusplus
}
#endif

#endif // _PSP2_KERNEL_CLIB_H_
