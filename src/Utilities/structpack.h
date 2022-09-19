#pragma once

#include <stdint.h>

#if __BIG_ENDIAN__
#define kIsBigEndianNative 1
#else
#define kIsBigEndianNative 0
#endif

#ifdef __cplusplus

#include <algorithm>

template<typename T> T UnpackBEScalar(T x)
{
#if __BIG_ENDIAN__
	return x;
#else
	char* b = (char*)&x;
	std::reverse(b, b + sizeof(T));
	return x;
#endif
}

template<typename T> T UnpackLEScalar(T x)
{
#if !(__BIG_ENDIAN__)
	return x;
#else
	char* b = (char*)&x;
	std::reverse(b, b + sizeof(T));
	return x;
#endif
}

extern "C" {

#endif  // __cplusplus

//-----------------------------------------------------------------------------
// Byteswap an array of scalars

int ByteswapInts(int intSize, int intCount, void* buffer);

static inline int UnpackIntsBE(int intSize, int intCount, void* buffer)
{
#if __BIG_ENDIAN__
	// no-op on big-endian systems
	(void) buffer;
	return intCount * intSize;
#else
	return ByteswapInts(intSize, intCount, buffer);
#endif
}

static inline int UnpackIntsLE(int intSize, int intCount, void* buffer)
{
#if __BIG_ENDIAN__
	return ByteswapInts(intSize, intCount, buffer);
#else
	// no-op on little-endian systems
	(void) buffer;
	return intCount * intSize;
#endif
}

//-----------------------------------------------------------------------------
// Unpack an array of structures

int UnpackStructs(const char* format, int structSize, int structCount, void* buffer);

//-----------------------------------------------------------------------------
// Unpack basic unsigned scalars

static inline uint16_t UnpackU16BE(const void* data)
{
#if __BIG_ENDIAN__
	// no-op on big-endian systems
	return *(const uint16_t*) data;
#else
	const uint8_t* p = (uint8_t*) data;
	return	( p[0] << 8 )
		|	( p[1]      );
#endif
}

static inline uint16_t UnpackU16LE(const void* data)
{
#if __BIG_ENDIAN__
	const uint8_t* p = (uint8_t*) data;
	return	( p[0]      )
		|	( p[1] << 8 );
#else
	// no-op on little-endian systems
	return *(const uint16_t*) data;
#endif
}

static inline uint32_t UnpackU32BE(const void* data)
{
#if __BIG_ENDIAN_
	// no-op on big-endian systems
	return *(const uint32_t*) data;
#else
	const uint8_t* p = (uint8_t*) data;
	return	( p[0] << 24 )
		|	( p[1] << 16 )
		|	( p[2] <<  8 )
		|	( p[3]       );
#endif
}

static inline uint32_t UnpackU32LE(const void* data)
{
#if __BIG_ENDIAN__
	const uint8_t* p = (uint8_t*) data;
	return	( p[0]       )
		|	( p[1] <<  8 )
		|	( p[2] << 16 )
		|	( p[3] << 24 );
#else
	// no-op on little-endian systems
	return *(const uint32_t*) data;
#endif
}

//-----------------------------------------------------------------------------
// Swap in place

static inline uint16_t UnpackU16BEInPlace(void* data)
{
#if __BIG_ENDIAN__
	// no-op on big-endian systems
	return *(uint16_t*) data;
#else
	uint16_t result = UnpackU16BE(data);
	*(uint16_t*) data = result;
	return result;
#endif
}

static inline uint16_t UnpackU16LEInPlace(void* data)
{
#if __BIG_ENDIAN__
	uint16_t result = UnpackU16LE(data);
	*(uint16_t*) data = result;
	return result;
#else
	// no-op on little-endian systems
	return *(uint16_t*) data;
#endif
}

static inline uint32_t UnpackU32BEInPlace(void* data)
{
#if __BIG_ENDIAN__
	// no-op on big-endian systems
	return *(uint32_t*) data;
#else
	uint32_t result = UnpackU32BE(data);
	*(uint32_t*) data = result;
	return result;
#endif
}

static inline uint32_t UnpackU32LEInPlace(void* data)
{
#if __BIG_ENDIAN__
	uint32_t result = UnpackU32LE(data);
	*(uint32_t*) data = result;
	return result;
#else
	// no-op on little-endian systems
	return *(uint32_t*) data;
#endif
}

//-----------------------------------------------------------------------------
// Signed variants

static inline int16_t UnpackI16BE(const void* data) { return (int16_t) UnpackU16BE(data); }
static inline int32_t UnpackI32BE(const void* data) { return (int32_t) UnpackU32BE(data); }

static inline int16_t UnpackI16LE(const void* data) { return (int16_t) UnpackU16LE(data); }
static inline int32_t UnpackI32LE(const void* data) { return (int32_t) UnpackU32LE(data); }

static inline int16_t UnpackI16BEInPlace(void* data) { return (int16_t) UnpackU16BEInPlace(data); }
static inline int32_t UnpackI32BEInPlace(void* data) { return (int32_t) UnpackU32BEInPlace(data); }

static inline int16_t UnpackI16LEInPlace(void* data) { return (int16_t) UnpackU16LEInPlace(data); }
static inline int32_t UnpackI32LEInPlace(void* data) { return (int32_t) UnpackU32LEInPlace(data); }

//-----------------------------------------------------------------------------
// Pack variants. Functionally identical to unpack, but with a different intent:
// convert a native scalar to a specific endianness.

static inline uint32_t PackU32BE(const void *nativeEndianData) { return UnpackU32BE(nativeEndianData); }

#ifdef __cplusplus
}
#endif
