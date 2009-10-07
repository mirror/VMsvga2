/*
 *  VMsvga2Accel.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on July 29th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include <libkern/OSAtomic.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include "vmw_options_ac.h"
#include "VMsvga2Accel.h"
#include "VMsvga2Surface.h"
#include "VMsvga22DContext.h"
#include "VMsvga2GLContext.h"
#include "VMsvga2Allocator.h"
#include "VMsvga2.h"

#define UPDATES_COUNTER_THRESHOLD 10

#define CLASS VMsvga2Accel
#define super IOAccelerator
OSDefineMetaClassAndStructors(VMsvga2Accel, IOAccelerator);

UInt32 vmw_options_ac = 0;

#define VLOG_PREFIX_STR "log IOAC: "
#define VLOG_PREFIX_LEN (sizeof VLOG_PREFIX_STR - 1)
#define VLOG_BUF_SIZE 256

extern "C" char VMLog_SendString(char const* str);

#if LOGGING_LEVEL >= 1
#define ACLog(log_level, fmt, ...) do { if (log_level <= m_log_level_ac) VLog(fmt, ##__VA_ARGS__); } while(false)
#else
#define ACLog(log_level, fmt, ...)
#endif

#pragma mark -
#pragma mark Static Functions
#pragma mark -

static inline int log_2_32(UInt32 x)
{
	int y = 0;

	if (x & 0xFFFF0000U) { y |= 16; x >>= 16; }
	if (x & 0xFF00U) { y |= 8; x >>= 8; }
	if (x & 0xF0U) { y |= 4; x >>= 4; }
	if (x & 0xCU) { y |= 2; x >>= 2; }
	if (x & 0x2U) { y |= 1; x >>= 1; }
	if (!(x & 0x1U)) return -1;
	return y;
}

static inline int log_2_64(UInt64 x)
{
	int y = 0;

	if (x & 0xFFFFFFFF00000000ULL) { y |= 32; x >>= 32; }
	y |= log_2_32(static_cast<UInt32>(x));
	return y;
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

void CLASS::Cleanup()
{
	if (bHaveSVGA3D) {
		bHaveSVGA3D = false;
		svga3d.Init(0);
	}
#ifdef FB_NOTIFIER
	if (m_fbNotifier) {
		m_fbNotifier->remove();
		m_fbNotifier = 0;
	}
#endif
	if (m_allocator) {
		m_allocator->release();
		m_allocator = 0;
	}
	if (m_bar1) {
		m_bar1->release();
		m_bar1 = 0;
	}
	if (m_framebuffer) {
		m_svga = 0;
		m_framebuffer->release();
		m_framebuffer = 0;
	}
	if (m_iolock) {
		IOLockFree(m_iolock);
		m_iolock = 0;
	}
}

void CLASS::VLog(char const* fmt, ...)
{
	va_list ap;
	char print_buf[VLOG_BUF_SIZE];

	va_start(ap, fmt);
	strlcpy(&print_buf[0], VLOG_PREFIX_STR, sizeof print_buf);
	vsnprintf(&print_buf[VLOG_PREFIX_LEN], sizeof print_buf - VLOG_PREFIX_LEN, fmt, ap);
	va_end(ap);
	VMLog_SendString(&print_buf[0]);
}

#ifdef TIMING
void CLASS::timeSyncs()
{
	static uint32_t first_stamp = 0;
	static uint32_t num_syncs = 0;
	static uint32_t last_stamp;
	uint32_t current_stamp;
	uint32_t dummy;
	float freq;

	if (!first_stamp) {
		clock_get_system_microtime(&first_stamp, &dummy);
		last_stamp = first_stamp;
		return;
	}
	++num_syncs;
	clock_get_system_microtime(&current_stamp, &dummy);
	if (current_stamp >= last_stamp + 60) {
		last_stamp = current_stamp;
		freq = static_cast<float>(num_syncs)/static_cast<float>(current_stamp - first_stamp);
		if (m_framebuffer)
			m_framebuffer->setProperty("VMwareSVGASyncFrequency", static_cast<UInt64>(freq * 1000.0F), 64U);
	}
}
#endif

bool CLASS::createMasterSurface(UInt32 width, UInt32 height)
{
	UInt32 cid;

	m_master_surface_id = AllocSurfaceID();
	if (createSurface(m_master_surface_id, SVGA3dSurfaceFlags(0), SVGA3D_X8R8G8B8, width, height) != kIOReturnSuccess) {
		FreeSurfaceID(m_master_surface_id);
		return false;
	}
	cid = AllocContextID();
	if (createContext(cid) != kIOReturnSuccess) {
		FreeContextID(cid);
		destroySurface(m_master_surface_id);
		FreeSurfaceID(m_master_surface_id);
		return false;
	}
	setupRenderContext(cid, m_master_surface_id);
	clearContext(cid, static_cast<SVGA3dClearFlag>(SVGA3D_CLEAR_COLOR), 0, 0, width, height, 0, 1.0F, 0);
	destroyContext(cid);
	FreeContextID(cid);
	return true;
}

void CLASS::destroyMasterSurface()
{
	destroySurface(m_master_surface_id);
	FreeSurfaceID(m_master_surface_id);
}

void CLASS::processOptions()
{
	UInt32 boot_arg;

	vmw_options_ac = VMW_OPTION_AC_2D_CONTEXT | VMW_OPTION_AC_SURFACE_CONNECT;
	if (PE_parse_boot_argn("vmw_options_ac", &boot_arg, sizeof boot_arg))
		vmw_options_ac = boot_arg;
	if (PE_parse_boot_argn("-svga3d", &boot_arg, sizeof boot_arg))
		vmw_options_ac |= VMW_OPTION_AC_SVGA3D;
	if (PE_parse_boot_argn("-vmw_no_yuv", &boot_arg, sizeof boot_arg))
		vmw_options_ac |= VMW_OPTION_AC_NO_YUV;
	if (PE_parse_boot_argn("-vmw_direct_blit", &boot_arg, sizeof boot_arg))
		vmw_options_ac |= VMW_OPTION_AC_DIRECT_BLIT;
	setProperty("VMwareSVGAAccelOptions", static_cast<UInt64>(vmw_options_ac), 32U);
	if (PE_parse_boot_argn("vmw_options_ga", &boot_arg, sizeof boot_arg)) {
		m_options_ga = boot_arg;
		setProperty("VMwareSVGAGAOptions", static_cast<UInt64>(m_options_ga), 32U);
	}
	if (PE_parse_boot_argn("vmw_log_ac", &boot_arg, sizeof boot_arg))
		m_log_level_ac = static_cast<SInt32>(boot_arg);
	setProperty("VMwareSVGAAccelLogLevel", static_cast<UInt64>(m_log_level_ac), 32U);
	if (PE_parse_boot_argn("vmw_log_ga", &boot_arg, sizeof boot_arg)) {
		m_log_level_ga = static_cast<SInt32>(boot_arg);
		setProperty("VMwareSVGAGALogLevel", static_cast<UInt64>(m_log_level_ga), 32U);
	}
}

IOReturn CLASS::findFramebuffer()
{
	OSIterator* it;
	OSObject* obj;
	VMsvga2* fb = 0;

	if (!m_provider)
		return kIOReturnNotReady;
	it = m_provider->getClientIterator();
	if (!it)
		return kIOReturnNotFound;
	while ((obj = it->getNextObject()) != 0) {
		fb = OSDynamicCast(VMsvga2, obj);
		if (!fb)
			continue;
		if (!fb->supportsAccel()) {
			fb = 0;
			continue;
		}
		break;
	}
	if (!fb) {
		it->release();
		return kIOReturnNotFound;
	}
	fb->retain();
	m_framebuffer = fb;
	it->release();
	return kIOReturnSuccess;
}

IOReturn CLASS::setupAllocator()
{
	IOReturn rc;
	IOByteCount bytes_reserve, s;
	void* p;

	if (m_bar1)
		return kIOReturnSuccess;
	if (!m_framebuffer)
		return kIOReturnNotReady;
	m_bar1 = m_framebuffer->getVRAMRange();
	if (!m_bar1)
		return kIOReturnInternalError;
	p = reinterpret_cast<void*>(m_framebuffer->getVRAMPtr());
	s = m_bar1->getLength();

	rc = m_allocator->Init(p, s);
	if (rc != kIOReturnSuccess) {
		ACLog(1, "%s: Allocator Init(%p, 0x%lx) failed with 0x%x\n", __FUNCTION__, p, static_cast<size_t>(s), rc);
		m_bar1->release();
		m_bar1 = 0;
		return rc;
	}
	bytes_reserve = bHaveSVGA3D ? 0 : SVGA_FB_MAX_TRACEABLE_SIZE;
	rc = m_allocator->Release(bytes_reserve, s);
	if (rc != kIOReturnSuccess) {
		ACLog(1, "%s: Allocator Release(0x%lx, 0x%lx) failed with 0x%x\n", __FUNCTION__, static_cast<size_t>(bytes_reserve), static_cast<size_t>(s), rc);
		m_bar1->release();
		m_bar1 = 0;
	}
	return rc;
}

#ifdef FB_NOTIFIER
IOReturn CLASS::fbNotificationHandler(void* ref, class IOFramebuffer* framebuffer, SInt32 event, void* info)
{
	ACLog(3, "%s: ref == %p, framebuffer == %p, event == %u, info == %p\n", __FUNCTION__, ref, framebuffer, event, info);
	return kIOReturnSuccess;
}
#endif

#pragma mark -
#pragma mark Methods from IOService
#pragma mark -

bool CLASS::init(OSDictionary* dictionary)
{
	if (!super::init(dictionary))
		return false;
	svga3d.Init(0);
	m_log_level_ac = LOGGING_LEVEL;
	m_log_level_ga = -1;
	m_blitbug_result = kIOReturnNotFound;
	m_present_tracker.init();
	return true;
}

bool CLASS::start(IOService* provider)
{
	char pathbuf[256];
	int len;
	OSObject* plug;

	m_provider = OSDynamicCast(IOPCIDevice, provider);
	if (!m_provider)
		return false;
	if (!super::start(provider))
		return false;
	IOLog("IOAC: start\n");
	VMLog_SendString("log IOAC: start\n");
	processOptions();
	/*
	 * TBD: is there a possible race condition here where VMsvga2Accel::start
	 *   is called before VMsvga2 gets attached to its provider?
	 */
	if (findFramebuffer() != kIOReturnSuccess) {
		ACLog(1, "Unable to locate suitable framebuffer\n");
		stop(provider);
		return false;
	}
	m_iolock = IOLockAlloc();
	if (!m_iolock) {
		ACLog(1, "Unable to allocate IOLock\n");
		stop(provider);
		return false;
	}
	m_allocator = VMsvga2Allocator::factory();
	if (!m_allocator) {
		ACLog(1, "Unable to create Allocator\n");
		stop(provider);
		return false;
	}
	m_svga = m_framebuffer->getDevice();
	if (checkOptionAC(VMW_OPTION_AC_SVGA3D) && svga3d.Init(m_svga)) {
		bHaveSVGA3D = true;
		ACLog(1, "SVGA3D On\n");
	}
	if (checkOptionAC(VMW_OPTION_AC_NO_YUV))
		ACLog(1, "YUV Off\n");
	plug = getProperty(kIOCFPlugInTypesKey);
	if (plug)
		m_framebuffer->setProperty(kIOCFPlugInTypesKey, plug);
	len = sizeof pathbuf;
	if (getPath(&pathbuf[0], &len, gIOServicePlane)) {
		m_framebuffer->setProperty(kIOAccelTypesKey, pathbuf);
		m_framebuffer->setProperty(kIOAccelIndexKey, 0ULL, 32U);
		m_framebuffer->setProperty(kIOAccelRevisionKey, static_cast<UInt64>(kCurrentGraphicsInterfaceRevision), 32U);
	}
#ifdef FB_NOTIFIER
	m_fbNotifier = m_framebuffer->addFramebufferNotification(OSMemberFunctionCast(IOFramebufferNotificationHandler, this, &CLASS::fbNotificationHandler), this, 0);
	if (!m_fbNotifier)
		ACLog(1, "Unable to register framebuffer notification handler\n");
#endif
	return true;
}

void CLASS::stop(IOService* provider)
{
	ACLog(2, "%s\n", __FUNCTION__);

	Cleanup();
	super::stop(provider);
}

IOReturn CLASS::newUserClient(task_t owningTask, void* securityID, UInt32 type, IOUserClient ** handler)
{
	IOUserClient* client;

	/*
	 * Client Types
	 * 0 - Surface
	 * 1 - GL Context
	 * 2 - 2D Context
	 * 3 - DVD Context
	 * 4 - DVD Context
	 */
	ACLog(2, "%s: owningTask==%p, securityID==%p, type==%u\n", __FUNCTION__, owningTask, securityID, type);
	if (!handler)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	switch (type) {
		case kIOAccelSurfaceClientType:
			if (!checkOptionAC(VMW_OPTION_AC_SURFACE_CONNECT))
				return kIOReturnUnsupported;
			if (setupAllocator() != kIOReturnSuccess)
				return kIOReturnNoMemory;
			client = VMsvga2Surface::withTask(owningTask, securityID, type);
			break;
		case 1:
			if (!checkOptionAC(VMW_OPTION_AC_GL_CONTEXT))
				return kIOReturnUnsupported;
			client = VMsvga2GLContext::withTask(owningTask, securityID, type);
			break;
		case 2:
			if (!checkOptionAC(VMW_OPTION_AC_2D_CONTEXT))
				return kIOReturnUnsupported;
			client = VMsvga22DContext::withTask(owningTask, securityID, type);
			break;
		case 3:
			if (!checkOptionAC(VMW_OPTION_AC_DVD_CONTEXT))
				return kIOReturnUnsupported;
		default:
			return kIOReturnUnsupported;
	}
	if (!client)
		return kIOReturnNoResources;
	if (!client->attach(this)) {
		client->release();
		return kIOReturnInternalError;
	}
	if (!client->start(this)) {
		client->detach(this);
		client->release();
		return kIOReturnInternalError;
	}
	*handler = client;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark SVGA FIFO Sync Methods
#pragma mark -

IOReturn CLASS::SyncFIFO()
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->SyncFIFO();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::RingDoorBell()
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->RingDoorBell();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::SyncToFence(UInt32 fence)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->SyncToFence(fence);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark SVGA FIFO Acceleration Methods for 2D Context
#pragma mark -

IOReturn CLASS::useAccelUpdates(uintptr_t state)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->useAccelUpdates(state != 0);
	return kIOReturnSuccess;
}

/*
 * Note: RectCopy, RectFill, UpdateFramebuffer* and CopyRegion don't work in SVGA3D
 *   mode.  This is ok, since the WindowServer doesn't use them.
 *   UpdateFramebuffer* are called from IOFBSynchronize in GA.  The other functions
 *   are called from the various blitters.  In SVGA3D mode the WindowServer doesn't
 *   use IOFBSynchronize (it uses surface_flush instead), and CopyRegion blits
 *   are done to a surface destination.
 */
IOReturn CLASS::RectCopy(struct IOBlitCopyRectangleStruct const* copyRects, size_t copyRectsSize)
{
	size_t i, count = copyRectsSize / sizeof(IOBlitCopyRectangle);
	bool rc;

	if (!count || !copyRects)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	for (i = 0; i < count; ++i) {
		rc = m_svga->RectCopy(reinterpret_cast<UInt32 const*>(&copyRects[i]));
		if (!rc)
			break;
	}
	m_framebuffer->unlockDevice();
	return rc ? kIOReturnSuccess : kIOReturnNoMemory;
}

IOReturn CLASS::RectFill(uintptr_t color, struct IOBlitRectangleStruct const* rects, size_t rectsSize)
{
	size_t i, count = rectsSize / sizeof(IOBlitRectangle);
	bool rc;

	if (!count || !rects)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	for (i = 0; i < count; ++i) {
		rc = m_svga->RectFill(static_cast<UInt32>(color), reinterpret_cast<UInt32 const*>(&rects[i]));
		if (!rc)
			break;
	}
	m_framebuffer->unlockDevice();
	return rc ? kIOReturnSuccess : kIOReturnNoMemory;
}

IOReturn CLASS::UpdateFramebuffer(UInt32 const* rect)
{
	if (!rect)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->UpdateFramebuffer2(rect);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::UpdateFramebufferAutoRing(UInt32 const* rect)
{
	if (!rect)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	++m_updates_counter;
	if (m_updates_counter == UPDATES_COUNTER_THRESHOLD)
		m_updates_counter = 0;
	m_framebuffer->lockDevice();
	m_svga->UpdateFramebuffer2(rect);
	if (!m_updates_counter)
		m_svga->RingDoorBell();
	m_framebuffer->unlockDevice();
#ifdef TIMING
	timeSyncs();
#endif
	return kIOReturnSuccess;
}

IOReturn CLASS::CopyRegion(intptr_t destX, intptr_t destY, void /* IOAccelDeviceRegion */ const* region, size_t regionSize)
{
	IOAccelDeviceRegion const* rgn = static_cast<IOAccelDeviceRegion const*>(region);
	IOAccelBounds const* rect;
	SInt32 deltaX, deltaY;
	UInt32 i, copyRect[6];
	bool rc;

	if (!rgn || regionSize < IOACCEL_SIZEOF_DEVICE_REGION(rgn))
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	rect = &rgn->bounds;
	if (checkOptionAC(VMW_OPTION_AC_REGION_BOUNDS_COPY)) {
		copyRect[0] = rect->x;
		copyRect[1] = rect->y;
		copyRect[2] = static_cast<UInt32>(destX);
		copyRect[3] = static_cast<UInt32>(destY);
		copyRect[4] = rect->w;
		copyRect[5] = rect->h;
		rc = m_svga->RectCopy(&copyRect[0]);
	} else {
		deltaX = static_cast<SInt32>(destX) - rect->x;
		deltaY = static_cast<SInt32>(destY) - rect->y;
		for (i = 0; i < rgn->num_rects; ++i) {
			rect = &rgn->rect[i];
			copyRect[0] = rect->x;
			copyRect[1] = rect->y;
			copyRect[2] = rect->x + deltaX;
			copyRect[3] = rect->y + deltaY;
			copyRect[4] = rect->w;
			copyRect[5] = rect->h;
			rc = m_svga->RectCopy(&copyRect[0]);
			if (!rc)
				break;
		}
	}
	m_framebuffer->unlockDevice();
	return rc ? kIOReturnSuccess : kIOReturnNoMemory;
}

#pragma mark -
#pragma mark Acceleration Methods for Surfaces
#pragma mark -

IOReturn CLASS::createSurface(UInt32 sid, SVGA3dSurfaceFlags surfaceFlags, SVGA3dSurfaceFormat surfaceFormat, UInt32 width, UInt32 height)
{
	bool rc;
	SVGA3dSize* mipSizes;
	SVGA3dSurfaceFace* faces;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	rc = svga3d.BeginDefineSurface(sid, surfaceFlags, surfaceFormat, &faces, &mipSizes, 1U);
	if (!rc)
		goto exit;
	faces[0].numMipLevels = 1;
	mipSizes[0].width = width;
	mipSizes[0].height = height;
	mipSizes[0].depth = 1;
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::destroySurface(UInt32 sid)
{
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	svga3d.DestroySurface(sid);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::surfaceDMA2D(UInt32 sid, SVGA3dTransferType transfer, void /* IOAccelDeviceRegion */ const* region, ExtraInfo const* extra, UInt32* fence)
{
	bool rc;
	UInt32 i, numCopyBoxes;
	SVGA3dCopyBox* copyBoxes;
	SVGA3dGuestImage guestImage;
	SVGA3dSurfaceImageId hostImage;
	IOAccelDeviceRegion const* rgn;

	if (!extra)
		return kIOReturnBadArgument;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numCopyBoxes = rgn ? rgn->num_rects : 0;
	hostImage.sid = sid;
	hostImage.face = 0;
	hostImage.mipmap = 0;
	guestImage.ptr.gmrId = static_cast<UInt32>(-2) /* SVGA_GMR_FRAMEBUFFER */;
	guestImage.ptr.offset = static_cast<UInt32>(extra->mem_offset_in_bar1);
	guestImage.pitch = static_cast<UInt32>(extra->mem_pitch);
	m_framebuffer->lockDevice();
	rc = svga3d.BeginSurfaceDMA(&guestImage, &hostImage, transfer, &copyBoxes, numCopyBoxes);
	if (!rc)
		goto exit;
	for (i = 0; i < numCopyBoxes; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dCopyBox* dst = &copyBoxes[i];
		dst->srcx = src->x + extra->srcDeltaX;
		dst->srcy = src->y + extra->srcDeltaY;
		dst->x = src->x + extra->dstDeltaX;
		dst->y = src->y + extra->dstDeltaY;
		dst->w = src->w;
		dst->h = src->h;
		dst->d = 1;
	}
	m_svga->FIFOCommitAll();
	if (fence)
		*fence = m_svga->InsertFence();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::surfaceCopy(UInt32 src_sid, UInt32 dst_sid, void /* IOAccelDeviceRegion */ const* region, ExtraInfo const* extra)
{
	bool rc;
	UInt32 i, numCopyBoxes;
	SVGA3dCopyBox* copyBoxes;
	SVGA3dSurfaceImageId srcImage, dstImage;
	IOAccelDeviceRegion const* rgn;

	if (!extra)
		return kIOReturnBadArgument;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numCopyBoxes = rgn ? rgn->num_rects : 0;
	bzero(&srcImage, sizeof srcImage);
	bzero(&dstImage, sizeof dstImage);
	srcImage.sid = src_sid;
	dstImage.sid = dst_sid;
	m_framebuffer->lockDevice();
	rc = svga3d.BeginSurfaceCopy(&srcImage, &dstImage, &copyBoxes, numCopyBoxes);
	if (!rc)
		goto exit;
	for (i = 0; i < numCopyBoxes; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dCopyBox* dst = &copyBoxes[i];
		dst->srcx = src->x + extra->srcDeltaX;
		dst->srcy = src->y + extra->srcDeltaY;
		dst->x = src->x + extra->dstDeltaX;
		dst->y = src->y + extra->dstDeltaY;
		dst->w = src->w;
		dst->h = src->h;
		dst->d = 1;
	}
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::surfaceStretch(UInt32 src_sid, UInt32 dst_sid, SVGA3dStretchBltMode mode, void /* IOAccelBounds */ const* src_rect, void /* IOAccelBounds */ const* dest_rect)
{
	SVGA3dBox srcBox, dstBox;
	SVGA3dSurfaceImageId srcImage, dstImage;
	IOAccelBounds const* s_rect;
	IOAccelBounds const* d_rect;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	s_rect = static_cast<IOAccelBounds const*>(src_rect);
	d_rect = static_cast<IOAccelBounds const*>(dest_rect);
	bzero(&srcImage, sizeof srcImage);
	bzero(&dstImage, sizeof dstImage);
	srcImage.sid = src_sid;
	dstImage.sid = dst_sid;
	srcBox.x = static_cast<UInt32>(s_rect->x);
	srcBox.y = static_cast<UInt32>(s_rect->y);
	srcBox.z = 0;
	srcBox.w = static_cast<UInt32>(s_rect->w);
	srcBox.h = static_cast<UInt32>(s_rect->h);
	srcBox.d = 1;
	dstBox.x = static_cast<UInt32>(d_rect->x);
	dstBox.y = static_cast<UInt32>(d_rect->y);
	dstBox.z = 0;
	dstBox.w = static_cast<UInt32>(d_rect->w);
	dstBox.h = static_cast<UInt32>(d_rect->h);
	dstBox.d = 1;
	m_framebuffer->lockDevice();
	svga3d.SurfaceStretchBlt(&srcImage, &dstImage, &srcBox, &dstBox, mode);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::surfacePresentAutoSync(UInt32 sid, void /* IOAccelDeviceRegion */ const* region, ExtraInfo const* extra)
{
	bool rc;
	UInt32 i, numCopyRects;
	SVGA3dCopyRect* copyRects;
	IOAccelDeviceRegion const* rgn;

	if (!extra)
		return kIOReturnBadArgument;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numCopyRects = rgn ? rgn->num_rects : 0;
	m_framebuffer->lockDevice();
	m_svga->SyncToFence(m_present_tracker.before());
	rc = svga3d.BeginPresent(sid, &copyRects, numCopyRects);
	if (!rc)
		goto exit;
	for (i = 0; i < numCopyRects; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dCopyRect* dst = &copyRects[i];
		dst->srcx = src->x + extra->srcDeltaX;
		dst->srcy = src->y + extra->srcDeltaY;
		dst->x = src->x + extra->dstDeltaX;
		dst->y = src->y + extra->dstDeltaY;
		dst->w = src->w;
		dst->h = src->h;
	}
	m_svga->FIFOCommitAll();
	m_present_tracker.after(m_svga->InsertFence());
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::surfacePresentReadback(void /* IOAccelDeviceRegion */ const* region)
{
	bool rc;
	UInt32 i, numRects;
	SVGA3dRect* rects;
	IOAccelDeviceRegion const* rgn;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numRects = rgn ? rgn->num_rects : 0;
	m_framebuffer->lockDevice();
	rc = svga3d.BeginPresentReadback(&rects, numRects);
	if (!rc)
		goto exit;
	for (i = 0; i < numRects; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dRect* dst = &rects[i];
		dst->x = src->x;
		dst->y = src->y;
		dst->w = src->w;
		dst->h = src->h;
	}
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

#if 0
IOReturn CLASS::setupRenderContext(UInt32 cid, UInt32 color_sid, UInt32 depth_sid, UInt32 width, UInt32 height)
{
	SVGA3dRenderState* rs;
	SVGA3dSurfaceImageId colorImage;
	SVGA3dSurfaceImageId depthImage;
	SVGA3dRect rect;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	bzero(&colorImage, sizeof(SVGA3dSurfaceImageId));
	bzero(&depthImage, sizeof(SVGA3dSurfaceImageId));
	bzero(&rect, sizeof(SVGA3dRect));
	colorImage.sid = color_sid;
	depthImage.sid = depth_sid;
	rect.w = width;
	rect.h = height;
	m_framebuffer->lockDevice();
	svga3d.SetRenderTarget(cid, SVGA3D_RT_COLOR0, &colorImage);
	svga3d.SetRenderTarget(cid, SVGA3D_RT_DEPTH, &depthImage);
	svga3d.SetViewport(cid, &rect);
	svga3d.SetZRange(cid, 0.0F, 1.0F);
	if (svga3d.BeginSetRenderState(cid, &rs, 1)) {
		rs->state = SVGA3D_RS_SHADEMODE;
		rs->uintValue = SVGA3D_SHADEMODE_SMOOTH;
		m_svga->FIFOCommitAll();
	}
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}
#else
IOReturn CLASS::setupRenderContext(UInt32 cid, UInt32 color_sid)
{
	SVGA3dSurfaceImageId colorImage;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	bzero(&colorImage, sizeof(SVGA3dSurfaceImageId));
	colorImage.sid = color_sid;
	m_framebuffer->lockDevice();
	svga3d.SetRenderTarget(cid, SVGA3D_RT_COLOR0, &colorImage);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}
#endif

IOReturn CLASS::clearContext(UInt32 cid, SVGA3dClearFlag flags, UInt32 x, UInt32 y, UInt32 width, UInt32 height, UInt32 color, float depth, UInt32 stencil)
{
	bool rc;
	SVGA3dRect* rect;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	rc = svga3d.BeginClear(cid, flags, color, depth, stencil, &rect, 1);
	if (!rc)
		goto exit;
	rect->x = x;
	rect->y = y;
	rect->w = width;
	rect->h = height;
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::createContext(UInt32 cid)
{
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	svga3d.DefineContext(cid);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::destroyContext(UInt32 cid)
{
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	svga3d.DestroyContext(cid);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Misc Methods
#pragma mark -

IOReturn CLASS::getScreenInfo(IOAccelSurfaceReadData* info)
{
	if (!info)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	bzero(info, sizeof(IOAccelSurfaceReadData));
	m_framebuffer->lockDevice();
	info->w = m_svga->getCurrentWidth();
	info->h = m_svga->getCurrentHeight();
#if IOACCELTYPES_10_5 || (IOACCEL_TYPES_REV < 12 && !defined(kIODescriptionKey))
	info->client_addr = reinterpret_cast<void*>(m_framebuffer->getVRAMPtr());
#else
	info->client_addr = static_cast<mach_vm_address_t>(m_framebuffer->getVRAMPtr());
#endif
	info->client_row_bytes = m_svga->getCurrentPitch();
	m_framebuffer->unlockDevice();
	ACLog(2, "%s: width == %d, height == %d\n", __FUNCTION__, info->w, info->h);
	return kIOReturnSuccess;
}

bool CLASS::retainMasterSurface()
{
	bool rc = true;
	UInt32 width, height;

	if (!bHaveSVGA3D)
		return false;
	if (OSIncrementAtomic(&m_master_surface_retain_count) == 0) {
		m_framebuffer->lockDevice();
		width = m_svga->getCurrentWidth();
		height = m_svga->getCurrentHeight();
		m_framebuffer->unlockDevice();
		ACLog(2, "%s: Master Surface width == %u, height == %u\n", __FUNCTION__, width, height);
		rc = createMasterSurface(width, height);
		if (!rc)
			m_master_surface_retain_count = 0;
	}
	return rc;
}

void CLASS::releaseMasterSurface()
{
	if (!bHaveSVGA3D)
		return;
	SInt32 v = OSDecrementAtomic(&m_master_surface_retain_count);
	if (v <= 0) {
		m_master_surface_retain_count = 0;
		return;
	}
	if (v == 1)
		destroyMasterSurface();
}

void CLASS::lockAccel()
{
	IOLockLock(m_iolock);
}

void CLASS::unlockAccel()
{
	IOLockUnlock(m_iolock);
}

#pragma mark -
#pragma mark Video Methods
#pragma mark -

IOReturn CLASS::VideoSetRegsInRange(UInt32 streamId, struct SVGAOverlayUnit const* regs, UInt32 regMin, UInt32 regMax, UInt32* fence)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	if (regs)
		m_svga->VideoSetRegsInRange(streamId, regs, regMin, regMax);
	m_svga->VideoFlush(streamId);
	if (fence)
		*fence = m_svga->InsertFence();
	m_svga->RingDoorBell();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::VideoSetReg(UInt32 streamId, UInt32 registerId, UInt32 value, UInt32* fence)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->VideoSetReg(streamId, registerId, value);
	m_svga->VideoFlush(streamId);
	if (fence)
		*fence = m_svga->InsertFence();
	m_svga->RingDoorBell();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark ID Allocation Methods
#pragma mark -

UInt32 CLASS::AllocSurfaceID()
{
	int i;
	UInt64 x;

	lockAccel();
	x = (~m_surface_id_mask) & (m_surface_id_mask + 1);
	m_surface_id_mask |= x;
	i = log_2_64(x);
	if (i < 0)
		i = static_cast<int>(8U * sizeof(UInt64) + m_surface_ids_unmanaged++);
	unlockAccel();
	return static_cast<UInt32>(i);
}

void CLASS::FreeSurfaceID(UInt32 sid)
{
	if (sid >= 8U * static_cast<UInt32>(sizeof(UInt64)))
		return;
	lockAccel();
	m_surface_id_mask &= ~(1ULL << sid);
	unlockAccel();
}

UInt32 CLASS::AllocContextID()
{
	int i;
	UInt64 x;

	lockAccel();
	x = (~m_context_id_mask) & (m_context_id_mask + 1);
	m_context_id_mask |= x;
	i = log_2_64(x);
	if (i < 0)
		i = static_cast<int>(8U * sizeof(UInt64) + m_context_ids_unmanaged++);
	unlockAccel();
	return static_cast<UInt32>(i);
}

void CLASS::FreeContextID(UInt32 cid)
{
	if (cid >= 8U * static_cast<UInt32>(sizeof(UInt64)))
		return;
	lockAccel();
	m_context_id_mask &= ~(1ULL << cid);
	unlockAccel();
}

UInt32 CLASS::AllocStreamID()
{
	int i;
	UInt32 x;

	lockAccel();
	x = (~m_stream_id_mask) & (m_stream_id_mask + 1);
	m_stream_id_mask |= x;
	i = log_2_32(x);
	unlockAccel();
	return static_cast<UInt32>(i);
}

void CLASS::FreeStreamID(UInt32 streamId)
{
	if (streamId >= 8U * static_cast<UInt32>(sizeof(UInt32)))
		return;
	lockAccel();
	m_stream_id_mask &= ~(1U << streamId);
	unlockAccel();
}

#pragma mark -
#pragma mark Memory Methods
#pragma mark -

void* CLASS::VRAMMalloc(size_t bytes)
{
	IOReturn rc;
	void* p = 0;

	if (!m_allocator)
		return 0;
	lockAccel();
	rc = m_allocator->Malloc(bytes, &p);
	unlockAccel();
	if (rc != kIOReturnSuccess)
		ACLog(1, "%s(%lu) failed\n", __FUNCTION__, bytes);
	return p;
}

void* CLASS::VRAMRealloc(void* ptr, size_t bytes)
{
	IOReturn rc;
	void* newp = 0;

	if (!m_allocator)
		return 0;
	lockAccel();
	rc = m_allocator->Realloc(ptr, bytes, &newp);
	unlockAccel();
	if (rc != kIOReturnSuccess)
		ACLog(1, "%s(%p, %lu) failed\n", __FUNCTION__, ptr, bytes);
	return newp;
}

void CLASS::VRAMFree(void* ptr)
{
	IOReturn rc;

	if (!m_allocator)
		return;
	lockAccel();
	rc = m_allocator->Free(ptr);
	unlockAccel();
	if (rc != kIOReturnSuccess)
		ACLog(1, "%s(%p) failed\n", __FUNCTION__, ptr);
}

IOMemoryMap* CLASS::mapVRAMRangeForTask(task_t task, vm_offset_t offset_in_bar1, vm_size_t size)
{
	if (!m_bar1)
		return 0;
	return m_bar1->createMappingInTask(task,
									   0,
									   kIOMapAnywhere,
									   offset_in_bar1,
									   size);
}
