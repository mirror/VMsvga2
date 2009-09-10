/*
 *  VMsvga2Client.h
 *  VMsvga2
 *
 *  Created by Zenith432 on July 4th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#ifndef _VMSVGA2CLIENT_H_
#define _VMSVGA2CLIENT_H_

#include <IOKit/IOUserClient.h>
#include "common_fb.h"

class VMsvga2;

class VMsvga2Client: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga2Client);

private:
	task_t m_owning_task;				// offset 0x78
	VMsvga2* m_provider;				// offset 0x7C
	IOExternalMethod* m_funcs_cache;	// offset 0x80
	VMFBIOLog m_log_level;				// offset 0x84

	void LogPrintf(VMFBIOLog log_level, char const* fmt, ...);

public:
	IOReturn clientDied();
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn closeUserClient();
	IOReturn clientClose();
	IOReturn openUserClient();
	bool start(IOService* provider);
	void stop(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
};

#endif /* _VMSVGA2CLIENT_H_ */
