/*
 *  VMsvga2.h
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

#ifndef __VMSVGA2_H__
#define __VMSVGA2_H__

#include <IOKit/graphics/IOFramebuffer.h>
#include "SVGADevice.h"

struct CustomModeData;
class IOTimerEventSource;

class VMsvga2 : public IOFramebuffer
{
	OSDeclareDefaultStructors(VMsvga2);

#if 0
	friend class VMsvga2Client;
#endif
	friend class VMsvga2Accel;

private:
	SVGADevice svga;
	VMFBIOLog m_log_level;			// offset 0x138
	IOPCIDevice* m_provider;		// offset 0x13C
	IODeviceMemory* m_bar1;			// offset 0x140
	IOMemoryMap* m_bar1_map;		// offset 0x144
	IOVirtualAddress m_bar1_ptr;	// offset 0x148
	IOPhysicalAddress m_fb_offset;	// offset 0x14C
	UInt32 m_aperture_size;			// offset 0x150
	IODisplayModeID m_display_mode;	// offset 0x154
	IOIndex m_depth_mode;			// offset 0x158
	thread_call_t m_restore_call;	// offset 0x15C
	IODisplayModeID m_modes[NUM_DISPLAY_MODES];	// offset 0x160
	UInt32 m_custom_switch;			// offset 0x198
	struct {
		OSObject* target;
		void* ref;
		IOFBInterruptProc proc;
	} m_intr;						// offset 0x19C
	IOLock* m_iolock;				// offset 0x1A8
	SInt32 m_hotspot_x;				// offset 0x1AC
	SInt32 m_hotspot_y;				// offset 0x1B0

	/*
	 * Begin Added
	 */
	bool m_intr_enabled;
	bool m_accel_updates;
	thread_call_t m_refresh_call;
	UInt32 m_refresh_fence_count;
	UInt32 m_refresh_quantum_ms;
	/*
	 * End Added
	 */

	void Cleanup();
	void LogPrintf(VMFBIOLog log_level, char const* fmt, ...);
	static UInt32 FindDepthMode(IOIndex depth);
	DisplayModeEntry const* FindDisplayMode(IODisplayModeID displayMode);
	static void IOSelectToString(IOSelect io_select, char* output);
	static void ConvertAlphaCursor(UInt32* cursor, UInt32 width, UInt32 height);
	void CustomSwitchStepWait(UInt32 value);
	void CustomSwitchStepSet(UInt32 value);
	void EmitConnectChangedEvent();
	void RestoreAllModes();
	static void _RestoreAllModes(thread_call_param_t param0, thread_call_param_t param1);

	/*
	 * Begin Added
	 */
	void scheduleRefreshTimer(UInt32 milliSeconds);
	void scheduleRefreshTimer();
	void cancelRefreshTimer();
	void refreshTimerAction(IOTimerEventSource* sender);
	static void _RefreshTimerAction(thread_call_param_t param0, thread_call_param_t param1);
	void setupRefreshTimer();
	void deleteRefreshTimer();
	IODisplayModeID TryDetectCurrentDisplayMode(IODisplayModeID defaultMode) const;
	/*
	 * End Added
	 */

	/*
	 * Accelerator Support
	 */
	SVGADevice* getDevice() { return &svga; }
	IOVirtualAddress getVRAMPtr() const { return m_bar1_ptr; }
	void lockDevice();
	void unlockDevice();
	bool supportsAccel();
	void useAccelUpdates(bool state);

public:
	UInt64 getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth);
	IOReturn setCursorState(SInt32 x, SInt32 y, bool visible);
	IOReturn setCursorImage(void* cursorImage);
	IOReturn setInterruptState(void* interruptRef, UInt32 state);
	IOReturn unregisterInterrupt(void* interruptRef);
	IOItemCount getConnectionCount();
	IOReturn getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth);
	IOReturn getDisplayModes(IODisplayModeID* allDisplayModes);
	IOItemCount getDisplayModeCount();
	const char* getPixelFormats();
	IODeviceMemory* getVRAMRange();
	UInt32 getApertureSize(IODisplayModeID displayMode, IOIndex depth);
	IODeviceMemory* getApertureRange(IOPixelAperture aperture);
	bool isConsoleDevice();
	IOReturn setupForCurrentConfig();
	bool start(IOService* provider);
	void stop(IOService* provider);
	IOReturn getAttribute(IOSelect attribute, uintptr_t* value);
	IOReturn getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t* value);
	IOReturn setAttribute(IOSelect attribute, uintptr_t value);
	IOReturn setAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t value);
	IOReturn registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc, OSObject* target, void* ref, void** interruptRef);
	IOReturn CustomMode(CustomModeData const* inData, CustomModeData* outData, size_t inSize, size_t* outSize);
	IOReturn getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation* info);
	IOReturn getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation* pixelInfo);
	IOReturn setDisplayMode(IODisplayModeID displayMode, IOIndex depth);

	/*
	 * Begin Added
	 */
	IOReturn getDDCBlock(IOIndex connectIndex, UInt32 blockNumber, IOSelect blockType, IOOptionBits options, UInt8* data, IOByteCount* length);
	bool hasDDCConnect(IOIndex connectIndex);
	UInt32 getCurrentApertureSize() const;
	/*
	 * End Added
	 */

#if 0
	IOReturn getStartupDisplayMode(IODisplayModeID* displayMode, IOIndex* depth);
	IOReturn getTimingInfoForDisplayMode(IODisplayModeID displayMode, IOTimingInformation* info);
	IOReturn setDetailedTimings(OSArray* array);
	IOReturn setGammaTable(UInt32 channelCount, UInt32 dataCount, UInt32 dataWidth, void* data);
	IOReturn setStartupDisplayMode(IODisplayModeID displayMode, IOIndex depth);
	IOReturn validateDetailedTiming(void* description, IOByteCount descripSize);
#endif
};

#endif /* __VMSVGA2_H__ */
