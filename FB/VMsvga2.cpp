/*
 *  VMsvga2.cpp
 *  VMsvga2
 *
 *  Created by Zenith432 on July 2nd 2009.
 *  Copyright 2009-2010 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#include <stdarg.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODeviceTreeSupport.h>
#include "VMsvga2.h"
#include "vmw_options_fb.h"
#include "VLog.h"

#include "svga_apple_header.h"
#include "svga_reg.h"
#include "svga_apple_footer.h"

#define CLASS VMsvga2
#define super IOFramebuffer
OSDefineMetaClassAndStructors(VMsvga2, IOFramebuffer);

#if LOGGING_LEVEL >= 1
#define LogPrintf(log_level, fmt, ...) do { if (log_level <= logLevelFB) VLog("IOFB: ", fmt, ##__VA_ARGS__); } while (false)
#else
#define LogPrintf(log_level, fmt, ...)
#endif

#define FMT_D(x) static_cast<int>(x)
#define FMT_U(x) static_cast<unsigned>(x)

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1056
/*
 * The availability of hotspot information in IOHardwareCursorInfo
 * was silently introduced in OS 10.5.6 without any versioning change.
 */
#define HAVE_CURSOR_HOTSPOT
#warning Building for OS 10.5.6 and later
#endif

#ifndef HAVE_CURSOR_HOTSPOT
#define	GetShmem(instance)	((StdFBShmem_t *)(instance->priv))
#endif

typedef unsigned long long __m64 __attribute__((vector_size(8), may_alias));

static __attribute__((used)) char const copyright[] = "Copyright 2009-2010 Zenith432";

static char const pixelFormatStrings[] = IO32BitDirectPixels "\0";

static UInt8 edid[128];

static bool have_edid = false;

unsigned __attribute__((visibility("hidden"))) vmw_options_fb = 0;

int __attribute__((visibility("hidden"))) logLevelFB = LOGGING_LEVEL;

#pragma mark -
#pragma mark Private Methods
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::Cleanup()
{
	cancelRefreshTimer();
	if (m_restore_call)
		thread_call_cancel(m_restore_call);
	deleteRefreshTimer();
#if 0
	if (svga.HasCapability(0xFFFFFFFFU))
		svga.Disable();
#endif
	svga.Cleanup();
	if (m_restore_call) {
		thread_call_free(m_restore_call);
		m_restore_call = 0;
	}
#if 0	/* VMwareGfx 3.1.1 */
	if (m_cursor_image) {
		IOFree(m_cursor_image, 16384U);
		m_cursor_image = 0;
	}
#endif
	if (m_bar1_map) {
		m_bar1_map->release();
		m_bar1_map = 0;
		m_bar1_ptr = 0;
	}
	if (m_bar1) {
		m_bar1->release();
		m_bar1 = 0;
	}
	if (m_iolock) {
		IOLockFree(m_iolock);
		m_iolock = 0;
	}
}

__attribute__((visibility("hidden")))
UInt CLASS::FindDepthMode(IOIndex depth)
{
	return depth ? 0 : 32;
}

__attribute__((visibility("hidden")))
DisplayModeEntry const* CLASS::GetDisplayMode(IODisplayModeID displayMode)
{
	if (displayMode == CUSTOM_MODE_ID)
		return &customMode;
	if (displayMode >= 1 && displayMode <= NUM_DISPLAY_MODES)
		return &modeList[displayMode - 1];
	LogPrintf(1, "%s: Bad mode ID=%d\n", __FUNCTION__, FMT_D(displayMode));
	return 0;
}

__attribute__((visibility("hidden")))
void CLASS::IOSelectToString(IOSelect io_select, char* output)
{
	*output = static_cast<char>(io_select >> 24);
	output[1] = static_cast<char>(io_select >> 16);
	output[2] = static_cast<char>(io_select >> 8);
	output[3] = static_cast<char>(io_select);
	output[4] = '\0';
}

__attribute__((visibility("hidden")))
IODisplayModeID CLASS::TryDetectCurrentDisplayMode(IODisplayModeID defaultMode) const
{
	UInt w = svga.getCurrentWidth();
	UInt h = svga.getCurrentHeight();
	for (unsigned i = 1; i < NUM_DISPLAY_MODES; ++i)
		if (w == modeList[i].width && h == modeList[i].height)
			return i + 1;
	return defaultMode;
}

#pragma mark -
#pragma mark Cursor Methods
#pragma mark -

IOReturn CLASS::setCursorState(SInt32 x, SInt32 y, bool visible)
{
	if (!checkOptionFB(VMW_OPTION_FB_FIFO_INIT))
		return kIOReturnUnsupported;
	LogPrintf(2, "%s: xy=%d %d visi=%d\n", __FUNCTION__, FMT_D(x), FMT_D(y), visible ? 1 : 0);
	x += m_hotspot_x;
	if (x < 0)
		x = 0;
	y += m_hotspot_y;
	if (y < 0)
		y = 0;
	IOLockLock(m_iolock);
	svga.setCursorState(
		static_cast<UInt>(x),
		static_cast<UInt>(y),
		visible);
	IOLockUnlock(m_iolock);
	return kIOReturnSuccess /* kIOReturnUnsupported */;
}

__attribute__((visibility("hidden")))
void CLASS::ConvertAlphaCursor(UInt* cursor, UInt width, UInt height)
{
	/*
	 * Pre-multiply alpha cursor
	 */
#ifdef VECTORIZE
	static __m64 const datum = { 0x808180818081ULL };
	__m64 const mm_zero = { 0ULL };
	__m64 mm0;
	UInt alpha, num_pixels;
	for (num_pixels = width * height; num_pixels; --num_pixels, ++cursor) {
		alpha = (*cursor) >> 24U;
		if (!alpha || static_cast<UInt8>(alpha) == 255U)
			continue;
		mm0 = __builtin_ia32_punpcklbw((__m64){*cursor}, mm_zero);
		*cursor = __builtin_ia32_vec_ext_v2si(
			__builtin_ia32_packuswb(
			__builtin_ia32_vec_set_v4hi(
			__builtin_ia32_psrlwi(
			__builtin_ia32_pmulhuw(
			__builtin_ia32_pmullw(
			__builtin_ia32_pshufw(mm0, 255), mm0), datum), 7), alpha, 3), mm_zero), 0);
	}
	__builtin_ia32_emms();
#else /* VECTORIZE */
	UInt i, pixel, alpha, r, g, b;
	UInt num_pixels = width * height;
#if 0
	LogPrintf(2, "%s: %ux%u pixels @ %p\n", __FUNCTION__, width, height, cursor);
#endif
	for (i = 0; i < num_pixels; ++i) {
		pixel = cursor[i];
		alpha = pixel >> 24;
		if (!alpha || alpha == 255U)
			continue;
		b = (pixel & 0xFFU) * alpha / 255U;
		g = ((pixel >> 8) & 0xFFU) * alpha / 255U;
		r = ((pixel >> 16) & 0xFFU) * alpha / 255U;
		cursor[i] = (pixel & 0xFF000000U) | (r << 16) | (g << 8) | b;
	}
#endif /* VECTORIZE */
}

IOReturn CLASS::setCursorImage(void* cursorImage)
{
	void* harware_cursor;
#ifndef HAVE_CURSOR_HOTSPOT
	static int once = 1;
	StdFBShmem_t *shmem;
#else
	UInt16 const* p_hotspots;
#endif
	IOHardwareCursorDescriptor curd;
	IOHardwareCursorInfo curi;

	if (!checkOptionFB(VMW_OPTION_FB_FIFO_INIT))
		return kIOReturnUnsupported;
	LogPrintf(2, "%s: cursor wxh=%dx%d\n", __FUNCTION__, 64, 64);
	IOLockLock(m_iolock);
#if 0	/* VMwareGfx 3.1.1 */
	if (!m_cursor_image) {
		m_cursor_image = IOMalloc(16384U);
		if (!m_cursor_image) {
			LogPrintf(1, "%s: out of memory on cacheCursorMemory %ux%u\n",
					  __FUNCTION__, FMT_U(curi.cursorWidth), FMT_U(curi.cursorHeight));
			return kIOReturnUnsupported;
		}
	}
#else
	harware_cursor = svga.BeginDefineAlphaCursor(64, 64, 4);
#endif
	m_hotspot_x = 0;
	m_hotspot_y = 0;
	bzero(&curd, sizeof curd);
	curd.majorVersion = kHardwareCursorDescriptorMajorVersion;
	curd.minorVersion = kHardwareCursorDescriptorMinorVersion;
	curd.width = 64;
	curd.height = 64;
	curd.bitDepth = 32;
	curd.supportedSpecialEncodings = kInvertingEncodedPixel;
	curd.specialEncodings[kInvertingEncoding] = 0xFF000000U;
	bzero(&curi, sizeof curi);
	curi.majorVersion = kHardwareCursorInfoMajorVersion;
	curi.minorVersion = kHardwareCursorInfoMinorVersion;
	curi.hardwareCursorData = static_cast<UInt8*>(harware_cursor); /* m_cursor_image in VMwareGfx 3.1.1 */
#ifdef HAVE_CURSOR_HOTSPOT
	p_hotspots = reinterpret_cast<UInt16 const*>(&curi.hardwareCursorData + 1);
#else
	shmem = GetShmem(this);
	if (shmem) {
		if (shmem->version == kIOFBTenPtTwoShmemVersion) {
			UInt u = (cursorImage != 0 ? 1 : 0);
			m_hotspot_x = shmem->hotSpot[u].x;
			m_hotspot_y = shmem->hotSpot[u].y;
#if 0
			if (shmem->cursorSize[u].width < 64)
				curd.width = shmem->cursorSize[u].width;
			if (shmem->cursorSize[u].height < 64)
				curd.height = shmem->cursorSize[u].height;
#endif
		} else if (once) {
			LogPrintf(1, "%s: Unknown version %d.\n", __FUNCTION__, shmem->version);
			once = 0;
		}
	}
#endif
	LogPrintf(3, "%s: cursor %p: desc %ux%u @ %u\n", __FUNCTION__,
			  curi.hardwareCursorData, FMT_U(curd.width), FMT_U(curd.height), FMT_U(curd.bitDepth));
	if (!convertCursorImage(cursorImage, &curd, &curi)) {
		svga.FIFOCommit(0); /* not needed in VMwareGfx 3.1.1 */
		IOLockUnlock(m_iolock);
		LogPrintf(1, "%s: convertCursorImage() failed %ux%u\n", __FUNCTION__,
				  FMT_U(curi.cursorWidth), FMT_U(curi.cursorHeight));
		return kIOReturnUnsupported;
	}
	LogPrintf(3, "%s: cursor %p: info %ux%u\n", __FUNCTION__,
			  curi.hardwareCursorData, FMT_U(curi.cursorWidth), FMT_U(curi.cursorHeight));
#ifdef HAVE_CURSOR_HOTSPOT
	LogPrintf(3, "%s: hotspots: %d vs %d (x), %d vs %d (y)\n", __FUNCTION__,			// Added
			  m_hotspot_x, FMT_D(*p_hotspots), m_hotspot_y, FMT_D(p_hotspots[1]));
	m_hotspot_x = *p_hotspots;
	m_hotspot_y = p_hotspots[1];
#endif
	ConvertAlphaCursor(
		reinterpret_cast<UInt*>(curi.hardwareCursorData),
		curi.cursorWidth,
		curi.cursorHeight);
#if 0	/* VMwareGfx 3.1.1 */
	memcpy(svga.BeginDefineAlphaCursor(64, 64, 4), curi.hardwareCursorData, 16384U);
#endif
	svga.EndDefineAlphaCursor(
		curi.cursorWidth,
		curi.cursorHeight,
		4,
		m_hotspot_x,
		m_hotspot_y);
	IOLockUnlock(m_iolock);
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark IOFramebuffer Methods
#pragma mark -

UInt64 CLASS::getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	return 0ULL;
}

IOReturn CLASS::setInterruptState(void* interruptRef, UInt32 state)
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	if (interruptRef != &m_intr)
		return kIOReturnBadArgument;
	m_intr_enabled = (state != 0);
	return kIOReturnSuccess /* kIOReturnUnsupported */;
}

IOReturn CLASS::unregisterInterrupt(void* interruptRef)
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	if (interruptRef != &m_intr)
		return kIOReturnBadArgument;
	bzero(interruptRef, sizeof m_intr);
	m_intr_enabled = false;
	return kIOReturnSuccess;
}

IOItemCount CLASS::getConnectionCount()
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	return 1;
}

IOReturn CLASS::getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth)
{
	if (displayMode)
		*displayMode = m_display_mode;
	if (depth)
		*depth = m_depth_mode;
	LogPrintf(2, "%s: display mode ID=%d, depth mode ID=%d\n", __FUNCTION__,
			  FMT_D(m_display_mode), FMT_D(m_depth_mode));
	return kIOReturnSuccess;
}

IOReturn CLASS::getDisplayModes(IODisplayModeID* allDisplayModes)
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	if (!allDisplayModes)
		return kIOReturnBadArgument;
	if (m_custom_switch) {
		*allDisplayModes = CUSTOM_MODE_ID;
		return kIOReturnSuccess;
	}
	memcpy(allDisplayModes, &m_modes[0], m_num_active_modes * sizeof(IODisplayModeID));
	return kIOReturnSuccess;
}

IOItemCount CLASS::getDisplayModeCount()
{
	IOItemCount r;
	r = m_custom_switch ? 1 : m_num_active_modes;
	LogPrintf(2, "%s: mode count=%u\n", __FUNCTION__, FMT_U(r));
	return r;
}

const char* CLASS::getPixelFormats()
{
	LogPrintf(2, "%s: pixel formats=%s\n", __FUNCTION__, &pixelFormatStrings[0]);
	return &pixelFormatStrings[0];
}

IODeviceMemory* CLASS::getVRAMRange()
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	if (!m_bar1)
		return 0;
	m_bar1->retain();
	return m_bar1;
}

IODeviceMemory* CLASS::getApertureRange(IOPixelAperture aperture)
{
	UInt fb_offset, fb_size;
	IODeviceMemory* mem;

	if (aperture != kIOFBSystemAperture) {
		LogPrintf(1, "%s: Failed request for aperture=%d (%d)\n", __FUNCTION__,
				  FMT_D(aperture), kIOFBSystemAperture);
		return 0;
	}
	if (!m_bar1)
		return 0;
	IOLockLock(m_iolock);
	fb_offset = svga.getCurrentFBOffset();
	fb_size = svga.getCurrentFBSize();
	IOLockUnlock(m_iolock);
	LogPrintf(2, "%s: aperture=%d, fb offset=%u, fb size=%u\n", __FUNCTION__,
			  FMT_D(aperture), fb_offset, fb_size);
	mem = IODeviceMemory::withSubRange(m_bar1, fb_offset, fb_size);
	if (!mem)
		LogPrintf(1, "%s: Failed to create IODeviceMemory, aperture=%d\n", __FUNCTION__, kIOFBSystemAperture);
	return mem;
}

bool CLASS::isConsoleDevice()
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	return 0 != m_provider->getProperty("AAPL,boot-display");
}

IOReturn CLASS::setupForCurrentConfig()
{
	IOReturn r;

	LogPrintf(2, "%s: begins.\n", __FUNCTION__);
	r = super::setupForCurrentConfig();
	LogPrintf(2, "%s: ends return=%u.\n", __FUNCTION__, r);
	return r;
}

IOReturn CLASS::getAttribute(IOSelect attribute, uintptr_t* value)
{
	IOReturn r;
	char attr[5];

	if (attribute == kIOHardwareCursorAttribute) {
		if (value)
			*value = checkOptionFB(VMW_OPTION_FB_FIFO_INIT) ? 1 : 0;
		r = kIOReturnSuccess;
	} else
		r = super::getAttribute(attribute, value);
	if (logLevelFB >= 2) {
		IOSelectToString(attribute, &attr[0]);
		if (value)
			LogPrintf(2, "%s: attr=%s *value=0x%08lx ret=0x%08x\n", __FUNCTION__, &attr[0], *value, r);
		else
			LogPrintf(2, "%s: attr=%s ret=0x%08x\n", __FUNCTION__, &attr[0], r);
	}
	return r;
}

IOReturn CLASS::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t* value)
{
	IOReturn r;
	char attr[5];

	switch (attribute) {
		case kConnectionSupportsAppleSense:
		case kConnectionDisplayParameterCount:
		case kConnectionSupportsLLDDCSense:
		case kConnectionDisplayParameters:
		case kConnectionPower:
		case kConnectionPostWake:
			r = kIOReturnUnsupported;
			break;
		case kConnectionChanged:
			LogPrintf(2, "%s: kConnectionChanged value=%s\n", __FUNCTION__,
					  value ? "non-NULL" : "NULL");
			if (value)
				removeProperty("IOFBConfig");
			r = kIOReturnSuccess;
			break;
		case kConnectionEnable:
			LogPrintf(2, "%s: kConnectionEnable\n", __FUNCTION__);
			if (value)
				*value = 1;
			r = kIOReturnSuccess;
			break;
		case kConnectionFlags:
			LogPrintf(2, "%s: kConnectionFlags\n", __FUNCTION__);
			if (value)
				*value = 0;
			r = kIOReturnSuccess;
			break;
		case kConnectionSupportsHLDDCSense:
			r = have_edid ? kIOReturnSuccess : kIOReturnUnsupported;
			break;
		default:
			r = super::getAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	if (logLevelFB >= 2) {
		IOSelectToString(attribute, &attr[0]);
		if (value)
			LogPrintf(2, "%s: index=%d, attr=%s *value=0x%08lx ret=0x%08x\n", __FUNCTION__,
					  FMT_D(connectIndex), &attr[0], *value, r);
		else
			LogPrintf(2, "%s: index=%d, attr=%s ret=0x%08x\n", __FUNCTION__,
					  FMT_D(connectIndex), &attr[0], r);
	}
	return r;
}

IOReturn CLASS::setAttribute(IOSelect attribute, uintptr_t value)
{
	IOReturn r;
	char attr[5];

	r = super::setAttribute(attribute, value);
	if (logLevelFB >= 2) {
		IOSelectToString(attribute, &attr[0]);
		LogPrintf(2, "%s: attr=%s value=0x%08lx ret=0x%08x\n",
				  __FUNCTION__, &attr[0], value, r);
	}
	if (attribute == kIOCapturedAttribute &&
		!value &&
		m_custom_switch == 1 &&
		m_display_mode == CUSTOM_MODE_ID) {
		CustomSwitchStepSet(2);
	}
	return r;
}

IOReturn CLASS::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t value)
{
	IOReturn r;
	char attr[5];

	switch (attribute) {
		case kConnectionFlags:
			LogPrintf(2, "%s: kConnectionFlags %lu\n", __FUNCTION__, value);
			r = kIOReturnSuccess;
			break;
		case kConnectionProbe:
			LogPrintf(2, "%s: kConnectionProbe %lu\n", __FUNCTION__, value);
			r = kIOReturnSuccess;
			break;
		default:
			r = super::setAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	if (logLevelFB >= 2) {
		IOSelectToString(attribute, &attr[0]);
		LogPrintf(2, "%s: index=%d, attr=%s value=0x%08lx ret=0x%08x\n", __FUNCTION__,
				  FMT_D(connectIndex), &attr[0], value, r);
	}
	return r;
}

IOReturn CLASS::registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc, OSObject* target, void* ref, void** interruptRef)
{
	char int_type[5];

	if (logLevelFB >= 2) {
		IOSelectToString(interruptType, &int_type[0]);
		LogPrintf(2, "%s: interruptType=%s\n", __FUNCTION__, &int_type[0]);
	}
	if (interruptType != kIOFBConnectInterruptType)
		return kIOReturnUnsupported;
	bzero(&m_intr, sizeof m_intr);
	m_intr.target = target;
	m_intr.ref = ref;
	m_intr.proc = proc;
	m_intr_enabled = true;
	if (interruptRef)
		*interruptRef = &m_intr;
	return kIOReturnSuccess;
}

IOReturn CLASS::getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation* info)
{
	DisplayModeEntry const* dme;

	LogPrintf(2, "%s: mode ID=%d\n", __FUNCTION__, FMT_D(displayMode));
	if (!info)
		return kIOReturnBadArgument;
	dme = GetDisplayMode(displayMode);
	if (!dme) {
		LogPrintf(1, "%s: Display mode %d not found.\n", __FUNCTION__, FMT_D(displayMode));
		return kIOReturnBadArgument;
	}
	bzero(info, sizeof(IODisplayModeInformation));
	info->maxDepthIndex = 0;
	info->nominalWidth = dme->width;
	info->nominalHeight = dme->height;
	info->refreshRate = 60U << 16;
	info->flags = dme->flags;
	LogPrintf(2, "%s: mode ID=%d, max depth=%d, wxh=%ux%u, flags=0x%x\n", __FUNCTION__,
			  FMT_D(displayMode), 0, FMT_U(info->nominalWidth), FMT_U(info->nominalHeight), FMT_U(info->flags));
	return kIOReturnSuccess;
}

IOReturn CLASS::getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation* pixelInfo)
{
	DisplayModeEntry const* dme;

	LogPrintf(2, "%s: mode ID=%d\n", __FUNCTION__, FMT_D(displayMode));
	if (!pixelInfo)
		return kIOReturnBadArgument;
	if (aperture != kIOFBSystemAperture) {
		LogPrintf(1, "%s: aperture=%d not supported\n", __FUNCTION__, FMT_D(aperture));
		return kIOReturnUnsupportedMode;
	}
	if (depth) {
		LogPrintf(1, "%s: Depth mode %d not found.\n", __FUNCTION__, FMT_D(depth));
		return kIOReturnBadArgument;
	}
	dme = GetDisplayMode(displayMode);
	if (!dme) {
		LogPrintf(1, "%s: Display mode %d not found.\n", __FUNCTION__, FMT_D(displayMode));
		return kIOReturnBadArgument;
	}
	LogPrintf(2, "%s: mode ID=%d, wxh=%ux%u\n", __FUNCTION__,
			  FMT_D(displayMode), dme->width, dme->height);
	bzero(pixelInfo, sizeof(IOPixelInformation));
	pixelInfo->activeWidth = dme->width;
	pixelInfo->activeHeight = dme->height;
	pixelInfo->flags = dme->flags;
	strlcpy(&pixelInfo->pixelFormat[0], &pixelFormatStrings[0], sizeof(IOPixelEncoding));
	pixelInfo->pixelType = kIORGBDirectPixels;
	pixelInfo->componentMasks[0] = 0xFF0000U;
	pixelInfo->componentMasks[1] = 0x00FF00U;
	pixelInfo->componentMasks[2] = 0x0000FFU;
	pixelInfo->bitsPerPixel = 32;
	pixelInfo->componentCount = 3;
	pixelInfo->bitsPerComponent = 8;
	pixelInfo->bytesPerRow = ((pixelInfo->activeWidth + 7U) & (~7U)) << 2;
	LogPrintf(2, "%s: bytesPerRow=%u\n", __FUNCTION__, FMT_U(pixelInfo->bytesPerRow));
	return kIOReturnSuccess;
}

IOReturn CLASS::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	DisplayModeEntry const* dme;

	LogPrintf(2, "%s: display ID=%d, depth ID=%d\n", __FUNCTION__,
			  FMT_D(displayMode), FMT_D(depth));
	if (depth) {
		LogPrintf(1, "%s: Depth mode %d not found.\n", __FUNCTION__, FMT_D(depth));
		return kIOReturnBadArgument;
	}
	dme = GetDisplayMode(displayMode);
	if (!dme) {
		LogPrintf(1, "%s: Display mode %d not found.\n", __FUNCTION__, FMT_D(displayMode));
		return kIOReturnBadArgument;
	}
	if (!m_accel_updates)
		cancelRefreshTimer();	// Added
	IOLockLock(m_iolock);
	if (m_custom_switch == 1)
		bzero(reinterpret_cast<void*>(m_bar1_ptr + svga.getCurrentFBOffset()),
			  svga.getCurrentFBSize());
	svga.SetMode(dme->width, dme->height, 32);
	m_display_mode = displayMode;
	m_depth_mode = 0;
#if 0
	m_aperture_size = svga.getCurrentFBSize();
#endif
	if (checkOptionFB(VMW_OPTION_FB_REG_DUMP))	// Added
		svga.RegDump();						// Added
	IOLockUnlock(m_iolock);
	LogPrintf(2, "%s: display mode ID=%d, depth mode ID=%d\n", __FUNCTION__,
			  FMT_D(m_display_mode), FMT_D(m_depth_mode));
	if (!m_accel_updates)
		scheduleRefreshTimer(200 /* m_refresh_quantum_ms */);	// Added
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark DDC/EDID Injection Support Methods
#pragma mark -

IOReturn CLASS::getDDCBlock(IOIndex connectIndex, UInt32 blockNumber, IOSelect blockType, IOOptionBits options, UInt8* data, IOByteCount* length)
{
	if (connectIndex == 0 &&
		have_edid &&
		blockNumber == 1 &&
		blockType == kIODDCBlockTypeEDID &&
		data &&
		length &&
		*length >= sizeof edid) {
		memcpy(data, &edid[0], sizeof edid);
		*length = sizeof edid;
		return kIOReturnSuccess;
	}
	return super::getDDCBlock(connectIndex, blockNumber, blockType, options, data, length);
}

bool CLASS::hasDDCConnect(IOIndex connectIndex)
{
	if (connectIndex == 0 && have_edid)
		return true;
	return super::hasDDCConnect(connectIndex);
}

#pragma mark -
#pragma mark Public Methods
#pragma mark -

UInt CLASS::getApertureSize(IODisplayModeID displayMode, IOIndex depth)
{
	IOPixelInformation pixelInfo;
	LogPrintf(2, "%s: mode ID=%d, depth=%d\n", __FUNCTION__,
			  FMT_D(displayMode), FMT_D(depth));
	getPixelInformation(displayMode, depth, kIOFBSystemAperture, &pixelInfo);
	return pixelInfo.bytesPerRow * pixelInfo.activeHeight;
}

#pragma mark -
#pragma mark Custom Mode Methods
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::CustomSwitchStepWait(UInt value)
{
	LogPrintf(2, "%s: value=%u.\n", __FUNCTION__, value);
	while (m_custom_switch != value) {
		if (assert_wait(&m_custom_switch, THREAD_UNINT) != THREAD_WAITING)
			continue;
		if (m_custom_switch == value)
			thread_wakeup(&m_custom_switch);
		thread_block(0);
	}
	LogPrintf(2, "%s: done waiting.\n", __FUNCTION__);
}

__attribute__((visibility("hidden")))
void CLASS::CustomSwitchStepSet(UInt value)
{
	LogPrintf(2, "%s: value=%u.\n", __FUNCTION__, value);
	m_custom_switch = value;
	thread_wakeup(&m_custom_switch);
}

__attribute__((visibility("hidden")))
void CLASS::EmitConnectChangedEvent()
{
#if 0	/* VMwareGfx 3.1.1 */
	while (!m_intr.proc || !m_intr_enabled) {
		LogPrintf(2, "%s: Waiting for WindowServer.\n", __FUNCTION__);
		IOSleep(1000);
	}
#else
	if (!m_intr.proc || !m_intr_enabled)
		return;
#endif
	LogPrintf(2, "%s: Before call.\n", __FUNCTION__);
	m_intr.proc(m_intr.target, m_intr.ref);
	LogPrintf(2, "%s: After call.\n", __FUNCTION__);
}

__attribute__((visibility("hidden")))
void CLASS::RestoreAllModes()
{
	UInt i;
	IODisplayModeID t;
	DisplayModeEntry const* dme1;
	DisplayModeEntry const* dme2 = 0;

	if (m_custom_switch != 2)
		return;

	dme1 = GetDisplayMode(CUSTOM_MODE_ID);
	if (!dme1)
		return;
	for (i = 0; i < m_num_active_modes; ++i) {
		dme2 = GetDisplayMode(m_modes[i]);
		if (!dme2)
			continue;
		if (dme2->width != dme1->width || dme2->height != dme1->height)
			break;
	}
	if (i == m_num_active_modes)
		return;
	t = m_modes[0];
	m_modes[0] = m_modes[i];
	m_modes[i] = t;
	LogPrintf(2, "%s: Swapped mode IDs in slots 0 and %u.\n", __FUNCTION__, i);
	CustomSwitchStepSet(0);
	EmitConnectChangedEvent();
}

__attribute__((visibility("hidden")))
void CLASS::_RestoreAllModes(thread_call_param_t param0, thread_call_param_t param1)
{
	static_cast<CLASS*>(param0)->RestoreAllModes();
}

IOReturn CLASS::CustomMode(CustomModeData const* inData, CustomModeData* outData, size_t inSize, size_t* outSize)
{
	DisplayModeEntry const* dme1;
	DisplayModeEntry* dme2;
	UInt w, h;
	uint64_t deadline;

	if (!m_restore_call)
		return kIOReturnUnsupported;
	LogPrintf(2, "%s: inData=%p outData=%p inSize=%lu outSize=%lu.\n", __FUNCTION__,
			  inData, outData, inSize, outSize ? *outSize : 0UL);
	if (!inData) {
		LogPrintf(1, "%s: inData NULL.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (inSize < sizeof(CustomModeData)) {
		LogPrintf(1, "%s: inSize bad.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (!outData) {
		LogPrintf(1, "%s: outData NULL.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (!outSize || *outSize < sizeof(CustomModeData)) {
		LogPrintf(1, "%s: *outSize bad.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	dme1 = GetDisplayMode(m_display_mode);
	if (!dme1)
		return kIOReturnUnsupported;
	if (inData->flags & 1) {
		LogPrintf(3, "%s: Set resolution to %ux%u.\n", __FUNCTION__, inData->width, inData->height);
		w = inData->width;
		if (w < 800)
			w = 800;
		h = inData->height;
		if (h < 600)
			h = 600;
		if (w == dme1->width && h == dme1->height)
			goto finish_up;
		dme2 = &customMode;
		dme2->width = w;
		dme2->height = h;
		CustomSwitchStepSet(1);
		EmitConnectChangedEvent();
		CustomSwitchStepWait(2);	// TBD: this wait for the UserClient should be time-bounded
		LogPrintf(3, "%s: Scheduling RestoreAllModes().\n", __FUNCTION__);
		clock_interval_to_deadline(2000, kMillisecondScale, &deadline);
		thread_call_enter_delayed(m_restore_call, deadline);
	}
finish_up:
	dme1 = GetDisplayMode(m_display_mode);
	if (!dme1)
		return kIOReturnUnsupported;
	outData->flags = inData->flags;
	outData->width = dme1->width;
	outData->height = dme1->height;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Refresh Timer Methods
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::refreshTimerAction(IOTimerEventSource* sender)
{
	IOLockLock(m_iolock);
	svga.UpdateFullscreen();
	svga.RingDoorBell();
	IOLockUnlock(m_iolock);
	if (!m_accel_updates)
		scheduleRefreshTimer();
}

__attribute__((visibility("hidden")))
void CLASS::_RefreshTimerAction(thread_call_param_t param0, thread_call_param_t param1)
{
	static_cast<CLASS*>(param0)->refreshTimerAction(0);
}

__attribute__((visibility("hidden")))
void CLASS::scheduleRefreshTimer(UInt milliSeconds)
{
	uint64_t deadline;

	if (m_refresh_call) {
		clock_interval_to_deadline(milliSeconds, kMillisecondScale, &deadline);
		thread_call_enter_delayed(m_refresh_call, deadline);
	}
}

__attribute__((visibility("hidden")))
void CLASS::scheduleRefreshTimer()
{
	scheduleRefreshTimer(m_refresh_quantum_ms);
}

__attribute__((visibility("hidden")))
void CLASS::cancelRefreshTimer()
{
	if (m_refresh_call)
		thread_call_cancel(m_refresh_call);
}

__attribute__((visibility("hidden")))
void CLASS::setupRefreshTimer()
{
	m_refresh_call = thread_call_allocate(&_RefreshTimerAction, this);
}

__attribute__((visibility("hidden")))
void CLASS::deleteRefreshTimer()
{
	if (m_refresh_call) {
		thread_call_free(m_refresh_call);
		m_refresh_call = 0;
	}
}

#pragma mark -
#pragma mark IOService Methods
#pragma mark -

bool CLASS::start(IOService* provider)
{
	UInt boot_arg, max_w, max_h;
	OSString* o_name;	// Added
	OSData* o_edid;		// Added

	m_provider = OSDynamicCast(IOPCIDevice, provider);
	if (!m_provider)
		return false;
	if (!super::start(provider)) {
		LogPrintf(1, "%s: super::start failed.\n", __FUNCTION__);
		return false;
	}
	IOLog("IOFB: start\n");
	VMLog_SendString("log IOFB: start\n");
	if (PE_parse_boot_argn("vmw_log_fb", &boot_arg, sizeof boot_arg))
		logLevelFB = static_cast<int>(boot_arg);
	/*
	 * Begin Added
	 */
	setProperty("VMwareSVGAFBLogLevel", static_cast<UInt64>(logLevelFB), 32U);
	vmw_options_fb = VMW_OPTION_FB_FIFO_INIT | VMW_OPTION_FB_REFRESH_TIMER | VMW_OPTION_FB_ACCEL;
	if (PE_parse_boot_argn("vmw_options_fb", &boot_arg, sizeof boot_arg))
		vmw_options_fb = boot_arg;
	setProperty("VMwareSVGAFBOptions", static_cast<UInt64>(vmw_options_fb), 32U);
	m_refresh_quantum_ms = 50U;
	if (PE_parse_boot_argn("vmw_fps", &boot_arg, sizeof boot_arg) &&
		boot_arg > 0 && boot_arg <= 100U)
		m_refresh_quantum_ms = 1000U / boot_arg;
	setProperty("VMwareSVGARefreshQuantumMS", static_cast<UInt64>(m_refresh_quantum_ms), 32U);
	o_edid = OSDynamicCast(OSData, getProperty("EDID"));
	if (o_edid && o_edid->getLength() <= sizeof edid) {
		bzero(&edid, sizeof edid);
		memcpy(&edid, o_edid->getBytesNoCopy(), o_edid->getLength());
		have_edid = true;
	}
	o_name = OSDynamicCast(OSString, getProperty("IOHardwareModel"));
	if (o_name) {
		o_edid = OSData::withBytes(o_name->getCStringNoCopy(), o_name->getLength() + 1);
		if (o_edid) {
			provider->setProperty(gIODTModelKey, o_edid);
			o_edid->release();
		}
	}
	/*
	 * End Added
	 */
	m_bar1_map = 0;
	m_restore_call = 0;
	m_iolock = 0;
//	m_cursor_image = 0; /* VMwareGfx 3.1.1 */
	/*
	 * Begin Added
	 */
	m_bar1 = 0;
	m_refresh_call = 0;
	m_intr_enabled = false;
	m_accel_updates = false;
	/*
	 * End Added
	 */
	svga.Init();
	m_bar1 = svga.Start(m_provider);
	if (!m_bar1)
		goto fail;
	m_bar1->retain();
	/*
	 * Begin Added
	 */
	if (svga.getVRAMSize() < m_bar1->getLength()) {
		IODeviceMemory* mem = IODeviceMemory::withSubRange(m_bar1,
														   0,
														   svga.getVRAMSize());
		if (mem) {
			m_bar1->release();
			m_bar1 = mem;
		}
	}
	/*
	 * End Added
	 */
	m_bar1_map = m_bar1->createMappingInTask(kernel_task,
											 0,
											 kIOMapAnywhere);
	if (!m_bar1_map) {
		LogPrintf(1, "%s: Failed to map the VRAM.\n", __FUNCTION__);
		goto fail;
	}
	m_bar1_ptr = m_bar1_map->getVirtualAddress();
#ifdef TESTING
	SVGADevice::test_ram_size(getName(), m_bar1_ptr, m_bar1->getLength());
#endif
	/*
	 * Begin Added
	 */
	if (checkOptionFB(VMW_OPTION_FB_LINUX))
		svga.WriteReg(SVGA_REG_GUEST_ID, GUEST_OS_LINUX);
	if (checkOptionFB(VMW_OPTION_FB_REG_DUMP))
		svga.RegDump();
	memcpy(&customMode, &modeList[0], sizeof(DisplayModeEntry));
	/*
	 * End Added
	 */
	max_w = svga.getMaxWidth();
	max_h = svga.getMaxHeight();
	m_num_active_modes = 0;
	for (unsigned i = 0; i < NUM_DISPLAY_MODES; ++i)
		if (modeList[i].width <= max_w &&
			modeList[i].height <= max_h)
			m_modes[m_num_active_modes++] = i + 1;
	m_restore_call = thread_call_allocate(&_RestoreAllModes, this);
	if (!m_restore_call) {
		LogPrintf(1, "%s: Failed to allocate timer.\n", __FUNCTION__);
#if 0	// Note: not a critical error
		goto fail;
#endif
	}
	m_custom_switch = 0;
	if (checkOptionFB(VMW_OPTION_FB_FIFO_INIT)) {	// Added
		if (!svga.FIFOInit()) {
			LogPrintf(1, "%s: Failed FIFOInit.\n", __FUNCTION__);
			goto fail;
		}
		if (!svga.HasCapability(SVGA_CAP_TRACES) && checkOptionFB(VMW_OPTION_FB_REFRESH_TIMER))	// Added
			setupRefreshTimer();																// Added
	}
	m_iolock = IOLockAlloc();
	if (!m_iolock) {
		LogPrintf(1, "%s: Failed to allocate the FIFO mutex.\n", __FUNCTION__);
		goto fail;
	}
	m_display_mode = TryDetectCurrentDisplayMode(3);
	m_depth_mode = 0;
#if 0
	m_aperture_size = svga.getCurrentFBSize();
#endif
	scheduleRefreshTimer(1000U /* m_refresh_quantum_ms */);		// Added
	return true;

fail:
	Cleanup();
	super::stop(provider);
	return false;
}

void CLASS::stop(IOService* provider)
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	Cleanup();
	super::stop(provider);
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
/*
 * Note: this method of impersonating VMware's driver(s)
 *   doesn't work on OS 10.6, since Apple have made
 *   passiveMatch() private.  It's virtual only in
 *   the 32-bit kernel.
 *
 * There's an alternative way to impersonate by creating
 *   a fake meta-class for classnames VMwareIOFramebuffer/VMwareGfx
 *   but this involves too much messing around with
 *   IOKit internals.
 */
bool CLASS::passiveMatch(OSDictionary* matching, bool changesOK)
{
	OSString* str;
	if (!matching)
		goto done;
	str = OSDynamicCast(OSString, matching->getObject(gIOProviderClassKey));
	if (!str)
		goto done;
	if (str->isEqualTo("VMwareIOFramebuffer") || str->isEqualTo("VMwareGfx")) {
		LogPrintf(2, "%s: matched against VMwareIOFramebuffer/VMwareGfx\n", __FUNCTION__);
		str = OSString::withCString(getMetaClass()->getClassName());
		if (!str)
			goto done;
		matching->setObject(gIOProviderClassKey, str);
		str->release();
		goto done;
	}
done:
	return super::passiveMatch(matching, changesOK);
}
#endif

#pragma mark -
#pragma mark Accelerator Support Methods
#pragma mark -

void CLASS::lockDevice()
{
	IOLockLock(m_iolock);
}

void CLASS::unlockDevice()
{
	IOLockUnlock(m_iolock);
}

bool CLASS::supportsAccel()
{
	return checkOptionFB(VMW_OPTION_FB_FIFO_INIT) && checkOptionFB(VMW_OPTION_FB_ACCEL);
}

void CLASS::useAccelUpdates(bool state)
{
	if (state == m_accel_updates)
		return;
	m_accel_updates = state;
	if (state) {
		cancelRefreshTimer();
		IOLockLock(m_iolock);
		if (svga.HasCapability(SVGA_CAP_TRACES))
			svga.WriteReg(SVGA_REG_TRACES, 0);
		IOLockUnlock(m_iolock);
	} else {
		scheduleRefreshTimer(200);
		IOLockLock(m_iolock);
		if (svga.HasCapability(SVGA_CAP_TRACES)) {
			svga.WriteReg(SVGA_REG_TRACES, 1);
			svga.UpdateFullscreen();
			svga.RingDoorBell();
		}
		IOLockUnlock(m_iolock);
	}
	setProperty("VMwareSVGAAccelSynchronize", state);
	LogPrintf(2, "Accelerator Assisted Updates: %s\n", state ? "On" : "Off");
}
