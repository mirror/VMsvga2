/*
 *  VMsvga2.h
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

#ifndef __VMSVGA2_H__
#define __VMSVGA2_H__

#include <IOKit/graphics/IOFramebuffer.h>
#include "SVGADevice.h"
#include "common_fb.h"

class IOTimerEventSource;

class VMsvga2 : public IOFramebuffer
{
	OSDeclareDefaultStructors(VMsvga2);

private:
	SVGADevice svga;				// (now * at 0x10C)
	IOPCIDevice* m_provider;		// (eliminated)
	IODeviceMemory* m_bar1;			// offset 0x110
	IOMemoryMap* m_bar1_map;		// offset 0x114
	IOVirtualAddress m_bar1_ptr;	// offset 0x118
#if 0
	IOPhysicalAddress m_fb_offset;	// offset 0x11C
	UInt m_aperture_size;			// offset 0x120
#endif
	IODisplayModeID m_display_mode;	// offset 0x124
	IOIndex m_depth_mode;			// offset 0x128
	thread_call_t m_restore_call;	// offset 0x12C
	IODisplayModeID m_modes[NUM_DISPLAY_MODES];	// offset 0x130 (14 entries)
	UInt m_custom_switch;			// offset 0x168
	struct {
		OSObject* target;
		void* ref;
		IOFBInterruptProc proc;
	} m_intr;						// offset 0x16C
	IOLock* m_iolock;				// offset 0x178
//	void* m_cursor_image;			// offset 0x17C
	SInt m_hotspot_x;				// offset 0x180
	SInt m_hotspot_y;				// offset 0x184

	/*
	 * Begin Added
	 */
	bool m_intr_enabled;
	bool m_accel_updates;
	thread_call_t m_refresh_call;
	UInt m_refresh_quantum_ms;
	UInt m_num_active_modes;
	DisplayModeEntry customMode;
	/*
	 * End Added
	 */

	void Cleanup();
	static UInt FindDepthMode(IOIndex depth);
	DisplayModeEntry const* GetDisplayMode(IODisplayModeID displayMode);
	static void IOSelectToString(IOSelect io_select, char* output);
	static void ConvertAlphaCursor(UInt* cursor, UInt width, UInt height);
	void CustomSwitchStepWait(UInt value);
	void CustomSwitchStepSet(UInt value);
	void EmitConnectChangedEvent();
	void RestoreAllModes();
	static void _RestoreAllModes(thread_call_param_t param0, thread_call_param_t param1);

	/*
	 * Begin Added
	 */
	void scheduleRefreshTimer(UInt milliSeconds);
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
	UInt getApertureSize(IODisplayModeID displayMode, IOIndex depth);
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
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	bool passiveMatch(OSDictionary* matching, bool changesOK = false);
#endif
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
