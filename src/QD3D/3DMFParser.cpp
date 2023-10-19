#include "QD3D.h"
#include "PommeDebug.h"
#include "Pomme.h"
#include "3DMFInternal.h"

#if !(POMME_DEBUG_3DMF)
	#define printf(...) do{}while(0)
#endif

class Q3MetaFile_EarlyEOFException : public std::exception
{
public:
	const char *what() const noexcept override
	{
		return "Early EOF in 3DMF";
	}
};

static void Assert(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template<typename T>
static void ReadTriangleVertexIndices(Pomme::BigEndianIStream& f, int numTriangles, TQ3TriMeshData* currentMesh)
{
	for (int i = 0; i < numTriangles; i++)
	{
		T v0 = f.Read<T>();
		T v1 = f.Read<T>();
		T v2 = f.Read<T>();
		currentMesh->triangles[i] = {v0, v1, v2};
	}
}

Q3MetaFileParser::Q3MetaFileParser(std::istream& theBaseStream, TQ3MetaFile& dest)
		: metaFile(dest)
		, baseStream(theBaseStream)
		, f(baseStream)
		, currentMesh(nullptr)
{
}

uint32_t Q3MetaFileParser::Parse1Chunk()
{
	Assert(currentDepth >= 0, "depth underflow");

	//-----------------------------------------------------------
	// Get current chunk offset, type, size

	uint32_t chunkOffset	= f.Tell();
	uint32_t chunkType		= f.Read<uint32_t>();
	uint32_t chunkSize		= f.Read<uint32_t>();

//	std::string myChunk = Pomme::FourCCString(chunkType);
//	const char* myChunkC = myChunk.c_str();

	printf("\n%d-%08x ", currentDepth, chunkOffset);
	for (int i = 0; i < 1 + currentDepth - (chunkType=='endg'?1:0); i++)
		printf("\t");
	printf("%s\t", Pomme::FourCCString(chunkType).c_str());
	fflush(stdout);

	//-----------------------------------------------------------
	// Process current chunk

	switch (chunkType)
	{
		case 0:		// Happens in Diloph_Fin.3df at 0x00014233 -- signals early EOF? or corrupted file? either way, stop parsing.
			throw Q3MetaFile_EarlyEOFException();

		case 'cntr':    // Container
		{
			if (currentDepth == 1)
				__Q3EnlargeArray<TQ3TriMeshFlatGroup>(metaFile.topLevelGroups, metaFile.numTopLevelGroups, 'GLST');

			currentDepth++;
			auto limit = f.Tell() + (std::streamoff) chunkSize;
			while (f.Tell() != limit)
				Parse1Chunk();
			currentDepth--;
			currentMesh = nullptr;
			break;
		}

		case 'bgng':
			if (currentDepth == 1)
				__Q3EnlargeArray<TQ3TriMeshFlatGroup>(metaFile.topLevelGroups, metaFile.numTopLevelGroups, 'GLST');
			currentDepth++;
			f.Skip(chunkSize);		// bgng itself typically contains dspg, dgst
			while ('endg' != Parse1Chunk())
				;
			currentDepth--;
			currentMesh = nullptr;
			break;

		case 'endg':
			Assert(chunkSize == 0, "illegal endg size");
			break;

		case 'tmsh':    // TriMesh
		{
			Assert(!currentMesh, "nested meshes not supported");
			Parse_tmsh(chunkSize);
			Assert(currentMesh, "currentMesh wasn't get set by Parse_tmsh?");

			if (metaFile.numTopLevelGroups == 0)
				__Q3EnlargeArray<TQ3TriMeshFlatGroup>(metaFile.topLevelGroups, metaFile.numTopLevelGroups, 'GLST');

			TQ3TriMeshFlatGroup* group = &metaFile.topLevelGroups[metaFile.numTopLevelGroups-1];
			__Q3EnlargeArray(group->meshes, group->numMeshes, 'GMSH');
			group->meshes[group->numMeshes-1] = currentMesh;
			break;
		}

		case 'atar':    // AttributeArray
			Parse_atar(chunkSize);
			break;

		case 'attr':    // AttributeSet
			Assert(chunkSize == 0, "illegal attr size");
			break;

		case 'kdif':    // Diffuse Color
			Assert(chunkSize == 12, "illegal kdif size");
			Assert(currentMesh, "stray kdif");
			{
				static_assert(sizeof(float) == 4);
				currentMesh->diffuseColor.r = f.Read<float>();
				currentMesh->diffuseColor.g = f.Read<float>();
				currentMesh->diffuseColor.b = f.Read<float>();
			}
			break;

		case 'kxpr':    // Transparency Color
			Assert(chunkSize == 12, "illegal kxpr size");
			Assert(currentMesh, "stray kxpr");
			{
				static_assert(sizeof(float) == 4);
				float r	= f.Read<float>();
				float g	= f.Read<float>();
				float b	= f.Read<float>();
				float a = r;
				printf("%.3f %.3f %.3f\t", r, g, b);
				Assert(r == g && g == b, "kxpr: expecting all components to be equal");
				currentMesh->diffuseColor.a = a;
			}
			break;

		case 'txsu':    // TextureShader
		{
			uint32_t internalTextureID;

			Assert(chunkSize == 0, "illegal txsu size");

			if (knownTextures.find(chunkOffset) != knownTextures.end())
			{
				// We've seen this 'txsu' before. We're here because a 'rfrn' refers to it again.
				// Don't create a new texture for it.
				printf("Already seen this txsu.");
				internalTextureID = knownTextures[chunkOffset];
				// TODO: Just skip to end of container
			}
			else
			{
				internalTextureID = metaFile.numTextures;
				__Q3EnlargeArray(metaFile.textures, metaFile.numTextures, 'TXSU');
				knownTextures[chunkOffset] = internalTextureID;
			}

			if (currentMesh)
			{
				Assert(currentMesh->internalTextureID < 0, "txmm: current mesh already has a texture");
				Assert(currentMesh->texturingMode == kQ3TexturingModeOff, "txmm: current mesh already has a texturing mode");

				currentMesh->internalTextureID = internalTextureID;
				currentMesh->texturingMode = kQ3TexturingModeInvalid;	// set texturing mode to invalid because we don't know if the texture is opaque yet
			}

			break;
		}

		case 'txmm':	// MipmapTexture (after a txsu)
		case 'txpm':	// PixmapTexture (after a txsu)
			if (GetCurrentTextureShader().pixmap)
			{
				printf("Pixmap already set for this txsu\n");
				f.Skip(chunkSize);
			}
			else
			{
				GetCurrentTextureShader().pixmap = ParsePixmap(chunkType, chunkSize);
			}
			break;

		case 'shdr':	// UV clamp/wrap (after a txsu)
			Assert(chunkSize == 8, "illegal shdr size");
			GetCurrentTextureShader().boundaryU = (TQ3ShaderUVBoundary) f.Read<uint32_t>();
			GetCurrentTextureShader().boundaryV = (TQ3ShaderUVBoundary) f.Read<uint32_t>();
			break;

		case 'rfrn':    // Reference (into TOC)
		{
			Assert(chunkSize == 4, "illegal rfrn size");
			uint32_t target = f.Read<uint32_t>();
			printf("TOC#%d -----> %08lx", target, referenceTOC.at(target).offset);
			auto jumpBackTo = f.Tell();
			f.Goto(referenceTOC.at(target).offset);
			Parse1Chunk();
			f.Goto(jumpBackTo);
			break;
		}

		case 'toc ':
			// Already read TOC at beginning
			f.Skip(chunkSize);
			break;

		default:
			throw std::runtime_error("unrecognized 3DMF chunk");
	}

	return chunkType;
}

void Q3MetaFileParser::Parse3DMF()
{
	baseStream.seekg(0, std::ios::end);
	std::streampos fileLength = baseStream.tellg();
	baseStream.seekg(0, std::ios::beg);

	Assert(f.Read<uint32_t>() == '3DMF', "Not a 3DMF file");
	Assert(f.Read<uint32_t>() == 16, "Bad header length");

	uint16_t versionMajor = f.Read<uint16_t>();
	uint16_t versionMinor = f.Read<uint16_t>();
	Assert(versionMajor == 1 && (versionMinor == 5 || versionMinor == 6), "Unsupported 3DMF version");

	uint32_t flags = f.Read<uint32_t>();
	Assert(flags == 0, "Database or Stream aren't supported");

	uint64_t tocOffset = f.Read<uint64_t>();
	printf("Version %d.%d    tocOffset %08lx\n", versionMajor, versionMinor, tocOffset);

	// Read TOC
	if (tocOffset != 0)
	{
		Pomme::StreamPosGuard rewindAfterTOC(baseStream);

		f.Goto(tocOffset);

		Assert('toc ' == f.Read<uint32_t>(), "Expecting toc magic here");
		f.Skip(4); //uint32_t tocSize = f.Read<uint32_t>();
		f.Skip(8); //uint64_t nextToc = f.Read<uint64_t>();
		f.Skip(4); //uint32_t refSeed = f.Read<uint32_t>();
		f.Skip(4); //uint32_t typeSeed = f.Read<uint32_t>();
		uint32_t tocEntryType = f.Read<uint32_t>();
		uint32_t tocEntrySize = f.Read<uint32_t>();
		uint32_t nEntries = f.Read<uint32_t>();

		Assert(tocEntryType == 1, "only QD3D 1.5 3DMF TOCs are recognized");
		Assert(tocEntrySize == 16, "incorrect tocEntrySize");

		for (uint32_t i = 0; i < nEntries;i++)
		{
			uint32_t refID = f.Read<uint32_t>();
			uint64_t objLocation = f.Read<uint64_t>();
			uint32_t objType = f.Read<uint32_t>();

			printf("TOC: refID %d '%s' at %08lx\n", refID, Pomme::FourCCString(objType).c_str(), objLocation);

			referenceTOC[refID] = {objLocation, objType};
		}
	}


	// Chunk Loop
	try
	{
		while (f.Tell() != fileLength)
		{
			Parse1Chunk();
		}
	}
	catch (Q3MetaFile_EarlyEOFException&)
	{
		// Stop parsing
		printf("Early EOF");
	}

	printf("\n");
}

void Q3MetaFileParser::Parse_tmsh(uint32_t chunkSize)
{
	Assert(chunkSize >= 52, "Illegal tmsh size");
	Assert(!currentMesh, "current mesh already set");

	uint32_t	numTriangles			= f.Read<uint32_t>();
	f.Skip(4);	// numTriangleAttributes (u32) -- don't care how many attribs there are, we'll read them in as we go
	uint32_t	numEdges				= f.Read<uint32_t>();
	uint32_t	numEdgeAttributes		= f.Read<uint32_t>();
	uint32_t	numVertices				= f.Read<uint32_t>();
	f.Skip(4);	// numVertexAttributes (u32) -- don't care how many attribs there are, we'll read them in as we go
	printf("%d tris, %d vertices\t", numTriangles, numVertices);

	Assert(0 == numEdges, "edges are not supported");
	Assert(0 == numEdgeAttributes, "edge attributes are not supported");

	// Allocate the mesh.
	// Don't allocate vertex UVs, colors or normals yet. We'll allocate them later if the mesh needs them.
	currentMesh = Q3TriMeshData_New(numTriangles, numVertices, 0);

	__Q3EnlargeArray(metaFile.meshes, metaFile.numMeshes, 'MLST');
	metaFile.meshes[metaFile.numMeshes-1] = currentMesh;

	// Triangles
	if (numVertices <= 0xFF)
	{
		ReadTriangleVertexIndices<uint8_t>(f, numTriangles, currentMesh);
	}
	else if (numVertices <= 0xFFFF)
	{
		ReadTriangleVertexIndices<uint16_t>(f, numTriangles, currentMesh);
	}
	else
	{
		ReadTriangleVertexIndices<uint32_t>(f, numTriangles, currentMesh);
	}

	// Ensure all vertex indices are in the expected range
	for (uint32_t i = 0; i < numTriangles; i++)
	{
		for (auto index : currentMesh->triangles[i].pointIndices)
		{
			Assert(index < numVertices, "3DMF parser: vertex index out of range");
		}
	}


	// Edges
	// (not supported yet)


	// Vertices
	for (uint32_t i = 0; i < numVertices; i++)
	{
		float x = f.Read<float>();
		float y = f.Read<float>();
		float z = f.Read<float>();
		//printf("%f %f %f\n", vertexX, vertexY, vertexZ);
		currentMesh->points[i] = {x, y, z};
	}

	// Bounding box
	{
		float xMin = f.Read<float>();
		float yMin = f.Read<float>();
		float zMin = f.Read<float>();
		float xMax = f.Read<float>();
		float yMax = f.Read<float>();
		float zMax = f.Read<float>();
		uint32_t emptyFlag = f.Read<uint32_t>();
		currentMesh->bBox.min = {xMin, yMin, zMin};
		currentMesh->bBox.max = {xMax, yMax, zMax};
		currentMesh->bBox.isEmpty = emptyFlag? kQ3True: kQ3False;
		//printf("%f %f %f - %f %f %f (empty? %d)\n", xMin, yMin, zMin, xMax, yMax, zMax, emptyFlag);
	}
}

void Q3MetaFileParser::Parse_atar(uint32_t chunkSize)
{
	Assert(chunkSize >= 20, "Illegal atar size");
	Assert(currentMesh, "no current mesh");

	uint32_t attributeType = f.Read<uint32_t>();
	Assert(0 == f.Read<uint32_t>(), "expected zero here");
	uint32_t positionOfArray = f.Read<uint32_t>();
	uint32_t positionInArray = f.Read<uint32_t>();		// what's that?
	uint32_t attributeUseFlag = f.Read<uint32_t>();

	Assert(attributeType >= 1 && attributeType < kQ3AttributeTypeNumTypes, "illegal attribute type");
	Assert(positionOfArray <= 2, "illegal position of array");
	Assert(attributeUseFlag <= 1, "unrecognized attribute use flag");


	bool isTriangleAttribute = positionOfArray == 0;
	bool isVertexAttribute = positionOfArray == 2;

	Assert(isTriangleAttribute || isVertexAttribute, "only face or vertex attributes are supported");

	if (isVertexAttribute &&
			(attributeType == kQ3AttributeTypeShadingUV || attributeType == kQ3AttributeTypeSurfaceUV))
	{
		printf("vertex UVs");
		Assert(!currentMesh->vertexUVs, "current mesh already had a vertex UV array");

		currentMesh->vertexUVs = __Q3Alloc<TQ3Param2D>(currentMesh->numPoints, 'TMuv');

		for (int i = 0; i < currentMesh->numPoints; i++)
		{
			float u = f.Read<float>();
			float v = f.Read<float>();
			currentMesh->vertexUVs[i] = {u, 1-v};
		}
	}
	else if (isVertexAttribute && attributeType == kQ3AttributeTypeNormal)
	{
		printf("vertex normals");
		Assert(positionInArray == 0, "PIA must be 0 for normals");
		Assert(!currentMesh->vertexNormals, "current mesh already had a vertex normal array");

		currentMesh->vertexNormals = __Q3Alloc<TQ3Vector3D>(currentMesh->numPoints, 'TMvn');
		currentMesh->hasVertexNormals = true;

		for (int i = 0; i < currentMesh->numPoints; i++)
		{
			currentMesh->vertexNormals[i].x = f.Read<float>();
			currentMesh->vertexNormals[i].y = f.Read<float>();
			currentMesh->vertexNormals[i].z = f.Read<float>();
		}
	}
	else if (isVertexAttribute && attributeType == kQ3AttributeTypeDiffuseColor)	// used in Bugdom's Global_Models2.3dmf
	{
		printf("vertex diffuse");
//		Assert(positionInArray == 0, "PIA must be 0 for colors");
		Assert(!currentMesh->vertexColors, "current mesh already had a vertex color array");
		currentMesh->vertexColors = __Q3Alloc<TQ3ColorRGBA>(currentMesh->numPoints, 'TMvc');
		currentMesh->hasVertexColors = true;
		for (int i = 0; i < currentMesh->numPoints; i++)
		{
			currentMesh->vertexColors[i].r = f.Read<float>();
			currentMesh->vertexColors[i].g = f.Read<float>();
			currentMesh->vertexColors[i].b = f.Read<float>();
			currentMesh->vertexColors[i].a = 1.0f;
		}
	}
	else if (isTriangleAttribute && attributeType == kQ3AttributeTypeNormal)		// face normals
	{
		printf("face normals (ignore)");
		f.Skip(currentMesh->numTriangles * 3 * 4);
	}
	else
	{
		Assert(false, "unsupported combo");
	}
}

TQ3Pixmap* Q3MetaFileParser::ParsePixmap(uint32_t chunkType, uint32_t chunkSize)
{
	size_t chunkHeaderSize = chunkType == 'txmm'? 8*4: 7*4;
	Assert(chunkSize >= chunkHeaderSize, "incorrect chunk header size");

	uint32_t pixelType;
	uint32_t bitOrder;
	uint32_t byteOrder;
	uint32_t width;
	uint32_t height;
	uint32_t rowBytes;

	if (chunkType == 'txmm')
	{
		uint32_t useMipmapping	= f.Read<uint32_t>();
		pixelType				= f.Read<uint32_t>();
		bitOrder				= f.Read<uint32_t>();
		byteOrder				= f.Read<uint32_t>();
		width					= f.Read<uint32_t>();
		height					= f.Read<uint32_t>();
		rowBytes				= f.Read<uint32_t>();
		uint32_t offset			= f.Read<uint32_t>();
		Assert(!useMipmapping, "mipmapping not supported");
		Assert(offset == 0, "unsupported texture offset");
	}
	else if (chunkType == 'txpm')
	{
		width			= f.Read<uint32_t>();
		height			= f.Read<uint32_t>();
		rowBytes		= f.Read<uint32_t>();
		f.Skip(4); //pixelSize		= f.Read<uint32_t>();
		pixelType		= f.Read<uint32_t>();
		bitOrder		= f.Read<uint32_t>();
		byteOrder		= f.Read<uint32_t>();
	}
	else
	{
		Assert(false, "ParsePixmap: Illegal chunkType");
		return nullptr;
	}

	uint32_t imageSize = rowBytes * height;
	if ((imageSize & 3) != 0)
		imageSize = (imageSize & 0xFFFFFFFC) + 4;

	Assert(chunkSize == chunkHeaderSize + imageSize, "incorrect chunk size");
	Assert(bitOrder == kQ3EndianBig, "unsupported bit order");

#if POMME_DEBUG_3DMF
	printf("%d*%d rb=%d", width, height, rowBytes);

	switch (pixelType)
	{
		case kQ3PixelTypeRGB32:		printf(" RGB32");				break;
		case kQ3PixelTypeARGB32:	printf(" ARGB32");				break;
		case kQ3PixelTypeRGB16:		printf(" RGB16");				break;
		case kQ3PixelTypeARGB16:	printf(" ARGB16");				break;
		case kQ3PixelTypeRGB16_565:	printf(" RGB16_565");			break;
		case kQ3PixelTypeRGB24:		printf(" RGB24");				break;
		case kQ3PixelTypeRGBA32:	printf(" RGBA32");				break;
		default:					printf(" UnknownPixelType");	break;
	}
#endif

	// Find bytes per pixel
	int bytesPerPixel = 0;
	if (pixelType == kQ3PixelTypeRGB16 || pixelType == kQ3PixelTypeARGB16)
		bytesPerPixel = 2;
	else if (pixelType == kQ3PixelTypeRGB32 || pixelType == kQ3PixelTypeARGB32)
		bytesPerPixel = 4;
	else
		Assert(false, "unrecognized pixel type");

	int trimmedRowBytes = bytesPerPixel * width;

	TQ3Pixmap* pixmap = __Q3Alloc<TQ3Pixmap>(1, 'PXMP');

	pixmap->pixelType		= pixelType;
	pixmap->bitOrder		= bitOrder;
	pixmap->byteOrder		= byteOrder;
	pixmap->width			= width;
	pixmap->height			= height;
	pixmap->pixelSize		= bytesPerPixel * 8;
	pixmap->rowBytes		= trimmedRowBytes;
	pixmap->image			= __Q3Alloc<uint8_t>(trimmedRowBytes * height, 'IMAG');

	// Trim padding at end of rows
	for (uint32_t y = 0; y < height; y++)
	{
		f.Read((Ptr) pixmap->image + y*pixmap->rowBytes, pixmap->rowBytes);
		f.Skip(rowBytes - width * bytesPerPixel);
	}

	// Convert to native endianness (especially to avoid breaking 16-bit 1-5-5-5 ARGB textures)
	if (byteOrder != kQ3EndianNative)
	{
		ByteswapInts(bytesPerPixel, width*height, pixmap->image);
		pixmap->byteOrder = kQ3EndianNative;
	}

	Q3Pixmap_ApplyEdgePadding(pixmap);

	return pixmap;
}

TQ3TextureShader& Q3MetaFileParser::GetCurrentTextureShader()
{
	Assert(metaFile.numTextures > 0, "txmm/txpm: no txsu opened");
	return metaFile.textures[metaFile.numTextures - 1];
}
