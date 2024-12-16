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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#ifdef __cplusplus
extern "C" 
{
#endif
#include <direct/list.h>
#ifdef __cplusplus
}
#endif 
#include "libIARM.h"
#include "libIBus.h"
#include "libIBusDaemon.h"
#include "libIBusDaemonInternal.h"
#include "iarmUtil.h"

#include "libIARMCore.h"


#define direct_list_top(list) ((list))


#define IS_RESOURCETYPE_VALID(type)  (((type) >= 0) && ((type) < IARM_BUS_RESOURCE_MAX))
static IARM_Bus_Member_t *m_resourceOwner[IARM_BUS_RESOURCE_MAX] = {NULL};
static DirectLink *m_registeredList = NULL; /*TBD: Add skirmish protection*/

static IARM_Result_t _RegisterMember	    (void *arg);
static IARM_Result_t _UnRegisterMember	    (void *arg);
static IARM_Result_t _RequestOwnership	    (void *arg);
static IARM_Result_t _ReleaseOwnership	    (void *arg);
static IARM_Result_t _CheckRegistration	    (void *arg);
static IARM_Result_t _PowerPreChange        (void *arg);
static IARM_Result_t _ResolutionPreChange   (void *arg);
static IARM_Result_t _SysModeChange         (void *arg);

static void _dumpRegisteredMembers(void);

IARM_Result_t IARM_Bus_DaemonStart(int argc, char *argv[])
{
    int i = 0;
	char *settingsFile = NULL;

	if (argc == 2) settingsFile = argv[1];

    IARM_Bus_Init(IARM_BUS_DAEMON_NAME);
    IARM_Bus_Connect();

    IARM_Bus_RegisterEvent(IARM_BUS_SIGNAL_MAX);

    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_RequestOwnership,  	_RequestOwnership);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_ReleaseOwnership,  	_ReleaseOwnership);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_RegisterMember,    	_RegisterMember);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_UnRegisterMember,  	_UnRegisterMember);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_PowerPreChange,   	_PowerPreChange);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_CheckRegistration, 	_CheckRegistration);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_ResolutionPreChange, 	_ResolutionPreChange);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_ResolutionPostChange,  _ResolutionPostChange);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_SysModeChange, 	        _SysModeChange);

    for (i = 0; i < IARM_BUS_SIGNAL_MAX ; i++) {
    }

    return IARM_RESULT_SUCCESS;
}


IARM_Result_t IARM_Bus_DaemonStop(void)
{
    IARM_Bus_Disconnect();
    IARM_Bus_Term();
    return IARM_RESULT_SUCCESS;
}

/**
 *
 * @param [in] arg reference of IARM_Bus_Member_t of the caller trying to register with UIMgr. This is allocated from shared memory
 */
static IARM_Result_t _RegisterMember(void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Member_t *member = (IARM_Bus_Member_t *)arg;
	/*log("Entering [%s] - [%s]\r\n", __FUNCTION__, member->selfName);*/
    direct_list_append(&m_registeredList, &member->link);
    _dumpRegisteredMembers();
    return retCode;

}

static IARM_Result_t _UnRegisterMember(void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Member_t *member = (IARM_Bus_Member_t *)arg;
	/*log("Entering [%s] - [%s]\r\n", __FUNCTION__, member->selfName);*/
    direct_list_remove(&m_registeredList, &member->link);
    return retCode;
}

static IARM_Result_t _CheckRegistration(void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    int found = 0;
    IARM_Bus_Daemon_CheckRegistration_Param_t *pParam = (IARM_Bus_Daemon_CheckRegistration_Param_t*) arg; 
    IARM_Bus_Member_t *registeredMember = NULL;
    DirectLink *next = NULL;
    DirectLink *temp = NULL;
    direct_list_foreach_safe( next, temp, m_registeredList) {
       registeredMember = container_of(next, IARM_Bus_Member_t, link);
       if (strcmp(((IARM_Bus_Member_t *)registeredMember)->selfName, pParam->memberName) == 0) {
            pParam->isRegistered = 1;
            break;
        }
    }

    return retCode;
}


static IARM_Result_t _RequestOwnership(void *arg)
{
	/*log("EEEntering _RequestOwnership, doing stuff\r\n");*/
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_Bus_Daemon_RequestOwnership_Param_t *reqIn= (IARM_Bus_Daemon_RequestOwnership_Param_t *)arg;

    if (IS_RESOURCETYPE_VALID(reqIn->resrcType)) {
        IARM_Result_t rpcRet = IARM_RESULT_SUCCESS;
        /* make sure requestor does not already have focus */
        if (m_resourceOwner[reqIn->resrcType]) {
        	
			log("[%s] owns it, ask by [%s] to release\r\n", m_resourceOwner[reqIn->resrcType]->selfName, reqIn->requestor->selfName);

            if (strcmp(m_resourceOwner[reqIn->resrcType]->selfName, reqIn->requestor->selfName) != 0) {
            	IARM_Bus_CommonAPI_ReleaseOwnership_Param_t param;
            	param.resrcType = reqIn->resrcType;
            	retCode = IARM_Bus_Call(m_resourceOwner[reqIn->resrcType]->selfName, IARM_BUS_COMMON_API_ReleaseOwnership, &param, sizeof(param));
				if (rpcRet == IARM_RESULT_SUCCESS) {
					m_resourceOwner[reqIn->resrcType] = reqIn->requestor;
				}
				else {
					retCode = rpcRet;
				}
            }
            else {
            	log("Owner owns it, leave it\r\n");
                retCode = IARM_RESULT_INVALID_STATE;
            }
        }
        else {
        	log("Nobody owns it, granted to %s\r\n", reqIn->requestor->selfName);
            m_resourceOwner[reqIn->resrcType] = reqIn->requestor;
        }
    }
    else {
    	log("INvalid Resource\r\n");
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if(reqIn)
    reqIn->rpcResult = retCode;
    return retCode;
}

static IARM_Result_t _ReleaseOwnership(void *arg)
{
	/*log("EEEntering _ReleaseOwnership, doing stuff\r\n");*/

    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_Bus_Daemon_ReleaseOwnership_Param_t *reqIn= (IARM_Bus_Daemon_ReleaseOwnership_Param_t *)arg;

    if (IS_RESOURCETYPE_VALID(reqIn->resrcType)) {
        /* make sure requestor does already have focus */
        if (m_resourceOwner[reqIn->resrcType] && strcmp(m_resourceOwner[reqIn->resrcType]->selfName, reqIn->requestor->selfName) == 0) {
            /* TBD: Broadcast of focus availability */
        	log("Owner is releasing it. greatful\r\n");
            /* Assume request->requestor is from shared memory */
            m_resourceOwner[reqIn->resrcType] = NULL;
            {
            	IARM_Bus_EventData_t  eventData;
                eventData.resrcType = reqIn->resrcType;
		        IARM_Bus_BroadcastEvent(IARM_BUS_DAEMON_NAME,IARM_BUS_EVENT_RESOURCEAVAILABLE,&eventData,sizeof(eventData));
            }
        }
        else {
        	/*log("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXno body owns it so there is no is releasing it\r\n");*/
            retCode = IARM_RESULT_INVALID_STATE;
        }
    }
    else {
    	log("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXno invalid resource\r\n");
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if(reqIn)
    reqIn->rpcResult = retCode;

    return retCode;
}

static void _dumpRegisteredMembers(void)
{
	#if 0
    IARM_Bus_Member_t *registeredMember = NULL;
    DirectLink *next = NULL;
    DirectLink *temp = NULL;
    direct_list_foreach_safe( next, temp, m_registeredList) {
        registeredMember = container_of(next, IARM_Bus_Member_t, link);
        log("Registered Member [%s]\r\n", ((IARM_Bus_Member_t *)registeredMember)->selfName);
    }
	#endif
}

static IARM_Result_t _PowerPreChange(void *arg)
{
   log("EEEntering _PowerPreChange, doing stuff\r\n");

    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Member_t *registeredMember = NULL;
    DirectLink *next = NULL;
    DirectLink *temp = NULL;

    IARM_Bus_Daemon_PowerPreChange_Param_t *reqIn= (IARM_Bus_Daemon_PowerPreChange_Param_t *)arg;

    direct_list_foreach_safe( next, temp, m_registeredList) {
        int isRegistered = 0; 	
        IARM_Bus_CommonAPI_PowerPreChange_Param_t param;
        registeredMember = container_of(next, IARM_Bus_Member_t, link);
        IARM_IsCallRegistered(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_PowerPreChange, &isRegistered);
        if(isRegistered)
        {
            param.newState = reqIn->newState;
            param.curState = reqIn->curState;
            IARM_Bus_Call(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_PowerPreChange, &param, sizeof(param));
        }
    }
    log("EEExiting _PowerPrechange\n");
        
    return retCode; //As there could be multiple member calls, success of each is not checked.
}

static IARM_Result_t _ResolutionPreChange(void *arg)
{
   /* log("EEEntering _ResoultionPreChange, doing stuff\r\n");*/

    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_Bus_Member_t *registeredMember = NULL;
    DirectLink *next = NULL;
    DirectLink *temp = NULL;

    IARM_Bus_Daemon_ResolutionChange_Param_t *reqIn= (IARM_Bus_Daemon_ResolutionChange_Param_t *)arg;

    direct_list_foreach_safe( next, temp, m_registeredList) {
        int isRegistered = 0; 	
        IARM_Bus_CommonAPI_ResChange_Param_t param;
        registeredMember = container_of(next, IARM_Bus_Member_t, link);
        IARM_IsCallRegistered(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_ResolutionPreChange, &isRegistered);
        if(isRegistered)
        {
            param.width  = reqIn->width;
            param.height = reqIn->height; 
            IARM_Bus_Call(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_ResolutionPreChange, 
                                                                             &param, sizeof(param));
        }
    }

    return retCode; //As there could be multiple member calls, success of each is not checked.
}

static IARM_Result_t _SysModeChange(void *arg)
{
   /* log("Entering _SysModeChange, doing stuff\r\n");*/

    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_Bus_Member_t *registeredMember = NULL;
    DirectLink *next = NULL;
    DirectLink *temp = NULL;

    IARM_Bus_Daemon_SysModeChange_Param_t *reqIn= (IARM_Bus_Daemon_SysModeChange_Param_t *)arg;

    direct_list_foreach_safe( next, temp, m_registeredList) {
        int isRegistered = 0; 	
        IARM_Bus_CommonAPI_SysModeChange_Param_t param;
        registeredMember = container_of(next, IARM_Bus_Member_t, link);
        IARM_IsCallRegistered(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_SysModeChange, &isRegistered);
        if(isRegistered)
        {
            param.oldMode = reqIn->oldMode;
            param.newMode = reqIn->newMode; 
            IARM_Bus_Call(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_SysModeChange, 
                                                                             &param, sizeof(param));
        }
    }

    return retCode; //As there could be multiple member calls, success of each is not checked.
}
