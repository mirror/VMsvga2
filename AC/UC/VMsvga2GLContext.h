/*
 *  VMsvga2GLContext.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 21st 2009.
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

#ifndef __VMSVGA2GLCONTEXT_H__
#define __VMSVGA2GLCONTEXT_H__

#include <IOKit/IOUserClient.h>

typedef uintptr_t eIOGLContextModeBits;
struct VendorCommandBufferHeader;
class IOMemoryDescriptor;

struct VMsvga2CommandBuffer
{
	UInt32 pad1[2];
	IOMemoryDescriptor* md;
	UInt32 pad2[8];
	VendorCommandBufferHeader* kernel_ptr;
	size_t size;
};

class VMsvga2GLContext: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga2GLContext);

private:
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	IOExternalMethod* m_funcs_cache;
	SInt32 m_log_level;

	UInt32 m_mem_type;	// offset 0x19C
	class OSSet* m_gc;	// offset 0xFC
	VMsvga2CommandBuffer m_command_buffer;	// offset 0xC8 - 0xFC
	IOMemoryDescriptor* m_context_buffer0;	// offset 0x108
	VendorCommandBufferHeader* m_context_buffer0_ptr;			// offset 0x12C
	IOMemoryDescriptor* m_context_buffer1;	// offset 0x138
	VendorCommandBufferHeader* m_context_buffer1_ptr;			// offset 0x15C
	IOMemoryDescriptor* m_type2;	// offset 0xB4
	size_t m_type2_len;		// offset 0xB8
	VendorCommandBufferHeader* m_type2_ptr;		// offset 0xBC

	void Cleanup();
	bool allocCommandBuffer(VMsvga2CommandBuffer*, size_t);
	void initCommandBufferHeader(VendorCommandBufferHeader*, size_t);
	bool allocAllContextBuffers();

public:
	/*
	 * Methods overridden from superclass
	 */
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
	IOReturn connectClient(IOUserClient* client);
#if 0
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* arguments, IOExternalMethodDispatch* dispatch = 0, OSObject* target = 0, void* reference = 0);
#endif
	bool start(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga2GLContext* withTask(task_t owningTask, void* securityToken, UInt32 type);

	/*
	 * Methods corresponding to Apple's GeForce.kext GL Context User Client
	 */
	/*
	 * IONVGLContext
	 */
	IOReturn set_surface(uintptr_t, eIOGLContextModeBits, uintptr_t, uintptr_t);
	IOReturn set_swap_rect(intptr_t, intptr_t, intptr_t, intptr_t);
	IOReturn set_swap_interval(intptr_t, intptr_t);
	IOReturn get_config(UInt32*, UInt32*, UInt32*);	// OS 10.5
	IOReturn get_surface_size(UInt32*, UInt32*, UInt32*, UInt32*);
	IOReturn get_surface_info(uintptr_t, UInt32*, UInt32*, UInt32*);
	IOReturn read_buffer(struct sIOGLContextReadBufferData const*, size_t);
	IOReturn finish();
	IOReturn wait_for_stamp(uintptr_t);
	IOReturn new_texture(struct sIOGLNewTextureData const*,
						 struct sIOGLNewTextureReturnData*,
						 size_t,
						 size_t*);		// OS 10.5
	IOReturn delete_texture(uintptr_t);		// OS 10.5
	IOReturn become_global_shared(uintptr_t);
	IOReturn page_off_texture(struct sIOGLContextPageoffTexture const*, size_t);		// OS 10.5
	IOReturn purge_texture(uintptr_t);
	IOReturn set_surface_volatile_state(uintptr_t);
	IOReturn set_surface_get_config_status(struct sIOGLContextSetSurfaceData const*,
										   struct sIOGLContextGetConfigStatus*,
										   size_t,
										   size_t*);
	IOReturn reclaim_resources();
	IOReturn get_data_buffer(struct sIOGLContextGetDataBuffer*, size_t*);
	IOReturn set_stereo(uintptr_t, uintptr_t);
	IOReturn purge_accelerator(uintptr_t);
	IOReturn get_channel_memory(struct sIOGLChannelMemoryData*, size_t*);		// OS 10.5
	IOReturn submit_command_buffer(uintptr_t do_get_data,
								   struct sIOGLGetCommandBuffer*,
								   size_t*);		// OS 10.6

	/*
	 * NVGLContext
	 */
	IOReturn get_query_buffer(uintptr_t c1, struct sIOGLGetQueryBuffer*, size_t*);
	IOReturn get_notifiers(UInt32*, UInt32*);
	IOReturn new_heap_object(struct sNVGLNewHeapObjectData const*,
							 struct sIOGLNewTextureReturnData*,
							 size_t,
							 size_t*);	// OS 10.5
	IOReturn kernel_printf(char const*, size_t);
	IOReturn nv_rm_config_get(UInt32 const*, UInt32*, size_t, size_t*);
	IOReturn nv_rm_config_get_ex(UInt32 const*, UInt32*, size_t, size_t*);
	IOReturn nv_client_request(void const*, void*, size_t, size_t*);
	IOReturn pageoff_surface_texture(struct sNVGLContextPageoffSurfaceTextureData const*, size_t);
	IOReturn get_data_buffer_with_offset(struct sIOGLContextGetDataBuffer*, size_t*);
	IOReturn nv_rm_control(UInt32 const*, UInt32*, size_t, size_t*);
	IOReturn get_power_state(UInt32*, UInt32*);
	IOReturn set_watchdog_timer(uintptr_t);		// OS 10.6
	IOReturn GetHandleIndex(UInt32*, UInt32*);	// OS 10.6
	IOReturn ForceTextureLargePages(uintptr_t);		// OS 10.6
};

#endif /* __VMSVGA2GLCONTEXT_H__ */
