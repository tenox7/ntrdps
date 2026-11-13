#ifndef NTRDPS_STDINT_SHIM
#define NTRDPS_STDINT_SHIM
/* Minimal <stdint.h> for Visual C++ 4.2 (cl 10.20, 1996). */
typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef int                int32_t;
typedef unsigned int       uint32_t;
typedef __int64            int64_t;
typedef unsigned __int64   uint64_t;
#ifdef _WIN64
typedef __int64            intptr_t;
typedef unsigned __int64   uintptr_t;
#else
typedef int                intptr_t;
typedef unsigned int       uintptr_t;
#endif
#endif
