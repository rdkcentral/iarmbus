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

extern IARM_Result_t IARM_Bus_DaemonStart(int argc, char *argv[]);
extern IARM_Result_t IARM_Bus_DaemonStop(void);

int main()
{
	printf("server Entering %d\r\n", getpid());
    //IARM_Bus_DaemonStart(0, NULL);
    int i = 0;
    int timedur = 10;;
    int sleepus = 5 * 1000 * 1000;
    while(i++ < timedur) {
    	printf("Send HeartBeat %d\r\n", i);
    	IARM_Bus_EventData_t eventData;
    	eventData.resrcType = (IARM_Bus_ResrcType_t)i;
    	if (i % 3 == 0) {
    		IARM_Bus_BroadcastEvent(IARM_BUS_DAEMON_NAME, IARM_BUS_EVENT_RESOURCEAVAILABLE, (void*) &eventData, sizeof(eventData));
    	}
    	else if (i % 3 == 1) {

    	}
    	else {

    	}
    	usleep(sleepus);

    	printf("Send HeartBeat DONE\r\n");
    }
    //IARM_Bus_DaemonStop();
}



/** @} */
/** @} */
