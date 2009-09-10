/*
 *  VMsvga22DContext.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 10th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *  Portions Copyright (c) Apple Computer, Inc.
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

#ifndef __VMSVGA22DCONTEXT_H__
#define __VMSVGA22DCONTEXT_H__

#include <IOKit/IOUserClient.h>

typedef IOOptionBits eIOContextModeBits;
struct IOSurfacePagingControlInfoStruct;
struct IOSurfaceVsyncControlInfoStruct;

class VMsvga22DContext: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga22DContext);

private:
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	IOExternalMethod* m_funcs_cache;

	unsigned bTargetIsCGSSurface:1;
	class VMsvga2Surface* surface_client;

	IOReturn locateSurface(UInt32 surface_id);

public:
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
#if 0
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
#endif
	/*
	 * Methods overridden from superclass
	 */
	bool start(IOService* provider);
	void stop(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga22DContext* withTask(task_t owningTask, void* securityToken, UInt32 type);

	/*
	 * GA Support Methods
	 */
	IOReturn CopyRegion(UInt32 source_surface_id, UInt32 destX, UInt32 destY, IOAccelDeviceRegion const* region, size_t regionSize);

	/*
	 * Methods corresponding to Apple's GeForce.kext 2D Context User Client
	 */
	/*
	 * IONV2DContext
	 */
	IOReturn set_surface(UInt32 surface_id, eIOContextModeBits options, void* output_struct, size_t* output_struct_size);
	IOReturn get_config(IOOptionBits* config_1, IOOptionBits* config_2);
	IOReturn get_surface_info1(UInt32, eIOContextModeBits, void *, size_t*);
	IOReturn swap_surface(IOOptionBits options, IOOptionBits* swapFlags);
	IOReturn scale_surface(IOOptionBits options, UInt32 width, UInt32 height);
	IOReturn lock_memory(IOOptionBits options, vm_address_t* address, vm_size_t* rowBytes);
	IOReturn unlock_memory(IOOptionBits options, IOOptionBits* swapFlags);
	IOReturn finish(IOOptionBits options);
	IOReturn declare_image(UInt32, UInt32, UInt32, UInt32*);
	IOReturn create_image(UInt32, UInt32, UInt32*, UInt32*);
	IOReturn create_transfer(UInt32, UInt32, UInt32*, UInt32*);
	IOReturn delete_image(UInt32 image_id);
	IOReturn wait_image(UInt32 image_id);
	IOReturn set_surface_paging_options(IOSurfacePagingControlInfoStruct const*, IOSurfacePagingControlInfoStruct*, size_t, size_t*);
	IOReturn set_surface_vsync_options(IOSurfaceVsyncControlInfoStruct const*, IOSurfaceVsyncControlInfoStruct*, size_t, size_t*);
	IOReturn set_macrovision(IOOptionBits new_state);

	/*
	 * NV2DContext
	 */
	IOReturn read_configs(UInt32 const* input_struct, UInt32* output_struct, size_t input_struct_size, size_t* output_struct_size);
	IOReturn read_config_Ex(UInt32 const* input_struct, UInt32* output_struct, size_t input_struct_size, size_t* output_struct_size);
	IOReturn write_configs(UInt32 const*, size_t);
	IOReturn write_config_Ex(UInt32 const*, size_t);
	IOReturn get_surface_info2(UInt32 const*, UInt32*, size_t, size_t*);
	IOReturn kernel_printf(char const*, size_t);
};

#endif /* __VMSVGA22DCONTEXT_H__ */
