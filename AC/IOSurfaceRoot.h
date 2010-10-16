/*
 *  IOSurfaceRoot.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on October 16th 2010.
 *  Copyright 2010 Zenith432. All rights reserved.
 *
 */

#ifndef __IOSURFACEROOT_H__
#define __IOSURFACEROOT_H__

#include <IOKit/IOService.h>

class IOSurfaceRoot : public IOService
{
	OSDeclareDefaultStructors(IOSurfaceRoot);

public:
	virtual void* find_surface(unsigned, task_t, class IOSurfaceRootUserClient*);
	virtual void add_surface_buffer(class IOSurface*);
	virtual void remove_surface_buffer(class IOSurface*);
	virtual void* createSurface(task_t, OSDictionary*);
	virtual void* lookupSurface(unsigned, task_t);
	virtual unsigned generateUniqueAcceleratorID(void*);
	virtual void updateLimits(unsigned, unsigned, unsigned, unsigned, unsigned);
};

#endif /* __IOSURFACEROOT_H__ */
