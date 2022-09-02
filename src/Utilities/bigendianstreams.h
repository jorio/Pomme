#pragma once

#include <algorithm>
#include <istream>
#include <vector>

namespace Pomme
{
	class StreamPosGuard
	{
		std::istream& stream;
		const std::streampos backup;
		bool active;

	public:
		StreamPosGuard(std::istream& f);

		~StreamPosGuard();

		void Cancel();
	};

	class BigEndianIStream
	{
	public:
		BigEndianIStream(std::istream& theStream);

		void Read(char* dst, size_t n);

		void Skip(size_t n);

		void Goto(std::streamoff absoluteOffset);

		std::streampos Tell() const;

		StreamPosGuard GuardPos();

		std::vector<unsigned char> ReadBytes(size_t n);

		std::string ReadPascalString(int padToAlignment = 1);

		std::string ReadPascalString_FixedLengthRecord(const int maxChars);

		double Read80BitFloat();

		template<typename T>
		T Read()
		{
			char b[sizeof(T)];
			Read(b, sizeof(T));
#if !(__BIG_ENDIAN__)
			if constexpr (sizeof(T) > 1)
			{
				std::reverse(b, b + sizeof(T));
			}
#endif
			return *(T*) b;
		}
	
	private:
		std::istream& stream;
	};

	class BigEndianOStream
	{
	public:
		BigEndianOStream(std::ostream& theStream);

		void Write(const char* src, size_t n);

		void Goto(std::streamoff absoluteOffset);

		std::streampos Tell() const;

		void WritePascalString(const std::string& text, int padToAlignment = 1);

		void WriteRawString(const std::string& text);

		template<typename T>
		void Write(T value)
		{
			char* b = (char*) &value;
#if !(__BIG_ENDIAN__)
			if constexpr (sizeof(T) > 1)
			{
				std::reverse(b, b + sizeof(T));
			}
#endif
			Write(b, sizeof(T));
		}

	private:
		std::ostream& stream;
	};
}
