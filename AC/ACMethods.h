/*
 *  ACMethods.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on September 4th 2009.
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

#ifndef __ACMETHODS_H__
#define __ACMETHODS_H__

enum eIOVM2DMethods {
	kIOVM2DSetSurface,
	kIOVM2DGetConfig,
	kIOVM2DGetSurfaceInfo1,
	kIOVM2DSwapSurface,
	kIOVM2DScaleSurface,
	kIOVM2DLockMemory,
	kIOVM2DUnlockMemory,
	kIOVM2DFinish,
	kIOVM2DDeclareImage,
	kIOVM2DCreateImage,
	kIOVM2DCreateTransfer,
	kIOVM2DDeleteImage,
	kIOVM2DWaitImage,
	kIOVM2DSetSurfacePagingOptions,
	kIOVM2DSetSurfaceVsyncOptions,
	kIOVM2DSetMacrovision,

	kIOVM2DReadConfigs,
	kIOVM2DReadConfigEx,
	kIOVM2DWriteConfigs,
	kIOVM2DWriteConfigEx,
	kIOVM2DGetSurfaceInfo2,
	kIOVM2DKernelPrintf,

	kIOVM2DCopyRegion,
	kIOVM2DUseAccelUpdates,
	kIOVM2DRectCopy,
	kIOVM2DRectFill,
	kIOVM2DUpdateFramebuffer,

	kIOVM2DNumMethods
};

#endif /* __ACMETHODS_H__ */
