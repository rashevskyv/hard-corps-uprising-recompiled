/* glibc-style endian.h for newlib (thirdparty/crypto/sha256.cpp). */
#ifndef HCU_SWITCH_ENDIAN_H
#define HCU_SWITCH_ENDIAN_H

#include <machine/endian.h>

#ifndef __BYTE_ORDER
#define __BYTE_ORDER _BYTE_ORDER
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#define __BIG_ENDIAN _BIG_ENDIAN
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER _BYTE_ORDER
#define LITTLE_ENDIAN _LITTLE_ENDIAN
#define BIG_ENDIAN _BIG_ENDIAN
#endif

#ifndef htobe16
#define htobe16(x) __builtin_bswap16(x)
#define htole16(x) (x)
#define be16toh(x) __builtin_bswap16(x)
#define le16toh(x) (x)
#define htobe32(x) __builtin_bswap32(x)
#define htole32(x) (x)
#define be32toh(x) __builtin_bswap32(x)
#define le32toh(x) (x)
#define htobe64(x) __builtin_bswap64(x)
#define htole64(x) (x)
#define be64toh(x) __builtin_bswap64(x)
#define le64toh(x) (x)
#endif

#endif
