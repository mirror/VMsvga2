/*
 *  VMsvga2GLContext.cpp
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

#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "VMsvga2GLContext.h"
#include "ACMethods.h"

#define CLASS VMsvga2GLContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2GLContext, IOUserClient);

#if LOGGING_LEVEL >= 1
#define GLLog(log_level, fmt, ...) do { if (log_level <= m_log_level) VLog("IOGL: ", fmt, ##__VA_ARGS__); } while (false)
#else
#define GLLog(log_level, fmt, ...)
#endif

static IOExternalMethod iofbFuncsCache[kIOVMGLNumMethods] =
{
// Note: methods from IONVGLContext
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface), kIOUCScalarIScalarO, 4, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_swap_rect), kIOUCScalarIScalarO, 4, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_swap_interval), kIOUCScalarIScalarO, 2, 0},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::get_config), kIOUCScalarIScalarO, 0, 3},
#endif
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_size), kIOUCScalarIScalarO, 0, 4},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info), kIOUCScalarIScalarO, 1, 3},
{0, reinterpret_cast<IOMethod>(&CLASS::read_buffer), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::finish), kIOUCScalarIScalarO, 0, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::wait_for_stamp), kIOUCScalarIScalarO, 1, 0},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::new_texture), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::delete_texture), kIOUCScalarIScalarO, 1, 0},
#endif
{0, reinterpret_cast<IOMethod>(&CLASS::become_global_shared), kIOUCScalarIScalarO, 1, 0},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::page_off_texture), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
#endif
{0, reinterpret_cast<IOMethod>(&CLASS::purge_texture), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_volatile_state), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_get_config_status), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::reclaim_resources), kIOUCScalarIScalarO, 0, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::get_data_buffer), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::set_stereo), kIOUCScalarIScalarO, 2, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::purge_accelerator), kIOUCScalarIScalarO, 1, 0},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::get_channel_memory), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
#else
{0, reinterpret_cast<IOMethod>(&CLASS::submit_command_buffer), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
#endif
// Note: Methods from NVGLContext
{0, reinterpret_cast<IOMethod>(&CLASS::get_query_buffer), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_notifiers), kIOUCScalarIScalarO, 0, 2},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
{0, reinterpret_cast<IOMethod>(&CLASS::new_heap_object), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
#endif
{0, reinterpret_cast<IOMethod>(&CLASS::kernel_printf), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_config_get), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_config_get_ex), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_client_request), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::pageoff_surface_texture), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_data_buffer_with_offset), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_control), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_power_state), kIOUCScalarIScalarO, 0, 2},
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
{0, reinterpret_cast<IOMethod>(&CLASS::set_watchdog_timer), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::GetHandleIndex), kIOUCScalarIScalarO, 0, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::ForceTextureLargePages), kIOUCScalarIScalarO, 1, 0},
#endif
// Note: VM Methods
};

#pragma mark -
#pragma mark struct definitions
#pragma mark -

struct sIOGLContextReadBufferData
{
};

struct sIOGLNewTextureData
{
};

struct sIOGLNewTextureReturnData
{
};

struct sIOGLContextPageoffTexture
{
};

struct sIOGLContextSetSurfaceData
{
};

struct sIOGLContextGetConfigStatus
{
};

struct sIOGLChannelMemoryData
{
};

struct sIOGLGetCommandBuffer
{
	UInt32 len0;
	UInt32 len1;
	mach_vm_address_t addr0;
	mach_vm_address_t addr1;
};

struct sIOGLContextGetDataBuffer
{
	UInt32 len;
	mach_vm_address_t addr;
} __attribute__((packed));

struct VendorCommandBufferHeader
{
	UInt32 data[8];
};

#pragma mark -
#pragma mark Private Methods
#pragma mark -

void CLASS::Cleanup()
{
	if (m_gc) {
		m_gc->flushCollection();
		m_gc->release();
		m_gc = 0;
	}
	if (m_command_buffer.md) {
		m_command_buffer.md->release();
		m_command_buffer.md = 0;
	}
	if (m_context_buffer0) {
		m_context_buffer0->release();
		m_context_buffer0 = 0;
	}
	if (m_context_buffer1) {
		m_context_buffer1->release();
		m_context_buffer1 = 0;
	}
	if (m_type2) {
		m_type2->release();
		m_type2 = 0;
	}
}

bool CLASS::allocCommandBuffer(VMsvga2CommandBuffer* buffer, size_t size)
{
	IOBufferMemoryDescriptor* bmd;

	/*
	 * Intel915 ors an optional flag @ IOAccelerator+0x924
	 */
	bmd = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared |
												kIOMemoryPageable |
												kIODirectionOut,
												size,
												page_size);
	buffer->md = bmd;
	if (!bmd)
		return false;
	buffer->size = size;
	buffer->kernel_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
	initCommandBufferHeader(buffer->kernel_ptr, size);
	return true;
}

void CLASS::initCommandBufferHeader(VendorCommandBufferHeader* buffer_ptr, size_t size)
{
	bzero(buffer_ptr, 9U * sizeof(UInt32));
	buffer_ptr->data[4] = static_cast<UInt32>((size - sizeof *buffer_ptr) / sizeof(UInt32));
	buffer_ptr->data[6] = 0;	// Intel915 puts (submitStamp - 1) here
}

bool CLASS::allocAllContextBuffers()
{
	IOBufferMemoryDescriptor* bmd;
	VendorCommandBufferHeader* p;

	bmd = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared |
												kIOMemoryPageable |
												kIODirectionInOut,
												4096U,
												page_size);
	m_context_buffer0 = bmd;
	if (!bmd)
		return false;
	m_context_buffer0_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
	p = m_context_buffer0_ptr;
	bzero(p, sizeof *p);
	p->data[5] = 1;
	p->data[3] = 1007;

	/*
	 * Intel915 ors an optional flag @ IOAccelerator+0x924
	 */
	bmd = IOBufferMemoryDescriptor::withOptions(kIOMemoryKernelUserShared |
												kIOMemoryPageable |
												kIODirectionInOut,
												4096U,
												page_size);
	if (!bmd)
		return false; // TBD frees previous ones
	m_context_buffer1 = bmd;
	m_context_buffer1_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
	p = m_context_buffer1_ptr;
	bzero(p, sizeof *p);
	p->data[5] = 1;
	p->data[3] = 1007;
	// TBD: allocates another one like this
	return true;
}

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (index >= kIOVMGLNumMethods)
		GLLog(2, "%s(%p, %u)\n", __FUNCTION__, targetP, index);
	if (!targetP || index >= kIOVMGLNumMethods)
		return 0;
#if 0
	if (index >= kIOVMGLNumMethods) {
		if (m_provider)
			*targetP = m_provider;
		else
			return 0;
	} else
		*targetP = this;
#else
	*targetP = this;
#endif
	return &m_funcs_cache[index];
}

IOReturn CLASS::clientClose()
{
	GLLog(2, "%s\n", __FUNCTION__);
	Cleanup();
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	VendorCommandBufferHeader* p;

	GLLog(2, "%s(%u, %p, %p)\n", __FUNCTION__, type, options, memory);
	if (type > 4 || !options || !memory)
		return kIOReturnBadArgument;
	switch (type) {
		case 0:
			// TBD: monstrous
			break;
		case 1:
			*memory = m_context_buffer0;
			*options = 0;
			m_context_buffer0->retain();
			p = m_context_buffer0_ptr;
			bzero(p, sizeof *p);
			p->data[5] = 1;
			p->data[3] = 1007;
			return kIOReturnSuccess;
		case 2:
			if (!m_type2) {
				m_type2_len = page_size;
				IOBufferMemoryDescriptor* bmd = IOBufferMemoryDescriptor::withOptions(0x10023U,
																					  m_type2_len,
																					  page_size);
				m_type2 = bmd;
				if (!bmd)
					return kIOReturnNoResources;
				m_type2_ptr = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
			} else {
				size_t d = 2U * m_type2_len;

				IOBufferMemoryDescriptor* bmd = IOBufferMemoryDescriptor::withOptions(0x10023U,
																					  d,
																					  page_size);
				if (!bmd)
					return kIOReturnNoResources;
				p = static_cast<VendorCommandBufferHeader*>(bmd->getBytesNoCopy());
				memcpy(p, m_type2_ptr, m_type2_len);
				m_type2->release();
				m_type2 = bmd;
				m_type2_len = d;
				m_type2_ptr = p;
			}
			m_type2->retain();
			*memory = m_type2;
			*options = 0;
			return kIOReturnSuccess;
		case 3:
			// TBD: from provider @ offset 0xB4
			break;
		case 4:
			*memory = m_command_buffer.md;
			*options = 0;
			m_command_buffer.md->retain();
			p = m_command_buffer.kernel_ptr;
			initCommandBufferHeader(p, m_command_buffer.size);
			p->data[5] = 0;
			p->data[7] = 1;
			p->data[8] = 0x1000000;
			p->data[6] = 0;		// from this+0x7C
			return kIOReturnSuccess;
	}
	return super::clientMemoryForType(type, options, memory);
}

IOReturn CLASS::connectClient(IOUserClient* client)
{
	GLLog(2, "%s(%p), name == %s\n", __FUNCTION__, client, client ? client->getName() : "NULL");
	return kIOReturnSuccess;
#if 0
	return super::connectClient(client);
#endif
}

#if 0
/*
 * Note: IONVGLContext has an override on this method
 *   In OS 10.5 it redirects function number 17 to get_data_buffer()
 *   In OS 10.6 it redirects function number 13 to get_data_buffer()
 *     Sets structureOutputSize for get_data_buffer() to 12
 */
IOReturn CLASS::externalMethod(uint32_t selector, IOExternalMethodArguments* arguments, IOExternalMethodDispatch* dispatch, OSObject* target, void* reference)
{
	return super::externalMethod(selector, arguments, dispatch, target, reference);
}
#endif

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider)
		return false;
	if (!super::start(provider))
		return false;
	m_log_level = m_provider->getLogLevelAC();
	m_mem_type = 4;
	m_gc = OSSet::withCapacity(2);
	if (!m_gc) {
		super::stop(provider);
		return false;
	}
	// TBD getVRAMDescriptors
	if (!allocAllContextBuffers()) {
		Cleanup();
		super::stop(provider);
		return false;
	}
	if (!allocCommandBuffer(&m_command_buffer, 0x10000U)) {
		Cleanup();
		super::stop(provider);
		return false;
	}
	m_command_buffer.kernel_ptr->data[5] = 2U;
	m_command_buffer.kernel_ptr->data[9] = 1000000U;
	return true;
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_log_level = 1;
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
	m_funcs_cache = &iofbFuncsCache[0];
	return true;
}

CLASS* CLASS::withTask(task_t owningTask, void* securityToken, UInt32 type)
{
	CLASS* inst;

	inst = new CLASS;

	if (inst && !inst->initWithTask(owningTask, securityToken, type))
	{
		inst->release();
		inst = 0;
	}

	return (inst);
}

#pragma mark -
#pragma mark IONVGLContext Methods
#pragma mark -

IOReturn CLASS::set_surface(uintptr_t c1, eIOGLContextModeBits c2, uintptr_t c3, uintptr_t c4)
{
	GLLog(2, "%s(%lu, %lu, %lu, %lu)\n", __FUNCTION__, c1, c2, c3, c4);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_swap_rect(intptr_t c1, intptr_t c2, intptr_t c3, intptr_t c4)
{
	GLLog(2, "%s(%ld, %ld, %ld, %ld)\n", __FUNCTION__, c1, c2, c3, c4);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_swap_interval(intptr_t c1, intptr_t c2)
{
	GLLog(2, "%s(%ld, %ld)\n", __FUNCTION__, c1, c2);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_config(UInt32* c1, UInt32* c2, UInt32* c3)
{
	UInt32 const vram_size = m_provider->getVRAMSize();

	*c1 = 0;
	*c2 = vram_size;
	*c3 = vram_size;
	GLLog(2, "%s(*%u, *%u, *%u)\n", __FUNCTION__, *c1, *c2, *c3);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_size(UInt32* c1, UInt32* c2, UInt32* c3, UInt32* c4)
{
	*c1 = 1;
	*c2 = 2;
	*c3 = 3;
	*c4 = 4;
	GLLog(2, "%s(*%u, *%u, *%u, *%u)\n", __FUNCTION__, *c1, *c2, *c3, *c4);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_info(uintptr_t c1, UInt32* c2, UInt32* c3, UInt32* c4)
{
	*c2 = 0x4081;
	*c3 = 0x4082;
	*c4 = 0x4083;
	GLLog(2, "%s(%lu, *%u, *%u, *%u)\n", __FUNCTION__, c1, *c2, *c3, *c4);
	return kIOReturnSuccess;
}

IOReturn CLASS::read_buffer(struct sIOGLContextReadBufferData const* struct_in, size_t struct_in_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_in, struct_in_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::finish()
{
	GLLog(2, "%s()\n", __FUNCTION__);
	return kIOReturnSuccess;
}

IOReturn CLASS::wait_for_stamp(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::new_texture(struct sIOGLNewTextureData const* struct_in,
							struct sIOGLNewTextureReturnData* struct_out,
							size_t struct_in_size,
							size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::delete_texture(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::become_global_shared(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::page_off_texture(struct sIOGLContextPageoffTexture const* struct_in, size_t struct_in_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_in, struct_in_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::purge_texture(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_surface_volatile_state(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_surface_get_config_status(struct sIOGLContextSetSurfaceData const* struct_in,
											  struct sIOGLContextGetConfigStatus* struct_out,
											  size_t struct_in_size,
											  size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::reclaim_resources()
{
	GLLog(2, "%s()\n", __FUNCTION__);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_data_buffer(struct sIOGLContextGetDataBuffer* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_out, *struct_out_size);
	if (*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	*struct_out_size = sizeof *struct_out;
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::set_stereo(uintptr_t c1, uintptr_t c2)
{
	GLLog(2, "%s(%lu, %lu)\n", __FUNCTION__, c1, c2);
	return kIOReturnSuccess;
}

IOReturn CLASS::purge_accelerator(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_channel_memory(struct sIOGLChannelMemoryData* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_out, *struct_out_size);
	bzero(struct_out, *struct_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::submit_command_buffer(uintptr_t do_get_data,
									  struct sIOGLGetCommandBuffer* struct_out,
									  size_t* struct_out_size)
{
	IOReturn rc;
	IOOptionBits options;
	IOMemoryDescriptor* md;
	IOMemoryMap* mm;
	struct sIOGLContextGetDataBuffer db;
	size_t dbsize;

	GLLog(2, "%s(%lu, %p, %lu)\n", __FUNCTION__, do_get_data, struct_out, *struct_out_size);
	if (*struct_out_size < sizeof *struct_out)
		return kIOReturnBadArgument;
	options = 0;
	md = 0;
	bzero(struct_out, *struct_out_size);
	rc = clientMemoryForType(m_mem_type, &options, &md);
	m_mem_type = 0;
	if (rc != kIOReturnSuccess)
		return rc;
	mm = md->createMappingInTask(m_owning_task,
								 0,
								 kIOMapAnywhere);
	md->release();
	if (!mm)
		return kIOReturnNoMemory;
	struct_out->addr0 = mm->getAddress();
	struct_out->len0 = static_cast<UInt32>(mm->getLength());
	if (do_get_data != 0) {
		// increment dword @offset 0x8dc on the provider
		dbsize = sizeof db;
		rc = get_data_buffer(&db, &dbsize);
		if (rc != kIOReturnSuccess) {
			mm->release();	// Added
			return rc;
		}
		struct_out->addr1 = db.addr;
		struct_out->len1 = db.len;
	}
	// IOLockLock on provider @+0xC4
	m_gc->setObject(mm);
	// IOUnlockLock on provider+0xC4
	mm->release();
	return rc;
}

#pragma mark -
#pragma mark NVGLContext Methods
#pragma mark -

IOReturn CLASS::get_query_buffer(uintptr_t c1, struct sIOGLGetQueryBuffer* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(%lu, %p, %lu)\n", __FUNCTION__, c1, struct_out, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_notifiers(UInt32*, UInt32*)
{
	GLLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

IOReturn CLASS::new_heap_object(struct sNVGLNewHeapObjectData const* struct_in,
								struct sIOGLNewTextureReturnData* struct_out,
								size_t struct_in_size,
								size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::kernel_printf(char const* str, size_t str_size)
{
	GLLog(2, "%s: %s\n", __FUNCTION__, str);
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_config_get(UInt32 const* struct_in,
								 UInt32* struct_out,
								 size_t struct_in_size,
								 size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_config_get_ex(UInt32 const* struct_in,
									UInt32* struct_out,
									size_t struct_in_size,
									size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_client_request(void const* struct_in,
								  void* struct_out,
								  size_t struct_in_size,
								  size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::pageoff_surface_texture(struct sNVGLContextPageoffSurfaceTextureData const* struct_in, size_t struct_in_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_in, struct_in_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_data_buffer_with_offset(struct sIOGLContextGetDataBuffer* struct_out, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, struct_out, struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_control(UInt32 const* struct_in,
							  UInt32* struct_out,
							  size_t struct_in_size,
							  size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, struct_in, struct_out, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_power_state(UInt32*, UInt32*)
{
	GLLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_watchdog_timer(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnUnsupported;
}

IOReturn CLASS::GetHandleIndex(UInt32*, UInt32*)
{
	GLLog(2, "%s(out1, out2)\n", __FUNCTION__);
	return kIOReturnUnsupported;
}

IOReturn CLASS::ForceTextureLargePages(uintptr_t c1)
{
	GLLog(2, "%s(%lu)\n", __FUNCTION__, c1);
	return kIOReturnUnsupported;
}
