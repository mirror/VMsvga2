/*
 *  vmw_options_ac.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 18th 2009.
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

#ifndef __VMW_OPTIONS_AC_H__
#define __VMW_OPTIONS_AC_H__

#define VMW_OPTION_AC_SURFACE_CONNECT		0x0001
#define VMW_OPTION_AC_2D_CONTEXT			0x0002
#define VMW_OPTION_AC_GL_CONTEXT			0x0004
#define VMW_OPTION_AC_DVD_CONTEXT			0x0008

#define VMW_OPTION_AC_SVGA3D				0x0010
#define VMW_OPTION_AC_NO_YUV				0x0020
#define VMW_OPTION_AC_DIRECT_BLIT			0x0040
#define VMW_OPTION_AC_PACKED_BACKING		0x0080
#define VMW_OPTION_AC_TEST_SMALL			0x0100
#define VMW_OPTION_AC_TEST_PRESENT			0x0200
#define VMW_OPTION_AC_TEST_READBACK			0x0400
#define VMW_OPTION_AC_TEST_DMA				0x0800
#define VMW_OPTION_AC_TEST_MASK				0x0F00

#define VMW_OPTION_AC_NO_SCREEN_OBJECT		0x1000

#define VMW_OPTION_AC_REGION_BOUNDS_COPY	0x1000

#ifdef __cplusplus
extern "C" {
#endif

extern UInt32 vmw_options_ac;

static inline bool checkOptionAC(UInt32 mask)
{
	return (vmw_options_ac & mask) != 0;
}
	
#ifdef __cplusplus
}
#endif

#endif /* __VMW_OPTIONS_AC_H__ */
