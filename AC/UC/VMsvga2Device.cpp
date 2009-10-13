/*
 *  VMsvga2Device.cpp
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

#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "VMsvga2Accel.h"
#include "VMsvga2Device.h"

#define CLASS VMsvga2Device
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2Device, IOUserClient);

#define VLOG_PREFIX_STR "log IODV: "
#define VLOG_PREFIX_LEN (sizeof VLOG_PREFIX_STR - 1)
#define VLOG_BUF_SIZE 256

extern "C" char VMLog_SendString(char const* str);

#if LOGGING_LEVEL >= 1
#define DVLog(log_level, fmt, ...) do { if (log_level <= m_log_level) VLog(fmt, ##__VA_ARGS__); } while (false)
#else
#define DVLog(log_level, fmt, ...)
#endif

#define NUM_DV_METHODS 13
#define VM_METHODS_START 13

static IOExternalMethod iofbFuncsCache[NUM_DV_METHODS] =
{
// IONVDevice
{0, reinterpret_cast<IOMethod>(&CLASS::create_shared), kIOUCScalarIScalarO, 0, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::get_config), kIOUCScalarIScalarO, 0, 5},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info), kIOUCScalarIScalarO, 1, 3},
{0, reinterpret_cast<IOMethod>(&CLASS::get_name), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::wait_for_stamp), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::new_texture), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::delete_texture), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::page_off_texture), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_channel_memory), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
// NVDevice
{0, reinterpret_cast<IOMethod>(&CLASS::kernel_printf), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_config_get), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_config_get_ex), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::nv_rm_control), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
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
	DVLog(1, "%s(%p, %u)\n", __FUNCTION__, targetP, index);
	if (!targetP || index >= NUM_DV_METHODS)
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
	DVLog(2, "%s\n", __FUNCTION__);
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	DVLog(1, "%s(%u, %p, %p)\n", __FUNCTION__, type, options, memory);
	if (type == 1 &&
		options != 0 &&
		memory != 0) {
		IOBufferMemoryDescriptor* md = IOBufferMemoryDescriptor::withOptions(kIOMemoryPageable | kIOMemoryKernelUserShared,
																			 0x400,
																			 PAGE_SIZE);
		*memory = md;
		*options = kIOMapAnywhere;
		return kIOReturnSuccess;
	}
	return super::clientMemoryForType(type, options, memory);
}

#if 0
/*
 * Note: NVDevice has an override on this method
 *   In OS 10.6 redirects methods 14 - 19 to local code implemented in externalMethod()
 *     methods 16 - 17 call NVDevice::getSupportedEngines(ulong *,ulong &,ulong)
 *     method 18 calls NVDevice::getGRInfo(NV2080_CTRL_GR_INFO *,ulong)
 *     method 19 calls NVDevice::getArchImplBus(ulong *,ulong)
 *
 *   iofbFuncsCache only defines methods 0 - 12, so 13 is missing and 14 - 19 as above.
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
#pragma mark IONVDevice Methods
#pragma mark -

IOReturn CLASS::create_shared()
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_config(io_user_scalar_t* c1, io_user_scalar_t* c2, io_user_scalar_t* c3, io_user_scalar_t* c4, io_user_scalar_t* c5)
{
	if (c1)
		*c1 = 0;
	if (c2)
		*c2 = 0;
	if (c3)
		*c3 = 0;
	if (c4)
		*c4 = 0;
	if (c5)
		*c5 = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_info(uintptr_t, io_user_scalar_t*, io_user_scalar_t*, io_user_scalar_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_name(char *, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::wait_for_stamp(uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::new_texture(struct VendorNewTextureDataRec const*, struct sIONewTextureReturnData*, size_t struct_in_size, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::delete_texture(uintptr_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::page_off_texture(struct sIODevicePageoffTexture const*, size_t struct_in_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_channel_memory(struct sIODeviceChannelMemoryData*, size_t* struct_out_size)
{
	return kIOReturnUnsupported;
}

#pragma mark -
#pragma mark NVDevice Methods
#pragma mark -

IOReturn CLASS::kernel_printf(char const*, size_t struct_in_size)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::nv_rm_config_get(UInt32 const* in_struct, UInt32* out_struct, size_t struct_in_size, size_t* struct_out_size)
{
	DVLog(1, "%s(..., ..., %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	memcpy(out_struct, in_struct, struct_in_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::nv_rm_config_get_ex(UInt32 const* in_struct, UInt32* out_struct, size_t struct_in_size, size_t* struct_out_size)
{
	DVLog(1, "%s(..., ..., %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	memcpy(out_struct, in_struct, struct_in_size);
	return kIOReturnSuccess;
}

IOReturn CLASS::nv_rm_control(UInt32 const* in_struct, UInt32* out_struct, size_t struct_in_size, size_t* struct_out_size)
{
	DVLog(1, "%s(..., ..., %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	memcpy(out_struct, in_struct, struct_in_size);
	return kIOReturnSuccess;
}
