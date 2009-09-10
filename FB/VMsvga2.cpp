/*
 *  VMsvga2.cpp
 *  VMsvga2
 *
 *  Created by Zenith432 on July 2nd 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
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
#include "VMsvga2.h"
#include "vmw_options_fb.h"

#include "svga_apple_header.h"
#include "svga_reg.h"
#include "svga_apple_footer.h"

#define CLASS VMsvga2
#define super IOFramebuffer
OSDefineMetaClassAndStructors(VMsvga2, IOFramebuffer);

#define LOGPRINTF_PREFIX_STR "log IOFB: "
#define LOGPRINTF_PREFIX_LEN (sizeof(LOGPRINTF_PREFIX_STR) - 1)
#define LOGPRINTF_PREFIX_SKIP 4				// past "log "
#define LOGPRINTF_BUF_SIZE 256

#define REFRESH_FENCE_THRESHOLD 10

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

static char const pixelFormatStrings[] = IO32BitDirectPixels "\0";

static UInt8 edid[128];

static bool have_edid = false;

UInt32 vmw_options_fb = 0;

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
	if (m_bar1_map) {
		m_bar1_map->release();
		m_bar1_map = 0;
		m_bar1_ptr = 0;
	}
	if (m_iolock) {
		IOLockFree(m_iolock);
		m_iolock = 0;
	}
}

#pragma mark -
#pragma mark IOFramebuffer Methods
#pragma mark -

UInt64 CLASS::getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	return 0ULL;
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

UInt32 CLASS::FindDepthMode(IOIndex depth)
{
	return depth ? 0 : 32;
}

void CLASS::IOSelectToString(IOSelect io_select, char* output)
{
	*output = static_cast<char>(io_select >> 24);
	output[1] = static_cast<char>(io_select >> 16);
	output[2] = static_cast<char>(io_select >> 8);
	output[3] = static_cast<char>(io_select);
	output[4] = '\0';
}

void CLASS::LogPrintf(VMFBIOLog log_level, char const* fmt, ...)
{
	va_list ap;
	char print_buf[LOGPRINTF_BUF_SIZE];

	if (log_level > m_log_level)
		return;
	va_start(ap, fmt);
	strlcpy(&print_buf[0], LOGPRINTF_PREFIX_STR, sizeof(print_buf));
	vsnprintf(&print_buf[LOGPRINTF_PREFIX_LEN], sizeof(print_buf) - LOGPRINTF_PREFIX_LEN, fmt, ap);
	va_end(ap);
	IOLog("%s", &print_buf[LOGPRINTF_PREFIX_SKIP]);
	if (!VMLog_SendString(&print_buf[0]))
		IOLog("%s: SendString failed.\n", __FUNCTION__);
}

#pragma mark -
#pragma mark Cursor Methods
#pragma mark -

IOReturn CLASS::setCursorState(SInt32 x, SInt32 y, bool visible)
{
	if (!checkOptionFB(VMW_OPTION_FB_FIFO_INIT))
		return kIOReturnUnsupported;
	LogPrintf(4, "%s: xy=%d %d visi=%d\n", __FUNCTION__, x, y, visible ? 1 : 0);
	x += m_hotspot_x;
	if (x < 0)
		x = 0;
	y += m_hotspot_y;
	if (y < 0)
		y = 0;
	IOLockLock(m_iolock);
	svga.setCursorState(
		static_cast<UInt32>(x),
		static_cast<UInt32>(y),
		visible);
	IOLockUnlock(m_iolock);
	return kIOReturnSuccess /* kIOReturnUnsupported */;
}

void CLASS::ConvertAlphaCursor(UInt32* cursor, UInt32 width, UInt32 height)
{
	/*
	 * Pre-multiply alpha cursor
	 */
	UInt32 i, pixel, alpha, r, g, b;
	UInt32 num_pixels = width * height;
#if 0
	LogPrintf(4, "%s: %ux%u pixels @ %p\n", __FUNCTION__, width, height, cursor);
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
	LogPrintf(4, "%s: cursor wxh=%dx%d @ %d\n", __FUNCTION__, 64, 64, 4);
	IOLockLock(m_iolock);
	harware_cursor = svga.BeginDefineAlphaCursor(64, 64, 4);
	m_hotspot_x = 0;
	m_hotspot_y = 0;
	memset(&curd, 0, sizeof(curd));
	curd.majorVersion = kHardwareCursorDescriptorMajorVersion;
	curd.minorVersion = kHardwareCursorDescriptorMinorVersion;
	curd.width = 64;
	curd.height = 64;
	curd.bitDepth = 32;
	curd.supportedSpecialEncodings = kInvertingEncodedPixel;
	curd.specialEncodings[kInvertingEncoding] = 0xFF000000U;
	memset(&curi, 0, sizeof(curi));
	curi.majorVersion = kHardwareCursorInfoMajorVersion;
	curi.minorVersion = kHardwareCursorInfoMinorVersion;
	curi.hardwareCursorData = static_cast<UInt8*>(harware_cursor);
#ifdef HAVE_CURSOR_HOTSPOT
	p_hotspots = reinterpret_cast<UInt16 const*>(&curi.hardwareCursorData + 1);
#else
	shmem = GetShmem(this);
	if (shmem) {
		if (shmem->version == kIOFBTenPtTwoShmemVersion) {
			UInt32 u = (cursorImage != 0 ? 1 : 0);
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
	LogPrintf(5, "%s: cursor %p: desc %dx%d @ %d\n", __FUNCTION__,
			  curi.hardwareCursorData, curd.width, curd.height, curd.bitDepth);
	if (!convertCursorImage(cursorImage, &curd, &curi)) {
		svga.FIFOCommit(0);
		IOLockUnlock(m_iolock);
		LogPrintf(1, "%s: convertCursorImage() failed %dx%d\n", __FUNCTION__,
				  curi.cursorWidth, curi.cursorHeight);
		return kIOReturnUnsupported;
	}
	LogPrintf(5, "%s: cursor %p: info %dx%d\n", __FUNCTION__,
			  curi.hardwareCursorData, curi.cursorWidth, curi.cursorHeight);
#ifdef HAVE_CURSOR_HOTSPOT
	LogPrintf(5, "%s: hotspots: %d vs %d (x), %d vs %d (y)\n", __FUNCTION__,			// Added
			  m_hotspot_x, static_cast<int>(*p_hotspots), m_hotspot_y, static_cast<int>(p_hotspots[1]));
	m_hotspot_x = static_cast<SInt32>(*p_hotspots);
	m_hotspot_y = static_cast<SInt32>(p_hotspots[1]);
#endif
	ConvertAlphaCursor(
		reinterpret_cast<UInt32*>(curi.hardwareCursorData),
		curi.cursorWidth,
		curi.cursorHeight);
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

IOReturn CLASS::setInterruptState(void* interruptRef, UInt32 state)
{
	LogPrintf(4, "%s\n", __FUNCTION__);
	if (interruptRef != &m_intr)
		return kIOReturnBadArgument;
	m_intr_enabled = (state != 0);
	return kIOReturnSuccess /* kIOReturnUnsupported */;
}

IOReturn CLASS::unregisterInterrupt(void* interruptRef)
{
	LogPrintf(4, "%s\n", __FUNCTION__);
	if (interruptRef != &m_intr)
		return kIOReturnBadArgument;
	memset(interruptRef, 0, sizeof(m_intr));
	m_intr_enabled = false;
	return kIOReturnSuccess;
}

IOItemCount CLASS::getConnectionCount()
{
	LogPrintf(4, "%s: %d\n", __FUNCTION__, 1);
	return 1;
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

DisplayModeEntry const* CLASS::FindDisplayMode(IODisplayModeID displayMode)
{
	if (displayMode == CUSTOM_MODE_ID)
		return &customMode;
	for (unsigned i = 0; i < NUM_DISPLAY_MODES; ++i)
		if (modeList[i].mode_id == displayMode)
			return &modeList[i];
	LogPrintf(1, "%s: bad mode ID=%d\n", __FUNCTION__, displayMode);
	return 0;
}

#pragma mark -
#pragma mark IOFramebuffer Methods
#pragma mark -

IOReturn CLASS::getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth)
{
	if (displayMode)
		*displayMode = m_display_mode;
	if (depth)
		*depth = m_depth_mode;
	LogPrintf(4, "%s: display mode ID=%d, depth mode ID=%d\n", __FUNCTION__, m_display_mode, m_depth_mode);
	return kIOReturnSuccess;
}

IOReturn CLASS::getDisplayModes(IODisplayModeID* allDisplayModes)
{
	LogPrintf(4, "%s: mode ID list\n", __FUNCTION__);
	if (!allDisplayModes)
		return kIOReturnBadArgument;
	if (m_custom_switch) {
		*allDisplayModes = CUSTOM_MODE_ID;
		return kIOReturnSuccess;
	}
	memcpy(allDisplayModes, &m_modes[0], sizeof(m_modes));
	return kIOReturnSuccess;
}

IOItemCount CLASS::getDisplayModeCount()
{
	IOItemCount r;
	r = m_custom_switch ? 1 : NUM_DISPLAY_MODES;
	LogPrintf(4, "%s: mode count=%d\n", __FUNCTION__, r);
	return r;
}

const char* CLASS::getPixelFormats()
{
	LogPrintf(4, "%s: pixel formats=%s\n", __FUNCTION__, &pixelFormatStrings[0]);
	return &pixelFormatStrings[0];
}

IODeviceMemory* CLASS::getVRAMRange()
{
	LogPrintf(4, "%s\n", __FUNCTION__);
	if (!m_bar1)
		return 0;
	m_bar1->retain();
	return m_bar1;
}

#pragma mark -
#pragma mark Public Methods
#pragma mark -

UInt32 CLASS::getApertureSize(IODisplayModeID displayMode, IOIndex depth)
{
	IOPixelInformation pixelInfo;
	LogPrintf(4, "%s: mode ID=%d, depth=%d\n", __FUNCTION__, displayMode, depth);
	getPixelInformation(displayMode, depth, kIOFBSystemAperture, &pixelInfo);
	return pixelInfo.bytesPerRow * pixelInfo.activeHeight;
}

#pragma mark -
#pragma mark IOFramebuffer Methods
#pragma mark -

IODeviceMemory* CLASS::getApertureRange(IOPixelAperture aperture)
{
	UInt32 fb_size;
	IODeviceMemory* mem;

	if (aperture != kIOFBSystemAperture) {
		LogPrintf(1, "%s: failed request for aperture=%d (%d)\n", __FUNCTION__, aperture, kIOFBSystemAperture);
		return 0;
	}
	if (!m_bar1)
		return 0;
	fb_size = getCurrentApertureSize() /* getApertureSize(m_display_mode, m_depth_mode) */;
	LogPrintf(4, "%s: aperture=%d, fb size=%d\n", __FUNCTION__, aperture, fb_size);
	mem = IODeviceMemory::withSubRange(m_bar1, m_fb_offset, fb_size);
	if (!mem)
		LogPrintf(1, "%s: failed to create IODeviceMemory, aperture=%d\n", __FUNCTION__, kIOFBSystemAperture);
	return mem;
}

bool CLASS::isConsoleDevice()
{
	LogPrintf(4, "%s\n", __FUNCTION__);
	return 0 != m_provider->getProperty("AAPL,boot-display");
}

#pragma mark -
#pragma mark Custom Mode Methods
#pragma mark -

void CLASS::CustomSwitchStepWait(UInt32 value)
{
	LogPrintf(4, "%s: value=%u.\n", __FUNCTION__, value);
	while (m_custom_switch != value) {
		if (assert_wait(&m_custom_switch, THREAD_UNINT) != THREAD_WAITING)
			continue;
		if (m_custom_switch == value)
			thread_wakeup(&m_custom_switch);
		thread_block(0);
	}
	LogPrintf(4, "%s: done waiting.\n", __FUNCTION__);
}

void CLASS::CustomSwitchStepSet(UInt32 value)
{
	LogPrintf(4, "%s: value=%u.\n", __FUNCTION__, value);
	m_custom_switch = value;
	thread_wakeup(&m_custom_switch);
}

void CLASS::EmitConnectChangedEvent()
{
	if (!m_intr.proc || !m_intr_enabled)
		return;
	LogPrintf(4, "%s: Before call.\n", __FUNCTION__);
	m_intr.proc(m_intr.target, m_intr.ref);
	LogPrintf(4, "%s: After call.\n", __FUNCTION__);
}

#pragma mark -
#pragma mark IOFramebuffer Methods
#pragma mark -

IOReturn CLASS::setupForCurrentConfig()
{
	IOReturn r;

	LogPrintf(4, "%s begins.\n", __FUNCTION__);
	r = super::setupForCurrentConfig();
	LogPrintf(4, "%s ends return=%u.\n", __FUNCTION__, r);
	return r;
}

#pragma mark -
#pragma mark Refresh Timer Methods
#pragma mark -

void CLASS::refreshTimerAction(IOTimerEventSource* sender)
{
	++m_refresh_fence_count;
	if (m_refresh_fence_count == REFRESH_FENCE_THRESHOLD)
		m_refresh_fence_count = 0;
	IOLockLock(m_iolock);
	svga.UpdateFullscreen();
	if (!m_refresh_fence_count)
		svga.RingDoorBell();
	IOLockUnlock(m_iolock);
	if (!m_accel_updates)
		scheduleRefreshTimer();
}

void CLASS::_RefreshTimerAction(thread_call_param_t param0, thread_call_param_t param1)
{
	static_cast<CLASS*>(param0)->refreshTimerAction(0);
}

void CLASS::scheduleRefreshTimer(UInt32 milliSeconds)
{
	UInt64 deadline;

	if (m_refresh_call) {
		clock_interval_to_deadline(milliSeconds, kMillisecondScale, &deadline);
		thread_call_enter_delayed(m_refresh_call, deadline);
	}
}

void CLASS::scheduleRefreshTimer()
{
	scheduleRefreshTimer(m_refresh_quantum_ms);
}

void CLASS::cancelRefreshTimer()
{
	if (m_refresh_call)
		thread_call_cancel(m_refresh_call);
}

void CLASS::setupRefreshTimer()
{
	m_refresh_fence_count = 0;
	m_refresh_call = thread_call_allocate(&_RefreshTimerAction, this);
}

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
	UInt32 boot_arg;
	UInt16 vendor_id, device_id, subvendor_id, subsystem_id;
	UInt8 revision_id;
	OSData* o_edid;		// Added

	m_provider = OSDynamicCast(IOPCIDevice, provider);
	if (!m_provider)
		return false;
	IOLog("IOFB: start\n");
	VMLog_SendString("log IOFB: start\n");
	m_log_level = 1;
	if (PE_parse_boot_arg("vmw_log_fb", &boot_arg))
		m_log_level = static_cast<VMFBIOLog>(boot_arg);
	/*
	 * Begin Added
	 */
	setProperty("VMwareSVGAFBLogLevel", static_cast<UInt64>(m_log_level), 32U);
	vmw_options_fb = VMW_OPTION_FB_FIFO_INIT | VMW_OPTION_FB_REFRESH_TIMER | VMW_OPTION_FB_ACCEL;
	if (PE_parse_boot_arg("vmw_options_fb", &boot_arg))
		vmw_options_fb = boot_arg;
	setProperty("VMwareSVGAFBOptions", static_cast<UInt64>(vmw_options_fb), 32U);
	m_refresh_quantum_ms = 50U;
	if (PE_parse_boot_arg("vmw_fps", &boot_arg) &&
		boot_arg > 0 && boot_arg <= 100U)
		m_refresh_quantum_ms = 1000U / boot_arg;
	setProperty("VMwareSVGARefreshQuantumMS", static_cast<UInt64>(m_refresh_quantum_ms), 32U);
	o_edid = OSDynamicCast(OSData, getProperty("EDID"));
	if (o_edid && o_edid->getLength() <= sizeof(edid)) {
		memset(&edid, 0, sizeof(edid));
		memcpy(&edid, o_edid->getBytesNoCopy(), o_edid->getLength());
		have_edid = true;
	}
	/*
	 * End Added
	 */
	m_bar1_map = 0;
	m_restore_call = 0;
	m_iolock = 0;
	/*
	 * Begin Added
	 */
	m_refresh_call = 0;
	m_intr_enabled = false;
	m_accel_updates = false;
	/*
	 * End Added
	 */
	if (m_provider->getFunctionNumber()) {
		LogPrintf(1, "%s: failed to get PCI function number\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	vendor_id = m_provider->configRead16(kIOPCIConfigVendorID);
	device_id = m_provider->configRead16(kIOPCIConfigDeviceID);
	subvendor_id = m_provider->configRead16(kIOPCIConfigSubSystemVendorID);
	subsystem_id = m_provider->configRead16(kIOPCIConfigSubSystemID);
	revision_id = m_provider->configRead8(kIOPCIConfigRevisionID);
	LogPrintf(3, "%s: PCI vendor&device: 0x%04x 0x%04x, Subsystem vendor: 0x%04x, Subsystem: 0x%04x, Revision: 0x%04x\n",
		__FUNCTION__, vendor_id, device_id, subvendor_id, subsystem_id, revision_id);
	m_provider->setMemoryEnable(true);
	m_bar1 = m_provider->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress1);
	if (!m_bar1) {
		LogPrintf(1, "%s: failed to get device map BAR1 registers\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_bar1_map = m_provider->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress1);
	if (!m_bar1_map) {
		LogPrintf(1, "%s: failed to get memory map BAR1 registers\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_fb_offset = 0;
	m_bar1_ptr = m_bar1_map->getVirtualAddress();
	if (!svga.Init(m_provider, m_log_level)) {
		Cleanup();
		return false;
	}
#ifdef TESTING
	SVGADevice::test_ram_size(getName(), m_bar1_ptr, m_bar1->getLength());
#endif
	svga.WriteReg(SVGA_REG_ID, SVGA_ID_2);
	if (svga.ReadReg(SVGA_REG_ID) != SVGA_ID_2) {
		LogPrintf(1, "%s: REG ID=0x%08x is wrong version\n", __FUNCTION__, SVGA_ID_2);
		Cleanup();
		return false;
	}
	LogPrintf(3, "%s: REG ID=0x%08x\n", __FUNCTION__, SVGA_ID_2);
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
	for (unsigned i = 0; i < NUM_DISPLAY_MODES; ++i)
		m_modes[i] = modeList[i].mode_id;
	m_restore_call = thread_call_allocate(&_RestoreAllModes, this);
	if (!m_restore_call) {
		LogPrintf(1, "%s: Failed to allocate timer.\n", __FUNCTION__);
#if 0	// Note: not a critical error
		Cleanup();
		return false;
#endif
	}
	m_custom_switch = 0;
	if (checkOptionFB(VMW_OPTION_FB_FIFO_INIT)) {	// Added
		if (!svga.FIFOInit()) {
			LogPrintf(1, "%s: failed FIFOInit()\n", __FUNCTION__);
			Cleanup();
			return false;
		}
		if (!svga.HasCapability(SVGA_CAP_TRACES) && checkOptionFB(VMW_OPTION_FB_REFRESH_TIMER))	// Added
			setupRefreshTimer();																// Added
	}
	m_iolock = IOLockAlloc();
	if (!m_iolock) {
		LogPrintf(1, "%s: failed fifoLock alloc\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_display_mode = TryDetectCurrentDisplayMode(3);
	m_depth_mode = 0;
	m_aperture_size = getCurrentApertureSize() /* getApertureSize(m_display_mode, m_depth_mode) */;
	if (!super::start(provider)) {
		LogPrintf(1, "%s: failed to start super\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	scheduleRefreshTimer(1000U /* m_refresh_quantum_ms */);		// Added
	return true;
}

void CLASS::stop(IOService* provider)
{
	LogPrintf(4, "%s\n", __FUNCTION__);
	Cleanup();
	super::stop(provider);
}

#pragma mark -
#pragma mark IOFramebuffer Methods
#pragma mark -

IOReturn CLASS::getAttribute(IOSelect attribute, UInt32* value)
{
	IOReturn r;
	char attr[5];

	if (attribute == kIOHardwareCursorAttribute) {
		if (value)
			*value = checkOptionFB(VMW_OPTION_FB_FIFO_INIT) ? 1 : 0;
		r = kIOReturnSuccess;
	} else
		r = super::getAttribute(attribute, value);
	if (m_log_level >= 4) {
		IOSelectToString(attribute, &attr[0]);
		if (value)
			LogPrintf(4, "%s: attr=%s *value=0x%08x ret=0x%08x\n", __FUNCTION__, &attr[0], *value, r);
		else
			LogPrintf(4, "%s: attr=%s ret=0x%08x\n", __FUNCTION__, &attr[0], r);
	}
	return r;
}

IOReturn CLASS::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, UInt32* value)
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
			LogPrintf(4, "%s: kConnectionChanged value=%s\n", __FUNCTION__,
					  value ? "non-NULL" : "NULL");
			if (value)
				removeProperty("IOFBConfig");
			r = kIOReturnSuccess;
			break;
		case kConnectionEnable:
			LogPrintf(4, "%s: kConnectionEnable\n", __FUNCTION__);
			if (value)
				*value = 1;
			r = kIOReturnSuccess;
			break;
		case kConnectionFlags:
			LogPrintf(4, "%s: kConnectionFlags\n", __FUNCTION__);
			r = kIOReturnSuccess;
			break;
		case kConnectionSupportsHLDDCSense:
			r = have_edid ? kIOReturnSuccess : kIOReturnUnsupported;
			break;
		default:
			r = super::getAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	if (m_log_level >= 4) {
		IOSelectToString(attribute, &attr[0]);
		if (value)
			LogPrintf(4, "%s: index=%d, attr=%s *value=0x%08x ret=0x%08x\n", __FUNCTION__,
					  connectIndex, &attr[0], *value, r);
		else
			LogPrintf(4, "%s: index=%d, attr=%s ret=0x%08x\n", __FUNCTION__,
					  connectIndex, &attr[0], r);
	}
	return r;
}

IOReturn CLASS::setAttribute(IOSelect attribute, UInt32 value)
{
	IOReturn r;
	char attr[5];

	r = super::setAttribute(attribute, value);
	if (m_log_level >= 4) {
		IOSelectToString(attribute, &attr[0]);
		LogPrintf(4, "%s: attr=%s value=0x%08x ret=0x%08x\n",
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

IOReturn CLASS::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute, UInt32 value)
{
	IOReturn r;
	char attr[5];

	switch (attribute) {
		case kConnectionFlags:
			LogPrintf(4, "%s: kConnectionFlags %d\n", __FUNCTION__, value);
			r = kIOReturnSuccess;
			break;
		case kConnectionProbe:
			LogPrintf(4, "%s: kConnectionProbe %d\n", __FUNCTION__, value);
			r = kIOReturnSuccess;
			break;
		default:
			r = super::setAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	if (m_log_level >= 4) {
		IOSelectToString(attribute, &attr[0]);
		LogPrintf(4, "%s: index=%d, attr=%s value=0x%08x ret=0x%08x\n", __FUNCTION__,
				  connectIndex, &attr[0], value, r);
	}
	return r;
}

IOReturn CLASS::registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc, OSObject* target, void* ref, void** interruptRef)
{
	char int_type[5];

	if (m_log_level >= 4) {
		IOSelectToString(interruptType, &int_type[0]);
		LogPrintf(4, "%s: interruptType=%s\n", __FUNCTION__, &int_type[0]);
	}
	if (interruptType != kIOFBConnectInterruptType)
		return kIOReturnUnsupported;
	memset(&m_intr, 0, sizeof(m_intr));
	m_intr.target = target;
	m_intr.ref = ref;
	m_intr.proc = proc;
	m_intr_enabled = true;
	if (interruptRef)
		*interruptRef = &m_intr;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Custom Mode Methods
#pragma mark -

void CLASS::RestoreAllModes()
{
	UInt32 i;
	IODisplayModeID t;
	DisplayModeEntry const* dme1;
	DisplayModeEntry const* dme2 = 0;

	if (m_custom_switch != 2)
		return;

	dme1 = FindDisplayMode(CUSTOM_MODE_ID);
	if (!dme1)
		return;
	for (i = 0; i < NUM_DISPLAY_MODES; ++i) {
		dme2 = FindDisplayMode(m_modes[i]);
		if (!dme2)
			continue;
		if (dme2->width != dme1->width || dme2->height != dme1->height)
			break;
	}
	if (i == NUM_DISPLAY_MODES)
		return;
	t = m_modes[0];
	m_modes[0] = m_modes[i];
	m_modes[i] = t;
	LogPrintf(4, "%s: Swapped mode IDs in slots 0 and %u.\n", __FUNCTION__, i);
	CustomSwitchStepSet(0);
	EmitConnectChangedEvent();
}

void CLASS::_RestoreAllModes(thread_call_param_t param0, thread_call_param_t param1)
{
	static_cast<CLASS*>(param0)->RestoreAllModes();
}

IOReturn CLASS::CustomMode(CustomModeData* inData, CustomModeData* outData, size_t inSize, size_t* outSize)
{
	DisplayModeEntry const* dme1;
	DisplayModeEntry* dme2;
	UInt32 w, h;
	UInt64 deadline;

	if (!m_restore_call)
		return kIOReturnUnsupported;
	LogPrintf(4, "%s: inData=%p outData=%p inSize=%u outSize=%u.\n", __FUNCTION__,
			  inData, outData, static_cast<unsigned>(inSize), outSize ? static_cast<unsigned>(*outSize) : 0);
	if (!inData) {
		LogPrintf(1, "%s: inData NULL\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (inSize != sizeof(CustomModeData)) {
		LogPrintf(1, "%s: inSize bad\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (!outData) {
		LogPrintf(1, "%s: outData NULL\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (!outSize || *outSize != sizeof(CustomModeData)) {
		LogPrintf(1, "%s: *outSize bad\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	dme1 = FindDisplayMode(m_display_mode);
	if (!dme1)
		return kIOReturnUnsupported;
	if (inData->flags & 1) {
		LogPrintf(4, "%s: Set resolution to %ux%u.\n", __FUNCTION__, inData->width, inData->height);
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
		LogPrintf(4, "%s: Scheduling RestoreAllModes().\n", __FUNCTION__);
		clock_interval_to_deadline(2000, kMillisecondScale, &deadline);
		thread_call_enter_delayed(m_restore_call, deadline);
	}
finish_up:
	dme1 = FindDisplayMode(m_display_mode);
	if (!dme1)
		return kIOReturnUnsupported;
	outData->flags = inData->flags;
	outData->width = dme1->width;
	outData->height = dme1->height;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark IOFramebuffer Methods
#pragma mark -

IOReturn CLASS::getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation* info)
{
	DisplayModeEntry const* dme;

	LogPrintf(4, "%s: mode ID=%d\n", __FUNCTION__, displayMode);
	if (!info)
		return kIOReturnBadArgument;
	dme = FindDisplayMode(displayMode);
	if (!dme) {
		LogPrintf(1, "%s: displayMode not found.  bad mode ID=%d\n", __FUNCTION__, displayMode);
		return kIOReturnBadArgument;
	}
	memset(info, 0, sizeof(IODisplayModeInformation));
	info->maxDepthIndex = 0;
	info->nominalWidth = dme->width;
	info->nominalHeight = dme->height;
	info->refreshRate = 60U << 16;
	info->flags = dme->flags;
	LogPrintf(4, "%s: mode ID=%d, max depth=%d, wxh=%dx%d, flags=0x%x\n", __FUNCTION__,
			  displayMode, 0, info->nominalWidth, info->nominalHeight, info->flags);
	return kIOReturnSuccess;
}

IOReturn CLASS::getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation* pixelInfo)
{
	DisplayModeEntry const* dme;

	LogPrintf(4, "%s: mode ID=%d\n", __FUNCTION__, displayMode);
	if (!pixelInfo)
		return kIOReturnBadArgument;
	if (aperture != kIOFBSystemAperture) {
		LogPrintf(1, "%s: aperture=%d not supported\n", __FUNCTION__, aperture);
		return kIOReturnUnsupportedMode;
	}
	if (depth) {
		LogPrintf(1, "%s: depth mode not found.  bad mode ID=%d\n", __FUNCTION__, depth);
		return kIOReturnBadArgument;
	}
	dme = FindDisplayMode(displayMode);
	if (!dme) {
		LogPrintf(1, "%s: displayMode not found.  bad mode ID=%d\n", __FUNCTION__, displayMode);
		return kIOReturnBadArgument;
	}
	LogPrintf(4, "%s: mode ID=%d, wxh=%ux%u\n", __FUNCTION__, displayMode, dme->width, dme->height);
	memset(pixelInfo, 0, sizeof(IOPixelInformation));
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
	LogPrintf(4, "%s: bytesPerRow=%d\n", __FUNCTION__, pixelInfo->bytesPerRow);
	return kIOReturnSuccess;
}

IOReturn CLASS::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	DisplayModeEntry const* dme;

	LogPrintf(4, "%s: display ID=%d, depth ID=%d\n", __FUNCTION__, displayMode, depth);
	if (depth) {
		LogPrintf(1, "%s: depth mode not found.  bad depth ID=%d\n", __FUNCTION__, depth);
		return kIOReturnBadArgument;
	}
	dme = FindDisplayMode(displayMode);
	if (!dme) {
		LogPrintf(1, "%s: displayMode not found.  bad mode ID=%d\n", __FUNCTION__, displayMode);
		return kIOReturnBadArgument;
	}
	if (!m_accel_updates)
		cancelRefreshTimer();	// Added
	IOLockLock(m_iolock);
	if (m_custom_switch == 1)
		memset(reinterpret_cast<void*>(m_bar1_ptr), 0, m_aperture_size);
	svga.SetMode(dme->width, dme->height, 32);
	m_display_mode = displayMode;
	m_depth_mode = 0;
	IOLockUnlock(m_iolock);
	m_aperture_size = getCurrentApertureSize() /* getApertureSize(m_display_mode, m_depth_mode) */;
	LogPrintf(4, "%s: display mode ID=%d, depth mode ID=%d\n", __FUNCTION__, m_display_mode, m_depth_mode);
	if (checkOptionFB(VMW_OPTION_FB_REG_DUMP))	// Added
		svga.RegDump();						// Added
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
		*length >= sizeof(edid)) {
		memcpy(data, &edid[0], sizeof(edid));
		*length = sizeof(edid);
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
#pragma mark Private Display Mode Related Methods
#pragma mark -

IODisplayModeID CLASS::TryDetectCurrentDisplayMode(IODisplayModeID defaultMode) const
{
	UInt32 w = svga.getCurrentWidth();
	UInt32 h = svga.getCurrentHeight();
	for (unsigned i = 0; i < NUM_DISPLAY_MODES; ++i)
		if (w == modeList[i].width && h == modeList[i].height)
			return modeList[i].mode_id;
	return defaultMode;
}

UInt32 CLASS::getCurrentApertureSize() const
{
	return svga.getCurrentPitch() * svga.getCurrentHeight();
}

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
		if (svga.HasCapability(SVGA_CAP_TRACES))
			svga.WriteReg(SVGA_REG_TRACES, 1);
		IOLockUnlock(m_iolock);
	}
	setProperty("VMwareSVGAAccelSynchronize", state);
}
