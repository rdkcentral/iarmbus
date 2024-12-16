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
* @defgroup template
* @{
**/


#include <stdio.h>
#include <unistd.h>
#include "iarmUtil.h"
#include "libIARM.h"
#include "libIBus.h"
#include "barMgr.h"
#include "barMgrInternal.h"

static IARM_Result_t _dummyAPI0	(void *arg);
static IARM_Result_t _dummyAPI1	(void *arg);

IARM_Result_t IARM_BarMgr_Start(int argc, char *argv[])
{

    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    /* If any, Process argc, argv */

    /*
     * Register and Connect to the IARM Bus
     */
    IARM_Bus_Init(IARM_BUS_BARMGR_NAME);
    IARM_Bus_Connect();

    /*
     * Register Events that this module publishes
     */
    IARM_Bus_RegisterEvent(IARM_BUS_BARMGR_EVENT_MAX);

    /*
     * Register APIs that can be RPCs by other entities on the bus.
     */
     IARM_Bus_RegisterCall(IARM_BUS_BARMGR_API_DummyAPI0,  	_dummyAPI0);
     IARM_Bus_RegisterCall(IARM_BUS_BARMGR_API_DummyAPI1,  	_dummyAPI1);

    return retCode;
}

void IARM_BarMgr_Loop()
{
	int i = 0;
	while (++i) {
		/* Infinite Loop listening for component specific events from drivers */
		printf("[%s]: Heartbeat\r\n", __FUNCTION__);
		IARM_Bus_BarMgr_EventData_t eventData;
		/* Populate Event Data Here */
		eventData.data.dummy0.dummyData = i;
		IARM_Bus_BroadcastEvent(IARM_BUS_BARMGR_NAME, IARM_BUS_BARMGR_EVENT_DUMMY0, &eventData, sizeof(eventData));
		sleep(10);
	}
}

IARM_Result_t IARM_BarMgr_Stop(void)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
	IARM_Bus_Disconnect();
    IARM_Bus_Term();
    return retCode;
}

/**
 *
 * @param [in] arg reference of IARM_Bus_Member_t of the caller trying to register with UIMgr. This is allocated from shared memory
 */
static IARM_Result_t _dummyAPI0(void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_BarMgr_DummyAPI0_Param_t *param = (IARM_Bus_BarMgr_DummyAPI0_Param_t *)arg;
    param = param;
    /*
     * Implementation;
     */
    return retCode;

}

static IARM_Result_t _dummyAPI1(void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_BarMgr_DummyAPI1_Param_t *param = (IARM_Bus_BarMgr_DummyAPI1_Param_t *)arg;
    param = param;
    /*
     * Implementation;
     */
    return retCode;
}





/** @} */
/** @} */
