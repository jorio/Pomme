#include "Pomme.h"
#include "PommeFiles.h"
#include "PommeGraphics.h"
#include "PommeMemory.h"
#include "SysFont.h"
#include "Utilities/memstream.h"

#include <iostream>
#include <memory>
#include <cstring>

using namespace Pomme;
using namespace Pomme::Graphics;

static bool IntersectRects(const Rect* r1, Rect* r2)
{
	r2->left   = std::max(r1->left, r2->left);
	r2->right  = std::min(r1->right, r2->right);
	r2->top    = std::max(r1->top, r2->top);
	r2->bottom = std::min(r1->bottom, r2->bottom);
	return r2->left < r2->right && r2->top < r2->bottom;
}

// ---------------------------------------------------------------------------- -
// Types

struct GrafPortImpl
{
	GrafPort port;
	ARGBPixmap pixels;
	bool dirty;
	Rect dirtyRect;
	PixMap macpm;
	PixMap* macpmPtr;

	GrafPortImpl(const Rect boundsRect)
		: port({boundsRect, this})
		, pixels(boundsRect.right - boundsRect.left, boundsRect.bottom - boundsRect.top)
		, dirty(false)
	{
		macpm = {};
		macpm.bounds = boundsRect;
		macpm.pixelSize = 32;
		macpm.rowBytes = (pixels.width * macpm.pixelSize / 8) | (1 << 15);		// bit 15 = 1: structure is PixMap, not BitMap
		macpm._impl = (Ptr) &pixels;
		macpmPtr = &macpm;
	}

	void DamageRegion(const Rect& r)
	{
		if (!dirty)
		{
			dirtyRect = r;
		}
		else
		{
			// Already dirty, expand existing dirty rect
			dirtyRect.top    = std::min(dirtyRect.top,    r.top);
			dirtyRect.left   = std::min(dirtyRect.left,   r.left);
			dirtyRect.bottom = std::max(dirtyRect.bottom, r.bottom);
			dirtyRect.right  = std::max(dirtyRect.right,  r.right);
		}
		dirty = true;
	}

	void DamageRegion(SInt16 x, SInt16 y, SInt16 w, SInt16 h)
	{
		Rect r = {y, x, static_cast<SInt16>(y + h), static_cast<SInt16>(x + w)};
		DamageRegion(r);
	}

	~GrafPortImpl()
	{
		macpm._impl = nullptr;
	}
};

// ---------------------------------------------------------------------------- -
// Internal State

static std::unique_ptr<GrafPortImpl> screenPort = nullptr;
static GrafPortImpl* curPort = nullptr;

// Pen colors are stored as ARGB in the host's native endianness
static UInt32 penFG = 0xFF'FF'00'FF;
static UInt32 penBG = 0xFF'00'00'FF;

static int penX = 0;
static int penY = 0;

// ---------------------------------------------------------------------------- -
// Initialization

CGrafPtr Pomme::Graphics::GetScreenPort(void)
{
	return &screenPort->port;
}

void Pomme::Graphics::Init()
{
	Rect boundsRect = {0, 0, 480, 640};
	screenPort = std::make_unique<GrafPortImpl>(boundsRect);
	curPort = screenPort.get();
}

// ---------------------------------------------------------------------------- -
// Internal utils

static UInt32 GetEightColorPaletteValue(long color)
{
	switch (color) {
	case whiteColor:	return clut4[0];
	case yellowColor:	return clut4[1];
	case redColor:		return clut4[3];
	case magentaColor:	return clut4[4];
	case blackColor:	return clut4[15];
	case cyanColor:		return clut4[7];
	case greenColor:	return clut4[8]; // I'm assuming this is light green rather than dark
	case blueColor:		return clut4[6];
	default:			return 0xFF'FF'00'FF;
	}
}

// ---------------------------------------------------------------------------- -
// Errors

OSErr QDError(void)
{
	TODOMINOR();
	return noErr;
}

// ---------------------------------------------------------------------------- -
// PICT resources

static PicHandle GetPictureFromStream(std::istream& stream, bool skip512)
{
	ARGBPixmap pm = ReadPICT(stream, skip512);

	// Tack the data onto the end of the Picture struct,
	// so that DisposeHandle frees both the Picture and the data.
	PicHandle ph = (PicHandle) NewHandle(int(sizeof(Picture) + pm.data.size()));

	Picture& pic = **ph;
	Ptr pixels = (Ptr) *ph + sizeof(Picture);

	pic.picFrame = Rect{0, 0, (SInt16) pm.height, (SInt16) pm.width};
	pic.picSize = -1;
	pic.__pomme_pixelsARGB32 = pixels;

	memcpy(pic.__pomme_pixelsARGB32, pm.data.data(), pm.data.size());

	return ph;
}

PicHandle GetPicture(short PICTresourceID)
{
	Handle rawResource = GetResource('PICT', PICTresourceID);
	if (rawResource == nil)
		return nil;

	memstream stream(*rawResource, GetHandleSize(rawResource));
	PicHandle ph = GetPictureFromStream(stream, false);
	ReleaseResource(rawResource);
	return ph;
}

PicHandle GetPictureFromFile(const FSSpec* spec)
{
	short refNum;

	OSErr error = FSpOpenDF(spec, fsRdPerm, &refNum);
	if (error != noErr)
		return nil;

	auto& stream = Pomme::Files::GetStream(refNum);
	PicHandle ph = GetPictureFromStream(stream, true);
	FSClose(refNum);
	return ph;
}

// ---------------------------------------------------------------------------- -
// Rect

void SetRect(Rect* r, short left, short top, short right, short bottom)
{
	r->left = left;
	r->top = top;
	r->right = right;
	r->bottom = bottom;
}

void OffsetRect(Rect* r, short dh, short dv)
{
	r->left		+= dh;
	r->right	+= dh;
	r->top		+= dv;
	r->bottom	+= dv;
}

// ---------------------------------------------------------------------------- -
// GWorld

static inline GrafPortImpl& GetImpl(GWorldPtr offscreenGWorld)
{
	return *(GrafPortImpl*) offscreenGWorld->_impl;
}

static inline ARGBPixmap& GetImpl(PixMapPtr pixMap)
{
	return *(ARGBPixmap*) pixMap->_impl;
}

OSErr NewGWorld(GWorldPtr* offscreenGWorld, short pixelDepth, const Rect* boundsRect, void* junk1, void* junk2, long junk3)
{
	(void) pixelDepth;
	(void) junk1;
	(void) junk2;
	(void) junk3;

	GrafPortImpl* impl = new GrafPortImpl(*boundsRect);
	*offscreenGWorld = &impl->port;
	return noErr;
}

void DisposeGWorld(GWorldPtr offscreenGWorld)
{
	delete &GetImpl(offscreenGWorld);
}

void GetGWorld(CGrafPtr* port, GDHandle* gdh)
{
	*port = &curPort->port;
	*gdh = nil;
}

void SetGWorld(CGrafPtr port, GDHandle gdh)
{
	(void) gdh;
	SetPort(port);
}

PixMapHandle GetGWorldPixMap(GWorldPtr offscreenGWorld)
{
	return &GetImpl(offscreenGWorld).macpmPtr;
}

Ptr GetPixBaseAddr(PixMapHandle pm)
{
	return (Ptr) GetImpl(*pm).data.data();
}

// ---------------------------------------------------------------------------- -
// Port

void SetPort(GrafPtr port)
{
	curPort = &GetImpl(port);
}

void GetPort(GrafPtr* outPort)
{
	*outPort = &curPort->port;
}

Boolean IsPortDamaged(void)
{
	return curPort->dirty;
}

void GetPortDamageRegion(Rect* r)
{
	*r = curPort->dirtyRect;
}

void ClearPortDamage(void)
{
	curPort->dirty = false;
}

void DamagePortRegion(const Rect* r)
{
	curPort->DamageRegion(*r);
}

CGrafPtr GetWindowPort(WindowPtr window)
{
	return window;
}

PixMap* GetPortBitMapForCopyBits(CGrafPtr window)
{
	return GetImpl(window).macpmPtr;
}

Rect* GetPortBounds(CGrafPtr port, Rect* rect)
{
	*rect = port->portRect;
	return rect;
}

void DumpPortTGA(const char* outPath)
{
	curPort->pixels.WriteTGA(outPath);
}

// ---------------------------------------------------------------------------- -
// Pen state manipulation

void MoveTo(short h, short v)
{
	penX = h;
	penY = v;
}

void ForeColor(long color)
{
	penFG = GetEightColorPaletteValue(color);
}

void BackColor(long color)
{
	penBG = GetEightColorPaletteValue(color);
}

void GetForeColor(RGBColor* rgb)
{
	rgb->red = (penFG >> 16 & 0xFF) << 8;
	rgb->green = (penFG >> 8 & 0xFF) << 8;
	rgb->blue = (penFG & 0xFF) << 8;
}

void RGBBackColor(const RGBColor* color)
{
	penBG
		= 0xFF'00'00'00
		| ((color->red >> 8) << 16)
		| ((color->green >> 8) << 8)
		| (color->blue >> 8)
		;
}

void RGBForeColor(const RGBColor* color)
{
	penFG
		= 0xFF'00'00'00
		| ((color->red >> 8) << 16)
		| ((color->green >> 8) << 8)
		| (color->blue >> 8)
		;
}

void RGBBackColor2(const UInt32 color)
{
	penBG = 0xFF000000 | (color & 0x00FFFFFF);
}

void RGBForeColor2(const UInt32 color)
{
	penFG = 0xFF000000 | (color & 0x00FFFFFF);
}

void PenNormal(void)
{
	TODOMINOR();
}

void PenSize(short width, short height)
{
	(void) width;
	(void) height;
	TODOMINOR();
}

// ---------------------------------------------------------------------------- -
// Paint

static void _FillRect(const int left, const int top, const int right, const int bottom, UInt32 fillColor)
{
	if (!curPort)
	{
		throw std::runtime_error("_FillRect: no port set");
	}

	Rect dstRect;
	dstRect.left   = left;
	dstRect.top    = top;
	dstRect.right  = right;
	dstRect.bottom = bottom;
	Rect clippedDstRect = dstRect;
	if (!IntersectRects(&curPort->port.portRect, &clippedDstRect))
	{
		return;
	}
	curPort->DamageRegion(clippedDstRect);

	fillColor = PackU32BE(&fillColor);		// convert to big-endian

	UInt32* dst = curPort->pixels.GetPtr(clippedDstRect.left, clippedDstRect.top);

	for (int y = clippedDstRect.top; y < clippedDstRect.bottom; y++)
	{
		for (int x = 0; x < clippedDstRect.right - clippedDstRect.left; x++)
		{
			dst[x] = fillColor;
		}
		dst += curPort->pixels.width;
	}
}

void PaintRect(const struct Rect* r)
{
	_FillRect(r->left, r->top, r->right, r->bottom, penFG);
}

void EraseRect(const struct Rect* r)
{
	_FillRect(r->left, r->top, r->right, r->bottom, penBG);
}

void LineTo(short x1, short y1)
{
	UInt32 color = PackU32BE(&penFG);

	auto offx = curPort->port.portRect.left;
	auto offy = curPort->port.portRect.top;

	int x0 = penX;
	int y0 = penY;
	int dx = std::abs(x1 - x0);
	int sx = x0 < x1 ? 1 : -1;
	int dy = -std::abs(y1 - y0);
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	curPort->DamageRegion(penX, penY, dx, dy);
	while (1)
	{
		curPort->pixels.Plot(x0 - offx, y0 - offy, color);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 >= dy)
		{
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx)
		{
			err += dx;
			y0 += sy;
		}
	}
	penX = x0;
	penY = y0;
}

void FrameRect(const Rect* r)
{
	UInt32 color = PackU32BE(&penFG);

	auto& pm = curPort->pixels;
	auto offx = curPort->port.portRect.left;
	auto offy = curPort->port.portRect.top;

	for (int x = r->left; x < r->right; x++) pm.Plot(x            - offx, r->top        - offy, color);
	for (int x = r->left; x < r->right; x++) pm.Plot(x            - offx, r->bottom - 1 - offy, color);
	for (int y = r->top; y < r->bottom; y++) pm.Plot(r->left      - offx, y             - offy, color);
	for (int y = r->top; y < r->bottom; y++) pm.Plot(r->right - 1 - offx, y             - offy, color);

	curPort->DamageRegion(*r);
}

void FrameArc(const Rect* r, short startAngle, short arcAngle)
{
	(void) r;
	(void) startAngle;
	(void) arcAngle;

	TODOMINOR();
}

void Pomme::Graphics::DrawARGBPixmap(int left, int top, ARGBPixmap& pixmap)
{
	if (!curPort)
	{
		throw std::runtime_error("DrawARGBPixmap: no port set");
	}

	Rect dstRect;
	dstRect.left   = left;
	dstRect.top    = top;
	dstRect.right  = left + pixmap.width;
	dstRect.bottom = top  + pixmap.height;
	Rect clippedDstRect = dstRect;
	if (!IntersectRects(&curPort->port.portRect, &clippedDstRect))
	{
		return;  // wholly outside bounds
	}
	curPort->DamageRegion(clippedDstRect);

	UInt32* src = pixmap.GetPtr(clippedDstRect.left - dstRect.left, clippedDstRect.top - dstRect.top);
	UInt32* dst = curPort->pixels.GetPtr(clippedDstRect.left, clippedDstRect.top);

	for (int y = clippedDstRect.top; y < clippedDstRect.bottom; y++)
	{
		memcpy(dst, src, Width(clippedDstRect) * sizeof(UInt32));
		dst += curPort->pixels.width;
		src += pixmap.width;
	}
}

void DrawPicture(PicHandle myPicture, const Rect* dstRect)
{
	auto& pic = **myPicture;

	UInt32* srcPixels = (UInt32*) pic.__pomme_pixelsARGB32;

	int dstWidth = Width(*dstRect);
	int dstHeight = Height(*dstRect);
	int srcWidth = Width(pic.picFrame);
	int srcHeight = Height(pic.picFrame);

	if (srcWidth != dstWidth || srcHeight != dstHeight)
		TODOFATAL2("we only support dstRect with the same width/height as the source picture");

	for (int y = 0; y < dstHeight; y++)
	{
		memcpy(
			curPort->pixels.GetPtr(dstRect->left, dstRect->top + y),
			srcPixels + y * srcWidth,
			4 * dstWidth);
	}

	curPort->DamageRegion(*dstRect);
}

void CopyBits(
	const PixMap* srcBits,
	PixMap* dstBits,
	const Rect* srcRect,
	const Rect* dstRect,
	short mode,
	void* maskRgn
)
{
	(void) maskRgn;

	auto& srcPM = GetImpl((PixMapPtr) srcBits);
	auto& dstPM = GetImpl(dstBits);

	const auto& srcBounds = srcBits->bounds;
	const auto& dstBounds = dstBits->bounds;

	int srcRectWidth = Width(*srcRect);
	int srcRectHeight = Height(*srcRect);
	int dstRectWidth = Width(*dstRect);
	int dstRectHeight = Height(*dstRect);

	if (srcRectWidth != dstRectWidth || srcRectHeight != dstRectHeight)
		TODOFATAL2("can only copy between rects of same dimensions");

	switch (mode)
	{
		case srcCopy:
			for (int y = 0; y < srcRectHeight; y++)
			{
				memcpy(
						dstPM.GetPtr(dstRect->left - dstBounds.left, dstRect->top - dstBounds.top + y),
						srcPM.GetPtr(srcRect->left - srcBounds.left, srcRect->top - srcBounds.top + y),
						4 * srcRectWidth
				);
			}
			break;

		case srcCopy|transparent:
		{
			// Replaces the destination pixel with the source pixel
			// if the source pixel is not equal to the background color.

			UInt32 transparentColor = penBG;
#if !(__BIG_ENDIAN__)
			ByteswapInts(sizeof(transparentColor), 1, &transparentColor);  // need to byteswap because ARGBPixmap.GetPtr returns a pointer to raw (big-endian) ARGB ints
#endif

			for (int y = 0; y < srcRectHeight; y++)
			{
				UInt32* dstPix = dstPM.GetPtr(dstRect->left - dstBounds.left, dstRect->top - dstBounds.top + y);
				UInt32* srcPix = srcPM.GetPtr(srcRect->left - srcBounds.left, srcRect->top - srcBounds.top + y);
				for (int x = 0; x < srcRectWidth; x++)
				{
					if (*srcPix != transparentColor)
					{
						*dstPix = *srcPix;
					}
					dstPix++;
					srcPix++;
				}
			}
			break;
		}

		default:
			TODOFATAL2("unsupported CopyBits mode " << mode);
			break;
	}

	curPort->DamageRegion(*dstRect);
}

// ---------------------------------------------------------------------------- -
// Text rendering

short TextWidthC(const char* cstr)
{
	if (!cstr) return 0;

	int totalWidth = -SysFont::charSpacing;
	for (; *cstr; cstr++)
	{
		totalWidth += SysFont::charSpacing;
		totalWidth += SysFont::GetGlyph(*cstr).width;
	}
	return totalWidth;
}

void DrawStringC(const char* cstr)
{
	if (!cstr) return;

	_FillRect(
		penX,
		penY - SysFont::ascend,
		penX + TextWidthC(cstr),
		penY + SysFont::descend,
		penBG
	);

	penX -= SysFont::charSpacing;
	for (; *cstr; cstr++)
	{
		penX += SysFont::charSpacing;
		DrawChar(*cstr);
	}
}

void DrawChar(char c)
{
	UInt32 fg = PackU32BE(&penFG);

	auto& glyph = SysFont::GetGlyph(c);

	// Theoretical coordinates of top-left corner of glyph (may be outside port bounds!)
	Rect dstRect;
	dstRect.left   = penX - SysFont::leftMargin;
	dstRect.top    = penY - SysFont::ascend;
	dstRect.right  = dstRect.left + SysFont::widthBits;
	dstRect.bottom = dstRect.top  + SysFont::rows;

	// Advance pen position
	penX += glyph.width;

	Rect clippedDstRect = dstRect;
	if (!IntersectRects(&curPort->port.portRect, &clippedDstRect))
	{
		return;  // wholly outside bounds
	}
	curPort->DamageRegion(clippedDstRect);

	// Glyph boundaries
	int minCol = clippedDstRect.left - dstRect.left;
	int minRow = clippedDstRect.top  - dstRect.top;

	auto* dst2 = curPort->pixels.GetPtr(clippedDstRect.left, clippedDstRect.top);

	for (int glyphY = minRow; glyphY < minRow + Height(clippedDstRect); glyphY++)
	{
		auto rowBits = glyph.bits[glyphY];

		rowBits >>= minCol;

		auto* dstRow = dst2;

		for (int glyphX = minCol; glyphX < minCol + Width(clippedDstRect); glyphX++)
		{
			if (rowBits & 1)
			{
				*dstRow = fg;
			}
			rowBits >>= 1;
			dstRow++;
		}

		dst2 += curPort->pixels.width;
	}
}
