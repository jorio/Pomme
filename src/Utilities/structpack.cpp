#include "structpack.h"
#include <algorithm>
#include <stdexcept>

#if __BIG_ENDIAN__
#define kNativeEndiannessIndicator '>'
#else
#define kNativeEndiannessIndicator '<'
#endif

static int Unpack(const char* format, char* buffer)
{
	int totalBytes = 0;
	int repeat = 0;

	if (!format)
	{
		throw std::invalid_argument("can't unpack without a format");
	}
	
	char endiannessIndicator = format[0];
	if (endiannessIndicator != '<' && endiannessIndicator != '>')
	{
		throw std::invalid_argument("first format char must be endianness indicator");
	}

	// If the struct's endianness matches our native endianness,
	// we won't do the byteswapping, but we'll still check the format string for consistency
	bool isNative = endiannessIndicator == kNativeEndiannessIndicator;

	for (const char* c2 = format+1; *c2; c2++)
	{
		char c = *c2;
		int fieldLength = -1;

		switch (c)
		{
		case ' ':
		case '\r':
		case '\n':
		case '\t':
			continue;

		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (repeat) repeat *= 10;
			repeat += c - '0';
			continue;

		case 'x': // pad byte
		case 'c': // char
		case 'b': // signed char
		case 'B': // unsigned char
		case '?': // bool
			fieldLength = 1;
			break;

		case 'h': // short
		case 'H': // unsigned short
			fieldLength = 2;
			break;

		case 'i': // int
		case 'I': // unsigned int
		case 'l': // long
		case 'L': // unsigned long
		case 'f': // float
			fieldLength = 4;
			break;

		case 'q': // long long
		case 'Q': // unsigned long long
		case 'd': // double
			fieldLength = 8;
			break;

		default:
			throw std::invalid_argument("unknown format char in structpack format");
		}

		if (totalBytes % fieldLength != 0)
		{
			throw std::invalid_argument("illegal word alignment in structpack format");
		}

		if (!repeat)
			repeat = 1;

		bool doSwap = !isNative && fieldLength > 1;

		if (buffer)
		{
			if (doSwap)
			{
				for (int i = 0; i < repeat; i++)
				{
					std::reverse(buffer, buffer + fieldLength);
					buffer += fieldLength;
				}
			}
			else
			{
				buffer += repeat * fieldLength;
			}
		}

		totalBytes += repeat * fieldLength;
		repeat = 0;
	}

	return totalBytes;
}

int UnpackStructs(const char* format, int structSize, int structCount, void* buffer)
{
	char* byteBuffer = (char*) buffer;
	int totalBytes = 0;
	for (int i = 0; i < structCount; i++)
	{
		int newSize = Unpack(format, byteBuffer);
		byteBuffer += newSize;
		totalBytes += newSize;
	}
	if (totalBytes != structSize * structCount)
	{
		throw std::invalid_argument("unexpected length after byteswap");
	}
	return totalBytes;
}

int ByteswapInts(int intSize, int intCount, void* buffer)
{
	char* byteBuffer = (char*) buffer;
	for (int i = 0; i < intCount; i++)
	{
		std::reverse(byteBuffer, byteBuffer + intSize);
		byteBuffer += intSize;
	}
	return intCount * intSize;
}
