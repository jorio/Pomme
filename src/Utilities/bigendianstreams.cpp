#include "Utilities/bigendianstreams.h"
#include "Utilities/IEEEExtended.h"
#include <cstdint>

Pomme::StreamPosGuard::StreamPosGuard(std::istream& theStream) :
	stream(theStream)
	, backup(theStream.tellg())
	, active(true)
{
}

Pomme::StreamPosGuard::~StreamPosGuard()
{
	if (active)
	{
		stream.seekg(backup, std::ios_base::beg);
	}
}

void Pomme::StreamPosGuard::Cancel()
{
	active = false;
}

// ----------------------------------------------------------------------------

Pomme::BigEndianIStream::BigEndianIStream(std::istream& theStream) :
	stream(theStream)
{
}

void Pomme::BigEndianIStream::Read(char* dst, size_t n)
{
	stream.read(dst, n);
	if (stream.eof())
	{
		throw std::out_of_range("Read past end of stream!");
	}
}

std::vector<unsigned char> Pomme::BigEndianIStream::ReadBytes(size_t n)
{
	std::vector<unsigned char> buf(n);
	Read(reinterpret_cast<char*>(buf.data()), n);
	return buf;
}

std::string Pomme::BigEndianIStream::ReadPascalString(int padToAlignment)
{
	int length = Read<uint8_t>();
	auto bytes = ReadBytes(length);
	bytes.push_back('\0');

	int padding = (length + 1) % padToAlignment;
	if (padding != 0)
		Skip(padding);

	return std::string((const char*) &bytes.data()[0]);
}

std::string Pomme::BigEndianIStream::ReadPascalString_FixedLengthRecord(const int maxChars)
{
	int length = Read<uint8_t>();
	char buf[256];
	stream.read(buf, maxChars);
	return std::string(buf, length);
}

double Pomme::BigEndianIStream::Read80BitFloat()
{
	auto bytes = ReadBytes(10);
	return ConvertFromIeeeExtended((unsigned char*) bytes.data());
}

void Pomme::BigEndianIStream::Goto(std::streamoff absoluteOffset)
{
	stream.seekg(absoluteOffset, std::ios_base::beg);
}

void Pomme::BigEndianIStream::Skip(size_t n)
{
	stream.seekg(n, std::ios_base::cur);
}

std::streampos Pomme::BigEndianIStream::Tell() const
{
	return stream.tellg();
}

Pomme::StreamPosGuard Pomme::BigEndianIStream::GuardPos()
{
	return Pomme::StreamPosGuard(stream);
}

// ----------------------------------------------------------------------------

Pomme::BigEndianOStream::BigEndianOStream(std::ostream& theStream) :
	stream(theStream)
{
}

void Pomme::BigEndianOStream::Write(const char* src, size_t n)
{
	stream.write(src, n);
}

void Pomme::BigEndianOStream::WritePascalString(const std::string& text, int padToAlignment)
{
	size_t length = text.length();
	if (length > 255)
	{
		throw std::out_of_range("WritePascalString: must be <255 characters!");
	}

	Write<uint8_t>((uint8_t) length);
	Write(text.data(), length);

	size_t padding = (length + 1) % padToAlignment;
	while (padding > 0)
	{
		Write<uint8_t>(0);
		padding--;
	}
}

void Pomme::BigEndianOStream::WriteRawString(const std::string& text)
{
	Write(text.data(), text.length());
}

void Pomme::BigEndianOStream::Goto(std::streamoff absoluteOffset)
{
	stream.seekp(absoluteOffset, std::ios_base::beg);
}

std::streampos Pomme::BigEndianOStream::Tell() const
{
	return stream.tellp();
}

