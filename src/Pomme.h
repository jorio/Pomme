#pragma once

#if __APPLE__ && __POWERPC__
#include "CompilerSupport/CoexistWithCarbon.h"
#endif

#include "PommeTypes.h"
#include "PommeEnums.h"
#include "PommeDebug.h"

#include <stddef.h>

//-----------------------------------------------------------------------------
// Structure unpacking

#include "Utilities/structpack.h"

//-----------------------------------------------------------------------------
// PowerPC intrinsics

#define __fres(x) (1.0f/x)
#define __fabs(x) fabs(x)
#if defined(__aarch64__)
#include <arm_neon.h>
static inline float __frsqrte(float f)
{
    return vrsqrteq_f32(vdupq_n_f32(f))[0];
}
#else
#define __frsqrte(x) (1.0f/sqrtf(x))
#endif

//-----------------------------------------------------------------------------
// Source code compat

#define nil NULL

#ifdef __cplusplus
#define POMME_NORETURN [[ noreturn ]]
#else
#define POMME_NORETURN _Noreturn
#endif

#ifdef __cplusplus
extern "C"
{
#endif

//-----------------------------------------------------------------------------
// File/volume management

OSErr FSMakeFSSpec(short vRefNum, long dirID, const char* cstrFileName, FSSpec* spec);

short FSpOpenResFile(const FSSpec* spec, char permission);

// Open a file's data fork
OSErr FSpOpenDF(const FSSpec* spec, char permission, short* refNum);

// Open a file's resource fork
OSErr FSpOpenRF(const FSSpec* spec, char permission, short* refNum);

// Open a file's data fork with fsRdPerm (TODO: it should be fsCurPerm, but we don't support fsCurPerm yet)
// in directory 0 of the given volume. It is legal to pass in a colon-separated hierarchical path in cName.
// (Note: this is an ancient function that predates HFS - IM vol II, 1985)
OSErr FSOpen(const char* cName, short vRefNum, short* refNum);

// Open a file's resource fork with fsRdPerm (TODO: it should be fsCurPerm, but we don't support fsCurPerm yet)
// in directory 0 of the application's volume. It is legal to pass in a colon-separated hierarchical path in cName.
// (Note: this is an ancient function that predates HFS - IM vol II, 1985)
short OpenResFile(const char* cName);

OSErr FSpCreate(const FSSpec* spec, OSType creator, OSType fileType, ScriptCode scriptTag);

OSErr FSpDelete(const FSSpec* spec);

OSErr ResolveAlias(const FSSpec* spec, AliasHandle alias, FSSpec* target, Boolean* wasChanged);

OSErr FindFolder(short vRefNum, OSType folderType, Boolean createFolder, short* foundVRefNum, long* foundDirID);

OSErr DirCreate(short vRefNum, long parentDirID, const char* cstrDirectoryName, long* createdDirID);

OSErr GetVol(char* outVolNameC, short* vRefNum);

//-----------------------------------------------------------------------------
// File I/O

OSErr FSRead(short refNum, long* count, Ptr buffPtr);

OSErr FSWrite(short refNum, long* count, Ptr buffPtr);

OSErr FSClose(short refNum);

OSErr GetEOF(short refNum, long* logEOF);

OSErr SetEOF(short refNum, long logEOF);

OSErr GetFPos(short refNum, long* filePos);

OSErr SetFPos(short refNum, short posMode, long filePos);

//-----------------------------------------------------------------------------
// Resource file management

// MoreMacintoshToolbox.pdf p174
OSErr ResError(void);

void UseResFile(short refNum);

// Gets the file reference number of the current resource file.
short CurResFile(void);

void CloseResFile(short refNum);

// Returns total number of resources of the given type
// in the current resource file only.
short Count1Resources(ResType);

// Returns total number of resource types
// in the current resource file only.
short Count1Types(void);

// Gets resource type available in current resource file.
// Note that the index is 1-based!
void Get1IndType(ResType* theType, short index);

Handle GetResource(ResType theType, short theID);

// Reads a resource from the current resource file.
// `index` isn't the actual resource ID!
// `index` ranges from 1 to the number returned by Get1IndType().
Handle Get1IndResource(ResType theType, short index);

void GetResInfo(Handle theResource, short* theID, ResType* theType, char* name256);

void ReleaseResource(Handle theResource);

void RemoveResource(Handle theResource);

void AddResource(Handle theData, ResType theType, short theID, const char* name);

void ChangedResource(Handle theResource);

void WriteResource(Handle theResource);

void DetachResource(Handle theResource);

long GetResourceSizeOnDisk(Handle);

long SizeResource(Handle);

//-----------------------------------------------------------------------------
// QuickDraw 2D: Errors

OSErr QDError(void);

//-----------------------------------------------------------------------------
// QuickDraw 2D: Shapes

void SetRect(Rect* r, short left, short top, short right, short bottom);

void OffsetRect(Rect* r, short dh, short dv);

// ----------------------------------------------------------------------------
// QuickDraw 2D: PICT

// Read picture from 'PICT' resource.
PicHandle GetPicture(short PICTresourceID);

// Read a picture from a PICT file on disk.
// Pomme extension (not part of the original Toolbox API).
PicHandle GetPictureFromFile(const FSSpec* spec);

// ----------------------------------------------------------------------------
// QuickDraw 2D: GWorld

void DisposeGWorld(GWorldPtr offscreenGWorld);

// IM:QD:6-16
QDErr NewGWorld(
	GWorldPtr* offscreenGWorld,
	short pixelDepth,
	const Rect* boundsRect,
	void* junk1,	// CTabHandle cTable
	void* junk2,	// GDHandle aGDevice
	long junk3		// long flags
);

void GetGWorld(CGrafPtr* port, GDHandle* gdh);

void SetGWorld(CGrafPtr port, GDHandle gdh);

// IM:QD:6-31
PixMapHandle GetGWorldPixMap(GWorldPtr offscreenGWorld);

// IM:QD:6-38
Ptr GetPixBaseAddr(PixMapHandle pm);

// ----------------------------------------------------------------------------
// QuickDraw 2D: Port

void SetPort(GrafPtr port);

void GetPort(GrafPtr* port);

CGrafPtr GetWindowPort(WindowPtr window);

Rect* GetPortBounds(CGrafPtr port, Rect* rect);

// WARNING: actual toolbox function returns BitMap*, not PixMap*!
PixMap* GetPortBitMapForCopyBits(CGrafPtr window);

// ----------------------------------------------------------------------------
// QuickDraw 2D: Pen state manipulation

void MoveTo(short h, short v);

void GetForeColor(RGBColor* rgb);

void ForeColor(long color);

void BackColor(long color);

void RGBBackColor(const RGBColor* color);

void RGBForeColor(const RGBColor* color);

// Pomme extension (not part of the original Toolbox API).
void RGBBackColor2(UInt32 color);

// Pomme extension (not part of the original Toolbox API).
void RGBForeColor2(UInt32 color);

void PenNormal(void);

void PenSize(short width, short height);

// ----------------------------------------------------------------------------
// QuickDraw 2D: Paint

void PaintRect(const Rect* r);

void EraseRect(const Rect* r);

void LineTo(short h, short v);

void FrameRect(const Rect*);

void FrameArc(const Rect* r, short startAngle, short arcAngle);

// ----------------------------------------------------------------------------
// QuickDraw 2D: Text rendering

//short TextWidth(const char* textBuf, short firstByte, short byteCount);

short TextWidthC(const char* cstr);

void DrawChar(char c);

void DrawStringC(const char* cstr);

// IM:QD:7-44
void DrawPicture(PicHandle myPicture, const Rect* dstRect);

// WARNING: Actual toolbox function takes BitMap* arguments, not PixMap*!
void CopyBits(
	const PixMap* srcBits,
	PixMap* dstBits,
	const Rect* srcRect,
	const Rect* dstRect,
	short mode,
	void* maskRgn
);

//-----------------------------------------------------------------------------
// QuickDraw 2D extensions

// Returns true if the current port is "damaged".
// Pomme extension (not part of the original Toolbox API).
Boolean IsPortDamaged(void);

// Stores current port's damaged region into "r".
// You should only call this after having checked that IsPortDamaged() is true.
// Pomme extension (not part of the original Toolbox API).
void GetPortDamageRegion(Rect* r);

// Sets current port as undamaged.
// Pomme extension (not part of the original Toolbox API).
void ClearPortDamage(void);

// Extends the current port's damage region to include the given rectangle.
// Pomme extension (not part of the original Toolbox API).
void DamagePortRegion(const Rect*);

// Writes the current port to a Targa image.
// Pomme extension (not part of the original Toolbox API).
void DumpPortTGA(const char* path);

//-----------------------------------------------------------------------------
// QuickDraw 2D: Color Manager

void ProtectEntry(short index, Boolean protect);

void ReserveEntry(short index, Boolean reserve);

void SetEntries(short start, short count, CSpecArray aTable);

void GetEntryColor(PaletteHandle srcPalette, short srcEntry, RGBColor* dstRGB);

void SetEntryColor(PaletteHandle dstPalette, short dstEntry, const RGBColor* srcRGB);

PaletteHandle NewPalette(short entries, CTabHandle srcColors, short srcUsage, short srcTolerance);

void CopyPalette(PaletteHandle srcPalette, PaletteHandle dstPalette, short srcEntry,short dstEntry, short dstLength);

void RestoreDeviceClut(GDHandle gdh);

//-----------------------------------------------------------------------------
// Misc

POMME_NORETURN void ExitToShell();

void SysBeep(short duration);

void FlushEvents(short, short);

//-----------------------------------------------------------------------------
// Text

// Convert number to Pascal string (with length prefix byte)
void NumToString(long theNum, Str255 theString);

// Convert number to C string (zero-terminated)
int NumToStringC(long theNum, Str255 theStringC);

// Get substring in 'STR#' resource as C string (zero-terminated)
void GetIndStringC(Str255 theStringC, short strListID, short index);

//-----------------------------------------------------------------------------
// Input

void GetKeys(KeyMap);

// Gets current mouse coordinates relative to current port
void GetMouse(Point* mouseLoc);

Boolean Button(void);

//-----------------------------------------------------------------------------
// Memory: No-op

// No-op in Pomme.
static inline void MaxApplZone(void) {}

// No-op in Pomme.
static inline void MoreMasters(void) {}

// No-op in Pomme.
static inline Size CompactMem(Size size) { return size; }

// No-op in Pomme.
// Compact system heap zone manually.
static inline Size CompactMemSys(Size size) { return size; }

// No-op in Pomme.
static inline void PurgeMem(Size size) { (void) size; }

// No-op in Pomme.
static inline void PurgeMemSys(Size size) { (void) size; }

// No-op in Pomme.
// TODO: do something about `grow` and return a large integer to make it look like we have tons of memory.
static inline Size MaxMem(Size* grow) { (void) grow; return 0; }

// No-op in Pomme.
static inline void HNoPurge(Handle handle) { (void) handle; }	// no-op

// No-op in Pomme.
static inline void HLock(Handle handle) { (void) handle; }	// no-op

// No-op in Pomme.
static inline void HLockHi(Handle handle) { (void) handle; }	// no-op

// No-op in Pomme.
static inline void HUnlock(Handle handle) { (void) handle; }	// no-op

// No-op in Pomme.
static inline void NoPurgePixels(PixMapHandle handle) { (void) handle; }	// no-op

// No-op in Pomme.
// To prevent the base address for an offscreen pixel image from being moved
// while you draw into or copy from its pixel map.
static inline Boolean LockPixels(PixMapHandle handle) { (void) handle; return true; }	// no-op; shall always return true

// No-op in Pomme.
// If the Memory Manager started up in 24-bit mode, strips flag bits from 24-bit memory addresses;
// otherwise (in 32-bit mode), returns the address unchanged.
static inline Ptr StripAddress(Ptr ptr) { return ptr; }  // no-op

//-----------------------------------------------------------------------------
// Memory: Handle

Handle NewHandle(Size);

// Allocate prezeroed memory
Handle NewHandleClear(Size);

Handle NewHandleSys(Size);

Handle NewHandleSysClear(Size);

// Allocate temp memory
Handle TempNewHandle(Size, OSErr*);

Size GetHandleSize(Handle);

// Change the logical size of the relocatable block corresponding to a handle
void SetHandleSize(Handle, Size);

void DisposeHandle(Handle);

// Allocates a handle of the given size and copies the contents of srcPtr into it
OSErr PtrToHand(const void* srcPtr, Handle* dstHndl, Size size);

//-----------------------------------------------------------------------------
// Memory: Ptr

Ptr NewPtr(Size);

Ptr NewPtrSys(Size);

Ptr NewPtrClear(Size);

Size GetPtrSize(Ptr p);

void DisposePtr(Ptr p);

//-----------------------------------------------------------------------------
// Memory: heap statistics

// Pomme extension:
// Returns amount of Ptrs and Handles currently live
long Pomme_GetNumAllocs(void);

// Pomme extension:
// Returns lower bound of total heap allocated by application
Size Pomme_GetHeapSize(void);

//-----------------------------------------------------------------------------
// Memory: pointer tracking

void Pomme_FlushPtrTracking(bool issueWarnings);

//-----------------------------------------------------------------------------
// Memory: BlockMove

// Copies a sequence of bytes from one location in memory to another
void BlockMove(const void* srcPtr, void* destPtr, Size byteCount);

void BlockMoveData(const void* srcPtr, void* destPtr, Size byteCount);

//-----------------------------------------------------------------------------
// Time Manager

// Number of seconds elapsed since 1904-01-01 00:00
void GetDateTime(unsigned long* secs);

// Number of usecs elapsed since system startup
void Microseconds(UnsignedWide* microTickCount);

// Number of ticks elapsed since system startup (1 tick = approx. 1/60 of a second)
UInt32 TickCount();

//-----------------------------------------------------------------------------
// Mouse cursor

void InitCursor(void);

void HideCursor(void);

void ShowCursor(void);

//-----------------------------------------------------------------------------
// Sound Manager

OSErr GetDefaultOutputVolume(long*);

OSErr SetDefaultOutputVolume(long);

OSErr SndNewChannel(SndChannelPtr* chan, short synth, long init, SndCallBackProcPtr userRoutine);

OSErr SndDisposeChannel(SndChannelPtr chan, Boolean quietNow);

OSErr SndChannelStatus(SndChannelPtr chan, short theLength, SCStatusPtr theStatus);

OSErr SndDoImmediate(SndChannelPtr chan, const SndCommand* cmd);

OSErr SndDoCommand(SndChannelPtr chan, const SndCommand* cmd, Boolean noWait);

OSErr GetSoundHeaderOffset(SndListHandle sndHandle, long* offset);

OSErr SndStartFilePlay(SndChannelPtr chan, short fRefNum, short resNum, long bufferSize, Ptr theBuffer, /*AudioSelectionPtr*/ void* theSelection, FilePlayCompletionUPP theCompletion, Boolean async);

OSErr SndPauseFilePlay(SndChannelPtr chan);

OSErr SndStopFilePlay(SndChannelPtr chan, Boolean quietNow);

NumVersion SndSoundManagerVersion();

// Pomme extension
Boolean Pomme_DecompressSoundResource(SndListHandle* sndHandlePtr, long* offsetToHeader);

// Pomme extension
SndListHandle Pomme_SndLoadFileAsResource(short fRefNum);

#ifdef __cplusplus
}
#endif
