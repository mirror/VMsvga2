/*
 *  BlitHelper.cpp
 *  VMsvga2GA
 *
 *  Created by Zenith432 on August 12th 2009.
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

#include <IOKit/IOKitLib.h>
#include "BlitHelper.h"
#include "ACMethods.h"

IOReturn useAccelUpdates(io_connect_t context, int state)
{
	uint64_t input;

	input = state ? 1 : 0;
	return IOConnectCallMethod(context,
							   kIOVM2DUseAccelUpdates,
							   &input, 1,
							   0, 0,
							   0, 0,
							   0, 0);
}

IOReturn RectCopy(io_connect_t context, void const* copyRects, UInt32 copyRectsSize)
{
	return IOConnectCallMethod(context,
							   kIOVM2DRectCopy,
							   0, 0,
							   copyRects, copyRectsSize,
							   0, 0,
							   0, 0);
}

IOReturn RectFill(io_connect_t context, UInt32 color, void const* rects, UInt32 rectsSize)
{
	uint64_t input;

	input = color;
	return IOConnectCallMethod(context,
							   kIOVM2DRectFill,
							   &input, 1,
							   rects, rectsSize,
							   0, 0,
							   0, 0);
}

IOReturn UpdateFramebuffer(io_connect_t context, UInt32 const* rect)
{
	return IOConnectCallMethod(context,
							   kIOVM2DUpdateFramebuffer,
							   0, 0,
							   rect, 4 * sizeof(UInt32),
							   0, 0,
							   0, 0);
}

IOReturn CopyRegion(io_connect_t context, UInt32 source_surface_id, UInt32 destX, UInt32 destY, void const* region, UInt32 regionSize)
{
	uint64_t input[3];

	input[0] = source_surface_id;
	input[1] = destX;
	input[2] = destY;
	return IOConnectCallMethod(context,
							   kIOVM2DCopyRegion,
							   &input[0], 3,
							   region, regionSize,
							   0, 0,
							   0, 0);
}
