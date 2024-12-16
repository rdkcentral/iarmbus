/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2016 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/


/**
* @defgroup iarmbus
* @{
* @defgroup test
* @{
**/


#include "libIBus.h"
#include "libIBusDaemon.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


static void _handler_ResourceAvailable(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
{
	IARM_Bus_EventData_t *busEventData = (IARM_Bus_EventData_t *)data;
	printf("=======================_handler_ResourceAvailable Received Event [%d] from [%s] for [%d]", eventId, owner, busEventData->resrcType);
}

static void _handler_PostChange(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
{
	IARM_Bus_EventData_t *busEventData = (IARM_Bus_EventData_t *)data;
	printf("=============================_=handler_PostChange Received Event [%d] from [%s] for [%d]", eventId, owner, busEventData->resrcType);
}

static void _handler_PreChange(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
{
	IARM_Bus_EventData_t *busEventData = (IARM_Bus_EventData_t *)data;
	printf("================================_handler_PreChange Received Event [%d] from [%s] for [%d]", eventId, owner, busEventData->resrcType);
}

int main()
{
	printf("EventClient Entering %d\r\n", getpid());
	IARM_Bus_Init("Client");
	int i = 0;
	IARM_Bus_Connect();
	IARM_Bus_RegisterEventHandler(IARM_BUS_DAEMON_NAME, IARM_BUS_EVENT_RESOURCEAVAILABLE, 	_handler_ResourceAvailable);

	while(++i < 5) {
		printf("Client Hearbeat\r\n");
		sleep(5);
	}
	IARM_Bus_Disconnect();
	IARM_Bus_Term();
	printf("Client Exiting\r\n");
}


/** @} */
/** @} */
