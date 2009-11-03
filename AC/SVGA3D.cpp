/*
 *  SVGA3D.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 11th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2007-2009 VMware, Inc.  All rights reserved.
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

#include <IOKit/IOLib.h>
#include "SVGA3D.h"
#include "SVGADevice.h"

#if LOGGING_LEVEL >= 1
#define SLog(fmt, ...) IOLog(fmt, ##__VA_ARGS__)
#else
#define SLog(fmt, ...)
#endif

#define CLASS SVGA3D

bool CLASS::Init(SVGADevice* device)
{
	UInt32* fifo_ptr;

	HWVersion = 0;
	if (!device) {
		m_svga = 0;
		return false;
	}
	m_svga = device;
	if (!device->HasCapability(SVGA_CAP_EXTENDED_FIFO)) {
		SLog("%s: SVGA3D requires the Extended FIFO capability.\n", __FUNCTION__);
		return false;
	}
	fifo_ptr = device->getFifoPtr();
	if (fifo_ptr[SVGA_FIFO_MIN] <= static_cast<UInt32>(sizeof(UInt32) * SVGA_FIFO_GUEST_3D_HWVERSION)) {
		SLog("%s: SVGA3D: GUEST_3D_HWVERSION register not present.\n", __FUNCTION__);
		return false;
	}

	/*
	 * Check the host's version, make sure we're binary compatible.
	 */

	HWVersion = fifo_ptr[SVGA_FIFO_3D_HWVERSION];
	if (HWVersion == 0) {
		SLog("%s: SVGA3D: 3D disabled by host.\n", __FUNCTION__);
		return false;
	}
	if (HWVersion < SVGA3D_HWVERSION_WS65_B1) {
		SLog("%s: SVGA3D: Host SVGA3D protocol is too old, not binary compatible.\n", __FUNCTION__);
		return false;
	}
	return true;
}

void* CLASS::FIFOReserve(UInt32 cmd, size_t cmdSize)
{
	SVGA3dCmdHeader* header;

	header = static_cast<SVGA3dCmdHeader*>(m_svga->FIFOReserve(sizeof *header + cmdSize));
	if (!header)
		return 0;
	header->id = cmd;
	header->size = static_cast<UInt32>(cmdSize);

	return &header[1];
}

bool CLASS::BeginDefineSurface(UInt32 sid,                  // IN
							   SVGA3dSurfaceFlags flags,    // IN
							   SVGA3dSurfaceFormat format,  // IN
							   SVGA3dSurfaceFace **faces,   // OUT
							   SVGA3dSize **mipSizes,       // OUT
							   size_t numMipSizes)          // IN
{
	SVGA3dCmdDefineSurface* cmd;

	cmd = static_cast<SVGA3dCmdDefineSurface*>(FIFOReserve(SVGA_3D_CMD_SURFACE_DEFINE, sizeof *cmd + sizeof **mipSizes * numMipSizes));
	if (!cmd)
		return false;

	cmd->sid = sid;
	cmd->surfaceFlags = flags;
	cmd->format = format;

	*faces = &cmd->face[0];
	*mipSizes = reinterpret_cast<SVGA3dSize*>(&cmd[1]);

	bzero(*faces, sizeof **faces * SVGA3D_MAX_SURFACE_FACES);
	bzero(*mipSizes, sizeof **mipSizes * numMipSizes);
	return true;
}

bool CLASS::DestroySurface(UInt32 sid)  // IN
{
	SVGA3dCmdDestroySurface* cmd;
	cmd = static_cast<SVGA3dCmdDestroySurface*>(FIFOReserve(SVGA_3D_CMD_SURFACE_DESTROY, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->sid = sid;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::BeginSurfaceDMA(SVGA3dGuestImage const* guestImage,     // IN
							SVGA3dSurfaceImageId const* hostImage,  // IN
							SVGA3dTransferType transfer,      // IN
							SVGA3dCopyBox **boxes,            // OUT
							size_t numBoxes)                  // IN
{
	SVGA3dCmdSurfaceDMA* cmd;
	size_t boxesSize = sizeof **boxes * numBoxes;

	cmd = static_cast<SVGA3dCmdSurfaceDMA*>(FIFOReserve(SVGA_3D_CMD_SURFACE_DMA, sizeof *cmd + boxesSize));
	if (!cmd)
		return false;

	cmd->guest = *guestImage;
	cmd->host = *hostImage;
	cmd->transfer = transfer;
	*boxes = reinterpret_cast<SVGA3dCopyBox*>(&cmd[1]);

	bzero(*boxes, boxesSize);
	return true;
}

bool CLASS::BeginSurfaceDMAwithSuffix(SVGA3dGuestImage const *guestImage,
									  SVGA3dSurfaceImageId const *hostImage,
									  SVGA3dTransferType transfer,
									  SVGA3dCopyBox **boxes,
									  size_t numBoxes,
									  UInt32 maximumOffset,
									  SVGA3dSurfaceDMAFlags flags)
{
	SVGA3dCmdSurfaceDMA* cmd;
	SVGA3dCmdSurfaceDMASuffix* suffix;
	size_t boxesSize = sizeof **boxes * numBoxes;

	cmd = static_cast<SVGA3dCmdSurfaceDMA*>(FIFOReserve(SVGA_3D_CMD_SURFACE_DMA,
														sizeof *cmd + boxesSize + sizeof *suffix));
	if (!cmd)
		return false;

	cmd->guest = *guestImage;
	cmd->host = *hostImage;
	cmd->transfer = transfer;
	*boxes = reinterpret_cast<SVGA3dCopyBox*>(&cmd[1]);

	bzero(*boxes, boxesSize);
	suffix = reinterpret_cast<SVGA3dCmdSurfaceDMASuffix*>(&(*boxes)[numBoxes]);
	suffix->suffixSize = static_cast<UInt32>(sizeof *suffix);
	suffix->maximumOffset = maximumOffset,
	suffix->flags = flags;
	return true;
}

bool CLASS::DefineContext(UInt32 cid)
{
	SVGA3dCmdDefineContext* cmd;
	cmd = static_cast<SVGA3dCmdDefineContext*>(FIFOReserve(SVGA_3D_CMD_CONTEXT_DEFINE, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::DestroyContext(UInt32 cid)
{
	SVGA3dCmdDestroyContext* cmd;
	cmd = static_cast<SVGA3dCmdDestroyContext*>(FIFOReserve(SVGA_3D_CMD_CONTEXT_DESTROY, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::SetRenderTarget(UInt32 cid,                    // IN
							SVGA3dRenderTargetType type,   // IN
							SVGA3dSurfaceImageId const* target)  // IN
{
	SVGA3dCmdSetRenderTarget* cmd;
	cmd = static_cast<SVGA3dCmdSetRenderTarget*>(FIFOReserve(SVGA_3D_CMD_SETRENDERTARGET, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->type = type;
	cmd->target = *target;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::SetTransform(UInt32 cid,                // IN
						 SVGA3dTransformType type,  // IN
						 float const* matrix)       // IN
{
	SVGA3dCmdSetTransform* cmd;
	cmd = static_cast<SVGA3dCmdSetTransform*>(FIFOReserve(SVGA_3D_CMD_SETTRANSFORM, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->type = type;
	memcpy(&cmd->matrix[0], matrix, sizeof(float) * 16U);
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::SetMaterial(UInt32 cid,                      // IN
						SVGA3dFace face,                 // IN
						SVGA3dMaterial const* material)  // IN
{
	SVGA3dCmdSetMaterial* cmd;
	cmd = static_cast<SVGA3dCmdSetMaterial*>(FIFOReserve(SVGA_3D_CMD_SETMATERIAL, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->face = face;
	memcpy(&cmd->material, material, sizeof *material);
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::SetLightEnabled(UInt32 cid,    // IN
							UInt32 index,  // IN
							bool enabled)  // IN
{
	SVGA3dCmdSetLightEnabled* cmd;
	cmd = static_cast<SVGA3dCmdSetLightEnabled*>(FIFOReserve(SVGA_3D_CMD_SETLIGHTENABLED, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->index = index;
	cmd->enabled = enabled;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::SetLightData(UInt32 cid,                   // IN
						 UInt32 index,                 // IN
						 SVGA3dLightData const* data)  // IN
{
	SVGA3dCmdSetLightData* cmd;
	cmd = static_cast<SVGA3dCmdSetLightData*>(FIFOReserve(SVGA_3D_CMD_SETLIGHTDATA, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->index = index;
	memcpy(&cmd->data, data, sizeof *data);
	m_svga->FIFOCommitAll();
	return true;
}

IOReturn CLASS::DefineShader(UInt32 cid,                   // IN
						 UInt32 shid,                  // IN
						 SVGA3dShaderType type,        // IN
						 UInt32 const* bytecode,       // IN
						 size_t bytecodeLen)           // IN
{
	SVGA3dCmdDefineShader* cmd;

	if (bytecodeLen & 3) {
		SLog("%s: SVGA3D: Shader bytecode length isn't a multiple of 32 bits!\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}

	cmd = static_cast<SVGA3dCmdDefineShader*>(FIFOReserve(SVGA_3D_CMD_SHADER_DEFINE, sizeof *cmd + bytecodeLen));
	if (!cmd)
		return kIOReturnNoMemory;
	cmd->cid = cid;
	cmd->shid = shid;
	cmd->type = type;
	memcpy(&cmd[1], bytecode, bytecodeLen);
	m_svga->FIFOCommitAll();
	return kIOReturnSuccess;
}

bool CLASS::DestroyShader(UInt32 cid,             // IN
						  UInt32 shid,            // IN
						  SVGA3dShaderType type)  // IN
{
	SVGA3dCmdDestroyShader* cmd;
	cmd = static_cast<SVGA3dCmdDestroyShader*>(FIFOReserve(SVGA_3D_CMD_SHADER_DESTROY, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->shid = shid;
	cmd->type = type;
	m_svga->FIFOCommitAll();
	return true;
}

IOReturn CLASS::SetShaderConst(UInt32 cid,                   // IN
						   UInt32 reg,                   // IN
						   SVGA3dShaderType type,        // IN
						   SVGA3dShaderConstType ctype,  // IN
						   void const* value)            // IN
{
	SVGA3dCmdSetShaderConst* cmd;
	cmd = static_cast<SVGA3dCmdSetShaderConst*>(FIFOReserve(SVGA_3D_CMD_SET_SHADER_CONST, sizeof *cmd));
	if (!cmd)
		return kIOReturnNoMemory;
	cmd->cid = cid;
	cmd->reg = reg;
	cmd->type = type;
	cmd->ctype = ctype;

	switch (ctype) {
		case SVGA3D_CONST_TYPE_FLOAT:
		case SVGA3D_CONST_TYPE_INT:
			memcpy(&cmd->values, value, sizeof cmd->values);
			break;
		case SVGA3D_CONST_TYPE_BOOL:
			bzero(&cmd->values, sizeof cmd->values);
			cmd->values[0] = *static_cast<UInt32 const*>(value);
			break;
		default:
			SLog("%s: SVGA3D: Bad shader constant type.\n", __FUNCTION__);
			m_svga->FIFOCommit(0);
			return kIOReturnBadArgument;
			
	}
	m_svga->FIFOCommitAll();
	return kIOReturnSuccess;
}

bool CLASS::SetShader(UInt32 cid,             // IN
					  SVGA3dShaderType type,  // IN
					  UInt32 shid)            // IN
{
	SVGA3dCmdSetShader* cmd;
	cmd = static_cast<SVGA3dCmdSetShader*>(FIFOReserve(SVGA_3D_CMD_SET_SHADER, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->type = type;
	cmd->shid = shid;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::BeginPresent(UInt32 sid,              // IN
						 SVGA3dCopyRect **rects,  // OUT
						 size_t numRects)         // IN
{
	SVGA3dCmdPresent* cmd;
	cmd = static_cast<SVGA3dCmdPresent*>(FIFOReserve(SVGA_3D_CMD_PRESENT, sizeof *cmd + sizeof **rects * numRects));
	if (!cmd)
		return false;
	cmd->sid = sid;
	*rects = reinterpret_cast<SVGA3dCopyRect*>(&cmd[1]);
	return true;
}

bool CLASS::BeginClear(UInt32 cid,             // IN
					   SVGA3dClearFlag flags,  // IN
					   UInt32 color,           // IN
					   float depth,            // IN
					   UInt32 stencil,         // IN
					   SVGA3dRect **rects,     // OUT
					   size_t numRects)        // IN
{
	SVGA3dCmdClear* cmd;
	cmd = static_cast<SVGA3dCmdClear*>(FIFOReserve(SVGA_3D_CMD_CLEAR, sizeof *cmd + sizeof **rects * numRects));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->clearFlag = flags;
	cmd->color = color;
	cmd->depth = depth;
	cmd->stencil = stencil;
	*rects = reinterpret_cast<SVGA3dRect*>(&cmd[1]);
	return true;
}

bool CLASS::BeginDrawPrimitives(UInt32 cid,                    // IN
								SVGA3dVertexDecl **decls,      // OUT
								size_t numVertexDecls,         // IN
								SVGA3dPrimitiveRange **ranges, // OUT
								size_t numRanges)              // IN
{
	SVGA3dCmdDrawPrimitives *cmd;
	SVGA3dVertexDecl *declArray;
	SVGA3dPrimitiveRange *rangeArray;
	size_t declSize = sizeof **decls * numVertexDecls;
	size_t rangeSize = sizeof **ranges * numRanges;

	cmd = static_cast<SVGA3dCmdDrawPrimitives*>(FIFOReserve(SVGA_3D_CMD_DRAW_PRIMITIVES, sizeof *cmd + declSize + rangeSize));
	if (!cmd)
		return false;

	cmd->cid = cid;
	cmd->numVertexDecls = static_cast<UInt32>(numVertexDecls);
	cmd->numRanges = static_cast<UInt32>(numRanges);

	declArray = reinterpret_cast<SVGA3dVertexDecl*>(&cmd[1]);
	rangeArray = reinterpret_cast<SVGA3dPrimitiveRange*>(&declArray[numVertexDecls]);

	bzero(declArray, declSize);
	bzero(rangeArray, rangeSize);

	*decls = declArray;
	*ranges = rangeArray;
	return true;
}

bool CLASS::BeginSurfaceCopy(SVGA3dSurfaceImageId const* src,   // IN
							 SVGA3dSurfaceImageId const* dest,  // IN
							 SVGA3dCopyBox **boxes,       // OUT
							 size_t numBoxes)             // IN
{
	SVGA3dCmdSurfaceCopy *cmd;
	size_t boxesSize = sizeof **boxes * numBoxes;

	cmd = static_cast<SVGA3dCmdSurfaceCopy*>(FIFOReserve(SVGA_3D_CMD_SURFACE_COPY, sizeof *cmd + boxesSize));
	if (!cmd)
		return false;

	cmd->src = *src;
	cmd->dest = *dest;
	*boxes = reinterpret_cast<SVGA3dCopyBox*>(&cmd[1]);

	bzero(*boxes, boxesSize);
	return true;
}

bool CLASS::SurfaceStretchBlt(SVGA3dSurfaceImageId const* src,   // IN
							  SVGA3dSurfaceImageId const* dest,  // IN
							  SVGA3dBox const* boxSrc,           // IN
							  SVGA3dBox const* boxDest,          // IN
							  SVGA3dStretchBltMode mode)   // IN
{
	SVGA3dCmdSurfaceStretchBlt *cmd;
	cmd = static_cast<SVGA3dCmdSurfaceStretchBlt*>(FIFOReserve(SVGA_3D_CMD_SURFACE_STRETCHBLT, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->src = *src;
	cmd->dest = *dest;
	cmd->boxSrc = *boxSrc;
	cmd->boxDest = *boxDest;
	cmd->mode = mode;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::SetViewport(UInt32 cid,        // IN
						SVGA3dRect const* rect)  // IN
{
	SVGA3dCmdSetViewport *cmd;
	cmd = static_cast<SVGA3dCmdSetViewport*>(FIFOReserve(SVGA_3D_CMD_SETVIEWPORT, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->rect = *rect;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::SetZRange(UInt32 cid,  // IN
					  float zMin,  // IN
					  float zMax)  // IN
{
	SVGA3dCmdSetZRange *cmd;
	cmd = static_cast<SVGA3dCmdSetZRange*>(FIFOReserve(SVGA_3D_CMD_SETZRANGE, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cid = cid;
	cmd->zRange.min = zMin;
	cmd->zRange.max = zMax;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::BeginSetTextureState(UInt32 cid,                   // IN
								 SVGA3dTextureState **states,  // OUT
								 size_t numStates)             // IN
{
	SVGA3dCmdSetTextureState *cmd;
	cmd = static_cast<SVGA3dCmdSetTextureState*>(FIFOReserve(SVGA_3D_CMD_SETTEXTURESTATE, sizeof *cmd + sizeof **states * numStates));
	if (!cmd)
		return false;
	cmd->cid = cid;
	*states = reinterpret_cast<SVGA3dTextureState*>(&cmd[1]);
	return true;
}

bool CLASS::BeginSetRenderState(UInt32 cid,                  // IN
								SVGA3dRenderState **states,  // OUT
								size_t numStates)            // IN
{
	SVGA3dCmdSetRenderState *cmd;
	cmd = static_cast<SVGA3dCmdSetRenderState*>(FIFOReserve(SVGA_3D_CMD_SETRENDERSTATE, sizeof *cmd + sizeof **states * numStates));
	if (!cmd)
		return false;
	cmd->cid = cid;
	*states = reinterpret_cast<SVGA3dRenderState*>(&cmd[1]);
	return true;
}

bool CLASS::BeginPresentReadback(SVGA3dRect **rects,  // OUT
								 size_t numRects)     // IN
{
	void *cmd;
	cmd = FIFOReserve(SVGA_3D_CMD_PRESENT_READBACK, sizeof **rects * numRects);
	if (!cmd)
		return false;
	*rects = static_cast<SVGA3dRect*>(cmd);
	return true;
}

bool CLASS::BeginBlitSurfaceToScreen(SVGA3dSurfaceImageId const* srcImage,
									 SVGASignedRect const* srcRect,
									 UInt32 destScreenId,
									 SVGASignedRect const* destRect,
									 SVGASignedRect** clipRects,
									 UInt32 numClipRects)
{
	SVGA3dCmdBlitSurfaceToScreen* cmd = static_cast<SVGA3dCmdBlitSurfaceToScreen*>(FIFOReserve(SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN,
																							   sizeof *cmd + numClipRects * sizeof(SVGASignedRect)));
	if (!cmd)
		return false;
	cmd->srcImage = *srcImage;
	cmd->srcRect = *srcRect;
	cmd->destScreenId = destScreenId;
	cmd->destRect = *destRect;

	if (clipRects)
		*clipRects = reinterpret_cast<SVGASignedRect*>(&cmd[1]);
	return true;
}
