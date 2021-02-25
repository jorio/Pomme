#pragma once

#include <stdint.h>

#ifdef __cplusplus

#include <algorithm>

template<typename T> T ByteswapScalar(T x)
{
#if TARGET_RT_BIGENDIAN
	return x;
#else
	char* b = (char*)&x;
	std::reverse(b, b + sizeof(T));
	return x;
#endif
}

#endif


#ifdef __cplusplus
extern "C" {
#endif

int ByteswapStructs(const char* format, int structSize, int structCount, void* buffer);

int ByteswapInts(int intSize, int intCount, void* buffer);

static inline uint16_t Byteswap16(const void* data)
{
	const uint8_t* p = (uint8_t*) data;
	return	( p[0] << 8 )
		|	( p[1]      );
}

static inline int16_t Byteswap16Signed(const void* data)
{
	return (int16_t) Byteswap16(data);
}

static inline int32_t Byteswap16SignedRW(void* data)
{
	int16_t result = Byteswap16Signed(data);
	*(int16_t*) data = result;
	return result;
}

static inline uint32_t Byteswap32(const void* data)
{
	const uint8_t* p = (uint8_t*) data;
	return	( p[0] << 24 )
		|	( p[1] << 16 )
		|	( p[2] <<  8 )
		|	( p[3]       );
}

static inline int32_t Byteswap32Signed(const void* data)
{
	return (int32_t) Byteswap32(data);
}

static inline int32_t Byteswap32SignedRW(void* data)
{
	int32_t result = Byteswap32Signed(data);
	*(int32_t*) data = result;
	return result;
}

#ifdef __cplusplus
}
#endif
