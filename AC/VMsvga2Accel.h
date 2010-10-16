/*
 *  VMsvga2Accel.h
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

#ifndef __VMSVGA2ACCEL_H__
#define __VMSVGA2ACCEL_H__

#include <IOKit/graphics/IOAccelerator.h>
#include "SVGA3D.h"
#include "SVGAScreen.h"
#include "FenceTracker.h"

#define kIOMessageFindSurface iokit_vendor_specific_msg(0x10)

#define AUTO_SYNC_PRESENT_FENCE_COUNT	2

class VMsvga2Accel : public IOAccelerator
{
	OSDeclareDefaultStructors(VMsvga2Accel);

private:
	/*
	 * Base
	 */
	SVGA3D svga3d;
	SVGAScreen screen;
	class IOPCIDevice* m_provider;
	class VMsvga2* m_framebuffer;
	SVGADevice* m_svga;
	IODeviceMemory* m_bar1;
	class VMsvga2Allocator* m_allocator;
	IOLock* m_iolock;
#ifdef FB_NOTIFIER
	IONotifier* m_fbNotifier;
#endif
	task_t m_updating_ga;

	/*
	 * Logging and Options
	 */
	SInt32 m_log_level_ac;
	SInt32 m_log_level_ga;
	UInt32 m_options_ga;

	/*
	 * 3D area
	 */
	unsigned bHaveSVGA3D:1;
	unsigned bHaveScreenObject:1;
	UInt64 m_surface_id_mask;
	UInt64 m_context_id_mask;
	UInt64 m_gmr_id_mask;
	UInt32 m_surface_ids_unmanaged;
	UInt32 m_context_ids_unmanaged;
	SInt32 volatile m_master_surface_retain_count;
	UInt32 m_master_surface_id;
	IOReturn m_blitbug_result;

	/*
	 * AutoSync area
	 */
	FenceTracker<AUTO_SYNC_PRESENT_FENCE_COUNT> m_present_tracker;

	/*
	 * Video area
	 */
	UInt32 m_stream_id_mask;

	/*
	 * OS 10.6 specific
	 */
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	class IOSurfaceRoot* m_surface_root;
	unsigned m_surface_root_uuid;
#endif

	/*
	 * GL Area
	 */
	IOMemoryDescriptor* m_channel_memory;

	/*
	 * Private support methods
	 */
	void Cleanup();
#ifdef TIMING
	void timeSyncs();
#endif
	bool createMasterSurface(UInt32 width, UInt32 height);
	void destroyMasterSurface();
	void processOptions();
	IOReturn findFramebuffer();
	IOReturn setupAllocator();
#ifdef FB_NOTIFIER
	IOReturn fbNotificationHandler(void* ref,
								   class IOFramebuffer* framebuffer,
								   SInt32 event,
								   void* info);
#endif
	void initGLStuff();
	void cleanGLStuff();

public:
	/*
	 * Methods overridden from superclass
	 */
	bool init(OSDictionary* dictionary = 0);
	bool start(IOService* provider);
	void stop(IOService* provider);
	IOReturn newUserClient(task_t owningTask,
						   void* securityID,
						   UInt32 type,
						   IOUserClient ** handler);

	/*
	 * Standalone Sync Methods
	 */
	IOReturn SyncFIFO();
	IOReturn RingDoorBell();
	IOReturn SyncToFence(UInt32 fence);

	/*
	 * Methods for supporting VMsvga22DContext
	 */
	IOReturn useAccelUpdates(bool state, task_t owningTask);
	IOReturn RectCopy(UInt32 framebufferIndex,
					  struct IOBlitCopyRectangleStruct const* copyRects,
					  size_t copyRectsSize);
#if 0
	IOReturn RectFillScreen(UInt32 framebufferIndex,
							UInt32 color,
							struct IOBlitRectangleStruct const* rects,
							size_t numRects);
	IOReturn RectFill3D(UInt32 color,
						struct IOBlitRectangleStruct const* rects,
						size_t numRects);
#endif
	IOReturn RectFill(UInt32 framebufferIndex,
					  UInt32 color,
					  struct IOBlitRectangleStruct const* rects,
					  size_t rectsSize);
	IOReturn UpdateFramebufferAutoRing(UInt32 const* rect);	// rect is an array of 4 UInt32 - x, y, width, height
	IOReturn CopyRegion(UInt32 framebufferIndex,
						SInt32 destX,
						SInt32 destY,
						void /* IOAccelDeviceRegion */ const* region,
						size_t regionSize);
	struct FindSurface {
		UInt32 cgsSurfaceID;
		OSObject* client;
	};

	/*
	 * Methods for supporting VMsvga2Surface
	 */
	struct ExtraInfo {
		vm_offset_t mem_offset_in_bar1;
		vm_size_t mem_pitch;
		SInt32 srcDeltaX;
		SInt32 srcDeltaY;
		SInt32 dstDeltaX;
		SInt32 dstDeltaY;
	};
	/*
	 * SVGA3D Methods
	 */
	IOReturn createSurface(UInt32 sid,
						   SVGA3dSurfaceFlags surfaceFlags,
						   SVGA3dSurfaceFormat surfaceFormat,
						   UInt32 width,
						   UInt32 height);
	IOReturn destroySurface(UInt32 sid);
	IOReturn surfaceDMA2D(UInt32 sid,
						  SVGA3dTransferType transfer,
						  void /* IOAccelDeviceRegion */ const* region,
						  ExtraInfo const* extra,
						  UInt32* fence = 0);
	IOReturn surfaceCopy(UInt32 src_sid,
						 UInt32 dst_sid,
						 void /* IOAccelDeviceRegion */ const* region,
						 ExtraInfo const* extra);
	IOReturn surfaceStretch(UInt32 src_sid,
							UInt32 dst_sid,
							SVGA3dStretchBltMode mode,
							void /* IOAccelBounds */ const* src_rect,
							void /* IOAccelBounds */ const* dest_rect);
	IOReturn surfacePresentAutoSync(UInt32 sid,
									void /* IOAccelDeviceRegion */ const* region,
									ExtraInfo const* extra);
	IOReturn surfacePresentReadback(void /* IOAccelDeviceRegion */ const* region);
#if 0
	IOReturn setupRenderContext(UInt32 cid,
								UInt32 color_sid,
								UInt32 depth_sid,
								UInt32 width,
								UInt32 height);
#else
	IOReturn setupRenderContext(UInt32 cid,
								UInt32 color_sid);
#endif
	IOReturn clearContext(UInt32 cid,
						  SVGA3dClearFlag flags,
						  void /* IOAccelDeviceRegion */ const* region,
						  UInt32 color,
						  float depth,
						  UInt32 stencil);
	IOReturn createContext(UInt32 cid);
	IOReturn destroyContext(UInt32 cid);
	bool createClearSurface(UInt32 sid,
							UInt32 cid,
							SVGA3dSurfaceFormat format,
							UInt32 width,
							UInt32 height,
							UInt32 color = 0);

	/*
	 * Screen Methods
	 */
	IOReturn blitFromScreen(UInt32 srcScreenId,
							void /* IOAccelDeviceRegion */ const* region,
							ExtraInfo const* extra,
							UInt32* fence = 0);
	IOReturn blitToScreen(UInt32 destScreenId,
						  void /* IOAccelDeviceRegion */ const* region,
						  ExtraInfo const* extra,
						  UInt32* fence = 0);
	IOReturn blitSurfaceToScreen(UInt32 src_sid,
								 UInt32 destScreenId,
								 void /* IOAccelBounds */ const* src_rect,
								 void /* IOAccelDeviceRegion */ const* dest_region);
	/*
	 * GFB Methods
	 */
	IOReturn blitGFB(UInt32 framebufferIndex,
					 void /* IOAccelDeviceRegion */ const* region,
					 ExtraInfo const* extra,
					 vm_size_t limit,
					 int direction);
#if 0
	IOReturn clearGFB(UInt32 color,
					  struct IOBlitRectangleStruct const* rects,
					  size_t numRects);
#endif

	/*
	 * Misc support methods
	 */
	IOReturn getScreenInfo(IOAccelSurfaceReadData* info);
	bool retainMasterSurface();
	void releaseMasterSurface();
	UInt32 getMasterSurfaceID() const { return m_master_surface_id; }
	SInt32 getLogLevelAC() const { return m_log_level_ac; }
	SInt32 getLogLevelGA() const { return m_log_level_ga; }
	UInt32 getOptionsGA() const { return m_options_ga; }
	IOReturn getBlitBugResult() const { return m_blitbug_result; }
	void cacheBlitBugResult(IOReturn r) { m_blitbug_result = r; }
	void lockAccel();
	void unlockAccel();
	bool Have3D() const { return bHaveSVGA3D != 0; }
	bool HaveScreen() const { return bHaveScreenObject != 0; }
	bool HaveFrontBuffer() const { return bHaveScreenObject != 0 || bHaveSVGA3D != 0; }
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	unsigned getSurfaceRootUUID() const { return m_surface_root_uuid; }
#endif
	IOMemoryDescriptor* getChannelMemory() const { return m_channel_memory; }
	UInt32 getVRAMSize() const;

	/*
	 * Video Support
	 */
	IOReturn VideoSetRegsInRange(UInt32 streamId,
								 struct SVGAOverlayUnit const* regs,
								 UInt32 regMin,
								 UInt32 regMax,
								 UInt32* fence = 0);
	IOReturn VideoSetReg(UInt32 streamId,
						 UInt32 registerId,
						 UInt32 value,
						 UInt32* fence = 0);

	/*
	 * ID Allocation
	 */
	UInt32 AllocSurfaceID();
	void FreeSurfaceID(UInt32 sid);
	UInt32 AllocContextID();
	void FreeContextID(UInt32 cid);
	UInt32 AllocStreamID();
	void FreeStreamID(UInt32 streamId);
	UInt32 AllocGMRID();
	void FreeGMRID(UInt32 gmrId);

	/*
	 * Memory Support
	 */
	void* VRAMMalloc(size_t bytes);
	void* VRAMRealloc(void* ptr, size_t bytes);
	void VRAMFree(void* ptr);
	IOMemoryMap* mapVRAMRangeForTask(task_t task, vm_offset_t offset_in_bar1, vm_size_t size);
};

#endif /* __VMSVGA2ACCEL_H__ */
