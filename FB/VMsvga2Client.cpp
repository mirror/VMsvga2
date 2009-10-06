/*
 *  VMsvga2Client.cpp
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

#include <stdarg.h>
#include "VMsvga2Client.h"
#include "VMsvga2.h"

#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2Client, IOUserClient);

#define LOGPRINTF_PREFIX_STR "log IOFBClient: "
#define LOGPRINTF_PREFIX_LEN (sizeof LOGPRINTF_PREFIX_STR - 1)
#define LOGPRINTF_PREFIX_SKIP 4				// past "log "
#define LOGPRINTF_BUF_SIZE 256

static IOExternalMethod iofbFuncsCache[3] =
{
	{0, reinterpret_cast<IOMethod>(&VMsvga2Client::openUserClient), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&VMsvga2Client::closeUserClient), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&VMsvga2::CustomMode), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize}
};

void VMsvga2Client::LogPrintf(VMFBIOLog log_level, char const* fmt, ...)
{
	va_list ap;
	char print_buf[LOGPRINTF_BUF_SIZE];

	if (log_level > m_log_level)
		return;
	va_start(ap, fmt);
	strlcpy(&print_buf[0], LOGPRINTF_PREFIX_STR, sizeof print_buf);
	vsnprintf(&print_buf[LOGPRINTF_PREFIX_LEN], sizeof print_buf - LOGPRINTF_PREFIX_LEN, fmt, ap);
	va_end(ap);
	IOLog("%s", &print_buf[LOGPRINTF_PREFIX_SKIP]);
	if (!VMLog_SendString(&print_buf[0]))
		IOLog("%s: SendString failed.\n", __FUNCTION__);
}

IOReturn VMsvga2Client::clientDied()
{
	LogPrintf(2, "%s:\n", __FUNCTION__);
	return super::clientDied();
}

IOExternalMethod* VMsvga2Client::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	LogPrintf(4, "%s: index=%u\n", __FUNCTION__, index);
	if (!m_funcs_cache)	// Added
		return 0;
	switch (index) {
		case 1:
		case 2:
			*targetP = this;
			break;
		case 3:
			if (!m_provider)	// Added
				return 0;
			*targetP = m_provider;
			break;
		default:
			return 0;
	}
	return &m_funcs_cache[index - 1];
}

IOReturn VMsvga2Client::closeUserClient()
{
	LogPrintf(4, "%s:\n", __FUNCTION__);
	if (m_provider)
		return kIOReturnSuccess;
	LogPrintf(1, "%s: no provider\n", __FUNCTION__);
	return kIOReturnNotAttached;
}

IOReturn VMsvga2Client::clientClose()
{
	LogPrintf(2, "%s:\n", __FUNCTION__);
	closeUserClient();
	if (!terminate(0))
		LogPrintf(2, "%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

IOReturn VMsvga2Client::openUserClient()
{
	LogPrintf(4, "%s:\n", __FUNCTION__);
	if (!m_provider) {
		LogPrintf(1, "%s: no provider\n", __FUNCTION__);
		return kIOReturnNotAttached;
	}
	if (isInactive()) {
		LogPrintf(1, "%s: isInactive() is true, client forgot to call IOServiceOpen\n", __FUNCTION__);
		return kIOReturnNotAttached;
	}
	return kIOReturnSuccess;
}

bool VMsvga2Client::start(IOService* provider)
{
	m_log_level = 1;
	m_provider = OSDynamicCast(VMsvga2, provider);
	if (!m_provider) {
		LogPrintf(1, "%s: NULL provider return false\n", __FUNCTION__);
		return false;
	}
#if 0
	m_log_level = m_provider->m_log_level;
#else	// Added
	OSNumber* n = OSDynamicCast(OSNumber, m_provider->getProperty("VMwareSVGALogLevel"));
	if (n)
		m_log_level = static_cast<VMFBIOLog>(n->unsigned32BitValue());
#endif
	LogPrintf(4, "%s\n", __FUNCTION__);
	return super::start(provider);
}

void VMsvga2Client::stop(IOService* provider)
{
	LogPrintf(4, "%s\n", __FUNCTION__);
	return super::stop(provider);
}

bool VMsvga2Client::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
#if 0
	IOLog("VMsvga2Client::initWithTask(%p, %p, %u)\n", owningTask, securityToken, type);
#endif
	m_log_level = 1;
	m_funcs_cache = 0;
	if (!super::initWithTask(owningTask, securityToken, type)) {
		LogPrintf(1, "%s: super initWithTask failed\n", __FUNCTION__);
		return false;
	}
	m_owning_task = owningTask;
	m_provider = 0;
	m_funcs_cache = &iofbFuncsCache[0];
	return true;
}
