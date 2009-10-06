/*
 *  SVGADevice.h
 *
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

#ifndef __SVGADEVICE_H__
#define __SVGADEVICE_H__

#include <libkern/OSTypes.h>
#include "common_fb.h"
class IOPCIDevice;
class IOMemoryMap;

class SVGADevice
{
	friend class SVGA3D;

private:
	IOPCIDevice* m_provider;	// offset 0
	IOMemoryMap* m_bar0;		// offset 4
	IOMemoryMap* m_bar2;		// offset 8
	UInt32* m_fifo_ptr;			// offset 12
	void* m_cursor_ptr;			// offset 16
	UInt32 m_fifo_size;			// offset 20
	UInt8* m_bounce_buffer;		// offset 24
	bool m_using_bounce_buffer;	// offset 28
	size_t m_reserved_size;		// offset 32
	UInt32 m_next_fence;		// offset 36
	VMFBIOLog m_log_level;		// offset 40

	/*
	 * Begin Added
	 */
	UInt32 m_capabilities;
	UInt32 m_width;
	UInt32 m_height;
	UInt32 m_pitch;
	/*
	 * End Added
	 */

	void LogPrintf(VMFBIOLog log_level, char const* fmt, ...);
	void FIFOFull();

	/*
	 * SVGA3D Support
	 */
	UInt32* getFifoPtr() const { return m_fifo_ptr; }

public:
	SVGADevice();
	~SVGADevice();
	UInt32 ReadReg(UInt32 index);
	void WriteReg(UInt32 index, UInt32 value);
	void Cleanup();
	bool Init(IOPCIDevice* provider, VMFBIOLog log_level);
	void Disable();				// Added

	/*
	 * FIFO Stuff
	 */
	bool IsFIFORegValid(UInt32 reg) const;	// Added
	bool HasFIFOCap(UInt32 mask) const;
	bool FIFOInit();
	void* FIFOReserve(size_t bytes);
	void* FIFOReserveCmd(UInt32 type, size_t bytes);
	void* FIFOReserveEscape(UInt32 nsid, size_t bytes);		// Added
	void FIFOCommit(size_t bytes);
	void FIFOCommitAll();

	/*
	 * Fence Stuff
	 */
	UInt32 InsertFence();
	bool HasFencePassed(UInt32 fence) const;
	void SyncToFence(UInt32 fence);
	void RingDoorBell();		// Added
	void SyncFIFO();			// Added

	/*
	 * Cursor Stuff
	 */
	void setCursorState(UInt32 x, UInt32 y, bool visible);
	void* BeginDefineAlphaCursor(UInt32 width, UInt32 height, UInt32 bytespp);
	bool EndDefineAlphaCursor(UInt32 width, UInt32 height, UInt32 bytespp, UInt32 hotspot_x, UInt32 hotspot_y);

	void SetMode(UInt32 width, UInt32 height, UInt32 bpp);

	bool UpdateFramebuffer(UInt32 x, UInt32 y, UInt32 width, UInt32 height);
	bool UpdateFullscreen() { return UpdateFramebuffer(0, 0, m_width, m_height); }			// Added

	/*
	 * Video Stuff (Added)
	 */
	bool BeginVideoSetRegs(UInt32 streamId, size_t numItems, struct SVGAEscapeVideoSetRegs **setRegs);
	bool VideoSetRegsInRange(UInt32 streamId, struct SVGAOverlayUnit const* regs, UInt32 minReg, UInt32 maxReg);
	bool VideoSetRegsWithMash(UInt32 streamId, struct SVGAOverlayUnit const* regs, UInt32 regMask);
	bool VideoSetReg(UInt32 streamId, UInt32 registerId, UInt32 value);
	bool VideoFlush(UInt32 streamId);

	/*
	 * New Stuff (Added)
	 */
	bool HasCapability(UInt32 mask) const { return (m_capabilities & mask) != 0; }
	UInt32 getCurrentWidth() const { return m_width; }
	UInt32 getCurrentHeight() const { return m_height; }
	UInt32 getCurrentPitch() const { return m_pitch; }
	void RegDump();

	bool RectCopy(UInt32 const* copyRect);					// copyRect is an array of 6 UInt32 - same order as SVGAFifoCmdRectCopy
	bool RectFill(UInt32 color, UInt32 const* rect);		// rect is an array of 4 UInt32 - same order as SVGAFifoCmdFrontRopFill
	bool UpdateFramebuffer2(UInt32 const* rect);			// rect is an array of 4 UInt32 - same order as SVGAFifoCmdUpdate

#ifdef TESTING
	static void test_ram_size(char const* name, IOVirtualAddress ptr, IOByteCount count);
#endif
};

#endif /* __SVGADEVICE_H__ */
