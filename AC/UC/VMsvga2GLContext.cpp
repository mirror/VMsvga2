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

#define CLASS VMsvga2GLContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2GLContext, IOUserClient);

#if LOGGING_LEVEL >= 1
#define GLLog(log_level, fmt, ...) do { if (log_level <= m_log_level) VLog("IOGL: ", fmt, ##__VA_ARGS__); } while (false)
#else
#define GLLog(log_level, fmt, ...)
#endif

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#define NUM_GL_LOW_METHODS 21
#define NUM_GL_HIGH_METHODS 11
#else
#define NUM_GL_LOW_METHODS 17
#define NUM_GL_HIGH_METHODS 13
#endif
#define NUM_GL_METHODS (NUM_GL_LOW_METHODS + NUM_GL_HIGH_METHODS)
#define VM_METHODS_START NUM_GL_METHODS

static IOExternalMethod iofbFuncsCache[NUM_GL_METHODS] =
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
	IOByteCount len;
	UInt32 db0;
	mach_vm_address_t addr;
	UInt32 db1;
	UInt32 db2;
};

struct sIOGLContextGetDataBuffer
{
	UInt32 d[3];
};

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (index >= NUM_GL_LOW_METHODS)
		GLLog(2, "%s(%p, %u)\n", __FUNCTION__, targetP, index);
	if (!targetP || index >= NUM_GL_METHODS)
		return 0;
	if (index >= VM_METHODS_START) {
		if (m_provider)
			*targetP = m_provider;
		else
			return 0;
	} else
		*targetP = this;
	return &m_funcs_cache[index];
}

IOReturn CLASS::clientClose()
{
	GLLog(2, "%s\n", __FUNCTION__);
	if (m_mm_remember) {
		m_mm_remember->release();
		m_mm_remember = 0;
	}
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	GLLog(2, "%s(%u, %p, %p)\n", __FUNCTION__, type, options, memory);
	if (type > 4 || !options || !memory)
		return kIOReturnBadArgument;
	IOBufferMemoryDescriptor* md = IOBufferMemoryDescriptor::withOptions(kIOMemoryPageable | kIOMemoryKernelUserShared,
																		 PAGE_SIZE,
																		 PAGE_SIZE);
	*memory = md;
	*options = kIOMapAnywhere;
	return kIOReturnSuccess;
#if 0
	return super::clientMemoryForType(type, options, memory);
#endif
}

IOReturn CLASS::connectClient(IOUserClient* client)
{
	GLLog(2, "%s(%p)\n", __FUNCTION__, client);
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
	m_log_level = m_provider->getLogLevelAC();
	m_mem_type = 4;
	return super::start(provider);
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

IOReturn CLASS::get_config(io_user_scalar_t* c1, io_user_scalar_t* c2, io_user_scalar_t* c3)
{
	GLLog(2, "%s(out1, out2, out3)\n", __FUNCTION__);
	*c1 = 0x4071;
	*c2 = 128 * 1024 * 1024;
	*c3 = 0x4073;
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_size(io_user_scalar_t* c1, io_user_scalar_t* c2, io_user_scalar_t* c3, io_user_scalar_t* c4)
{
	GLLog(2, "%s(out1, out2, out3, out4)\n", __FUNCTION__);
	*c1 = 1;
	*c2 = 2;
	*c3 = 3;
	*c4 = 4;
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_info(uintptr_t c1, io_user_scalar_t* c2, io_user_scalar_t* c3, io_user_scalar_t* c4)
{
	GLLog(2, "%s(%lu, out2, out3, out4)\n", __FUNCTION__, c1);
	*c2 = 0x4081;
	*c3 = 0x4082;
	*c4 = 0x4083;
	return kIOReturnSuccess;
}

IOReturn CLASS::read_buffer(struct sIOGLContextReadBufferData const* in_struct, size_t struct_in_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, in_struct, struct_in_size);
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

IOReturn CLASS::new_texture(struct sIOGLNewTextureData const* in_struct, struct sIOGLNewTextureReturnData* out_struct, size_t struct_in_size, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, in_struct, out_struct, struct_in_size, *struct_out_size);
	bzero(out_struct, *struct_out_size);
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

IOReturn CLASS::page_off_texture(struct sIOGLContextPageoffTexture const* in_struct, size_t struct_in_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, in_struct, struct_in_size);
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

IOReturn CLASS::set_surface_get_config_status(struct sIOGLContextSetSurfaceData const* in_struct, struct sIOGLContextGetConfigStatus* out_struct, size_t struct_in_size, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %p, %lu, %lu)\n", __FUNCTION__, in_struct, out_struct, struct_in_size, *struct_out_size);
	bzero(out_struct, *struct_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::reclaim_resources()
{
	GLLog(2, "%s()\n", __FUNCTION__);
	return kIOReturnSuccess;
}

IOReturn CLASS::get_data_buffer(struct sIOGLContextGetDataBuffer* out_struct, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, out_struct, struct_out_size ? *struct_out_size : 0UL);
	if (!out_struct || !struct_out_size || *struct_out_size < sizeof(sIOGLContextGetDataBuffer))
		return kIOReturnBadArgument;
	bzero(out_struct, *struct_out_size);
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

IOReturn CLASS::get_channel_memory(struct sIOGLChannelMemoryData* out_struct, size_t* struct_out_size)
{
	GLLog(2, "%s(%p, %lu)\n", __FUNCTION__, out_struct, *struct_out_size);
	bzero(out_struct, *struct_out_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::submit_command_buffer(uintptr_t do_get_data, struct sIOGLGetCommandBuffer* out_struct, size_t* struct_out_size)
{
	IOReturn rc;
	IOOptionBits options;
	IOMemoryDescriptor* md;
	IOMemoryMap* mm;
	struct sIOGLContextGetDataBuffer db;
	size_t dbsize;

	GLLog(2, "%s(%lu, %p, %lu)\n", __FUNCTION__, do_get_data, out_struct, struct_out_size ? *struct_out_size : 0UL);
	if (!out_struct || !struct_out_size || *struct_out_size < sizeof(sIOGLGetCommandBuffer))
		return kIOReturnBadArgument;
	options = 0;
	md = 0;
	bzero(out_struct, *struct_out_size);
	rc = clientMemoryForType(m_mem_type, &options, &md);
	m_mem_type = 0;
	if (rc != kIOReturnSuccess)
		return rc;
	mm = md->createMappingInTask(m_owning_task,
								 0,
								 kIOMapAnywhere,
								 0,
								 0);
	md->release();
	if (!mm)
		return kIOReturnNoMemory;
	out_struct->addr = mm->getAddress();
	out_struct->len = mm->getLength();
	if (do_get_data != 0) {
		// increment dword @offset 0x8dc on the provider
		dbsize = sizeof db;
		rc = get_data_buffer(&db, &dbsize);
		if (rc != kIOReturnSuccess) {
			mm->release();	// Added
			return rc;
		}
		out_struct->db1 = db.d[1];
		out_struct->db2 = db.d[2];
		out_struct->db0 = db.d[0];
	}
	// IOLockLock on provider @+0xC4
	// get some object @+0xfc, call virt func +0xe8 on it with mm
	// IOUnlockLock on provider
	m_mm_remember == mm;
//	mm->release();
	return rc;
}

#pragma mark -
#pragma mark NVGLContext Methods
#pragma mark -

IOReturn CLASS::get_query_buffer(uintptr_t, struct sIOGLGetQueryBuffer*, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_notifiers(io_user_scalar_t*, io_user_scalar_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::new_heap_object(struct sNVGLNewHeapObjectData const*, struct sIOGLNewTextureReturnData*, size_t struct_in_size, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::kernel_printf(char const*, size_t struct_in_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_config_get(UInt32 const* struct_in, UInt32* struct_out, size_t struct_in_size, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_config_get_ex(UInt32 const* struct_in, UInt32* struct_out, size_t struct_in_size, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_client_request(void const* struct_in, void* struct_out, size_t struct_in_size, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::pageoff_surface_texture(struct sNVGLContextPageoffSurfaceTextureData const*, size_t struct_in_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_data_buffer_with_offset(struct sIOGLContextGetDataBuffer*, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_control(UInt32 const* struct_in, UInt32* struct_out, size_t struct_in_size, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_power_state(io_user_scalar_t*, io_user_scalar_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_watchdog_timer(uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::GetHandleIndex(io_user_scalar_t*, io_user_scalar_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::ForceTextureLargePages(uintptr_t)
{
	return kIOReturnUnsupported;
}
