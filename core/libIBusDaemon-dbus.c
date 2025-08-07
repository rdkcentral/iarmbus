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
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <glib.h>
#ifdef __cplusplus
extern "C" 
{
#endif
#ifdef __cplusplus
}
#endif 
#include "libIARM.h"
#include "libIBus.h"
#include "libIBusDaemon.h"
#include "libIBusDaemonInternal.h"
#include "iarmUtil.h"

#include "libIARMCore.h"

#include "safec_lib.h"

#define g_list_top(list) ((list))


#define IS_RESOURCETYPE_VALID(type)  (((type) >= 0) && ((type) < IARM_BUS_RESOURCE_MAX))
static IARM_Bus_Member_t *m_resourceOwner[IARM_BUS_RESOURCE_MAX] = {NULL};
static GList *m_registeredList = NULL; /*TBD: Add skirmish protection*/

static GList *m_preChangeRpcList = NULL; 


static IARM_Result_t _RegisterMember	    (void *arg);
static IARM_Result_t _UnRegisterMember	    (void *arg);
static IARM_Result_t _RequestOwnership	    (void *arg);
static IARM_Result_t _ReleaseOwnership	    (void *arg);
static IARM_Result_t _CheckRegistration	    (void *arg);
static IARM_Result_t _PowerPreChange        (void *arg);
static IARM_Result_t _DeepSleepWakeup        (void *arg);
static IARM_Result_t _ResolutionPreChange   (void *arg);
static IARM_Result_t _ResolutionPostChange   (void *arg);
static IARM_Result_t _SysModeChange         (void *arg);
static IARM_Result_t _RegisterPreChange     (void *arg);

static void _dumpRegisteredMembers(void);
static bool _IsRegistered(const char *memberName);
static bool IsPreChangeRegistered(char *compId);
static void _dumpPrechangeList(void);

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
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_DeepSleepWakeup,       _DeepSleepWakeup);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_CheckRegistration, 	_CheckRegistration);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_ResolutionPreChange, 	_ResolutionPreChange);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_ResolutionPostChange,  _ResolutionPostChange);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_SysModeChange,     	_SysModeChange);
    IARM_Bus_RegisterCall(IARM_BUS_DAEMON_API_RegisterPreChange,        _RegisterPreChange);
    
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

static IARM_Bus_Member_t * _findRegisteredMember(char *name)
{
    IARM_Bus_Member_t *registeredMember = NULL;
	GList *l = NULL;
	for (l = m_registeredList; l != NULL; l = l->next)
	{
		registeredMember = container_of((GList *)l->data, IARM_Bus_Member_t, link);
		if (strcmp(((IARM_Bus_Member_t *)registeredMember)->selfName, name) == 0) 
		{
			 //log("In[%s]- Registered member found [%s], returning member\r\n", __FUNCTION__, registeredMember->selfName);
			 return registeredMember;
		}
	}
	
	//log("In[%s]- Registered member NOT found, returning NULL\r\n", __FUNCTION__);

    return NULL;
}
   
/**
 *
 * @param [in] arg reference of IARM_Bus_Member_t of the caller trying to register with UIMgr. This is allocated from shared memory
 */
static IARM_Result_t _RegisterMember(void *arg)
{
    IARM_Bus_Member_t *member = (IARM_Bus_Member_t *) malloc(sizeof(IARM_Bus_Member_t));
    errno_t rc = -1;

    if (NULL == arg)
	    return IARM_RESULT_INVALID_PARAM;
    rc = memcpy_s(member,sizeof(IARM_Bus_Member_t), arg, sizeof(IARM_Bus_Member_t));
    if(rc!=EOK)
    {
	    ERR_CHK(rc);
    }

    //log("Entering [%s] - [%s]\r\n", __FUNCTION__, member->selfName);

    if ((strlen(member->selfName)) && _IsRegistered(member->selfName)) {
        log("Found and Unregistering old instance of client - [%s]\r\n", member->selfName);
        _UnRegisterMember (member);
    }

    m_registeredList = g_list_append(m_registeredList, &member->link);
    _dumpRegisteredMembers();
    return IARM_RESULT_SUCCESS;
}

static IARM_Result_t _UnRegisterMember(void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Member_t *member = (IARM_Bus_Member_t *)arg;
    IARM_Bus_Member_t *registeredMember = _findRegisteredMember(member->selfName);

    /*log("Entering [%s] - [%s]\r\n", __FUNCTION__, member->selfName);*/

    if (registeredMember == NULL)
    {
        log("The member %s is not found registered \n",member->selfName);
        return IARM_RESULT_INVALID_STATE;
    }

    m_registeredList = g_list_remove(m_registeredList, &registeredMember->link);
    /* If a member still controls the resource, warn and release it */
    {
        int i = 0;
        for (i = 0; i < IARM_BUS_RESOURCE_MAX; i++) {
            if (m_resourceOwner[i] == registeredMember) {
                printf("WARN: [%s] - [%s] still owns resource [%d], force release\r\n", __FUNCTION__, member->selfName, i);
                m_resourceOwner[i] = NULL;
            }
        }
    }
	free(registeredMember);
    return retCode;
}

static IARM_Result_t _CheckRegistration(void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Daemon_CheckRegistration_Param_t *pParam = (IARM_Bus_Daemon_CheckRegistration_Param_t*) arg; 
   
    pParam->isRegistered = _IsRegistered(pParam->memberName) ? 1 : 0;
  
    return retCode;
}

static bool _IsRegistered(const char *memberName)
{
    IARM_Bus_Member_t *registeredMember = NULL;
	GList *l = NULL;
	for (l = m_registeredList; l != NULL; l = l->next)
	{
		registeredMember = container_of((GList *)l->data, IARM_Bus_Member_t, link);
		if (strcmp(((IARM_Bus_Member_t *)registeredMember)->selfName,memberName) == 0) 
		{
			 //log("In[%s]- Registered member found [%s], returning TRUE\r\n", __FUNCTION__, registeredMember->selfName);
			 return true;
		}
	}
	
	//log("In[%s]- Registered member NOT found, returning FALSE\r\n", __FUNCTION__);

    return false;
	
}


static IARM_Result_t _RequestOwnership(void *arg)
{
	/*log("EEEntering _RequestOwnership, doing stuff\r\n");*/
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_Bus_Daemon_RequestOwnership_Param_t *reqIn= (IARM_Bus_Daemon_RequestOwnership_Param_t *)arg;
    if (! reqIn) {
        log("reqIn is NULL\r\n");
        return IARM_RESULT_INVALID_PARAM;
    }
    IARM_Bus_Member_t *registeredMember = _findRegisteredMember(reqIn->requestor.selfName);

    if (IS_RESOURCETYPE_VALID(reqIn->resrcType)) {
        IARM_Result_t rpcRet = IARM_RESULT_SUCCESS;
        /* make sure requestor does not already have focus */
        if (m_resourceOwner[reqIn->resrcType]) {
        	log("[%s] owns it, ask by [%s] to release\r\n", m_resourceOwner[reqIn->resrcType]->selfName, registeredMember->selfName);

            if (strcmp(m_resourceOwner[reqIn->resrcType]->selfName, registeredMember->selfName) != 0) {
            	IARM_Bus_CommonAPI_ReleaseOwnership_Param_t param;
            	param.resrcType = reqIn->resrcType;
            	rpcRet = IARM_Bus_Call(m_resourceOwner[reqIn->resrcType]->selfName, IARM_BUS_COMMON_API_ReleaseOwnership, &param, sizeof(param));
				if (rpcRet == IARM_RESULT_SUCCESS) {
					m_resourceOwner[reqIn->resrcType] = registeredMember;
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
        	log("Nobody owns it, granted to %s\r\n", reqIn->requestor.selfName);
            m_resourceOwner[reqIn->resrcType] = registeredMember;
        }
    }
    else {
    	log("INvalid Resource\r\n");
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    reqIn->rpcResult = retCode;
    return retCode;
}

static IARM_Result_t _ReleaseOwnership(void *arg)
{
	/*log("EEEntering _ReleaseOwnership, doing stuff\r\n");*/

    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_Bus_Daemon_ReleaseOwnership_Param_t *reqIn= (IARM_Bus_Daemon_ReleaseOwnership_Param_t *)arg;

    if (! reqIn) {
        log("reqIn is NULL\r\n");
        return IARM_RESULT_INVALID_PARAM;
    }

    IARM_Bus_Member_t *registeredMember = _findRegisteredMember(reqIn->requestor.selfName);

    if (IS_RESOURCETYPE_VALID(reqIn->resrcType)) {
        /* make sure requestor does already have focus */
        if (m_resourceOwner[reqIn->resrcType] && strcmp(m_resourceOwner[reqIn->resrcType]->selfName, registeredMember->selfName) == 0) {
            /* TBD: Broadcast of focus availability */
        	log("Owner is releasing it. greatful\r\n");

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

    reqIn->rpcResult = retCode;
    return retCode;
}
  
static IARM_Result_t _PowerPreChange(void *arg)
{
    /*log("EEEntering _PowerPreChange, doing stuff\r\n");*/
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Member_t *registeredMember = NULL;
    GList *l = NULL;
    
	IARM_Bus_Daemon_PowerPreChange_Param_t *reqIn= (IARM_Bus_Daemon_PowerPreChange_Param_t *)arg;

    for (l = m_registeredList; l != NULL; l = l->next)
	{
        char compId[2*IARM_MAX_NAME_LEN] = {0};
        registeredMember = container_of((GList *)l->data, IARM_Bus_Member_t, link);
        snprintf(compId, sizeof(compId), "%s_%s",((IARM_Bus_Member_t *)registeredMember)->selfName,IARM_BUS_COMMON_API_PowerPreChange);
        if(IsPreChangeRegistered(&compId[0]))
        {
            IARM_Bus_CommonAPI_PowerPreChange_Param_t param;
            param.newState = reqIn->newState;
            param.curState = reqIn->curState;
            IARM_Bus_Call(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_PowerPreChange, &param, sizeof(param));
	    }
    }
   return retCode; //As there could be multiple member calls, success of each is not checked.
}



static IARM_Result_t _DeepSleepWakeup(void *arg)
{
    /*log("EEEntering _DeepSleepWakeup, doing stuff\r\n");*/
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Member_t *registeredMember = NULL;
    GList *l = NULL;
    
    IARM_Bus_Daemon_PowerPreChange_Param_t *reqIn= (IARM_Bus_Daemon_PowerPreChange_Param_t *)arg;

    for (l = m_registeredList; l != NULL; l = l->next)
    {
        char compId[2*IARM_MAX_NAME_LEN] = {0};
        registeredMember = container_of((GList *)l->data, IARM_Bus_Member_t, link);
        snprintf(compId, sizeof(compId), "%s_%s",((IARM_Bus_Member_t *)registeredMember)->selfName,IARM_BUS_COMMON_API_DeepSleepWakeup);
        if(IsPreChangeRegistered(&compId[0]))
        {
            IARM_Bus_CommonAPI_PowerPreChange_Param_t param;
            param.newState = reqIn->newState;
            param.curState = reqIn->curState;
            IARM_Bus_Call(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_DeepSleepWakeup, &param, sizeof(param));
        }
    }
   return retCode; //As there could be multiple member calls, success of each is not checked.
}



static IARM_Result_t _ResolutionPreChange(void *arg)
{
    /*log("EEEntering _ResoultionPreChange, doing stuff\r\n");*/

    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Member_t *registeredMember = NULL;
    GList *l = NULL;
    
    IARM_Bus_Daemon_ResolutionChange_Param_t *reqIn= (IARM_Bus_Daemon_ResolutionChange_Param_t *)arg;

    for (l = m_registeredList; l != NULL; l = l->next)
	{
        char compId[2*IARM_MAX_NAME_LEN] = {0};
        registeredMember = container_of((GList *)l->data, IARM_Bus_Member_t, link);
        snprintf(compId, sizeof(compId), "%s_%s",((IARM_Bus_Member_t *)registeredMember)->selfName,IARM_BUS_COMMON_API_ResolutionPreChange);

        if(IsPreChangeRegistered(&compId[0]))
        {
            IARM_Bus_CommonAPI_ResChange_Param_t param;
            param.width  = reqIn->width;
            param.height = reqIn->height; 
            IARM_Bus_Call(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_ResolutionPreChange, 
                            &param, sizeof(param));
        }
    }

    return retCode; //As there could be multiple member calls, success of each is not checked.
}

static IARM_Result_t _ResolutionPostChange(void *arg)
{
    /*log("EEEntering _ResolutionPostChange, doing stuff\r\n");*/

    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Member_t *registeredMember = NULL;
    GList *l = NULL;
    IARM_Bus_Daemon_ResolutionChange_Param_t *reqIn= (IARM_Bus_Daemon_ResolutionChange_Param_t *)arg;

    for (l = m_registeredList; l != NULL; l = l->next)
    {
        char compId[2*IARM_MAX_NAME_LEN] = {0};
        registeredMember = container_of((GList *)l->data, IARM_Bus_Member_t, link);
        snprintf(compId, sizeof(compId), "%s_%s",((IARM_Bus_Member_t *)registeredMember)->selfName,IARM_BUS_COMMON_API_ResolutionPostChange);

        if(IsPreChangeRegistered(&compId[0]))
        {
            IARM_Bus_CommonAPI_ResChange_Param_t param;
            param.width  = reqIn->width;
            param.height = reqIn->height; 
            IARM_Bus_Call(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_ResolutionPostChange, 
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
    GList *l = NULL;
    IARM_Bus_Daemon_SysModeChange_Param_t *reqIn= (IARM_Bus_Daemon_SysModeChange_Param_t *)arg;

	for (l = m_registeredList; l != NULL; l = l->next)
	{
        char compId[2*IARM_MAX_NAME_LEN] = {0};	
        registeredMember = container_of((GList *)l->data, IARM_Bus_Member_t, link);
        snprintf(compId, sizeof(compId), "%s_%s",((IARM_Bus_Member_t *)registeredMember)->selfName,IARM_BUS_COMMON_API_SysModeChange);    
        
        if(IsPreChangeRegistered(&compId[0]))
        {
            IARM_Bus_CommonAPI_SysModeChange_Param_t param;
            param.oldMode = reqIn->oldMode;
            param.newMode = reqIn->newMode; 
            IARM_Bus_Call(((IARM_Bus_Member_t *)registeredMember)->selfName, IARM_BUS_COMMON_API_SysModeChange, 
                                                                                        &param, sizeof(param));
        }
    }

    return retCode; //As there could be multiple member calls, success of each is not checked.
}


/**
 *
 * @param [in] arg reference of IARM_Bus_Daemon_RegisterPreChange_Param_t of the caller trying to register with Daemon. 
 */
static IARM_Result_t _RegisterPreChange(void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Bus_Daemon_RegisterPreChange_Param_t *PreChangeMember = (IARM_Bus_Daemon_RegisterPreChange_Param_t *)arg;
    unsigned int size = (sizeof(char)*2*IARM_MAX_NAME_LEN); 
    char* compId = (char*)malloc(size);
    
    if (NULL != compId)
    {
        snprintf(compId,size,"%s_%s", PreChangeMember->ownerName,PreChangeMember->methodName);
        if(false == IsPreChangeRegistered(compId))
        {    
            m_preChangeRpcList = g_list_append (m_preChangeRpcList,compId);
           //_dumpPrechangeList();
        }
        else
        {
            free(compId);
        }
    }    
    return retCode;
}

static bool IsPreChangeRegistered(char *name)
{
    GList *l = NULL;
    for (l = m_preChangeRpcList; l != NULL; l = l->next)
    {
        if (strcmp((char *)l->data,name) == 0) 
        {
            //log("Found [%s] \r\n", (char *)l->data);
             return true;
        }
    }
    return false;
}

static void _dumpPrechangeList(void)
{   

    #if 0
    GList *l = NULL;
    for (l = m_preChangeRpcList; l != NULL; l = l->next)
    { 
        log("Registered Method are [%s] \r\n", (char *)l->data);
    }
    #endif
}


static void _dumpRegisteredMembers(void)
{   
#if 0
  
    IARM_Bus_Member_t *registeredMember = NULL;
    GList *l = NULL;
    for (l = m_registeredList; l != NULL; l = l->next)
        { 
            registeredMember = container_of((GList *)l->data, IARM_Bus_Member_t, link);
            log("Registered Member [%s]\r\n", ((IARM_Bus_Member_t *)registeredMember)->selfName);
    }
    
                   
 #endif
}
