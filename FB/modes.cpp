/*
 *  modes.cpp
 *  VMsvga2
 *
 *  Created by Zenith432 on July 11th 2009.
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

#include <IOKit/graphics/IOGraphicsTypes.h>
#include "common_fb.h"

DisplayModeEntry const modeList[NUM_DISPLAY_MODES] =
{
	1, 800, 600, kDisplayModeValidFlag | kDisplayModeSafeFlag,		// 4x3 Note: reserved for Custom Mode
	2, 800, 600, kDisplayModeValidFlag | kDisplayModeSafeFlag,		// 4x3
	3, 1024, 768, kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag,	// 4x3
	4, 1152, 864, kDisplayModeValidFlag | kDisplayModeSafeFlag,		// 4x3
	5, 1152, 900, kDisplayModeValidFlag | kDisplayModeSafeFlag,		// 32x25
	6, 1280, 800, kDisplayModeValidFlag | kDisplayModeSafeFlag,		// 8x5
	7, 1280, 1024, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 5x4
	8, 1376, 1032, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	9, 1440, 900, kDisplayModeValidFlag | kDisplayModeSafeFlag,		// 8x5 Note: was 1400x900
	10, 1400, 1050, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	11, 1600, 1200, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	12, 1680, 1050, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 8x5
	13, 1920, 1200, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 8x5
	14, 2360, 1770, kDisplayModeValidFlag | kDisplayModeSafeFlag	// 4x3 Note: was 2364x1773
};

DisplayModeEntry customMode;
