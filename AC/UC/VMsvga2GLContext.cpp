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
#include "VMsvga2Accel.h"
#include "VMsvga2GLContext.h"

#define CLASS VMsvga2GLContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2GLContext, IOUserClient);

#define VLOG_PREFIX_STR "log IOGL: "
#define VLOG_PREFIX_LEN (sizeof VLOG_PREFIX_STR - 1)
#define VLOG_BUF_SIZE 256

extern "C" char VMLog_SendString(char const* str);

#if LOGGING_LEVEL >= 1
#define GLLog(log_level, fmt, ...) do { if (log_level <= m_log_level) VLog(fmt, ##__VA_ARGS__); } while (false)
#else
#define GLLog(log_level, fmt, ...)
#endif

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#define NUM_GL_METHODS 32
#define VM_METHODS_START 32
#else
#define NUM_GL_METHODS 30
#define VM_METHODS_START 30
#endif

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

void CLASS::VLog(char const* fmt, ...)
{
	va_list ap;
	char print_buf[VLOG_BUF_SIZE];
	
	va_start(ap, fmt);
	strlcpy(&print_buf[0], VLOG_PREFIX_STR, sizeof print_buf);
	vsnprintf(&print_buf[VLOG_PREFIX_LEN], sizeof print_buf - VLOG_PREFIX_LEN, fmt, ap);
	va_end(ap);
	VMLog_SendString(&print_buf[0]);
}

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	GLLog(1, "%s(%p, %u)\n", __FUNCTION__, targetP, index);
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
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	GLLog(1, "%s(%u, %p, %p)\n", __FUNCTION__, type, options, memory);
	return super::clientMemoryForType(type, options, memory);
}

IOReturn CLASS::connectClient(IOUserClient* client)
{
	GLLog(1, "%s(%p)\n", __FUNCTION__, client);
	return super::connectClient(client);
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

IOReturn CLASS::set_surface(uintptr_t, eIOGLContextModeBits, uintptr_t, uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_swap_rect(intptr_t, intptr_t, intptr_t, intptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_swap_interval(intptr_t, intptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_config(io_user_scalar_t*, io_user_scalar_t*, io_user_scalar_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_surface_size(io_user_scalar_t*, io_user_scalar_t*, io_user_scalar_t*, io_user_scalar_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_surface_info(uintptr_t, io_user_scalar_t*, io_user_scalar_t*, io_user_scalar_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::read_buffer(struct sIOGLContextReadBufferData const*, size_t struct_in_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::finish()
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::wait_for_stamp(uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::new_texture(struct sIOGLNewTextureData const*, struct sIOGLNewTextureReturnData*, size_t struct_in_size, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::delete_texture(uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::become_global_shared(uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::page_off_texture(struct sIOGLContextPageoffTexture const*, size_t struct_in_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::purge_texture(uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_surface_volatile_state(uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_surface_get_config_status(struct sIOGLContextSetSurfaceData const*, struct sIOGLContextGetConfigStatus*, size_t struct_in_size, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::reclaim_resources()
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_data_buffer(struct sIOGLContextGetDataBuffer*, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_stereo(uintptr_t, uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::purge_accelerator(uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_channel_memory(struct sIOGLChannelMemoryData*, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::submit_command_buffer(uintptr_t, struct sIOGLGetCommandBuffer*, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
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
