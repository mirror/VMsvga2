/*
 *  VMsvga2Surface.cpp
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

#include <IOKit/IOLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

#include "vmw_options_ac.h"
#include "VMsvga2Accel.h"
#include "VMsvga2Surface.h"

#include "svga_apple_header.h"
#include "svga_overlay.h"
#include "svga_apple_footer.h"

#define CLASS VMsvga2Surface
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2Surface, IOUserClient);

#define VLOG_PREFIX_STR "log IOSF: "
#define VLOG_PREFIX_LEN (sizeof(VLOG_PREFIX_STR) - 1)
#define VLOG_BUF_SIZE 256

extern "C" char VMLog_SendString(char const* str);

#if LOGGING_LEVEL >= 1
#define SFLog(log_level, fmt, ...) do { if (log_level <= m_log_level) VLog(fmt, ##__VA_ARGS__); } while (false)
#else
#define SFLog(log_level, fmt, ...)
#endif

static IOExternalMethod iofbFuncsCache[kIOAccelNumSurfaceMethods] =
{
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_lock_options), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_unlock_options), kIOUCScalarIScalarO, 1, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::get_state), kIOUCScalarIScalarO, 0, 1},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_lock_options), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_unlock_options), kIOUCScalarIScalarO, 1, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_shape_backing), kIOUCScalarIStructI, 4, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_id_mode), kIOUCScalarIScalarO, 2, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_scale), kIOUCScalarIStructI, 1, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_shape), kIOUCScalarIStructI, 2, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_flush), kIOUCScalarIScalarO, 2, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_query_lock), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_lock), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_unlock), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_lock), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_unlock), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_control), kIOUCScalarIScalarO, 2, 1},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_shape_backing_length), kIOUCScalarIStructI, 5, kIOUCVariableStructureSize}
};

void CLASS::VLog(char const* fmt, ...)
{
	va_list ap;
	char print_buf[VLOG_BUF_SIZE];

	va_start(ap, fmt);
	strlcpy(&print_buf[0], VLOG_PREFIX_STR, sizeof(print_buf));
	vsnprintf(&print_buf[VLOG_PREFIX_LEN], sizeof(print_buf) - VLOG_PREFIX_LEN, fmt, ap);
	va_end(ap);
	VMLog_SendString(&print_buf[0]);
}

static inline vm_size_t round_up_to_power2(vm_size_t s, vm_size_t power2)
{
	return (s + power2 - 1) & ~(power2 - 1);
}

static inline void memset32(void* dest, UInt32 value, UInt32 size)
{
	asm volatile ("cld; rep stosl" : "+c" (size), "+D" (dest) : "a" (value) : "memory");
}

static bool isRegionEmpty(IOAccelDeviceRegion const* rgn)
{
	if (!rgn)
		return false;
	if (rgn->num_rects &&
		(rgn->rect[0].w <= 0 || rgn->rect[0].h <= 0))
		return true;
	return false;
}

static void set_region(IOAccelDeviceRegion* rgn, UInt32 x, UInt32 y, UInt32 w, UInt32 h)
{
	rgn->num_rects = 1;
	rgn->bounds.x = static_cast<SInt16>(x);
	rgn->bounds.y = static_cast<SInt16>(y);
	rgn->bounds.w = static_cast<SInt16>(w);
	rgn->bounds.h = static_cast<SInt16>(h);
	memcpy(&rgn->rect[0], &rgn->bounds, sizeof(IOAccelBounds));
}

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (!targetP || index >= kIOAccelNumSurfaceMethods)
		return 0;
	*targetP = this;
	return &m_funcs_cache[index];
}

IOReturn CLASS::clientClose()
{
	SFLog(2, "%s\n", __FUNCTION__);
	if (m_provider) {
		if (bHaveSVGA3D) {
			m_provider->releaseMasterSurface();
			m_provider->FreeSurfaceID(m_aux_surface_id[0]);
			m_provider->FreeSurfaceID(m_aux_surface_id[1]);
			m_provider->FreeContextID(m_aux_context_id);
		}
		surface_video_off();
	}
	releaseBacking();
	clearLastShape();
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

#if 0
IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	return super::clientMemoryForType(type, options, memory);
}
#endif

IOReturn CLASS::message(UInt32 type, IOService* provider, void* argument)
{
	VMsvga2Accel::FindSurface* fs;

	if (provider != m_provider || type != kIOMessageFindSurface)
		return super::message(type, provider, argument);
	if (!argument)
		return kIOReturnBadArgument;
	if (!bHaveID)
		return kIOReturnNotReady;
	fs = reinterpret_cast<VMsvga2Accel::FindSurface*>(argument);
	if (fs->cgsSurfaceID == m_wID)
		fs->client = this;
	return kIOReturnSuccess;
}

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider)
		return false;
	m_log_level = m_provider->getLogLevelAC();
	/*
	 * TBD: need to be notified of resolution changes
	 */
	if (m_provider->getScreenInfo(&screenInfo) != kIOReturnSuccess)
		return false /* kIOReturnNoDevice */;
	bHaveSVGA3D = m_provider->retainMasterSurface();
	if (bHaveSVGA3D) {
		m_aux_surface_id[0] = m_provider->AllocSurfaceID();
		m_aux_surface_id[1] = m_provider->AllocSurfaceID();
		m_aux_context_id = m_provider->AllocContextID();
		if (checkOptionAC(VMW_OPTION_AC_DIRECT_BLIT))
			bDirectBlit = true;
		else {
			IOReturn rc = m_provider->getBlitBugResult();
			if (rc == kIOReturnNotFound) {
				rc = detectBlitBug();
				m_provider->cacheBlitBugResult(rc);
				if (rc != kIOReturnSuccess)
					SFLog(1, "Blit Bug: Yes\n");
			}
			bDirectBlit = (rc == kIOReturnSuccess);
		}
	}
	return super::start(provider);
}

void CLASS::stop(IOService* provider)
{
	super::stop(provider);
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	Init();
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
	m_funcs_cache = &iofbFuncsCache[0];
	return true;
}

CLASS* CLASS::withTask(task_t owningTask, void* securityToken, UInt32 type)
{
	CLASS* inst;

	inst = new CLASS;

	if (inst && !inst->initWithTask(owningTask, securityToken, type))
	{
		inst->release();
		inst = 0;
	}

	return (inst);
}

#pragma mark -
#pragma mark Private Support Methods
#pragma mark -

void CLASS::Init()
{
	m_owning_task = 0;
	m_provider = 0;
	m_funcs_cache = 0;
	m_log_level = LOGGING_LEVEL;

	bHaveID = false;
	bHaveSVGA3D = false;
	bVideoMode = false;
	bDirectBlit = false;

	bIsLocked = 0;

	bzero(&m_backing, sizeof m_backing);

	m_last_shape = 0;
	m_last_region = 0;

	bzero(&m_scale, sizeof m_scale);

#ifdef TIMED_PRESENT
	m_last_present_time = 0;
#endif

	m_video.stream_id = static_cast<UInt32>(-1);
}

void CLASS::clearLastShape()
{
	if (m_last_shape) {
		m_last_shape->release();
		m_last_shape = 0;
		m_last_region = 0;
	}
}

bool CLASS::createAuxSurface(UInt32 sid, UInt32 cid, SVGA3dSurfaceFormat format, UInt32 width, UInt32 height)
{
	if (!width || !height)
		return false;
	/*
	 * Note: according to VMware documentation, if a surface with the same
	 *   sid already exists, the old surface is deleted and a new one
	 *   is created in its place.
	 */
	if (m_provider->createSurface(sid, SVGA3dSurfaceFlags(0), format, width, height) != kIOReturnSuccess)
		return false;
	if (m_provider->createContext(cid) != kIOReturnSuccess) {
		m_provider->destroySurface(sid);
		return false;
	}
	m_provider->setupRenderContext(cid, sid);
	m_provider->clearContext(cid, static_cast<SVGA3dClearFlag>(SVGA3D_CLEAR_COLOR), 0, 0, width, height, 0, 1.0F, 0);
	m_provider->destroyContext(cid);
	return true;
}

bool CLASS::isBackingValid() const
{
	return m_backing.self != 0;
}

bool CLASS::hasSourceGrown() const
{
	return static_cast<vm_size_t>(m_scale.reserved[0]) > m_backing.size;
}

bool CLASS::allocBacking()
{
	if (isBackingValid()) {
		/*
		 * We only resize the backing if it needs to grow.  The reasons for this:
		 *   1) The Allocator hasn't been stress tested.
		 *   2) Every reallocation needs to create/modify the memory maps in
		 *      the owning task[s].
		 */
		if (!hasSourceGrown())
			return true;
		if (m_backing.last_DMA_fence)
			m_provider->SyncToFence(m_backing.last_DMA_fence);
		for (UInt32 i = 0; i < 2; ++i)
			releaseBackingMap(i);
	}
	m_backing.size = m_scale.reserved[0];
	m_backing.self = static_cast<UInt8*>(m_provider->VRAMRealloc(m_backing.self, m_backing.size));
	if (!isBackingValid())
		return false;
	m_backing.offset = reinterpret_cast<vm_offset_t>(m_backing.self) - reinterpret_cast<vm_offset_t>(screenInfo.client_addr);
	SFLog(2, "%s: m_backing.offset is 0x%x\n", __FUNCTION__, m_backing.offset);
	m_backing.last_DMA_fence = 0;
	return true;
}

bool CLASS::mapBacking(task_t for_task, UInt32 index)
{
	IOMemoryMap** m;

	if (index >= 2)
		return false;
	m = &m_backing.map[index];
	if (*m)
		return true;
	*m = m_provider->mapVRAMRangeForTask(for_task, m_backing.offset, m_backing.size);
	return (*m) != 0;
}

void CLASS::releaseBacking()
{
	for (UInt32 i = 0; i < 2; ++i)
		if (m_backing.map[i])
			m_backing.map[i]->release();
	if (m_provider != 0 && m_backing.self != 0)
		m_provider->VRAMFree(m_backing.self);
	bzero(&m_backing, sizeof m_backing);
}

void CLASS::releaseBackingMap(UInt32 index)
{
	if (index >= 2)
		return;
	if (m_backing.map[index]) {
		m_backing.map[index]->release();
		m_backing.map[index] = 0;
	}
}

bool CLASS::isSourceValid() const
{
	return (m_scale.source.w > 0) && (m_scale.source.h > 0);
}

IOReturn CLASS::detectBlitBug()
{
	IOReturn rc;
	UInt32* ptr = 0;
	UInt32 const sid = 666;
	UInt32 const cid = 667;
	UInt32 const pixval_red = 0xFF0000U;
	UInt32 const pixval_green = 0xFF00U;
	UInt32 const w = 10;
	UInt32 const h = 10;
	UInt32 const bytes_per_pixel = sizeof(UInt32);
	UInt32 i, pixels;
	union {
		UInt8 b[sizeof(IOAccelDeviceRegion) + sizeof(IOAccelBounds)];
		IOAccelDeviceRegion r;
	} tmpRegion;
	VMsvga2Accel::ExtraInfo extra;

	if (!m_provider)
		return kIOReturnNotReady;
	pixels = w * h / 4;
	if (!createAuxSurface(sid, cid, SVGA3D_X8R8G8B8, w, h))
		return kIOReturnError;
	ptr = static_cast<UInt32*>(m_provider->VRAMMalloc(PAGE_SIZE));
	if (!ptr) {
		rc = kIOReturnNoMemory;
		goto exit;
	}
	set_region(&tmpRegion.r, 0, 0, w/2, h/2);
	for (i = 0; i < pixels; ++i)
		ptr[i] = pixval_green;
	bzero(&extra, sizeof(extra));
	extra.mem_offset_in_bar1 = reinterpret_cast<vm_offset_t>(ptr) - reinterpret_cast<vm_offset_t>(screenInfo.client_addr);
	extra.mem_pitch = w * bytes_per_pixel / 2;
	extra.dstDeltaX = w/2;
	extra.dstDeltaY = h/2;
	rc = m_provider->surfaceDMA2D(sid, SVGA3D_WRITE_HOST_VRAM, &tmpRegion.r, &extra);
	if (rc != kIOReturnSuccess) {
		rc = kIOReturnError;
		goto exit;
	}
	extra.dstDeltaX = 0;
	extra.dstDeltaY = 0;
	rc = m_provider->surfaceDMA2D(sid, SVGA3D_READ_HOST_VRAM, &tmpRegion.r, &extra);
	if (rc != kIOReturnSuccess) {
		rc = kIOReturnError;
		goto exit;
	}
	m_provider->SyncFIFO();
	rc = kIOReturnSuccess;
	for (i = 0; i < pixels; ++i)
		if (ptr[i] != pixval_red) {
			rc = kIOReturnUnsupported;
			break;
		}
exit:
	if (ptr)
		m_provider->VRAMFree(ptr);
	m_provider->destroySurface(sid);
	SFLog(2, "%s: returns 0x%x\n", __FUNCTION__, rc);
	return rc;
}

IOReturn CLASS::DMAOutDirect(bool withFence)
{
	VMsvga2Accel::ExtraInfo extra;

	if (!m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	/*
	 * Note: this code asssumes 1:1 scale (m_last_region->bounds.w == m_scale.buffer.w && m_last_region->bounds.h == m_scale.buffer.h)
	 */
	bzero(&extra, sizeof(extra));
	extra.mem_offset_in_bar1 = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	extra.srcDeltaX = -static_cast<SInt32>(m_last_region->bounds.x);
	extra.srcDeltaY = -static_cast<SInt32>(m_last_region->bounds.y);
	if (m_provider->surfaceDMA2D(m_provider->getMasterSurfaceID(),
								 SVGA3D_WRITE_HOST_VRAM,
								 m_last_region,
								 &extra,
								 withFence ? &m_backing.last_DMA_fence : 0) != kIOReturnSuccess)
		return kIOReturnDMAError;
	return kIOReturnSuccess;
}

IOReturn CLASS::DMAOutWithCopy(bool withFence)
{
	IOReturn rc;
	SInt32 deltaX, deltaY;
	VMsvga2Accel::ExtraInfo extra;

	if (!m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	/*
	 * Note: this code asssumes 1:1 scale (m_last_region->bounds.w == m_scale.buffer.w && m_last_region->bounds.h == m_scale.buffer.h)
	 */
	deltaX = -static_cast<SInt32>(m_last_region->bounds.x);
	deltaY = -static_cast<SInt32>(m_last_region->bounds.y);
	if (!createAuxSurface(m_aux_surface_id[0],
						  m_aux_context_id,
						  surfaceFormat,
						  static_cast<UInt32>(m_scale.buffer.w),
						  static_cast<UInt32>(m_scale.buffer.h)))
		return kIOReturnNoResources;
	bzero(&extra, sizeof(extra));
	extra.mem_offset_in_bar1 = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	extra.srcDeltaX = deltaX;
	extra.srcDeltaY = deltaY;
	extra.dstDeltaX = deltaX;
	extra.dstDeltaY = deltaY;
	rc = m_provider->surfaceDMA2D(m_aux_surface_id[0],
								  SVGA3D_WRITE_HOST_VRAM,
								  m_last_region,
								  &extra,
								  withFence ? &m_backing.last_DMA_fence : 0);
	if (rc != kIOReturnSuccess) {
		m_provider->destroySurface(m_aux_surface_id[0]);
		return kIOReturnDMAError;
	}
	bzero(&extra, sizeof(extra));
	extra.srcDeltaX = deltaX;
	extra.srcDeltaY = deltaY;
	rc = m_provider->surfaceCopy(m_aux_surface_id[0],
								 m_provider->getMasterSurfaceID(),
								 m_last_region,
								 &extra);
	m_provider->destroySurface(m_aux_surface_id[0]);
	if (rc != kIOReturnSuccess)
		return kIOReturnNotWritable;
	return kIOReturnSuccess;
}

IOReturn CLASS::DMAOutStretchWithCopy(bool withFence)
{
	IOReturn rc;
	UInt32 width, height;
	VMsvga2Accel::ExtraInfo extra;
	union {
		UInt8 b[sizeof(IOAccelDeviceRegion) + sizeof(IOAccelBounds)];
		IOAccelDeviceRegion r;
	} tmpRegion;

	if (!m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	width = static_cast<UInt32>(m_scale.buffer.w);
	height = static_cast<UInt32>(m_scale.buffer.h);
	if (!createAuxSurface(m_aux_surface_id[1],
						  m_aux_context_id,
						  surfaceFormat,
						  width,
						  height))
		return kIOReturnNoResources;
	set_region(&tmpRegion.r, 0, 0, width, height);
	bzero(&extra, sizeof(extra));
	extra.mem_offset_in_bar1 = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	rc = m_provider->surfaceDMA2D(m_aux_surface_id[1],
								  SVGA3D_WRITE_HOST_VRAM,
								  &tmpRegion.r,
								  &extra,
								  withFence ? &m_backing.last_DMA_fence : 0);
	if (rc != kIOReturnSuccess) {
		m_provider->destroySurface(m_aux_surface_id[1]);
		return kIOReturnDMAError;
	}
	if (!createAuxSurface(m_aux_surface_id[0],
						  m_aux_context_id,
						  surfaceFormat,
						  static_cast<UInt32>(m_last_region->bounds.w),
						  static_cast<UInt32>(m_last_region->bounds.h))) {
		m_provider->destroySurface(m_aux_surface_id[1]);
		return kIOReturnNoResources;
	}
	rc = m_provider->surfaceStretch(m_aux_surface_id[1],
									m_aux_surface_id[0],
									SVGA3D_STRETCH_BLT_LINEAR,
									&tmpRegion.r.bounds,
									&m_last_region->bounds);
	m_provider->destroySurface(m_aux_surface_id[1]);
	if (rc != kIOReturnSuccess) {
		m_provider->destroySurface(m_aux_surface_id[0]);
		return kIOReturnNotWritable;
	}
	bzero(&extra, sizeof(extra));
	extra.srcDeltaX = -static_cast<SInt32>(m_last_region->bounds.x);
	extra.srcDeltaY = -static_cast<SInt32>(m_last_region->bounds.y);
	rc = m_provider->surfaceCopy(m_aux_surface_id[0],
								 m_provider->getMasterSurfaceID(),
								 m_last_region,
								 &extra);
	m_provider->destroySurface(m_aux_surface_id[0]);
	if (rc != kIOReturnSuccess)
		return kIOReturnNotWritable;
	return kIOReturnSuccess;
}

IOReturn CLASS::doPresent()
{
	VMsvga2Accel::ExtraInfo extra;

	if (!m_last_region)
		return kIOReturnNotReady;
	bzero(&extra, sizeof(extra));
	if (m_provider->surfacePresentAutoSync(m_provider->getMasterSurfaceID(),
										   m_last_region,
										   &extra) != kIOReturnSuccess)
		return kIOReturnIOError;
	return kIOReturnSuccess;
}

#ifdef TIMED_PRESENT
IOReturn CLASS::doTimedPresent()
{
	UInt64 current_time = mach_absolute_time();
	VMsvga2Accel::ExtraInfo extra;
	union {
		UInt8 b[sizeof(IOAccelDeviceRegion) + sizeof(IOAccelBounds)];
		IOAccelDeviceRegion r;
	} tmpRegion;

	if (current_time < m_last_present_time + 50 * kMillisecondScale)
		return kIOReturnSuccess;
	bzero(&extra, sizeof(extra));
	m_last_present_time = current_time;
	set_region(&tmpRegion.r,
			   static_cast<UInt32>(screenInfo.x),
			   static_cast<UInt32>(screenInfo.y),
			   static_cast<UInt32>(screenInfo.w),
			   static_cast<UInt32>(screenInfo.h))
	if (m_provider->surfacePresentAutoSync(m_provider->getMasterSurfaceID(),
										   &tmpRegion.r,
										   &extra) != kIOReturnSuccess)
		return kIOReturnIOError;
	return kIOReturnSuccess;
}
#endif

void CLASS::calculateSurfaceInformation(IOAccelSurfaceInformation* info)
{
	info->address[0] = m_backing.map[0]->getVirtualAddress() + m_scale.reserved[2];
	info->width = static_cast<UInt32>(m_scale.buffer.w);
	info->height = static_cast<UInt32>(m_scale.buffer.h);
	info->rowBytes = m_scale.reserved[1];
	info->pixelFormat = m_pixel_format;
}

void CLASS::calculateScaleParameters()
{
	/*
	 * Byte size of source rounded up nearest PAGE_SIZE
	 */
	m_scale.reserved[0] = round_up_to_power2(static_cast<vm_size_t>(m_scale.source.w) *
											 static_cast<vm_size_t>(m_scale.source.h) *
											 m_bytes_per_pixel,
											 PAGE_SIZE);
	/*
	 * Source pitch
	 */
	m_scale.reserved[1] = static_cast<UInt32>(m_scale.source.w) * m_bytes_per_pixel;
	/*
	 * buffer byte offset within source
	 */
	m_scale.reserved[2] = m_scale.buffer.y * m_scale.reserved[1] + m_scale.buffer.x * m_bytes_per_pixel;
}

#pragma mark -
#pragma mark Private Support Methods - Video
#pragma mark -

void CLASS::clear_yuv_to_black(void* buffer, vm_size_t size)
{
	UInt32 s = size / sizeof(UInt32);	// size should already be aligned to PAGE_SIZE
	UInt32 pixval = 0;

	/*
	 * Make a superfast XMM version of this
	 */
	switch (m_video.vmware_pixel_format) {
		case VMWARE_FOURCC_UYVY:
			pixval = 0x10801080U;
			break;
		case VMWARE_FOURCC_YUY2:
			pixval = 0x80108010U;
			break;
		default:
			return;				// Unsupported
	}
	memset32(buffer, pixval, s);
}

/*
 * This sets up a trick in order to get IOYUVImageCodecMergeFloatingImageOntoWindow
 *   to believe that a YUV CGSSurface is all-black thereby fooling it into flooding the
 *   background window behind the video frame with black in order satify
 *   VMware's black colorkey requirement.
 *
 * The trick exploits a design flaw in Apple's CGSSurface technology.  All requests by
 *   an application to create a surface (CGSAddSurface) are routed to the WindowServer.
 *   As a result, the WindowServer is the owning task for all CGSSurfaces, and any
 *   attempt to lock it via IOAccelSurfaceConnect expect a virtual address that is valid
 *   inside the WindowServer, not inside the application requesting the surface.
 * OTOH, the application later attaches to the CGSSurface by using the GA's AllocateSurface
 *   method.  It then uses LockSurface and expects a virtual address that is valid in its
 *   own process.  This is a design flaw since the application expects to be able to "lock"
 *   the surface from two different tasks - its own and the WindowServer at the same time,
 *   and provide each a view.
 * The driver can use this flaw by handing the two tasks views to different memory, making
 *   the WindowServer believe that the surface is all black, even though the application
 *   has already placed the first video frame in the surface.
 */
IOReturn CLASS::setup_trick_buffer()
{
	IOBufferMemoryDescriptor* mem;
	IOMemoryMap* my_map_of_mem;

	if (!isBackingValid())
		return kIOReturnNotReady;
	mem = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut | kIOMemoryPageable | kIOMemoryKernelUserShared,
												m_backing.size,
												PAGE_SIZE);
	if (!mem)
		return kIOReturnNoMemory;
	if (mem->prepare() != kIOReturnSuccess) {
		mem->release();
		return kIOReturnNoMemory;
	}
	my_map_of_mem = mem->createMappingInTask(kernel_task, 0, kIOMapAnywhere);
	if (!my_map_of_mem) {
		mem->complete();
		mem->release();
		return kIOReturnNoMemory;
	}
	clear_yuv_to_black(reinterpret_cast<void*>(my_map_of_mem->getVirtualAddress()), m_backing.size);
	my_map_of_mem->release();
	mem->complete();
	releaseBackingMap(0);
	m_backing.map[0] = mem->createMappingInTask(m_owning_task, 0, kIOMapAnywhere);
	mem->release();
	return kIOReturnSuccess;
}

/*
 * Note: The WindowServer expects the driver to be able to clip the
 *   overlayed video frame based on the destination region, but
 *   VMware SVGA II doesn't support this.  So we take an all-or-nothing
 *   approach.  Either overlay the entire frame, or show nothing.
 */
bool CLASS::setVideoDest()
{
	if (!m_last_region)
		return false;
	if (isRegionEmpty(m_last_region)) {
		memset32(&m_video.unit.dstX, 0, 4);
		return true;
	}
	m_video.unit.dstX = static_cast<UInt32>(m_last_region->bounds.x);
	m_video.unit.dstY = static_cast<UInt32>(m_last_region->bounds.y);
	m_video.unit.dstWidth = static_cast<UInt32>(m_last_region->bounds.w);
	m_video.unit.dstHeight = static_cast<UInt32>(m_last_region->bounds.h);
	return true;
}

bool CLASS::setVideoRegs()
{
	if (!isBackingValid())
		return false;
	bzero(&m_video.unit, sizeof m_video.unit);
	m_video.unit.dataOffset = m_backing.offset;
	m_video.unit.format = m_video.vmware_pixel_format;
	m_video.unit.width = static_cast<UInt32>(m_scale.source.w);
	m_video.unit.height = static_cast<UInt32>(m_scale.source.h);
	m_video.unit.srcX = static_cast<UInt32>(m_scale.buffer.x);
	m_video.unit.srcY = static_cast<UInt32>(m_scale.buffer.y);
	m_video.unit.srcWidth = static_cast<UInt32>(m_scale.buffer.w);
	m_video.unit.srcHeight = static_cast<UInt32>(m_scale.buffer.h);
	m_video.unit.pitches[0] = m_scale.reserved[1];
	return true;
}

void CLASS::videoReshape()
{
	if (!bVideoMode || !m_video.unit.enabled)
		return;
	if (!setVideoDest())
		return;
	m_provider->VideoSetRegsInRange(m_video.stream_id,
									&m_video.unit,
									SVGA_VIDEO_DST_X,
									SVGA_VIDEO_DST_HEIGHT,
									&m_backing.last_DMA_fence);
}

#pragma mark -
#pragma mark IONVSurface Methods
#pragma mark -

IOReturn CLASS::surface_read_lock_options(eIOAccelSurfaceLockBits options, IOAccelSurfaceInformation* info, size_t* infoSize)
{
	SFLog(2, "%s(0x%x, %p, %u)\n", __FUNCTION__, options, info, infoSize ? static_cast<unsigned>(*infoSize) : 0);

	if (!info || !infoSize || *infoSize != sizeof *info)
		return kIOReturnBadArgument;
	if (!bHaveID || !isSourceValid())
		return kIOReturnNotReady;
	bzero(info, sizeof *info);
	if (OSTestAndSet(vmSurfaceLockRead, &bIsLocked))
		return kIOReturnCannotLock;
	if (!allocBacking() ||
		!mapBacking(m_owning_task, 0)) {
		OSTestAndClear(vmSurfaceLockRead, &bIsLocked);
		return kIOReturnNoMemory;
	}
	calculateSurfaceInformation(info);
	return kIOReturnSuccess;
}

IOReturn CLASS::surface_read_unlock_options(eIOAccelSurfaceLockBits options)
{
	SFLog(2, "%s(0x%x)\n", __FUNCTION__, options);

	OSTestAndClear(vmSurfaceLockRead, &bIsLocked);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_state(eIOAccelSurfaceStateBits* state)
{
	SFLog(2, "%s(%p)\n", __FUNCTION__, state);

	if (state)
		*state = kIOAccelSurfaceStateNone;
	return kIOReturnSuccess;
}

IOReturn CLASS::surface_write_lock_options(eIOAccelSurfaceLockBits options, IOAccelSurfaceInformation* info, size_t* infoSize)
{
	SFLog(3, "%s(0x%x, %p, %u)\n", __FUNCTION__, options, info, infoSize ? static_cast<unsigned>(*infoSize) : 0);

	if (!info || !infoSize || *infoSize != sizeof *info)
		return kIOReturnBadArgument;
	if (!bHaveID || !isSourceValid())
		return kIOReturnNotReady;
	bzero(info, sizeof *info);
	if (OSTestAndSet(vmSurfaceLockWrite, &bIsLocked))
		return kIOReturnCannotLock;
	if (!allocBacking() ||
		!mapBacking(m_owning_task, 0)) {
		OSTestAndClear(vmSurfaceLockWrite, &bIsLocked);
		return kIOReturnNoMemory;
	}
	calculateSurfaceInformation(info);
	/*
	 * If we're not using packed backing, we let the Window Server run free over
	 *   its shadow framebuffer without syncing to any pending DMA transfers.
	 */
	if (!m_scale.reserved[2])  {
		if (m_backing.last_DMA_fence)
			m_provider->SyncToFence(m_backing.last_DMA_fence);
	}
	return kIOReturnSuccess;
}

IOReturn CLASS::surface_write_unlock_options(eIOAccelSurfaceLockBits options)
{
	SFLog(3, "%s(0x%x)\n", __FUNCTION__, options);

	OSTestAndClear(vmSurfaceLockWrite, &bIsLocked);
	return kIOReturnSuccess;
}

IOReturn CLASS::surface_read(IOAccelSurfaceReadData const* parameters, size_t parametersSize)
{
	SFLog(2, "%s(%p, %u)\n", __FUNCTION__, parameters, static_cast<unsigned>(parametersSize));

	return kIOReturnUnsupported;
}

IOReturn CLASS::set_shape_backing(eIOAccelSurfaceShapeBits options,
								  size_t framebufferIndex,
								  IOVirtualAddress backing,
								  size_t rowbytes,
								  IOAccelDeviceRegion const* rgn,
								  size_t rgnSize)
{
	return set_shape_backing_length_ext(options, framebufferIndex, backing, rowbytes, rgn, rgnSize, rgn ? (rowbytes * rgn->bounds.h) : 0);
}

IOReturn CLASS::set_id_mode(UInt32 wID, eIOAccelSurfaceModeBits modebits)
{
	SFLog(2, "%s(0x%x, 0x%x)\n", __FUNCTION__, wID, modebits);

#ifdef TESTING
	if (checkOptionAC(VMW_OPTION_AC_TEST_MASK))
		runTest();
#endif

	/*
	 * WID == 1 is used by Mac OS X's WindowServer
	 * Let the WindowServer know if we support SVGA3D
	 */
	if (wID == 1 && !bHaveSVGA3D)
		return kIOReturnUnsupported;

	if (modebits & ~(kIOAccelSurfaceModeColorDepthBits | kIOAccelSurfaceModeWindowedBit))
		return kIOReturnUnsupported;
	if (!(modebits & kIOAccelSurfaceModeWindowedBit))
		return kIOReturnUnsupported;
	switch (modebits & kIOAccelSurfaceModeColorDepthBits) {
		case kIOAccelSurfaceModeColorDepth1555:
			surfaceFormat = SVGA3D_X1R5G5B5;
			m_bytes_per_pixel = sizeof(UInt16);
			m_pixel_format = kIO16LE555PixelFormat;
			break;
		case kIOAccelSurfaceModeColorDepth8888:
			surfaceFormat = SVGA3D_X8R8G8B8;
			m_bytes_per_pixel = sizeof(UInt32);
			m_pixel_format = kIO24BGRPixelFormat;
			break;
		case kIOAccelSurfaceModeColorDepthBGRA32:
			surfaceFormat = SVGA3D_A8R8G8B8;
			m_bytes_per_pixel = sizeof(UInt32);
			m_pixel_format = kIO32BGRAPixelFormat;
			break;
		case kIOAccelSurfaceModeColorDepthYUV:
		case kIOAccelSurfaceModeColorDepthYUV9:
		case kIOAccelSurfaceModeColorDepthYUV12:
		case kIOAccelSurfaceModeColorDepthYUV2:
			surfaceFormat = SVGA3D_BUMPL8V8U8;
			m_bytes_per_pixel = sizeof(UInt16);
			m_pixel_format = kIO2vuyPixelFormat;
			break;
		default:
			return kIOReturnUnsupported;
	}
	m_wID = wID;
	bHaveID = true;
	return kIOReturnSuccess;
}

IOReturn CLASS::set_scale(eIOAccelSurfaceScaleBits options, IOAccelSurfaceScaling const* scaling, size_t scalingSize)
{
	SFLog(2, "%s(0x%x, %p, %u)\n", __FUNCTION__, options, scaling, static_cast<unsigned>(scalingSize));

	if (!scaling || scalingSize != sizeof *scaling)
		return kIOReturnBadArgument;
	memcpy(&m_scale, scaling, sizeof *scaling);
	calculateScaleParameters();
	return kIOReturnSuccess;
}

IOReturn CLASS::set_shape(eIOAccelSurfaceShapeBits options, size_t framebufferIndex, IOAccelDeviceRegion const* rgn, size_t rgnSize)
{
	return set_shape_backing_length_ext(options, framebufferIndex, 0, static_cast<UInt32>(-1), rgn, rgnSize, 0);
}

IOReturn CLASS::surface_flush(size_t framebufferMask, IOOptionBits options)
{
	IOReturn rc;
	int DMA_type;
	bool withFence;

	SFLog(3, "%s(%u, 0x%x)\n", __FUNCTION__, static_cast<unsigned>(framebufferMask), options);

	if (!bHaveID || !m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	if (bVideoMode)
		return kIOReturnSuccess;
	if (!bHaveSVGA3D)
		return kIOReturnUnsupported;
	/*
	 * Note: conditions for direct blit:
	 *   surfaceFormat same
	 *   1:1 scale
	 *   bDirectBlit flag
	 */
	if (m_scale.buffer.w != m_last_region->bounds.w ||
		m_scale.buffer.h != m_last_region->bounds.h) {
		DMA_type = 2;	// stretch
	} else if (surfaceFormat == SVGA3D_X8R8G8B8 && bDirectBlit) {
		DMA_type = 0;	// direct
	} else {
		DMA_type = 1;	// copy
	}
	withFence = true;
	switch (DMA_type) {
		case 0:
			rc = DMAOutDirect(withFence);
			break;
		case 2:
			rc = DMAOutStretchWithCopy(withFence);
			break;
		case 1:
		default:
			rc = DMAOutWithCopy(withFence);
			break;
	}
#if 0
	releaseBacking();
#endif
	if (rc != kIOReturnSuccess)
		return rc;
	return doPresent();
}

IOReturn CLASS::surface_query_lock()
{
	SFLog(3, "%s()\n", __FUNCTION__);

	return (bIsLocked & ((128U >> vmSurfaceLockRead) | (128U >> vmSurfaceLockWrite))) != 0 ? kIOReturnCannotLock : kIOReturnSuccess;
}

IOReturn CLASS::surface_read_lock(IOAccelSurfaceInformation* info, size_t* infoSize)
{
	return surface_read_lock_options(kIOAccelSurfaceLockInDontCare, info, infoSize);
}

IOReturn CLASS::surface_read_unlock()
{
	return surface_read_unlock_options(kIOAccelSurfaceLockInDontCare);
}

IOReturn CLASS::surface_write_lock(IOAccelSurfaceInformation* info, size_t* infoSize)
{
	return surface_write_lock_options(kIOAccelSurfaceLockInAccel, info, infoSize);
}

IOReturn CLASS::surface_write_unlock()
{
	return surface_write_unlock_options(kIOAccelSurfaceLockInDontCare);
}

IOReturn CLASS::surface_control(size_t selector, size_t arg, size_t* result)
{
	SFLog(2, "%s(%u, %u, %u)\n",
		  __FUNCTION__, static_cast<unsigned>(selector), static_cast<unsigned>(arg), result ? static_cast<unsigned>(*result) : 0);

	return kIOReturnUnsupported;
}

IOReturn CLASS::set_shape_backing_length(eIOAccelSurfaceShapeBits options,
										 size_t framebufferIndex,
										 IOVirtualAddress backing,
										 size_t rowbytes,
										 size_t backingLength,
										 IOAccelDeviceRegion const* rgn /* , size_t rgnSize */)
{
	return set_shape_backing_length_ext(options, framebufferIndex, backing, rowbytes, rgn,  rgn ? IOACCEL_SIZEOF_DEVICE_REGION(rgn) : 0, backingLength);
}

IOReturn CLASS::set_shape_backing_length_ext(eIOAccelSurfaceShapeBits options,
											 size_t framebufferIndex,
											 IOVirtualAddress backing,
											 size_t rowbytes,
											 IOAccelDeviceRegion const* rgn,
											 size_t rgnSize,
											 size_t backingLength)
{
#if 0
	int const expectedOptions = kIOAccelSurfaceShapeIdentityScaleBit | kIOAccelSurfaceShapeFrameSyncBit;
#endif

	SFLog(3, "%s(0x%x, %u, %p, 0x%x, %p, %u, %u)\n",
		 __FUNCTION__,
		 options,
		 static_cast<unsigned>(framebufferIndex),
		 backing,
		 static_cast<unsigned>(rowbytes),
		 rgn,
		 static_cast<unsigned>(rgnSize),
		 static_cast<unsigned>(backingLength));

	if (!rgn || rgnSize < IOACCEL_SIZEOF_DEVICE_REGION(rgn))
		return kIOReturnBadArgument;

#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3) {
		SFLog(3, "%s:   rgn->num_rects == %u, rgn->bounds == %u, %u, %u, %u\n",
			  __FUNCTION__,
			  rgn->num_rects,
			  rgn->bounds.x,
			  rgn->bounds.y,
			  rgn->bounds.w,
			  rgn->bounds.h);
		for (size_t i = 0; i < rgn->num_rects; ++i) {
			SFLog(3, "%s:   rgn->rect[%u] == %u, %u, %u, %u\n",
				  __FUNCTION__,
				  i,
				  rgn->rect[i].x,
				  rgn->rect[i].y,
				  rgn->rect[i].w,
				  rgn->rect[i].h);
		}
	}
#endif

	clearLastShape();
#if 0
	if (!(options & kIOAccelSurfaceShapeNonBlockingBit))	// driver doesn't support waiting on locks
		return kIOReturnUnsupported;
	if ((options & expectedOptions) != expectedOptions)
		return kIOReturnSuccess;
#endif
	if (!bVideoMode && isRegionEmpty(rgn))
		return kIOReturnSuccess;
	if (!bHaveID)
		return kIOReturnNotReady;
	m_last_shape = OSData::withBytes(rgn, rgnSize);
	if (!m_last_shape)
		return kIOReturnNoMemory;
	m_last_region = static_cast<IOAccelDeviceRegion const*>(m_last_shape->getBytesNoCopy());
	if (options & kIOAccelSurfaceShapeIdentityScaleBit) {
		if (checkOptionAC(VMW_OPTION_AC_PACKED_BACKING)) {
			m_scale.source.w = rgn->bounds.w;
			m_scale.source.h = rgn->bounds.h;
			m_scale.buffer.x = 0;
			m_scale.buffer.y = 0;
			m_scale.buffer.w = m_scale.source.w;
			m_scale.buffer.h = m_scale.source.h;
		} else {
			memcpy(&m_scale.buffer, &rgn->bounds, sizeof(IOAccelBounds));
			m_scale.source.w = static_cast<SInt16>(screenInfo.w);
			m_scale.source.h = static_cast<SInt16>(screenInfo.h);
		}
		calculateScaleParameters();
	}
	videoReshape();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Video Support Methods
#pragma mark -

IOReturn CLASS::context_set_surface(UInt32 vmware_pixel_format, UInt32 apple_pixel_format)
{
	SFLog(2, "%s(0x%x, 0x%x)\n", __FUNCTION__, vmware_pixel_format, apple_pixel_format);

#if 0
	IOReturn rc = surface_query_lock();
	if (rc != kIOReturnSuccess)
		return rc;
#endif
	if (!vmware_pixel_format) {
		surface_video_off();
		bVideoMode = false;
		if (apple_pixel_format == kIO32BGRAPixelFormat) {
			surfaceFormat = SVGA3D_A8R8G8B8;
			m_bytes_per_pixel = sizeof(UInt32);
			m_pixel_format = apple_pixel_format;
		}
		return kIOReturnSuccess;
	}
	if (checkOptionAC(VMW_OPTION_AC_NO_YUV))
		return kIOReturnUnsupported;
	bVideoMode = true;
	m_video.unit.enabled = 0;
	m_video.vmware_pixel_format = vmware_pixel_format;
	m_pixel_format = apple_pixel_format;
	m_bytes_per_pixel = sizeof(UInt16);
	return kIOReturnSuccess;
}

IOReturn CLASS::context_scale_surface(IOOptionBits options, UInt32 width, UInt32 height)
{
	SFLog(2, "%s(0x%x, %u, %u)\n", __FUNCTION__, options, width, height);

	if (!(options & kIOBlitFixedSource))
		return kIOReturnSuccess;
	m_scale.source.w = static_cast<SInt16>(width);
	m_scale.source.h = static_cast<SInt16>(height);
	m_scale.buffer.x = 0;
	m_scale.buffer.y = 0;
	m_scale.buffer.w = m_scale.source.w;
	m_scale.buffer.h = m_scale.source.h;
	calculateScaleParameters();
	return kIOReturnSuccess;
}

IOReturn CLASS::context_lock_memory(task_t context_owning_task, vm_address_t* address, vm_size_t* rowBytes)
{
	SFLog(3, "%s(%p, %p, %p)\n", __FUNCTION__, context_owning_task, address, rowBytes);

	if (!address || !rowBytes)
		return kIOReturnBadArgument;
	if (!bHaveID || !isSourceValid())
		return kIOReturnNotReady;
	if (OSTestAndSet(vmSurfaceLockContext, &bIsLocked))
		return kIOReturnCannotLock;
	if (!allocBacking() ||
		!mapBacking(context_owning_task, 1)) {
		OSTestAndClear(vmSurfaceLockContext, &bIsLocked);
		return kIOReturnNoMemory;
	}
	if (bVideoMode && !bHaveSVGA3D)
		setup_trick_buffer();	// Note: error ignored
	*address = m_backing.map[1]->getVirtualAddress();
	*rowBytes = m_scale.reserved[1];
	return kIOReturnSuccess;
}

IOReturn CLASS::context_unlock_memory()
{
	SFLog(3, "%s()\n", __FUNCTION__);

	OSTestAndClear(vmSurfaceLockContext, &bIsLocked);
	releaseBackingMap(1);
	return kIOReturnSuccess;
}

/*
 * This routine blits a region from the master surface to a CGS surface.
 *   The WindowServer calls this function while holding a write-lock on the surface.
 *   It expects to be able to combine the result of the blit with other graphics
 *   rendered into the surface and then flush it to screen.  As a result, we cannot
 *   fully perform the blit in host VRAM.  We must copy the data into guest memory or
 *   the WindowServer won't be able to combine it.
 *   In principle, if the WindowServer didn't expect to be able to combine, it would
 *   be possible to perform the whole blit in host VRAM only, which would make it
 *   more efficient.
 */
IOReturn CLASS::context_copy_region(size_t destX, size_t destY, IOAccelDeviceRegion const* region, size_t regionSize)
{
	IOReturn rc;
	VMsvga2Accel::ExtraInfo extra;

	SFLog(3, "%s(%u, %u, %p, %u)\n", __FUNCTION__, destX, destY, region, regionSize);

	if (!region || regionSize < IOACCEL_SIZEOF_DEVICE_REGION(region))
		return kIOReturnBadArgument;

	if (!bHaveID || !isBackingValid())
		return kIOReturnNotReady;
	if (bVideoMode || !bHaveSVGA3D)
		return kIOReturnUnsupported;
	bzero(&extra, sizeof(extra));
	extra.mem_offset_in_bar1 = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	extra.dstDeltaX = static_cast<SInt32>(destX - region->bounds.x);
	extra.dstDeltaY = static_cast<SInt32>(destY - region->bounds.y);
	rc = m_provider->surfaceDMA2D(m_provider->getMasterSurfaceID(),
								  SVGA3D_READ_HOST_VRAM,
								  region,
								  &extra,
								  &m_backing.last_DMA_fence);
	if (rc != kIOReturnSuccess)
		return kIOReturnNotReadable;
	m_provider->SyncToFence(m_backing.last_DMA_fence);		// Note: regrettable but necessary - even RingDoorBell didn't do the job when swinging windows around
	return kIOReturnSuccess;
}

IOReturn CLASS::surface_video_off()
{
	if (!bVideoMode || !m_video.unit.enabled)
		return kIOReturnSuccess;
	m_video.unit.enabled = 0;
	m_provider->VideoSetReg(m_video.stream_id, SVGA_VIDEO_ENABLED, 0);
	m_provider->FreeStreamID(m_video.stream_id);
	return kIOReturnSuccess;
}

IOReturn CLASS::surface_flush_video()
{
	SFLog(3, "%s()\n", __FUNCTION__);

	if (!bHaveID || !m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	if (!bVideoMode)
		return kIOReturnUnsupported;
	/*
	 * Note: we don't SyncFIFO right after a flush, which means the backing may be reused
	 *   while there are still outgoing video flushes in the FIFO.  This may cause some tearing.
	 */
	if (m_video.unit.enabled)
		return m_provider->VideoSetRegsInRange(m_video.stream_id, 0, 0, 0, &m_backing.last_DMA_fence);
	m_video.stream_id = m_provider->AllocStreamID();
	if (m_video.stream_id == static_cast<UInt32>(-1))
		return kIOReturnNoResources;
	setVideoRegs();
	setVideoDest();
	m_video.unit.enabled = 1;
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3)
		for (size_t i = 0; i < SVGA_VIDEO_NUM_REGS; ++i)
			SFLog(3, "%s:   reg[%u] == 0x%x\n", __FUNCTION__, i, reinterpret_cast<UInt32 const*>(&m_video.unit)[i]);
#endif
	return m_provider->VideoSetRegsInRange(m_video.stream_id, &m_video.unit, SVGA_VIDEO_ENABLED, SVGA_VIDEO_PITCH_3, &m_backing.last_DMA_fence);
}

#pragma mark -
#pragma mark TEST
#pragma mark -

#ifdef TESTING
void CLASS::runTest()
{
	union {
		UInt8 b[sizeof(IOAccelDeviceRegion) + sizeof(IOAccelBounds)];
		IOAccelDeviceRegion r;
	} tmpRegion;
	bool rc;
	UInt32* ptr;
	UInt32 const color_sid = 12345;
#if 0
	UInt32 const depth_sid = 12346;
#endif
	UInt32 const cid = 12347;
	UInt32 const pixval_green = 0xFF00U, pixval_red = 0xFF0000U, pixval_random = 0x5A0086U;
	UInt32 i, w, h, pixels;
	UInt32 offset, bytes_per_pixel;
	UInt32 rect[4];
	VMsvga2Accel::ExtraInfo extra;

	offset = SVGA_FB_MAX_TRACEABLE_SIZE;
	bytes_per_pixel = sizeof(UInt32);
	if (checkOptionAC(VMW_OPTION_AC_TEST_SMALL)) {
		w = h = 512;
	} else {
		w = screenInfo.w;
		h = screenInfo.h;
	}
	pixels = w * h;
	rc = createAuxSurface(color_sid, cid, SVGA3D_X8R8G8B8, w, h);
	if (!rc) {
		SFLog(1, "%s: createAuxSurface failed\n", __FUNCTION__);
		return;
	}
	ptr = static_cast<UInt32*>(screenInfo.client_addr) + offset / sizeof(UInt32);
	set_region(&tmpRegion.r, 0, 0, w, h);
	rect[0] = 0;
	rect[1] = 0;
	rect[2] = w;
	rect[3] = h;
	for (i = 0; i < pixels; ++i)
		ptr[i] = pixval_green;
	bzero(&extra, sizeof(extra));
	extra.mem_offset_in_bar1 = offset;
	extra.mem_pitch = w * bytes_per_pixel;
	rc = (m_provider->surfaceDMA2D(color_sid, SVGA3D_WRITE_HOST_VRAM, &tmpRegion.r, &extra) != kIOReturnSuccess);
	if (!rc) {
		SFLog(1, "%s: surfaceDMA2D write failed\n", __FUNCTION__);
		return;
	}
	m_provider->SyncFIFO();
	bzero(ptr, pixels * bytes_per_pixel);
	for (i = 0; i < pixels; ++i)
		if (ptr[i] != pixval_green) {
			SFLog(1, "%s: DMA dummy diff ok\n", __FUNCTION__);
			break;
		}
	rc = (m_provider->surfaceDMA2D(color_sid, SVGA3D_READ_HOST_VRAM, &tmpRegion.r, &extra) != kIOReturnSuccess);
	if (!rc) {
		SFLog(1, "%s: surfaceDMA2D read failed\n", __FUNCTION__);
		return;
	}
	m_provider->SyncFIFO();
	for (i = 0; i < pixels; ++i)
		if (ptr[i] != pixval_green) {
			SFLog(1, "%s: DMA readback differs\n", __FUNCTION__);
			break;
		}
	if (!checkOptionAC(VMW_OPTION_AC_TEST_DMA))
		goto skip_dma_test;
#if 0
	for (i = w/2 - 50; i < w/2 + 50; ++i)
		for (j = h/2 - 50 ; j < h/2 + 50; ++j)
			ptr[j * w + i] = pixval_random;
	set_region(&tmpRegion.r, w/2 - 50, h/2 - 50, 100, 100);
#else
	for (i = 0; i < 100 * 100; ++i)
		ptr[i] = pixval_random;
	set_region(&tmpRegion.r, 0, 0, 100, 100);
	bzero(&extra, sizeof(extra));
	extra.mem_offset_in_bar1 = offset;
	extra.mem_pitch = 100 * bytes_per_pixel;
	extra.dstDeltaX = w/2 - 50;
	extra.dstDeltaY = h/2 - 50;
#endif
	rc = (m_provider->surfaceDMA2D(color_sid, SVGA3D_WRITE_HOST_VRAM, &tmpRegion.r, &extra) != kIOReturnSuccess);
	if (!rc) {
		SFLog(1, "%s: surfaceDMA2D 2nd write failed\n", __FUNCTION__);
		return;
	}
	m_provider->SyncFIFO();
	set_region(&tmpRegion.r, 0, 0, w, h);
	bzero(&extra, sizeof(extra));
	extra.mem_offset_in_bar1 = offset;
	extra.mem_pitch = w * bytes_per_pixel;
	rc = (m_provider->surfaceDMA2D(color_sid, SVGA3D_READ_HOST_VRAM, &tmpRegion.r, &extra) != kIOReturnSuccess);
	if (!rc) {
		SFLog(1, "%s: surfaceDMA2D 2nd read failed\n", __FUNCTION__);
		return;
	}
	m_provider->SyncFIFO();
	for (i = 0; i < w; ++i)
		if (ptr[i] != pixval_green) {
			SFLog(1, "%s: DMA 2nd readback differs\n", __FUNCTION__);
			setProperty("DMATestResult", static_cast<void*>(&ptr[0]), 128);
			break;
		}
skip_dma_test:
	if (checkOptionAC(VMW_OPTION_AC_TEST_PRESENT)) {
		bzero(&extra, sizeof(extra));
		rc = (m_provider->surfacePresentAutoSync(color_sid, &tmpRegion.r, &extra) != kIOReturnSuccess);
		if (!rc) {
			SFLog(1, "%s: surfacePresent failed\n", __FUNCTION__);
			return;
		}
		if (checkOptionAC(VMW_OPTION_AC_TEST_READBACK)) {
			rc = (m_provider->surfacePresentReadback(&tmpRegion.r) != kIOReturnSuccess);
			if (!rc) {
				SFLog(1, "%s: surfacePresentReadback failed\n", __FUNCTION__);
				return;
			}
		}
		m_provider->SyncFIFO();
	} else {
		extra.mem_offset_in_bar1 = 0;
		extra.mem_pitch = screenInfo.client_row_bytes;
		rc = (m_provider->surfaceDMA2D(color_sid, SVGA3D_READ_HOST_VRAM, &tmpRegion.r, &extra) != kIOReturnSuccess);
		if (!rc) {
			SFLog(1, "%s: surfaceDMA2D to FB failed\n", __FUNCTION__);
			return;
		}
		m_provider->UpdateFramebuffer(&rect[0]);
		m_provider->SyncFIFO();
	}
	SFLog(1, "%s: complete\n", __FUNCTION__);
	IOSleep(10000);
	m_provider->destroySurface(color_sid);
	m_provider->SyncFIFO();
}
#endif /* TESTING */
