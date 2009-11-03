/*
 *  SVGA3D.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 11th 2009.
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

#ifndef __SVGA3D_H__
#define __SVGA3D_H__

#include <libkern/OSTypes.h>
#include "svga_apple_header.h"
#include "svga3d_reg.h"
#include "svga_apple_footer.h"

class SVGADevice;

class SVGA3D
{
private:
	SVGADevice* m_svga;
	UInt32 HWVersion;

	void* FIFOReserve(UInt32 cmd, size_t cmdSize);

public:
	/*
	 * SVGA Device Interoperability
	 */

	bool Init(SVGADevice*);
	UInt32 getHWVersion() const { return HWVersion; }
	bool BeginPresent(UInt32 sid, SVGA3dCopyRect **rects, size_t numRects);
	bool BeginPresentReadback(SVGA3dRect **rects, size_t numRects);
	bool BeginBlitSurfaceToScreen(SVGA3dSurfaceImageId const* srcImage,
								  SVGASignedRect const* srcRect,
								  UInt32 destScreenId,
								  SVGASignedRect const* destRect,
								  SVGASignedRect** clipRects,
								  UInt32 numClipRects);

	/*
	 * Surface Management
	 */

	bool BeginDefineSurface(UInt32 sid,
							SVGA3dSurfaceFlags flags,
							SVGA3dSurfaceFormat format,
							SVGA3dSurfaceFace **faces,
							SVGA3dSize **mipSizes,
							size_t numMipSizes);
	bool DestroySurface(UInt32 sid);
	bool BeginSurfaceDMA(SVGA3dGuestImage const *guestImage,
						 SVGA3dSurfaceImageId const *hostImage,
						 SVGA3dTransferType transfer,
						 SVGA3dCopyBox **boxes,
						 size_t numBoxes);
	bool BeginSurfaceDMAwithSuffix(SVGA3dGuestImage const *guestImage,
								   SVGA3dSurfaceImageId const *hostImage,
								   SVGA3dTransferType transfer,
								   SVGA3dCopyBox **boxes,
								   size_t numBoxes,
								   UInt32 maximumOffset,
								   SVGA3dSurfaceDMAFlags flags);

	/*
	 * Context Management
	 */

	bool DefineContext(UInt32 cid);
	bool DestroyContext(UInt32 cid);

	/*
	 * Drawing Operations
	 */

	bool BeginClear(UInt32 cid, SVGA3dClearFlag flags,
					UInt32 color, float depth, UInt32 stencil,
					SVGA3dRect **rects, size_t numRects);
	bool BeginDrawPrimitives(UInt32 cid,
							 SVGA3dVertexDecl **decls,
							 size_t numVertexDecls,
							 SVGA3dPrimitiveRange **ranges,
							 size_t numRanges);

	/*
	 * Blits
	 */

	bool BeginSurfaceCopy(SVGA3dSurfaceImageId const* src,
						  SVGA3dSurfaceImageId const* dest,
						  SVGA3dCopyBox **boxes, size_t numBoxes);

	bool SurfaceStretchBlt(SVGA3dSurfaceImageId const* src,
						   SVGA3dSurfaceImageId const* dest,
						   SVGA3dBox const* boxSrc, SVGA3dBox const* boxDest,
						   SVGA3dStretchBltMode mode);

	/*
	 * Shared FFP/Shader Render State
	 */

	bool SetRenderTarget(UInt32 cid, SVGA3dRenderTargetType type, SVGA3dSurfaceImageId const* target);
	bool SetZRange(UInt32 cid, float zMin, float zMax);
	bool SetViewport(UInt32 cid, SVGA3dRect const* rect);
	bool SetScissorRect(UInt32 cid, SVGA3dRect const* rect);
	bool SetClipPlane(UInt32 cid, UInt32 index, float const* plane);
	bool BeginSetTextureState(UInt32 cid, SVGA3dTextureState **states, size_t numStates);
	bool BeginSetRenderState(UInt32 cid, SVGA3dRenderState **states, size_t numStates);

	/*
	 * Fixed-function Render State
	 */

	bool SetTransform(UInt32 cid, SVGA3dTransformType type, float const* matrix);
	bool SetMaterial(UInt32 cid, SVGA3dFace face, SVGA3dMaterial const* material);
	bool SetLightData(UInt32 cid, UInt32 index, SVGA3dLightData const* data);
	bool SetLightEnabled(UInt32 cid, UInt32 index, bool enabled);

	/*
	 * Shaders
	 */

	IOReturn DefineShader(UInt32 cid, UInt32 shid, SVGA3dShaderType type, UInt32 const* bytecode, size_t bytecodeLen);
	bool DestroyShader(UInt32 cid, UInt32 shid, SVGA3dShaderType type);
	IOReturn SetShaderConst(UInt32 cid, UInt32 reg, SVGA3dShaderType type, SVGA3dShaderConstType ctype, void const* value);
	bool SetShader(UInt32 cid, SVGA3dShaderType type, UInt32 shid);
};

#endif /* __SVGA3D_H__ */
