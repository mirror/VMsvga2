/*
 *  SVGADevice.cpp
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

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <stdarg.h>
#include "SVGADevice.h"
#include "vmw_options_fb.h"

#include "svga_apple_header.h"
#include "svga_reg.h"
#include "svga_overlay.h"
#include "svga_escape.h"
#include "svga_apple_footer.h"

#define BOUNCE_BUFFER_SIZE 0x10000U

#ifdef REQUIRE_TRACING
#warning Building for Fusion Host/Mac OS X Server Guest
#endif

#define LOGPRINTF_PREFIX_STR "log SVGADev: "
#define LOGPRINTF_PREFIX_LEN (sizeof LOGPRINTF_PREFIX_STR - 1)
#define LOGPRINTF_PREFIX_SKIP 4				// past "log "
#define LOGPRINTF_BUF_SIZE 256

#define TO_BYTE_PTR(x) reinterpret_cast<UInt8*>(const_cast<UInt32*>(x))
#define HasFencePassedUnguarded(fifo, fence) (static_cast<SInt32>(fifo[SVGA_FIFO_FENCE] - fence) >= 0)

static inline UInt32 count_bits(UInt32 mask)
{
	mask = ((mask & 0xAAAAAAAAU) >> 1) + (mask & 0x55555555U);
	mask = ((mask & 0xCCCCCCCCU) >> 2) + (mask & 0x33333333U);
	mask = ((mask & 0xF0F0F0F0U) >> 4) + (mask & 0x0F0F0F0FU);
	mask = ((mask & 0xFF00FF00U) >> 8) + (mask & 0x00FF00FFU);
	return ((mask & 0xFFFF0000U) >> 16) + (mask & 0x0000FFFFU);
}

SVGADevice::SVGADevice()
{
	m_provider = 0;
	m_bar0 = 0;
	m_bar2 = 0;
	m_fifo_ptr = 0;
	m_bounce_buffer = 0;
	m_log_level = 1;
	m_next_fence = 1;
	m_capabilities = 0;
}

SVGADevice::~SVGADevice()
{
	Cleanup();
}

void SVGADevice::Cleanup()
{
	if (m_provider)
		m_provider = 0;
	if (m_bar0) {
		m_bar0->release();
		m_bar0 = 0;
	}
	if (m_bar2) {
		m_bar2->release();
		m_bar2 = 0;
	}
	m_fifo_ptr = 0;
	if (m_bounce_buffer) {
		IOFree(m_bounce_buffer, BOUNCE_BUFFER_SIZE);
		m_bounce_buffer = 0;
	}
	m_capabilities = 0;
}

UInt32 SVGADevice::ReadReg(UInt32 index)
{
	m_provider->ioWrite32(SVGA_INDEX_PORT, index, m_bar0);
	return m_provider->ioRead32(SVGA_VALUE_PORT, m_bar0);
}

void SVGADevice::WriteReg(UInt32 index, UInt32 value)
{
	m_provider->ioWrite32(SVGA_INDEX_PORT, index, m_bar0);
	m_provider->ioWrite32(SVGA_VALUE_PORT, value, m_bar0);
}

void SVGADevice::LogPrintf(VMFBIOLog log_level, char const* fmt, ...)
{
	va_list ap;
	char print_buf[LOGPRINTF_BUF_SIZE];

	if (log_level > m_log_level)
		return;
	va_start(ap, fmt);
	strlcpy(&print_buf[0], LOGPRINTF_PREFIX_STR, sizeof print_buf);
	vsnprintf(&print_buf[LOGPRINTF_PREFIX_LEN], sizeof print_buf - LOGPRINTF_PREFIX_LEN, fmt, ap);
	va_end(ap);
	IOLog("%s", &print_buf[LOGPRINTF_PREFIX_SKIP]);
	if (!VMLog_SendString(&print_buf[0]))
		IOLog("%s: SendString failed.\n", __FUNCTION__);
}

bool SVGADevice::Init(IOPCIDevice* provider, VMFBIOLog log_level)
{
	UInt32 fb_size;
	UInt32 vram_size;
	UInt32 max_width;
	UInt32 max_height;
	UInt32 host_bpp;
	UInt32 guest_bpp;

	m_provider = provider;
	m_log_level = log_level;
	m_bar0 = m_provider->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
	if (!m_bar0) {
		LogPrintf(1, "%s: failed to map BAR0 registers\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_bar2 = m_provider->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
	if (!m_bar2) {
		LogPrintf(1, "%s: failed to get memory map BAR2 registers\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_fifo_ptr = reinterpret_cast<UInt32*>(m_bar2->getVirtualAddress());
#ifdef TESTING
	test_ram_size("SVGADevice", reinterpret_cast<IOVirtualAddress>(m_fifo_ptr), m_bar2->getLength());
#endif
	m_capabilities = ReadReg(SVGA_REG_CAPABILITIES);
#ifdef REQUIRE_TRACING
	LogPrintf(2, "%s: SVGA_REG caps = 0x%08x & 0x%08x\n", __FUNCTION__, m_capabilities, SVGA_CAP_TRACES);
	if (!HasCapability(SVGA_CAP_TRACES)) {
		LogPrintf(1, "%s: SVGA_CAP_TRACES failed\n", __FUNCTION__);
		Cleanup();
		return false;
	}
#else
	LogPrintf(2, "%s: SVGA_REG caps = 0x%08x\n", __FUNCTION__, m_capabilities);
#endif
	m_fifo_size = ReadReg(SVGA_REG_MEM_SIZE);
	fb_size = ReadReg(SVGA_REG_FB_SIZE);
	vram_size = ReadReg(SVGA_REG_VRAM_SIZE);
	max_width = ReadReg(SVGA_REG_MAX_WIDTH);
	max_height = ReadReg(SVGA_REG_MAX_HEIGHT);
	host_bpp = ReadReg(SVGA_REG_HOST_BITS_PER_PIXEL);
	guest_bpp = ReadReg(SVGA_REG_BITS_PER_PIXEL);
	m_width = ReadReg(SVGA_REG_WIDTH);
	m_height = ReadReg(SVGA_REG_HEIGHT);
	m_pitch = ReadReg(SVGA_REG_BYTES_PER_LINE);
	m_max_width = ReadReg(SVGA_REG_MAX_WIDTH);
	m_max_height = ReadReg(SVGA_REG_MAX_HEIGHT);
	if (HasCapability(SVGA_CAP_GMR))
		m_max_gmrs = ReadReg(SVGA_REG_GMR_MAX_IDS);
	LogPrintf(3, "%s: SVGA max w, h=%u, %u : host_bpp=%u, bpp=%u\n", __FUNCTION__, max_width, max_height, host_bpp, guest_bpp);
	LogPrintf(3, "%s: SVGA VRAM size=%u FB size=%u, FIFO size=%u\n", __FUNCTION__, vram_size, fb_size, m_fifo_size);
	if (HasCapability(SVGA_CAP_TRACES))
		WriteReg(SVGA_REG_TRACES, 1);
	m_bounce_buffer = static_cast<UInt8*>(IOMalloc(BOUNCE_BUFFER_SIZE));
	if (!m_bounce_buffer) {
		Cleanup();
		return false;
	}
	m_cursor_ptr = 0;
	provider->setProperty("VMwareSVGACapabilities", static_cast<UInt64>(m_capabilities), 32U);
	return true;
}

bool SVGADevice::HasFIFOCap(UInt32 mask) const
{
	return (m_fifo_ptr[SVGA_FIFO_CAPABILITIES] & mask) != 0;
}

void SVGADevice::FIFOFull()
{
	WriteReg(SVGA_REG_SYNC, 1);
	ReadReg(SVGA_REG_BUSY);
}

bool SVGADevice::FIFOInit()
{
	UInt32 fifo_capabilities;

	LogPrintf(4, "%s: FIFO: min=%u, size=%u\n", __FUNCTION__,
			  static_cast<unsigned>(SVGA_FIFO_NUM_REGS * sizeof(UInt32)), m_fifo_size);
	if (!HasCapability(SVGA_CAP_EXTENDED_FIFO)) {
		LogPrintf(1, "%s: SVGA_CAP_EXTENDED_FIFO failed\n", __FUNCTION__);
		return false;
	}
	m_fifo_ptr[SVGA_FIFO_MIN] = static_cast<UInt32>(SVGA_FIFO_NUM_REGS * sizeof(UInt32));
	m_fifo_ptr[SVGA_FIFO_MAX] = m_fifo_size;
	m_fifo_ptr[SVGA_FIFO_NEXT_CMD] = m_fifo_ptr[SVGA_FIFO_MIN];
	m_fifo_ptr[SVGA_FIFO_STOP] = m_fifo_ptr[SVGA_FIFO_MIN];
	WriteReg(SVGA_REG_CONFIG_DONE, 1);
	fifo_capabilities = m_fifo_ptr[SVGA_FIFO_CAPABILITIES];
	m_provider->setProperty("VMwareSVGAFIFOCapabilities", static_cast<UInt64>(fifo_capabilities), 32U);
	if (!(fifo_capabilities & SVGA_FIFO_CAP_CURSOR_BYPASS_3)) {
		LogPrintf(1, "%s: SVGA_FIFO_CAP_CUSSOR_BYPASS_3 failed\n", __FUNCTION__);
		return false;
	}
	if (!(fifo_capabilities & SVGA_FIFO_CAP_RESERVE)) {
		LogPrintf(1, "%s: SVGA_FIFO_CAP_RESERVE failed\n", __FUNCTION__);
		return false;
	}
	m_reserved_size = 0;
	m_using_bounce_buffer = false;
	return true;
}

void* SVGADevice::FIFOReserve(size_t bytes)
{
	UInt32 volatile* fifo = m_fifo_ptr;
	UInt32 max = fifo[SVGA_FIFO_MAX];
	UInt32 min = fifo[SVGA_FIFO_MIN];
	UInt32 next_cmd = fifo[SVGA_FIFO_NEXT_CMD];
	bool reservable = HasFIFOCap(SVGA_FIFO_CAP_RESERVE);

	if (bytes > BOUNCE_BUFFER_SIZE ||
		bytes > (max - min)) {
		LogPrintf(1, "FIFO command too large %lu > %u or (%u - %u)\n",
			bytes, BOUNCE_BUFFER_SIZE, max, min);
		return 0;
	}
	if (bytes % sizeof(UInt32)) {
		LogPrintf(1, "FIFO command length not 32-bit aligned %lu\n", bytes);
		return 0;
	}
	if (m_reserved_size) {
		LogPrintf(1, "FIFOReserve before FIFOCommit, reservedSize=%lu\n", m_reserved_size);
		return 0;
	}
	m_reserved_size = bytes;
	while (true) {
		UInt32 stop = fifo[SVGA_FIFO_STOP];
		bool reserve_in_place = false;
		bool need_bounce = false;
		if (next_cmd >= stop) {
			if (next_cmd + bytes < max ||
				(next_cmd + bytes == max && stop > min)) {
				reserve_in_place = true;
			} else if ((max - next_cmd) + (stop - min) <= bytes) {
				FIFOFull();
			} else {
				need_bounce = true;
			}
		} else {
			if (next_cmd + bytes < stop) {
				reserve_in_place = true;
			} else {
				FIFOFull();
			}
		}
		if (reserve_in_place) {
			if (reservable || bytes <= sizeof(UInt32)) {
				m_using_bounce_buffer = false;
				if (reservable) {
					fifo[SVGA_FIFO_RESERVED] = static_cast<UInt32>(bytes);
				}
				return TO_BYTE_PTR(fifo) + next_cmd;
			} else {
				need_bounce = true;
			}
		}
		if (need_bounce) {
			m_using_bounce_buffer = true;
			return m_bounce_buffer;
		}
	}
}

void* SVGADevice::FIFOReserveCmd(UInt32 type, size_t bytes)
{
	UInt32* cmd = static_cast<UInt32*>(FIFOReserve(bytes + sizeof type));
	if (!cmd)
		return 0;
	*cmd++ = type;
	return cmd;
}

void SVGADevice::FIFOCommit(size_t bytes)
{
	UInt32 volatile* fifo = m_fifo_ptr;
	UInt32 next_cmd = fifo[SVGA_FIFO_NEXT_CMD];
	UInt32 max = fifo[SVGA_FIFO_MAX];
	UInt32 min = fifo[SVGA_FIFO_MIN];
	bool reservable = HasFIFOCap(SVGA_FIFO_CAP_RESERVE);

	if (bytes % sizeof(UInt32)) {
		LogPrintf(1, "FIFO command length not 32-bit aligned %lu\n", bytes);
		return;
	}
	if (!m_reserved_size) {
		LogPrintf(1, "FIFOCommit before FIFOReserve, reservedSize == 0\n");
		return;
	}
	m_reserved_size = 0;
	if (m_using_bounce_buffer) {
		UInt8* buffer = m_bounce_buffer;
		if (reservable) {
			UInt32 chunk_size = max - next_cmd;
			if (bytes < chunk_size)
				chunk_size = static_cast<UInt32>(bytes);
			fifo[SVGA_FIFO_RESERVED] = static_cast<UInt32>(bytes);
			memcpy(TO_BYTE_PTR(fifo) + next_cmd, buffer, chunk_size);
			memcpy(TO_BYTE_PTR(fifo) + min, buffer + chunk_size, bytes - chunk_size);
		} else {
			UInt32* dword = reinterpret_cast<UInt32*>(buffer);
			while (bytes) {
				fifo[next_cmd / static_cast<UInt32>(sizeof *dword)] = *dword++;
				next_cmd += static_cast<UInt32>(sizeof *dword);
				if (next_cmd == max)
					next_cmd = min;
				fifo[SVGA_FIFO_NEXT_CMD] = next_cmd;
				bytes -= sizeof *dword;
			}
		}
	}
	if (!m_using_bounce_buffer || reservable) {
		next_cmd += static_cast<UInt32>(bytes);
		if (next_cmd >= max)
			next_cmd -= (max - min);
		fifo[SVGA_FIFO_NEXT_CMD] = next_cmd;
	}
	if (reservable)
		fifo[SVGA_FIFO_RESERVED] = 0;
}

void SVGADevice::FIFOCommitAll()
{
	LogPrintf(5, "%s: reservedSize=%lu\n", __FUNCTION__, m_reserved_size);
	FIFOCommit(m_reserved_size);
}

UInt32 SVGADevice::InsertFence()
{
	UInt32 fence;
	UInt32* cmd;

	if (!HasFIFOCap(SVGA_FIFO_CAP_FENCE))
		return 1;
	if (!m_next_fence)
		m_next_fence = 1;
	fence = m_next_fence++;
	cmd = static_cast<UInt32*>(FIFOReserve(2U * sizeof(UInt32)));
	if (!cmd)
		return 0;
	*cmd = SVGA_CMD_FENCE;
	cmd[1] = fence;
	FIFOCommitAll();
	return fence;
}

bool SVGADevice::HasFencePassed(UInt32 fence) const
{
	if (!fence)
		return true;
	if (!HasFIFOCap(SVGA_FIFO_CAP_FENCE))
		return false;
	return HasFencePassedUnguarded(m_fifo_ptr, fence);
}

void SVGADevice::SyncToFence(UInt32 fence)
{
	UInt32 volatile* fifo = m_fifo_ptr;

	if (!fence)
		return;
	if (!HasFIFOCap(SVGA_FIFO_CAP_FENCE)) {
		WriteReg(SVGA_REG_SYNC, 1);
		while (ReadReg(SVGA_REG_BUSY)) ;
		return;
	}
	if (HasFencePassedUnguarded(fifo, fence))
		return;
	WriteReg(SVGA_REG_SYNC, 1);
	while (!HasFencePassedUnguarded(fifo, fence)) {
		if (ReadReg(SVGA_REG_BUSY))
			continue;
		if (!HasFencePassedUnguarded(fifo, fence))
			LogPrintf(1, "%s: SyncToFence failed!\n", __FUNCTION__);
		break;
	}
}

void SVGADevice::SyncFIFO()
{
	/*
	 * Crude, but effective
	 */
	WriteReg(SVGA_REG_SYNC, 1);
	while (ReadReg(SVGA_REG_BUSY));
}

void SVGADevice::setCursorState(UInt32 x, UInt32 y, bool visible)
{
	if (checkOptionFB(VMW_OPTION_FB_CURSOR_BYPASS_2)) {	// Added
		// CURSOR_BYPASS_2
		WriteReg(SVGA_REG_CURSOR_ID, 1U);
		WriteReg(SVGA_REG_CURSOR_X, x);
		WriteReg(SVGA_REG_CURSOR_Y, y);
		WriteReg(SVGA_REG_CURSOR_ON, visible ? 1U : 0U);
		return;
	}
	// CURSOR_BYPASS_3
	m_fifo_ptr[SVGA_FIFO_CURSOR_ON] = visible ? 1U : 0U;
	m_fifo_ptr[SVGA_FIFO_CURSOR_X] = x;
	m_fifo_ptr[SVGA_FIFO_CURSOR_Y] = y;
	++m_fifo_ptr[SVGA_FIFO_CURSOR_COUNT];
}

void* SVGADevice::BeginDefineAlphaCursor(UInt32 width, UInt32 height, UInt32 bytespp)
{
	size_t cmd_len;
	SVGAFifoCmdDefineAlphaCursor* cmd;

	LogPrintf(4, "%s: %ux%u @ %u\n", __FUNCTION__, width, height, bytespp);
	cmd_len = sizeof *cmd + width * height * bytespp;
	cmd = static_cast<SVGAFifoCmdDefineAlphaCursor*>(FIFOReserveCmd(SVGA_CMD_DEFINE_ALPHA_CURSOR, cmd_len));
	if (!cmd)
		return 0;
	m_cursor_ptr = cmd;
	LogPrintf(5, "%s: cmdLen=%lu cmd=%p fifo=%p\n",
		__FUNCTION__, cmd_len, cmd, cmd + 1);
	return cmd + 1;
}

bool SVGADevice::EndDefineAlphaCursor(UInt32 width, UInt32 height, UInt32 bytespp, UInt32 hotspot_x, UInt32 hotspot_y)
{
	size_t cmd_len;
	SVGAFifoCmdDefineAlphaCursor* cmd = static_cast<SVGAFifoCmdDefineAlphaCursor*>(m_cursor_ptr);

	LogPrintf(4, "%s: %ux%u+%u+%u @ %u\n",
		__FUNCTION__, width, height, hotspot_x, hotspot_y, bytespp);
	if (!cmd)
		return false;
	cmd->id = 1;
	cmd->hotspotX = hotspot_x;
	cmd->hotspotY = hotspot_y;
	cmd->width = width;
	cmd->height = height;
	cmd_len = sizeof(UInt32) + sizeof *cmd + width * height * bytespp;
	LogPrintf(5, "%s: cmdLen=%lu cmd=%p\n", __FUNCTION__, cmd_len, cmd);
	FIFOCommit(cmd_len);
	m_cursor_ptr = 0;
	return true;
}

void SVGADevice::SetMode(UInt32 width, UInt32 height, UInt32 bpp)
{
	LogPrintf(4, "%s: mode w,h=%u, %u bpp=%u\n", __FUNCTION__, width, height, bpp);
	SyncFIFO();
	WriteReg(SVGA_REG_WIDTH, width);
	WriteReg(SVGA_REG_HEIGHT, height);
	WriteReg(SVGA_REG_BITS_PER_PIXEL, bpp);
	WriteReg(SVGA_REG_ENABLE, 1);
	if (checkOptionFB(VMW_OPTION_FB_LINUX))	// Added
		WriteReg(SVGA_REG_GUEST_ID, GUEST_OS_LINUX);
	m_pitch = ReadReg(SVGA_REG_BYTES_PER_LINE);
	LogPrintf(4, "%s: pitch=%u\n", __FUNCTION__, m_pitch);
	m_width = width;
	m_height = height;
}

bool SVGADevice::UpdateFramebuffer(UInt32 x, UInt32 y, UInt32 width, UInt32 height)
{
	SVGAFifoCmdUpdate* cmd = static_cast<SVGAFifoCmdUpdate*>(FIFOReserveCmd(SVGA_CMD_UPDATE, sizeof *cmd));
	if (!cmd)
		return false;
	LogPrintf(5, "%s: xy=%u %u, wh=%u %u\n", __FUNCTION__, x, y, width, height);
	cmd->x = x;
	cmd->y = y;
	cmd->width = width;
	cmd->height = height;
	FIFOCommitAll();
	return true;
}

void SVGADevice::Disable()
{
	WriteReg(SVGA_REG_ENABLE, 0);
}

bool SVGADevice::IsFIFORegValid(UInt32 reg) const
{
	return m_fifo_ptr[SVGA_FIFO_MIN] > (reg << 2);
}

void* SVGADevice::FIFOReserveEscape(UInt32 nsid, size_t bytes)
{
	size_t padded_bytes = (bytes + 3UL) & ~3UL;
	UInt32* header = static_cast<UInt32*>(FIFOReserve(padded_bytes + 3U * sizeof(UInt32)));
	if (!header)
		return 0;
	*header = SVGA_CMD_ESCAPE;
	header[1] = nsid;
	header[2] = static_cast<UInt32>(bytes);
	return header + 3;
}

void SVGADevice::RingDoorBell()
{
	if (IsFIFORegValid(SVGA_FIFO_BUSY)) {
		if (!m_fifo_ptr[SVGA_FIFO_BUSY]) {
			m_fifo_ptr[SVGA_FIFO_BUSY] = 1;
			WriteReg(SVGA_REG_SYNC, 1);
		}
	} else {
		WriteReg(SVGA_REG_SYNC, 1);
	}
}

bool SVGADevice::BeginVideoSetRegs(UInt32 streamId, size_t numItems, struct SVGAEscapeVideoSetRegs **setRegs)
{
	SVGAEscapeVideoSetRegs* cmd;
	size_t cmd_size = sizeof *cmd - sizeof cmd->items + numItems * sizeof cmd->items[0];
	cmd = static_cast<SVGAEscapeVideoSetRegs*>(FIFOReserveEscape(SVGA_ESCAPE_NSID_VMWARE, cmd_size));
	if (!cmd)
		return false;
	cmd->header.cmdType = SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS;
	cmd->header.streamId = streamId;
	*setRegs = cmd;
	return true;
}

bool SVGADevice::VideoSetRegsInRange(UInt32 streamId, struct SVGAOverlayUnit const* regs, UInt32 minReg, UInt32 maxReg)
{
	UInt32 const* regArray = reinterpret_cast<UInt32 const*>(regs);
	UInt32 const numRegs = maxReg - minReg + 1;
	SVGAEscapeVideoSetRegs *setRegs;
	UInt32 i;

	if (minReg > maxReg)
		return true;

	if (!BeginVideoSetRegs(streamId, numRegs, &setRegs))
		return false;

	for (i = 0; i < numRegs; i++) {
		setRegs->items[i].registerId = i + minReg;
		setRegs->items[i].value = regArray[i + minReg];
	}

	FIFOCommitAll();
	return true;
}

bool SVGADevice::VideoSetRegsWithMash(UInt32 streamId, struct SVGAOverlayUnit const* regs, UInt32 regMask)
{
	UInt32 const* regArray = reinterpret_cast<UInt32 const*>(regs);
	UInt32 i, numRegs;
	SVGAEscapeVideoSetRegs* setRegs;

	if (!regMask)
		return true;
	numRegs = count_bits(regMask);
	if (!BeginVideoSetRegs(streamId, numRegs, &setRegs))
		return false;
	for (numRegs = i = 0; regMask; (++i), (regMask >>= 1))
		if (regMask & 1U) {
			setRegs->items[numRegs].registerId = i;
			setRegs->items[numRegs].value = regArray[i];
			++numRegs;
		}

	FIFOCommitAll();
	return true;
}

bool SVGADevice::VideoSetReg(UInt32 streamId, UInt32 registerId, UInt32 value)
{
	SVGAEscapeVideoSetRegs* setRegs;

	if (!BeginVideoSetRegs(streamId, 1, &setRegs))
		return false;
	setRegs->items[0].registerId = registerId;
	setRegs->items[0].value = value;
	FIFOCommitAll();
	return true;
}

bool SVGADevice::VideoFlush(UInt32 streamId)
{
	SVGAEscapeVideoFlush* cmd;

	cmd = static_cast<SVGAEscapeVideoFlush*>(FIFOReserveEscape(SVGA_ESCAPE_NSID_VMWARE, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cmdType = SVGA_ESCAPE_VMWARE_VIDEO_FLUSH;
	cmd->streamId = streamId;
	FIFOCommitAll();
	return true;
}

void SVGADevice::RegDump()
{
	UInt32 regs[SVGA_REG_TOP];

	for (UInt32 i = SVGA_REG_ID; i < SVGA_REG_TOP; ++i)
		regs[i] = ReadReg(i);
	m_provider->setProperty("VMwareSVGADump", static_cast<void*>(&regs[0]), static_cast<unsigned>(sizeof regs));
}

bool SVGADevice::RectCopy(UInt32 const* copyRect)
{
	SVGAFifoCmdRectCopy* cmd = static_cast<SVGAFifoCmdRectCopy*>(FIFOReserveCmd(SVGA_CMD_RECT_COPY, sizeof *cmd));
	if (!cmd)
		return false;
	memcpy(&cmd->srcX, copyRect, 6U * sizeof(UInt32));
	FIFOCommitAll();
	return true;
}

bool SVGADevice::RectFill(UInt32 color, UInt32 const* rect)
{
	SVGAFifoCmdFrontRopFill* cmd = static_cast<SVGAFifoCmdFrontRopFill*>(FIFOReserveCmd(SVGA_CMD_FRONT_ROP_FILL, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->color = color;
	memcpy(&cmd->x, rect, 4U * sizeof(UInt32));
	cmd->rop = SVGA_ROP_COPY;
	FIFOCommitAll();
	return true;
}

bool SVGADevice::UpdateFramebuffer2(UInt32 const* rect)
{
	SVGAFifoCmdUpdate* cmd = static_cast<SVGAFifoCmdUpdate*>(FIFOReserveCmd(SVGA_CMD_UPDATE, sizeof *cmd));
	if (!cmd)
		return false;
	memcpy(&cmd->x, rect, 4U * sizeof(UInt32));
	FIFOCommitAll();
	return true;
}

#ifdef TESTING
void SVGADevice::test_ram_size(char const* name, IOVirtualAddress ptr, IOByteCount count)
{
	IOVirtualAddress a;
	for (a = ptr; a < ptr + count; a += PAGE_SIZE)
	{
		UInt32 volatile* p = reinterpret_cast<UInt32 volatile*>(a);
		*p = 0x55AA55AAU;
		if (*p != 0x55AA55AAU)
			break;
		*p = 0xAA55AA55U;
		if (*p != 0xAA55AA55U)
			break;
	}
	IOLog("%s: test_ram_size(%p, %u), result %u\n", name, reinterpret_cast<void*>(ptr), count, a - ptr);
}
#endif
