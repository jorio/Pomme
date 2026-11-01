#include "Pomme.h"
#include "PommeFiles.h"
#include "PommeGraphics.h"
#include "PommeMemory.h"
#include "SysFont.h"
#include "Utilities/memstream.h"

#include <iostream>
#include <memory>
#include <cstring>
#include <cmath>

using namespace Pomme;
using namespace Pomme::Graphics;

// Blends the 2x2 neighbourhood (c00 top-left, c10 top-right, c01 bottom-left,
// c11 bottom-right) with fractional weights fx/fy. Used by CopyBits to scale 
// an image when its source and destination rects are different sizes.
static UInt32 BilinearSample(UInt32 c00, UInt32 c10, UInt32 c01, UInt32 c11, float fx, float fy)
{
	UInt32 result = 0;
	for (int shift = 0; shift < 32; shift += 8)
	{
		float top = ((c00 >> shift) & 0xFF) * (1 - fx) + ((c10 >> shift) & 0xFF) * fx;
		float bottom = ((c01 >> shift) & 0xFF) * (1 - fx) + ((c11 >> shift) & 0xFF) * fx;
		UInt8 channel = (UInt8)(top * (1 - fy) + bottom * fy + 0.5f);
		result |= ((UInt32)channel) << shift;
	}
	return result;
}

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

void InsetRect(Rect* r, short dh, short dv)
{
	r->left		+= dh;
	r->right	-= dh;
	r->top		+= dv;
	r->bottom	-= dv;
}

Boolean PtInRect(Point pt, const Rect* r)
{
	return pt.h >= r->left
		&& pt.h < r->right
		&& pt.v >= r->top
		&& pt.v < r->bottom;
}

Boolean SectRect(const Rect* src1, const Rect* src2, Rect* dstRect)
{
	dstRect->left   = std::max(src1->left,   src2->left);
	dstRect->right  = std::min(src1->right,  src2->right);
	dstRect->top    = std::max(src1->top,    src2->top);
	dstRect->bottom = std::min(src1->bottom, src2->bottom);

	// If the resulting rectangle is empty, set it to all zeros
	if (dstRect->left >= dstRect->right || dstRect->top >= dstRect->bottom)
	{
		dstRect->left = dstRect->right = dstRect->top = dstRect->bottom = 0;
		return false;
	}
	return true;
}

void UnionRect(const Rect* src1, const Rect* src2, Rect* dstRect)
{
	dstRect->left   = std::min(src1->left,   src2->left);
	dstRect->right  = std::max(src1->right,  src2->right);
	dstRect->top    = std::min(src1->top,    src2->top);
	dstRect->bottom = std::max(src1->bottom, src2->bottom);
}

Boolean EqualRect(const Rect* rect1, const Rect* rect2)
{
	return rect1->left   == rect2->left
		&& rect1->right  == rect2->right
		&& rect1->top    == rect2->top
		&& rect1->bottom == rect2->bottom;
}

Boolean EmptyRect(const Rect* r)
{
	return r->left >= r->right || r->top >= r->bottom;
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

Boolean GetPixel(short h, short v)
{
	// GetPixel returns true if the pixel at (h,v) in the current port is black.
	// For color ports, we check if the pixel is "dark" (not the background color).
	if (!curPort)
		return false;

	auto offx = curPort->port.portRect.left;
	auto offy = curPort->port.portRect.top;

	int x = h - offx;
	int y = v - offy;

	// Bounds check
	if (x < 0 || x >= curPort->pixels.width || y < 0 || y >= curPort->pixels.height)
		return false;

	UInt32* pixel = curPort->pixels.GetPtr(x, y);
	UInt32 pixelValue = *pixel;

	// In the original QuickDraw, GetPixel returns true if the pixel is black.
	// For this ARGB implementation, check if it's not the background color.
	UInt32 bgColor = penBG;
#if !(__BIG_ENDIAN__)
	ByteswapInts(sizeof(bgColor), 1, &bgColor);
#endif

	return pixelValue != bgColor;
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
	// Returns a pointer that can be used with CopyBits.
	// Internally it's a PixMap but we return BitMap* for API compatibility.
	return (PixMap*) GetImpl(window).macpmPtr;
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

	// In classic QuickDraw, BitMap and PixMap had compatible layouts.
	// We treat all bitmaps as pixmaps internally.
	auto& srcPM = GetImpl((PixMapPtr) srcBits);
	auto& dstPM = GetImpl((PixMapPtr) dstBits);

	const auto& srcBounds = ((const PixMap*)srcBits)->bounds;
	const auto& dstBounds = ((const PixMap*)dstBits)->bounds;

	int srcRectWidth = Width(*srcRect);
	int srcRectHeight = Height(*srcRect);
	int dstRectWidth = Width(*dstRect);
	int dstRectHeight = Height(*dstRect);

	if (srcRectWidth != dstRectWidth || srcRectHeight != dstRectHeight)
	{
		// Only plain srcCopy supports scaling. The other modes (transparent
		// copy, and the srcOr/srcBic/notSrcBic stencil ops below) punch exact,
		// pixel-for-pixel results - e.g. punching a bullet hole into a target
		// mask - where a resampled source would corrupt the result, so those
		// still require matching rect sizes.
		if (mode != srcCopy || srcRectWidth <= 0 || srcRectHeight <= 0 || dstRectWidth <= 0 || dstRectHeight <= 0)
			TODOFATAL2("can only copy between rects of same dimensions");

		// Bilinear-resample srcRect into dstRect. This lets an image be
		// authored at a different resolution than the destination expects
		// (e.g. a background supplied at 2x the game's logical canvas size)
		// and have it scaled to fit automatically, rather than requiring an
		// exact pixel match.
		for (int y = 0; y < dstRectHeight; y++)
		{
			float srcYF = (y + 0.5f) * srcRectHeight / (float)dstRectHeight - 0.5f;
			int sy0 = (int)floorf(srcYF);
			float fy = srcYF - sy0;
			int sy1 = std::min(sy0 + 1, srcRectHeight - 1);
			sy0 = std::max(sy0, 0);

			const UInt32* srcRow0 = srcPM.GetPtr(srcRect->left - srcBounds.left, srcRect->top - srcBounds.top + sy0);
			const UInt32* srcRow1 = srcPM.GetPtr(srcRect->left - srcBounds.left, srcRect->top - srcBounds.top + sy1);
			UInt32* dstRow = dstPM.GetPtr(dstRect->left - dstBounds.left, dstRect->top - dstBounds.top + y);

			for (int x = 0; x < dstRectWidth; x++)
			{
				float srcXF = (x + 0.5f) * srcRectWidth / (float)dstRectWidth - 0.5f;
				int sx0 = (int)floorf(srcXF);
				float fx = srcXF - sx0;
				int sx1 = std::min(sx0 + 1, srcRectWidth - 1);
				sx0 = std::max(sx0, 0);

				dstRow[x] = BilinearSample(srcRow0[sx0], srcRow0[sx1], srcRow1[sx0], srcRow1[sx1], fx, fy);
			}
		}

		curPort->DamageRegion(*dstRect);
		return;
	}

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

		// srcOr/srcBic/notSrcBic are used to carve sprite-mask silhouettes (e.g.
		// punching bullet holes into a target mask). These masks are 32-bit
		// ARGB, but used as a binary stencil (same convention as CopyMask
		// above): a dark pixel means "set" (classic QuickDraw 1 = black),
		// white means "clear" (classic QuickDraw 0 = white). Each classic
		// bitwise op reduces to a single conditional write once expressed in
		// terms of that binary state - see Inside Macintosh: Imaging with
		// QuickDraw for the bit-level definitions this mirrors.
		case srcOr:
		case srcBic:
		case notSrcBic:
		{
			constexpr UInt32 kBlackPixel = 0xFF000000;
			constexpr UInt32 kWhitePixel = 0xFFFFFFFF;

			for (int y = 0; y < srcRectHeight; y++)
			{
				UInt32* dstPix = dstPM.GetPtr(dstRect->left - dstBounds.left, dstRect->top - dstBounds.top + y);
				UInt32* srcPix = srcPM.GetPtr(srcRect->left - srcBounds.left, srcRect->top - srcBounds.top + y);
				for (int x = 0; x < srcRectWidth; x++)
				{
					UInt32 s = *srcPix;
					UInt8 r = (s >> 16) & 0xFF;
					UInt8 g = (s >> 8) & 0xFF;
					UInt8 b = s & 0xFF;
					bool srcIsBlack = r < 128 || g < 128 || b < 128;

					switch (mode)
					{
						case srcOr:
							// dst = dst OR src: a black source forces dst black; a
							// white source leaves dst untouched.
							if (srcIsBlack) *dstPix = kBlackPixel;
							break;
						case srcBic:
							// dst = dst AND NOT src: a black source clears dst to
							// white (this is what punches the hole); white leaves
							// dst untouched.
							if (srcIsBlack) *dstPix = kWhitePixel;
							break;
						case notSrcBic:
							// dst = dst AND src: a white source clears dst to white;
							// black leaves dst untouched.
							if (!srcIsBlack) *dstPix = kWhitePixel;
							break;
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

void CopyMask(
	const PixMap* srcBits,
	const PixMap* maskBits,
              PixMap* dstBits,
	const Rect* srcRect,
	const Rect* maskRect,
	const Rect* dstRect
)
{
	/*
	 * CopyMask copies the source image to the destination, using the mask
	 * to determine which pixels to copy. Where the mask is black (non-zero),
	 * the source pixel is copied; where it's white (zero), the destination
	 * pixel is left unchanged.
	 *
	 * Per Inside Macintosh: Imaging with QuickDraw, the mask acts as a
	 * stencil through which the source is copied.
	 */

	// In classic QuickDraw, BitMap and PixMap had compatible layouts.
	// We treat all bitmaps as pixmaps internally.
	auto& srcPM = GetImpl((PixMapPtr) srcBits);
	auto& maskPM = GetImpl((PixMapPtr) maskBits);
	auto& dstPM = GetImpl((PixMapPtr) dstBits);

	const auto& srcBounds = ((const PixMap*)srcBits)->bounds;
	const auto& maskBounds = ((const PixMap*)maskBits)->bounds;
	const auto& dstBounds = ((const PixMap*)dstBits)->bounds;

	int srcRectWidth = Width(*srcRect);
	int srcRectHeight = Height(*srcRect);
	int maskRectWidth = Width(*maskRect);
	int maskRectHeight = Height(*maskRect);
	int dstRectWidth = Width(*dstRect);
	int dstRectHeight = Height(*dstRect);

	// All three rectangles should have the same dimensions for simple copy
	if (srcRectWidth != dstRectWidth || srcRectHeight != dstRectHeight ||
	    srcRectWidth != maskRectWidth || srcRectHeight != maskRectHeight)
	{
		TODOFATAL2("CopyMask: can only copy between rects of same dimensions");
	}

	for (int y = 0; y < srcRectHeight; y++)
	{
		UInt32* srcPix = srcPM.GetPtr(srcRect->left - srcBounds.left, srcRect->top - srcBounds.top + y);
		UInt32* maskPix = maskPM.GetPtr(maskRect->left - maskBounds.left, maskRect->top - maskBounds.top + y);
		UInt32* dstPix = dstPM.GetPtr(dstRect->left - dstBounds.left, dstRect->top - dstBounds.top + y);

		for (int x = 0; x < srcRectWidth; x++)
		{
			// In a 32-bit ARGB mask, check if mask pixel is non-white.
			// Classic Mac masks were 1-bit: black = copy, white = don't copy.
			UInt32 m = *maskPix;
			
			// Extract RGB (ignoring alpha) and see if it's dark
			UInt8 r = (m >> 16) & 0xFF;
			UInt8 g = (m >> 8) & 0xFF;
			UInt8 b = m & 0xFF;
			
			if (r < 128 || g < 128 || b < 128)
			{
				*dstPix = *srcPix;
			}

			srcPix++;
			maskPix++;
			dstPix++;
		}
	}

	curPort->DamageRegion(*dstRect);
}

// ---------------------------------------------------------------------------- -
// Oval drawing

static void _PlotOvalPoints(ARGBPixmap& pm, int cx, int cy, int x, int y, UInt32 color, int offx, int offy)
{
	// Plot 4 symmetric points of the oval
	pm.Plot(cx + x - offx, cy + y - offy, color);
	pm.Plot(cx - x - offx, cy + y - offy, color);
	pm.Plot(cx + x - offx, cy - y - offy, color);
	pm.Plot(cx - x - offx, cy - y - offy, color);
}

static void _DrawOvalOutline(ARGBPixmap& pm, const Rect* r, UInt32 color, int offx, int offy)
{
	/*
	 * Midpoint ellipse algorithm to draw oval outline.
	 * The oval is inscribed in the bounding rectangle r.
	 */
	int left = r->left;
	int top = r->top;
	int right = r->right;
	int bottom = r->bottom;

	// Center of the oval
	int cx = (left + right) / 2;
	int cy = (top + bottom) / 2;

	// Semi-axes
	int a = (right - left) / 2;  // horizontal radius
	int b = (bottom - top) / 2;  // vertical radius

	if (a <= 0 || b <= 0) return;

	int a2 = a * a;
	int b2 = b * b;
	int fa2 = 4 * a2;
	int fb2 = 4 * b2;

	int x, y, sigma;

	// First region: where slope > -1
	for (x = 0, y = b, sigma = 2 * b2 + a2 * (1 - 2 * b); b2 * x <= a2 * y; x++)
	{
		_PlotOvalPoints(pm, cx, cy, x, y, color, offx, offy);
		if (sigma >= 0)
		{
			sigma += fa2 * (1 - y);
			y--;
		}
		sigma += b2 * ((4 * x) + 6);
	}

	// Second region: where slope <= -1
	for (x = a, y = 0, sigma = 2 * a2 + b2 * (1 - 2 * a); a2 * y <= b2 * x; y++)
	{
		_PlotOvalPoints(pm, cx, cy, x, y, color, offx, offy);
		if (sigma >= 0)
		{
			sigma += fb2 * (1 - x);
			x--;
		}
		sigma += a2 * ((4 * y) + 6);
	}
}

static void _FillOval(ARGBPixmap& pm, const Rect* r, UInt32 color, int offx, int offy)
{
	/*
	 * Fill an oval inscribed in the bounding rectangle r.
	 * Uses scanline approach with midpoint ellipse algorithm.
	 */
	int left = r->left;
	int top = r->top;
	int right = r->right;
	int bottom = r->bottom;

	int cx = (left + right) / 2;
	int cy = (top + bottom) / 2;

	int a = (right - left) / 2;
	int b = (bottom - top) / 2;

	if (a <= 0 || b <= 0) return;

	// For each row from cy-b to cy+b, calculate the x extent and fill
	for (int y = -b; y <= b; y++)
	{
		// Ellipse equation: (x/a)^2 + (y/b)^2 = 1
		// Solving for x: x = a * sqrt(1 - (y/b)^2)
		double yRatio = (double)y / (double)b;
		double xExtent = a * std::sqrt(1.0 - yRatio * yRatio);
		int x1 = cx - (int)xExtent;
		int x2 = cx + (int)xExtent;

		// Fill horizontal line from x1 to x2 at row cy+y
		for (int x = x1; x <= x2; x++)
		{
			pm.Plot(x - offx, cy + y - offy, color);
		}
	}
}

void FrameOval(const Rect* r)
{
	if (!curPort) return;

	UInt32 color = PackU32BE(&penFG);
	auto offx = curPort->port.portRect.left;
	auto offy = curPort->port.portRect.top;

	_DrawOvalOutline(curPort->pixels, r, color, offx, offy);
	curPort->DamageRegion(*r);
}

void PaintOval(const Rect* r)
{
	if (!curPort) return;

	UInt32 color = PackU32BE(&penFG);
	auto offx = curPort->port.portRect.left;
	auto offy = curPort->port.portRect.top;

	_FillOval(curPort->pixels, r, color, offx, offy);
	curPort->DamageRegion(*r);
}

void FillOval(const Rect* r, const Pattern* pat)
{
	// For now, just paint with foreground color (ignoring pattern)
	// A full implementation would apply the pattern
	(void) pat;
	PaintOval(r);
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
