/*
 *  VMsvga2Device.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on October 11th 2009.
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

#ifndef __VMSVGA2DEVICE_H__
#define __VMSVGA2DEVICE_H__

#include <IOKit/IOUserClient.h>

class VMsvga2Device: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga2Device);

private:
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	IOExternalMethod* m_funcs_cache;
	SInt32 m_log_level;

public:
	/*
	 * Methods overridden from superclass
	 */
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
#if 0
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* arguments, IOExternalMethodDispatch* dispatch = 0, OSObject* target = 0, void* reference = 0);
#endif
	bool start(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga2Device* withTask(task_t owningTask, void* securityToken, UInt32 type);

	/*
	 * Methods from IONVDevice
	 */
	IOReturn create_shared();
	IOReturn get_config(io_user_scalar_t* c1, io_user_scalar_t* c2, io_user_scalar_t* c3, io_user_scalar_t* c4, io_user_scalar_t* c5);
	IOReturn get_surface_info(uintptr_t c1, io_user_scalar_t* c2, io_user_scalar_t* c3, io_user_scalar_t* c4);
	IOReturn get_name(char* out_name, size_t* struct_out_size);
	IOReturn wait_for_stamp(uintptr_t c1);
	IOReturn new_texture(struct VendorNewTextureDataRec const* in_struct, struct sIONewTextureReturnData* out_struct, size_t struct_in_size, size_t* struct_out_size);
	IOReturn delete_texture(uintptr_t c1);
	IOReturn page_off_texture(struct sIODevicePageoffTexture const* in_struct, size_t struct_in_size);
	IOReturn get_channel_memory(struct sIODeviceChannelMemoryData* out_struct, size_t* struct_out_size);

	/*
	 * NVDevice Methods
	 */
	IOReturn kernel_printf(char const* str, size_t struct_in_size);
	IOReturn nv_rm_config_get(UInt32 const* in_struct, UInt32* out_struct, size_t struct_in_size, size_t* struct_out_size);
	IOReturn nv_rm_config_get_ex(UInt32 const* in_struct, UInt32* out_struct, size_t struct_in_size, size_t* struct_out_size);
	IOReturn nv_rm_control(UInt32 const* in_struct, UInt32* out_struct, size_t struct_in_size, size_t* struct_out_size);
};

#endif /* __VMSVGA2DEVICE_H__ */
