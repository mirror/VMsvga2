/*
 *  VMsvga22DContext.cpp
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

#include <IOKit/IOLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include "VMsvga2Accel.h"
#include "VMsvga2Surface.h"
#include "VMsvga22DContext.h"
#include "ACMethods.h"

#include "svga_apple_header.h"
#include "svga_overlay.h"
#include "svga_apple_footer.h"

#define CLASS VMsvga22DContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga22DContext, IOUserClient);

static IOExternalMethod iofbFuncsCache[kIOVM2DNumMethods] =
{
// Note: methods from IONV2DContext
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_config), kIOUCScalarIScalarO, 0, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info1), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::swap_surface), kIOUCScalarIScalarO, 1, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::scale_surface), kIOUCScalarIScalarO, 3, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::lock_memory), kIOUCScalarIScalarO, 1, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::unlock_memory), kIOUCScalarIScalarO, 1, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::finish), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::declare_image), kIOUCScalarIScalarO, 3, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::create_image), kIOUCScalarIScalarO, 2, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::create_transfer), kIOUCScalarIScalarO, 2, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::delete_image), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::wait_image), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_paging_options), kIOUCStructIStructO, 12, 12},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_vsync_options), kIOUCStructIStructO, 12, 12},
{0, reinterpret_cast<IOMethod>(&CLASS::set_macrovision), kIOUCScalarIScalarO, 1, 0},
// Note: Methods from NV2DContext
{0, reinterpret_cast<IOMethod>(&CLASS::read_configs), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::read_config_Ex), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::write_configs), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::write_config_Ex), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info2), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::kernel_printf), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
// Note: VM Methods
{0, reinterpret_cast<IOMethod>(&CLASS::CopyRegion), kIOUCScalarIStructI, 3, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::useAccelUpdates), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::RectCopy), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::RectFill), kIOUCScalarIStructI, 1, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::UpdateFramebufferAutoRing), kIOUCScalarIStructI, 0, 4 * sizeof(UInt32)}
};

extern "C" char VMLog_SendString(char const* str);

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (!targetP || index >= kIOVM2DNumMethods)
		return 0;
	if (index >= kIOVM2DUseAccelUpdates) {
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
#if LOGGING_LEVEL >= 1
	if (m_provider->getLogLevelAC() >= 2)
		VMLog_SendString("log IO2D: clientClose\n");
#endif
	if (surface_client) {
		surface_client->release();
		surface_client = 0;
		bTargetIsCGSSurface = false;
	}
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

#if 0
IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	return super::clientMemoryForType(type, options, memory);
}
#endif

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider) {
		return false;
	}
	return super::start(provider);
}

void CLASS::stop(IOService* provider)
{
	return super::stop(provider);
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_owning_task = 0;
	m_provider = 0;
	m_funcs_cache = 0;
	bTargetIsCGSSurface = false;
	surface_client = 0;
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
#pragma mark Private Methods
#pragma mark -

IOReturn CLASS::locateSurface(UInt32 surface_id)
{
	VMsvga2Accel::FindSurface fs;

	if (!m_provider)
		return kIOReturnNotReady;
	bzero(&fs, sizeof fs);
	fs.cgsSurfaceID = surface_id;
	m_provider->messageClients(kIOMessageFindSurface, &fs, sizeof fs);
	if (!fs.client)
		return kIOReturnNotFound;
	surface_client = OSDynamicCast(VMsvga2Surface, fs.client);
	if (!surface_client)
		return kIOReturnNotFound;
	surface_client->retain();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark GA Support Methods
#pragma mark -

IOReturn CLASS::CopyRegion(UInt32 source_surface_id, UInt32 destX, UInt32 destY, IOAccelDeviceRegion const* region, size_t regionSize)
{
	/*
	 * surface-to-surface copy is not supported yet.  Due to the way the code is designed,
	 *   this is really a pure guest-memory to guest-memory blit.  It can be done by two
	 *   steps via the host if desired.
	 *
	 * surface-to-framebuffer is not supported either.  It's not difficult to do, but the
	 *   WindowsServer blits by using surface_flush() and QuickTime blits with SwapSurface,
	 *   so it's not really necessary.
	 */
	if (source_surface_id)
		return kIOReturnUnsupported;
	if (!bTargetIsCGSSurface) {
		/*
		 * destination is framebuffer, use classic mode
		 */
		if (!m_provider)
			return kIOReturnNotReady;
		return m_provider->CopyRegion(destX, destY, region, regionSize);
	}
	/*
	 * destination is a surface
	 */
	if (!surface_client)
		return kIOReturnNotReady;
	return surface_client->context_copy_region(destX, destY, region, regionSize);
}

#pragma mark -
#pragma mark IONV2DContext Methods
#pragma mark -

IOReturn CLASS::set_surface(UInt32 surface_id, eIOContextModeBits options, void* output_struct, size_t* output_struct_size)
{
	UInt32 vmware_pixel_format, apple_pixel_format;
	IOReturn rc;

	if (!output_struct || !output_struct_size)
		return kIOReturnBadArgument;
	bzero(output_struct, *output_struct_size);
	if (surface_client) {
		surface_client->release();
		surface_client = 0;
	}
	/*
	 * options == 0x800 -- has surface id
	 *            0x400 -- UYVY format ('2vuy' for Apple)
	 *			  0x200 -- YUY2 format ('yuvs' for Apple)
	 *			  0x100 -- BGRA format
	 *            0 -- framebuffer, surface_id is framebufferIndex
	 */
	if (!(options & 0x800U)) {
		/*
		 * set target to framebuffer
		 */
		bTargetIsCGSSurface = false;
		if (surface_id != 0)		// Note: framebuffer index must be 0
			return kIOReturnUnsupported;
		return kIOReturnSuccess;
	}
	bTargetIsCGSSurface = true;
	/*
	 * Note: VMWARE_FOURCC_YV12 is not supported (planar 4:2:0 format)
	 */
	if (options & 0x400U) {
		vmware_pixel_format = VMWARE_FOURCC_UYVY;
		apple_pixel_format = kIO2vuyPixelFormat;
	} else if (options & 0x200U) {
		vmware_pixel_format = VMWARE_FOURCC_YUY2;
		apple_pixel_format = kIOYUVSPixelFormat;
	} else if (options & 0x100U) {
		vmware_pixel_format = 0;
		apple_pixel_format = kIO32BGRAPixelFormat;
	} else {
		vmware_pixel_format = 0;
		apple_pixel_format = 0;
	}
	rc = locateSurface(surface_id);
	if (rc != kIOReturnSuccess)
		return rc;
	return surface_client->context_set_surface(vmware_pixel_format, apple_pixel_format);
}

IOReturn CLASS::get_config(IOOptionBits* config_1, IOOptionBits* config_2)
{
	if (!config_1 || !config_2)
		return kIOReturnBadArgument;
	*config_1 = 0;
	/*
	 * TBD: transfer m_provider->getOptionsGA() as well
	 */
	if (m_provider)
		*config_2 = static_cast<IOOptionBits>(m_provider->getLogLevelGA());
	else
		*config_2 = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::get_surface_info1(UInt32, eIOContextModeBits, void *, size_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::swap_surface(IOOptionBits options, IOOptionBits* swapFlags)
{
	if (!bTargetIsCGSSurface)
		return kIOReturnUnsupported;
	if (!surface_client)
		return kIOReturnNotReady;
	surface_client->surface_flush_video();
	if (swapFlags)
		*swapFlags = 0;			// Note: setting swapflags = 2 tells the client to flush the CGS surface after the swap
	return kIOReturnSuccess;
}

IOReturn CLASS::scale_surface(IOOptionBits options, UInt32 width, UInt32 height)
{
	if (!bTargetIsCGSSurface)
		return kIOReturnUnsupported;
	if (!surface_client)
		return kIOReturnNotReady;
	return surface_client->context_scale_surface(options, width, height);
}

IOReturn CLASS::lock_memory(IOOptionBits options, vm_address_t* address, vm_size_t* rowBytes)
{
	if (!address || !rowBytes)
		return kIOReturnBadArgument;
	if (!bTargetIsCGSSurface)
		return kIOReturnUnsupported;
	if (!surface_client)
		return kIOReturnNotReady;
	return surface_client->context_lock_memory(m_owning_task, address, rowBytes);
}

IOReturn CLASS::unlock_memory(IOOptionBits options, IOOptionBits* swapFlags)
{
	if (!bTargetIsCGSSurface)
		return kIOReturnUnsupported;
	if (!surface_client)
		return kIOReturnNotReady;
	if (swapFlags)
		*swapFlags = 0;
	return surface_client->context_unlock_memory();
}

IOReturn CLASS::finish(IOOptionBits options)
{
#if 0
	if (!(options & (kIOBlitWaitAll2D | kIOBlitWaitAll)))
		return kIOReturnUnsupported;
#endif
	if (m_provider)
		return m_provider->SyncFIFO();
	return kIOReturnSuccess;
}

IOReturn CLASS::declare_image(UInt32, UInt32, UInt32, UInt32*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::create_image(UInt32, UInt32, UInt32*, UInt32*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::create_transfer(UInt32, UInt32, UInt32*, UInt32*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::delete_image(UInt32 image_id)
{
	return kIOReturnSuccess;
}

IOReturn CLASS::wait_image(UInt32 image_id)
{
	return kIOReturnSuccess;
}

IOReturn CLASS::set_surface_paging_options(IOSurfacePagingControlInfoStruct const*, IOSurfacePagingControlInfoStruct*, size_t, size_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_surface_vsync_options(IOSurfaceVsyncControlInfoStruct const*, IOSurfaceVsyncControlInfoStruct*, size_t, size_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::set_macrovision(IOOptionBits new_state)
{
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark NV2DContext Methods
#pragma mark -

IOReturn CLASS::read_configs(UInt32 const* input_struct, UInt32* output_struct, size_t input_struct_size, size_t* output_struct_size)
{
	if (!input_struct || !output_struct || !output_struct_size)
		return kIOReturnBadArgument;
	if (input_struct_size != sizeof(UInt32) || *output_struct_size != sizeof(UInt32))
		return kIOReturnBadArgument;
	if (*input_struct == 2)
		*output_struct = 2;
	else
		*output_struct = 0;
	return kIOReturnSuccess;
}

IOReturn CLASS::read_config_Ex(UInt32 const* input_struct, UInt32* output_struct, size_t input_struct_size, size_t* output_struct_size)
{
	if (!input_struct || !output_struct || !output_struct_size)
		return kIOReturnBadArgument;
	if (input_struct_size < 2 * sizeof(UInt32))
		return kIOReturnBadArgument;
	bzero(output_struct, *output_struct_size);
	switch (input_struct[0]) {
		case 144:
			/*
			 * GetBeamPosition
			 */
			if (*output_struct_size < 2 * sizeof(UInt32))
				return kIOReturnBadArgument;
			output_struct[1] = 0;
			break;
		case 288:
			/*
			 * SetSurface
			 */
			if (*output_struct_size < 3 * sizeof(UInt32))
				return kIOReturnBadArgument;
			output_struct[0] = 64;
			output_struct[2] = 16;
			break;
		default:
			return kIOReturnUnsupported;
	}
	return kIOReturnSuccess;
}

IOReturn CLASS::write_configs(UInt32 const*, size_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::write_config_Ex(UInt32 const*, size_t)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::get_surface_info2(UInt32 const*, UInt32*, size_t, size_t*)
{
	return kIOReturnUnsupported;
}

IOReturn CLASS::kernel_printf(char const*, size_t)
{
	return kIOReturnUnsupported;
}
