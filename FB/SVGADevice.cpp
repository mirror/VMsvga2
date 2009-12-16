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
#include "SVGADevice.h"
#include "common_fb.h"
#include "vmw_options_fb.h"
#include "VLog.h"

#include "svga_apple_header.h"
#include "svga_reg.h"
#include "svga_overlay.h"
#include "svga_escape.h"
#include "svga_apple_footer.h"

#define BOUNCE_BUFFER_SIZE 0x10000U

#ifdef REQUIRE_TRACING
#warning Building for Fusion Host/Mac OS X Server Guest
#endif

#if LOGGING_LEVEL >= 1
#define LogPrintf(log_level, fmt, ...) do { if (log_level <= logLevelFB) VLog("SVGADev: ", fmt, ##__VA_ARGS__); } while (false)
#else
#define LogPrintf(log_level, fmt, ...)
#endif

#define TO_BYTE_PTR(x) reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(x))
#define HasFencePassedUnguarded(fifo, fence) (static_cast<int32_t>(fifo[SVGA_FIFO_FENCE] - fence) >= 0)

OS_INLINE uint32_t count_bits(uint32_t mask)
{
	mask = ((mask & 0xAAAAAAAAU) >> 1) + (mask & 0x55555555U);
	mask = ((mask & 0xCCCCCCCCU) >> 2) + (mask & 0x33333333U);
	mask = ((mask & 0xF0F0F0F0U) >> 4) + (mask & 0x0F0F0F0FU);
	mask = ((mask & 0xFF00FF00U) >> 8) + (mask & 0x00FF00FFU);
	return ((mask & 0xFFFF0000U) >> 16) + (mask & 0x0000FFFFU);
}

void SVGADevice::Init()
{
	LogPrintf(2, "%s()\n", __FUNCTION__);
	m_provider = 0;
	m_bar0 = 0;
	m_bar2 = 0;
	m_fifo_ptr = 0;
	m_cursor_ptr = 0;
	m_bounce_buffer = 0;
	m_next_fence = 1;
	m_capabilities = 0;
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

uint32_t SVGADevice::ReadReg(uint32_t index)
{
	m_provider->ioWrite32(SVGA_INDEX_PORT, index, m_bar0);
	return m_provider->ioRead32(SVGA_VALUE_PORT, m_bar0);
}

void SVGADevice::WriteReg(uint32_t index, uint32_t value)
{
	m_provider->ioWrite32(SVGA_INDEX_PORT, index, m_bar0);
	m_provider->ioWrite32(SVGA_VALUE_PORT, value, m_bar0);
}

bool SVGADevice::Start(IOPCIDevice* provider)
{
	uint32_t host_bpp, guest_bpp;

	LogPrintf(2, "%s(%p)", __FUNCTION__, provider);
	m_provider = provider;
	m_bar0 = m_provider->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
	if (!m_bar0) {
		LogPrintf(1, "%s: Failed to map the I/O registers.\n", __FUNCTION__);
		Cleanup();
		return false;
	}
#if 0
	bar1 = m_provider->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress1);
	if (!bar1) {
		LogPrintf(1, "%s: Failed to retrieve the VRAM memory descriptor.\n", __FUNCTION__);
		Cleanup();
		return false;
	}
#endif
	m_bar2 = m_provider->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
	if (!m_bar2) {
		LogPrintf(1, "%s: Failed to map the FIFO.\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_fifo_ptr = reinterpret_cast<uint32_t*>(m_bar2->getVirtualAddress());
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
	m_fb_offset = ReadReg(SVGA_REG_FB_OFFSET);
	m_fb_size = ReadReg(SVGA_REG_FB_SIZE);
	m_vram_size = ReadReg(SVGA_REG_VRAM_SIZE);
	m_max_width = ReadReg(SVGA_REG_MAX_WIDTH);
	m_max_height = ReadReg(SVGA_REG_MAX_HEIGHT);
	host_bpp = ReadReg(SVGA_REG_HOST_BITS_PER_PIXEL);
	guest_bpp = ReadReg(SVGA_REG_BITS_PER_PIXEL);
	m_width = ReadReg(SVGA_REG_WIDTH);
	m_height = ReadReg(SVGA_REG_HEIGHT);
	m_pitch = ReadReg(SVGA_REG_BYTES_PER_LINE);
	if (HasCapability(SVGA_CAP_GMR)) {
		m_max_gmr_ids = ReadReg(SVGA_REG_GMR_MAX_IDS);
		m_max_gmr_descriptor_length = ReadReg(SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH);
	}
	LogPrintf(3, "%s: SVGA max w, h=%u, %u : host_bpp=%u, bpp=%u\n", __FUNCTION__, m_max_width, m_max_height, host_bpp, guest_bpp);
	LogPrintf(3, "%s: SVGA VRAM size=%u FB size=%u, FIFO size=%u\n", __FUNCTION__, m_vram_size, m_fb_size, m_fifo_size);
	if (HasCapability(SVGA_CAP_GMR))
		LogPrintf(3, "%s: SVGA max GMR IDs == %u, max GMR descriptor length == %u\n", __FUNCTION__, m_max_gmr_ids, m_max_gmr_descriptor_length);
	if (HasCapability(SVGA_CAP_TRACES))
		WriteReg(SVGA_REG_TRACES, 1);
	m_bounce_buffer = static_cast<uint8_t*>(IOMalloc(BOUNCE_BUFFER_SIZE));
	if (!m_bounce_buffer) {
		Cleanup();
		return false;
	}
	m_cursor_ptr = 0;
	provider->setProperty("VMwareSVGACapabilities", static_cast<uint64_t>(m_capabilities), 32U);
	return true;
}

bool SVGADevice::HasFIFOCap(uint32_t mask) const
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
	uint32_t fifo_capabilities;

	LogPrintf(2, "%s: FIFO: min=%u, size=%u\n", __FUNCTION__,
			  static_cast<unsigned>(SVGA_FIFO_NUM_REGS * sizeof(uint32_t)), m_fifo_size);
	if (!HasCapability(SVGA_CAP_EXTENDED_FIFO)) {
		LogPrintf(1, "%s: SVGA_CAP_EXTENDED_FIFO failed\n", __FUNCTION__);
		return false;
	}
	m_fifo_ptr[SVGA_FIFO_MIN] = static_cast<uint32_t>(SVGA_FIFO_NUM_REGS * sizeof(uint32_t));
	m_fifo_ptr[SVGA_FIFO_MAX] = m_fifo_size;
	m_fifo_ptr[SVGA_FIFO_NEXT_CMD] = m_fifo_ptr[SVGA_FIFO_MIN];
	m_fifo_ptr[SVGA_FIFO_STOP] = m_fifo_ptr[SVGA_FIFO_MIN];
	WriteReg(SVGA_REG_CONFIG_DONE, 1);
	fifo_capabilities = m_fifo_ptr[SVGA_FIFO_CAPABILITIES];
	m_provider->setProperty("VMwareSVGAFIFOCapabilities", static_cast<uint64_t>(fifo_capabilities), 32U);
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
	uint32_t volatile* fifo = m_fifo_ptr;
	uint32_t max = fifo[SVGA_FIFO_MAX];
	uint32_t min = fifo[SVGA_FIFO_MIN];
	uint32_t next_cmd = fifo[SVGA_FIFO_NEXT_CMD];
	bool reservable = HasFIFOCap(SVGA_FIFO_CAP_RESERVE);

	if (bytes > BOUNCE_BUFFER_SIZE ||
		bytes > (max - min)) {
		LogPrintf(1, "FIFO command too large %lu > %u or (%u - %u)\n",
			bytes, BOUNCE_BUFFER_SIZE, max, min);
		return 0;
	}
	if (bytes % sizeof(uint32_t)) {
		LogPrintf(1, "FIFO command length not 32-bit aligned %lu\n", bytes);
		return 0;
	}
	if (m_reserved_size) {
		LogPrintf(1, "FIFOReserve before FIFOCommit, reservedSize=%lu\n", m_reserved_size);
		return 0;
	}
	m_reserved_size = bytes;
	while (true) {
		uint32_t stop = fifo[SVGA_FIFO_STOP];
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
			if (reservable || bytes <= sizeof(uint32_t)) {
				m_using_bounce_buffer = false;
				if (reservable) {
					fifo[SVGA_FIFO_RESERVED] = static_cast<uint32_t>(bytes);
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

void* SVGADevice::FIFOReserveCmd(uint32_t type, size_t bytes)
{
	uint32_t* cmd = static_cast<uint32_t*>(FIFOReserve(bytes + sizeof type));
	if (!cmd)
		return 0;
	*cmd++ = type;
	return cmd;
}

void SVGADevice::FIFOCommit(size_t bytes)
{
	uint32_t volatile* fifo = m_fifo_ptr;
	uint32_t next_cmd = fifo[SVGA_FIFO_NEXT_CMD];
	uint32_t max = fifo[SVGA_FIFO_MAX];
	uint32_t min = fifo[SVGA_FIFO_MIN];
	bool reservable = HasFIFOCap(SVGA_FIFO_CAP_RESERVE);

	if (bytes % sizeof(uint32_t)) {
		LogPrintf(1, "FIFO command length not 32-bit aligned %lu\n", bytes);
		return;
	}
	if (!m_reserved_size) {
		LogPrintf(1, "FIFOCommit before FIFOReserve, reservedSize == 0\n");
		return;
	}
	m_reserved_size = 0;
	if (m_using_bounce_buffer) {
		uint8_t* buffer = m_bounce_buffer;
		if (reservable) {
			uint32_t chunk_size = max - next_cmd;
			if (bytes < chunk_size)
				chunk_size = static_cast<uint32_t>(bytes);
			fifo[SVGA_FIFO_RESERVED] = static_cast<uint32_t>(bytes);
			memcpy(TO_BYTE_PTR(fifo) + next_cmd, buffer, chunk_size);
			memcpy(TO_BYTE_PTR(fifo) + min, buffer + chunk_size, bytes - chunk_size);
		} else {
			uint32_t* dword = reinterpret_cast<uint32_t*>(buffer);
			while (bytes) {
				fifo[next_cmd / static_cast<uint32_t>(sizeof *dword)] = *dword++;
				next_cmd += static_cast<uint32_t>(sizeof *dword);
				if (next_cmd == max)
					next_cmd = min;
				fifo[SVGA_FIFO_NEXT_CMD] = next_cmd;
				bytes -= sizeof *dword;
			}
		}
	}
	if (!m_using_bounce_buffer || reservable) {
		next_cmd += static_cast<uint32_t>(bytes);
		if (next_cmd >= max)
			next_cmd -= (max - min);
		fifo[SVGA_FIFO_NEXT_CMD] = next_cmd;
	}
	if (reservable)
		fifo[SVGA_FIFO_RESERVED] = 0;
}

void SVGADevice::FIFOCommitAll()
{
	LogPrintf(2, "%s: reservedSize=%lu\n", __FUNCTION__, m_reserved_size);
	FIFOCommit(m_reserved_size);
}

uint32_t SVGADevice::InsertFence()
{
	uint32_t fence;
	uint32_t* cmd;

	if (!HasFIFOCap(SVGA_FIFO_CAP_FENCE))
		return 1;
	if (!m_next_fence)
		m_next_fence = 1;
	fence = m_next_fence++;
	cmd = static_cast<uint32_t*>(FIFOReserve(2U * sizeof(uint32_t)));
	if (!cmd)
		return 0;
	*cmd = SVGA_CMD_FENCE;
	cmd[1] = fence;
	FIFOCommitAll();
	return fence;
}

bool SVGADevice::HasFencePassed(uint32_t fence) const
{
	if (!fence)
		return true;
	if (!HasFIFOCap(SVGA_FIFO_CAP_FENCE))
		return false;
	return HasFencePassedUnguarded(m_fifo_ptr, fence);
}

void SVGADevice::SyncToFence(uint32_t fence)
{
	uint32_t volatile* fifo = m_fifo_ptr;

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

void SVGADevice::setCursorState(uint32_t x, uint32_t y, bool visible)
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

void SVGADevice::setCursorState(uint32_t screenId, uint32_t x, uint32_t y, bool visible)
{
	if (HasFIFOCap(SVGA_FIFO_CAP_SCREEN_OBJECT))
		m_fifo_ptr[SVGA_FIFO_CURSOR_SCREEN_ID] = screenId;
	// CURSOR_BYPASS_3
	m_fifo_ptr[SVGA_FIFO_CURSOR_ON] = visible ? 1U : 0U;
	m_fifo_ptr[SVGA_FIFO_CURSOR_X] = x;
	m_fifo_ptr[SVGA_FIFO_CURSOR_Y] = y;
	++m_fifo_ptr[SVGA_FIFO_CURSOR_COUNT];
}

void* SVGADevice::BeginDefineAlphaCursor(uint32_t width, uint32_t height, uint32_t bytespp)
{
	size_t cmd_len;
	SVGAFifoCmdDefineAlphaCursor* cmd;

	LogPrintf(2, "%s: %ux%u @ %u\n", __FUNCTION__, width, height, bytespp);
	cmd_len = sizeof *cmd + width * height * bytespp;
	cmd = static_cast<SVGAFifoCmdDefineAlphaCursor*>(FIFOReserveCmd(SVGA_CMD_DEFINE_ALPHA_CURSOR, cmd_len));
	if (!cmd)
		return 0;
	m_cursor_ptr = cmd;
	LogPrintf(2, "%s: cmdLen=%lu cmd=%p fifo=%p\n",
		__FUNCTION__, cmd_len, cmd, cmd + 1);
	return cmd + 1;
}

bool SVGADevice::EndDefineAlphaCursor(uint32_t width, uint32_t height, uint32_t bytespp, uint32_t hotspot_x, uint32_t hotspot_y)
{
	size_t cmd_len;
	SVGAFifoCmdDefineAlphaCursor* cmd = static_cast<SVGAFifoCmdDefineAlphaCursor*>(m_cursor_ptr);

	LogPrintf(2, "%s: %ux%u+%u+%u @ %u\n",
		__FUNCTION__, width, height, hotspot_x, hotspot_y, bytespp);
	if (!cmd)
		return false;
	cmd->id = 1;
	cmd->hotspotX = hotspot_x;
	cmd->hotspotY = hotspot_y;
	cmd->width = width;
	cmd->height = height;
	cmd_len = sizeof(uint32_t) + sizeof *cmd + width * height * bytespp;
	LogPrintf(3, "%s: cmdLen=%lu cmd=%p\n", __FUNCTION__, cmd_len, cmd);
	FIFOCommit(cmd_len);
	m_cursor_ptr = 0;
	return true;
}

void SVGADevice::SetMode(uint32_t width, uint32_t height, uint32_t bpp)
{
	LogPrintf(2, "%s: mode w,h=%u, %u bpp=%u\n", __FUNCTION__, width, height, bpp);
	SyncFIFO();
	WriteReg(SVGA_REG_WIDTH, width);
	WriteReg(SVGA_REG_HEIGHT, height);
	WriteReg(SVGA_REG_BITS_PER_PIXEL, bpp);
	WriteReg(SVGA_REG_ENABLE, 1);
	if (checkOptionFB(VMW_OPTION_FB_LINUX))	// Added
		WriteReg(SVGA_REG_GUEST_ID, GUEST_OS_LINUX);
	m_pitch = ReadReg(SVGA_REG_BYTES_PER_LINE);
	LogPrintf(2, "%s: pitch=%u\n", __FUNCTION__, m_pitch);
	m_fb_size = ReadReg(SVGA_REG_FB_SIZE);
	m_width = width;
	m_height = height;
}

bool SVGADevice::UpdateFramebuffer(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	LogPrintf(2, "%s: xy=%u %u, wh=%u %u\n", __FUNCTION__, x, y, width, height);
	SVGAFifoCmdUpdate* cmd = static_cast<SVGAFifoCmdUpdate*>(FIFOReserveCmd(SVGA_CMD_UPDATE, sizeof *cmd));
	if (!cmd)
		return false;
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

bool SVGADevice::IsFIFORegValid(uint32_t reg) const
{
	return m_fifo_ptr[SVGA_FIFO_MIN] > (reg << 2);
}

void* SVGADevice::FIFOReserveEscape(uint32_t nsid, size_t bytes)
{
	size_t padded_bytes = (bytes + 3UL) & ~3UL;
	uint32_t* header = static_cast<uint32_t*>(FIFOReserve(padded_bytes + 3U * sizeof(uint32_t)));
	if (!header)
		return 0;
	*header = SVGA_CMD_ESCAPE;
	header[1] = nsid;
	header[2] = static_cast<uint32_t>(bytes);
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

bool SVGADevice::BeginVideoSetRegs(uint32_t streamId, size_t numItems, struct SVGAEscapeVideoSetRegs **setRegs)
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

bool SVGADevice::VideoSetRegsInRange(uint32_t streamId, struct SVGAOverlayUnit const* regs, uint32_t minReg, uint32_t maxReg)
{
	uint32_t const* regArray = reinterpret_cast<uint32_t const*>(regs);
	uint32_t const numRegs = maxReg - minReg + 1;
	SVGAEscapeVideoSetRegs *setRegs;
	uint32_t i;

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

bool SVGADevice::VideoSetRegsWithMask(uint32_t streamId, struct SVGAOverlayUnit const* regs, uint32_t regMask)
{
	uint32_t const* regArray = reinterpret_cast<uint32_t const*>(regs);
	uint32_t i, numRegs;
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

bool SVGADevice::VideoSetReg(uint32_t streamId, uint32_t registerId, uint32_t value)
{
	SVGAEscapeVideoSetRegs* setRegs;

	if (!BeginVideoSetRegs(streamId, 1, &setRegs))
		return false;
	setRegs->items[0].registerId = registerId;
	setRegs->items[0].value = value;
	FIFOCommitAll();
	return true;
}

bool SVGADevice::VideoFlush(uint32_t streamId)
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

bool SVGADevice::get3DHWVersion(UInt32* HWVersion)
{
	if (!HWVersion)
		return false;
	if (m_fifo_ptr[SVGA_FIFO_MIN] <= static_cast<uint32_t>(sizeof(uint32_t) * SVGA_FIFO_GUEST_3D_HWVERSION))
		return false;
	*HWVersion = m_fifo_ptr[SVGA_FIFO_3D_HWVERSION];
	return true;
}

void SVGADevice::RegDump()
{
	uint32_t regs[SVGA_REG_TOP];

	for (uint32_t i = SVGA_REG_ID; i < SVGA_REG_TOP; ++i)
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
		uint32_t volatile* p = reinterpret_cast<uint32_t volatile*>(a);
		*p = 0x55AA55AAU;
		if (*p != 0x55AA55AAU)
			break;
		*p = 0xAA55AA55U;
		if (*p != 0xAA55AA55U)
			break;
	}
	IOLog("%s: test_ram_size(%p, %lu), result %lu\n", name, reinterpret_cast<void*>(ptr), count, a - ptr);
}
#endif
