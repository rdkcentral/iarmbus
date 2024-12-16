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


#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "libIBus.h"
#include "barMgr.h"

static void _dummyEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len);

int main(int argc, char *argv[])
{
	int i = 0;
    /*
     * Register and Connect to the IARM Bus.
     *
     */
    IARM_Bus_Init("SampleApp");
    IARM_Bus_Connect();

    /*
     * After connection, SampleApp can communicate with entities on the IARM Bus.
     * 1. Register Handlers for BarMgr's published events.
     * 2. Invoke RPC calls to BarMgr's published methods.
     */
    IARM_Bus_RegisterEventHandler(IARM_BUS_BARMGR_NAME, IARM_BUS_BARMGR_EVENT_DUMMY1, _dummyEventHandler);
    IARM_Bus_RegisterEventHandler(IARM_BUS_BARMGR_NAME, IARM_BUS_BARMGR_EVENT_DUMMY2, _dummyEventHandler);

    while (i < 20) {
    	i++;
    	printf("SampleApp heartbeat\r\n");
    	IARM_Bus_BarMgr_DummyAPI0_Param_t param0;
    	IARM_Bus_BarMgr_DummyAPI1_Param_t param1;

    	/* populate param0, param1 here */
    	IARM_Bus_Call(IARM_BUS_BARMGR_NAME, IARM_BUS_BARMGR_API_DummyAPI0, (void *)&param0, sizeof(param0));
    	IARM_Bus_Call(IARM_BUS_BARMGR_NAME, IARM_BUS_BARMGR_API_DummyAPI1, (void *)&param1, sizeof(param1));
    	sleep(10);
    }

    IARM_Bus_Disconnect();
    IARM_Bus_Term();
}


static void _dummyEventHandler(const char *owner, IARM_EventId_t eventId, void *data, size_t len)
{

	if (strcmp(owner, IARM_BUS_BARMGR_NAME) == 0) {
		/* Handle BarMgr's events here */
		IARM_Bus_BarMgr_EventData_t *eventData = (IARM_Bus_BarMgr_EventData_t *)data;

		switch(eventId) {
		case IARM_BUS_BARMGR_EVENT_DUMMY0:
			break;
		case IARM_BUS_BARMGR_EVENT_DUMMY1:
			break;
		case IARM_BUS_BARMGR_EVENT_DUMMY2:
			break;
		}
	}
	else if (/* compare owner compare other names */1) {
		/* Handle Another owner's events here */
	}
	else {
	}
}


/** @} */
/** @} */
