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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>

#include <pthread.h>
#include <search.h>

#include <glib.h>

#include "libIARMCore.h"
#include "libIBus.h"
#include "libIBusDaemon.h"
#include "libIBusDaemonInternal.h"
#include "iarmUtil.h"

#include "safec_lib.h"

typedef struct _IARM_Bus_CallContext_t {
	char ownerName[IARM_MAX_NAME_LEN];
	char methodName[IARM_MAX_NAME_LEN];
	IARM_BusCall_t handler;
}IARM_Bus_CallContext_t;

typedef struct _IARM_Bus_EventContext_t {
	char ownerName[IARM_MAX_NAME_LEN];
	char eventId;
	IARM_EventHandler_t handler;
}IARM_Bus_EventContext_t;

static GList *m_registeredCallList = NULL;
static GList *m_eventHandlerList = NULL;

#define IBUS_Lock(lock) pthread_mutex_lock(&m_Lock)
#define IBUS_Unlock(lock) pthread_mutex_unlock(&m_Lock)
static pthread_mutex_t m_Lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static IARM_Bus_Member_t *m_member = NULL;

static volatile int m_initialized = 0;
static volatile int m_connected = 0;
static IARM_Result_t Register(void);
static IARM_Result_t UnRegister(void);

static void _BusCall_FuncWrapper(void *callCtx, unsigned long methodID, void *arg, unsigned int serial);
static void _EventHandler_FuncWrapper (void *ctx, void *arg);

#define MAX_LOG_BUFF 512

IARM_Bus_LogCb logCb = NULL;

void IARM_Bus_RegisterForLog(IARM_Bus_LogCb cb)
{
    logCb = cb;
} 
int log(const char *format, ...)
{
    char tmp_buff[MAX_LOG_BUFF] = {0};
    va_list args;
    int ret = 0;

    ret = snprintf(tmp_buff,MAX_LOG_BUFF-1,"[tid=%ld]", syscall(SYS_gettid));
    if (ret <= 0) return 0;

    va_start(args, format);
    vsnprintf(&tmp_buff[ret],MAX_LOG_BUFF-1,format, args);
    va_end(args);

    if(logCb != NULL)
    {
        logCb(tmp_buff);
    }
    else
    {
        ret = printf(tmp_buff);
    }
    return ret;	
}

IARM_Result_t IARM_Bus_Init(const char *name)
{
    errno_t rc = -1;
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

        log("Entering  %s : %d  connected: :%d \r\n",__FUNCTION__, m_initialized,m_connected);

    IARM_ASSERT(!m_initialized && !m_connected);

    IBUS_Lock(lock);

	if (!m_initialized && !m_connected) {

		void *gctx = NULL;
		IARM_Init(IARM_BUS_NAME, name);
		IARM_Malloc(IARM_MEMTYPE_PROCESSSHARE, sizeof(IARM_Bus_Member_t), (void **) &m_member);
		
		rc = strcpy_s(m_member->selfName,sizeof(m_member->selfName), name);
		if(rc!=EOK)
		{
			ERR_CHK(rc);
		}

		m_member->pid = getpid();
		m_member->gctx = gctx;

		//log("setting init done\r\n");
		m_initialized = 1;
	}
	else {
    	retCode = IARM_RESULT_INVALID_PARAM;
	}

	IBUS_Unlock(lock);
	return retCode;
}

IARM_Result_t IARM_Bus_Term(void)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_ASSERT(m_initialized && !m_connected);

    IBUS_Lock(lock);
	log("term start init %d\r\n", m_initialized);

    if (m_initialized && !m_connected) {
    	
		{
            /*log("Removing registered event handlers %p\r\n",m_eventHandlerList);*/
 			GList *list;
			for (list = m_eventHandlerList; list != NULL ; list = list->next)
			{
				IARM_Bus_EventContext_t *cctx = (IARM_Bus_EventContext_t *)list->data;
				IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, (void *)cctx);
				
			}
			if (m_eventHandlerList != NULL )
			{
				g_list_free(m_eventHandlerList);
			}
            m_eventHandlerList = NULL;
		}    	

    	{
      
			/*log("Removing registered calls ..%p\r\n",m_registeredCallList);*/
			GList *list;
			for (list = m_registeredCallList; list != NULL ; list = list->next)
			{
				IARM_Bus_CallContext_t *cctx = (IARM_Bus_CallContext_t *)list->data;
				IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, (void *)cctx);
				
			}

			if(m_registeredCallList != NULL )
			{
				 g_list_free(m_registeredCallList);
			}
			m_registeredCallList = NULL;
    	}
		
        IARM_Free(IARM_MEMTYPE_PROCESSSHARE, m_member);
        m_member = NULL;
		m_initialized = 0;
		IARM_Term();

    }
    else {
    	log("NOT INITD\r\n");
    	retCode = IARM_RESULT_INVALID_PARAM;
    }

    IBUS_Unlock(lock);
	return retCode;
}

/**
 * @brief Initialize underlying module for the IARM member.
 * 
 * This API allows IARM member to join the U_I_D_E_V world and communicate
 * to UI Manager and other processes of the same group. The name of the member
 * must be unique among the process group that it is joining.
 *
 * @param [in] name Name of the member process.
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Bus_Connect(void)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

        log("Entering  %s : %d  connected: :%d \r\n",__FUNCTION__, m_initialized,m_connected);
    IARM_ASSERT(m_initialized && !m_connected);

    IBUS_Lock(lock);
    if (m_initialized && !m_connected) {
	retCode = Register();
        if (retCode == IARM_RESULT_SUCCESS) {
            m_connected = 1;
        }
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);

    return retCode;
}

/**
 * @brief Terminate IARM module.
 * 
 * This API allows IARM member to quit the U_I_D_E_V world and releases
 * resources it has allocated.
 *
 * @param None
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Bus_Disconnect(void)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);
    if (m_initialized && m_connected) {
        if (m_member) {
            UnRegister();
            m_connected = 0;
        }
        else {
        }
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);
    return retCode;
}

/**
 * @brief Publish an Asynchronous event.
 *
 * This API allows a process to notify other processes of an asynchronous event.
 * Upon return of this functioin, all listeners are notified of the event. But
 * the the listeners may not necessarily complete the processing of this event.
 *
 * This must not be called from an eventHandler.
 * eventData must be copied onto shared heap.
 *
 * @param [in] eventId The event to publish.
 * @param [in] eventData Data carried by this event.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Bus_BroadcastEvent(const char *ownerName, IARM_EventId_t eventId, void *data, size_t len)
{
    errno_t rc = -1;
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);
    if (!m_initialized || !m_connected) {
        retCode = IARM_RESULT_INVALID_STATE;
    }
	else if (strlen(ownerName) > IARM_MAX_NAME_LEN) {
        retCode = IARM_RESULT_INVALID_PARAM;
	}
    else if (data == NULL) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }
	else {
        IARM_EventData_t *eventData = NULL;
        IARM_Malloc(IARM_MEMTYPE_PROCESSSHARE, sizeof(IARM_EventData_t) + len, (void **)&eventData);
	
	rc=strcpy_s(eventData->owner,IARM_MAX_NAME_LEN, ownerName);
	if(rc!=EOK)
                {
                        ERR_CHK(rc);
                }

		eventData->id = eventId;
		eventData->len = len;
		
		rc = memcpy_s(&eventData->data, len, data, len);
		if(rc!=EOK)
		{
			ERR_CHK(rc);
		}

		/*log("[%s\r\n", __FUNCTION__);*/
        IARM_NotifyEvent(ownerName, (IARM_EventId_t)eventId, (void *)eventData);
    }
    IBUS_Unlock(lock);

    return retCode;
}

/**
 * @brief Register to listen for event.
 *
 * This API register to listen to event and provide the callback function for event notification.
 * Execution of the handler will not block the process sending the event.
 *
 * @param [in] eventId The event to listen to.
 * @param [in] handler THe callback function for event notification.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Bus_RegisterEventHandler(const char *ownerName, IARM_EventId_t eventId, IARM_EventHandler_t handler)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

	log("Entering [%s] - [%s][%d][%p]\r\n", __FUNCTION__, ownerName, eventId, handler);
        log("Entering  %s : %d  connected: :%d \r\n",__FUNCTION__, m_initialized,m_connected);

	IARM_ASSERT(m_initialized && m_connected);

	if (0 && strcmp(ownerName, m_member->selfName) == 0) {
		log("Cannot Listen for own event!\r\n");
		retCode = IARM_RESULT_INVALID_PARAM;
	}

    IBUS_Lock(lock);
    if (retCode == IARM_RESULT_SUCCESS && m_initialized && m_connected) {
        {
            IARM_Bus_EventContext_t *cctx = NULL;
    		retCode = IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(*cctx), (void **) &cctx);
    		strncpy(cctx->ownerName, ownerName, IARM_MAX_NAME_LEN);
    		cctx->eventId = eventId;
    		cctx->handler = handler;

            {
        	    m_eventHandlerList = g_list_prepend(m_eventHandlerList, cctx);
				/*log("Added Event Handler context  [%p] to list [%p]\r\n", cctx, m_eventHandlerList);*/
            }
        }

        if (retCode == IARM_RESULT_SUCCESS) {
        	IARM_RegisterListner(ownerName, (IARM_EventId_t) eventId, _EventHandler_FuncWrapper, NULL);
        }
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);

    return retCode;
}

/**
 * @brief Register to remove listen for event.
 *
 * This API remove the process from the listeners of the event.
 *
 * @param [in] eventId The event whose listener to remove.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Bus_UnRegisterEventHandler(const char *ownerName, IARM_EventId_t eventId)
{
    IARM_Result_t retCode = IARM_RESULT_IPCCORE_FAIL;

	IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

	if (m_initialized && m_connected) {
        IARM_Bus_EventContext_t *cctx = NULL;
		GList *event_list = g_list_first(m_eventHandlerList);

		if(event_list != NULL) {
			do
			{
				cctx = (IARM_Bus_EventContext_t *)event_list->data;
				if (strcmp(cctx->ownerName, ownerName) == 0 && cctx->eventId == eventId) {
					log("Event Handler [%s][%d] is removed\r\n", ownerName, eventId);
					m_eventHandlerList = g_list_remove(m_eventHandlerList, (void *)cctx);
					IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, (void *)cctx);
					retCode = IARM_RESULT_SUCCESS;
					break;
				}
			}while ((event_list = g_list_next(event_list)) != NULL);
		}
		IARM_UnRegisterListner(ownerName, (IARM_EventId_t)eventId);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }

    IBUS_Unlock(lock);
    return retCode;
}

/**
 * @brief Check if current process is registered with UI Manager.
 *
 * In the event of a crash at UI Manager, the manager may have lost memory of registered process.
 * A member process can call this function regularly to see if it is still registered.
 *
 * @param [out] isRegistered True if still registered.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Bus_IsConnected(const char *memberName, int *isRegistered)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);
   
    if (m_initialized && m_connected) {

        IARM_Bus_Daemon_CheckRegistration_Param_t req;
       
        strncpy(req.memberName,memberName,IARM_MAX_NAME_LEN - 1);

        req.isRegistered = 0;

        IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_CheckRegistration, (void *)&req, sizeof(IARM_Bus_Daemon_CheckRegistration_Param_t));

        if(req.isRegistered == 0) //on failure case, we need to return failure.
        {
            retCode = IARM_RESULT_IPCCORE_FAIL;
        }
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }

    IBUS_Unlock(lock);

    if(retCode == IARM_RESULT_SUCCESS)
    {
        *isRegistered = 1;
    }
    else
    {
        *isRegistered = 0;
    }

    return retCode;
}


/**
 * @brief Check if current process is registered with UI Manager.
 *
 * In the event of a crash at UI Manager, the manager may have lost memory of registered process.
 * A member process can call this function regularly to see if it is still registered.
 *
 * @param [out] isRegistered True if still registered.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Bus_RegisterCall(const char *methodName, IARM_BusCall_t handler)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
	static int count = -1;

	IARM_ASSERT(m_initialized && m_connected);

    if ( methodName == NULL || (strlen(methodName) >= IARM_MAX_NAME_LEN)) {
    	retCode = IARM_RESULT_INVALID_PARAM;
    }

    IBUS_Lock(lock);
	count++;


    if (retCode == IARM_RESULT_SUCCESS && m_initialized && m_connected) {
    	/*log("Entering [%s] - [%s][%p]\r\n", __FUNCTION__, methodName, handler);*/

        IARM_Bus_CallContext_t *cctx = NULL;
		retCode = IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(*cctx), (void **) &cctx);
		strncpy(cctx->ownerName, m_member->selfName, IARM_MAX_NAME_LEN);
		strncpy(cctx->methodName, methodName, IARM_MAX_NAME_LEN);
		cctx->handler = handler;
        IARM_RegisterCall(m_member->selfName, methodName, _BusCall_FuncWrapper, (void *)cctx/*callCtx*/);
        {
            /*log("Adding registered call context [%p] to list [%p]\r\n", cctx, m_registeredCallList);*/
            m_registeredCallList = g_list_prepend(m_registeredCallList, cctx);
        }
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);
    return retCode;
}


IARM_Result_t IARM_Bus_Call(const char *ownerName,  const char *methodName, void *arg, size_t argLen)
{
    errno_t rc = -1;
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

	IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {
        void *argOut = NULL;
        
        log("Final call to %s-%s\r\n", ownerName, methodName);
        if(arg != NULL)
        {
            retCode = IARM_Malloc(IARM_MEMTYPE_PROCESSSHARE, argLen, (void **)&argOut);
            
	    rc = memcpy_s(argOut, argLen, arg, argLen);
	    if(rc != EOK)
	    {
		    ERR_CHK(rc);
	    }

        }
        IARM_Call(ownerName, methodName, argOut, (int *)&retCode);
        if(argOut != NULL)
        {
            
	    rc = memcpy_s(arg, argLen, argOut, argLen);
	    if(rc != EOK)
            {
                    ERR_CHK(rc);
            }

            IARM_Free(IARM_MEMTYPE_PROCESSSHARE, argOut);
        }
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);

    return retCode;
}


IARM_Result_t IARM_Bus_RegisterEvent(int maxEventId)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
	/*log("Entering [%s] - [%d]\r\n", __FUNCTION__, maxEventId);*/

	IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);
    if (m_initialized && m_connected) {
        IARM_RegisterEvent(m_member->selfName, maxEventId);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);
    return retCode;
}

/**
 * @brief Request to grab resource
 *
 * Ask UI Manager to grant resoruce. Upon the success return, the resource is
 * available to use.
 *
 * @param [in] resrcType: Resource type.
 *
 * @return IARM_Result_t Error Code.
 */

IARM_Result_t IARM_BusDaemon_RequestOwnership(IARM_Bus_ResrcType_t resrcType)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    /*log("Entering IARM_BusDaemon_RequestOwnership\r\n");*/

	IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {
    	int isRegistered = 0;
    	IARM_IsCallRegistered(m_member->selfName, IARM_BUS_DAEMON_API_ReleaseOwnership, &isRegistered);
    	if (isRegistered) {
    		/*log("Requestor [%s] has ReleaseOwnership\r\n", m_member->selfName);*/
        	retCode = IARM_RESULT_SUCCESS;
    	}
    	else {
    		/*log("Requestor [%s] does NOT has ReleaseOwnership\r\n", m_member->selfName);*/
        	retCode = IARM_RESULT_INVALID_STATE;
    	}

    }

    if (retCode == IARM_RESULT_SUCCESS && m_initialized && m_connected) {
        IARM_Bus_Daemon_RequestOwnership_Param_t  req;
		req.requestor = m_member;
		req.resrcType = resrcType;
        req.rpcResult = IARM_RESULT_SUCCESS;
		retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_RequestOwnership, &req, sizeof(req));
        if(req.rpcResult != IARM_RESULT_SUCCESS)
        {
            retCode = req.rpcResult;
        }
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);
    /*log("Exiting IARM_BusDaemon_RequestOwnership\r\n");*/

    return retCode;
}

/**
 * @brief Notify UI Manager that the resource is released.
 *
 * Upon success return, this client is no longer the owner of the resource.
 *
 * @param [in] resrcType: Resource type.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_BusDaemon_ReleaseOwnership(IARM_Bus_ResrcType_t resrcType)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    /*log("Entering IARM_BusDaemon_ReleaseOwnership\r\n");*/

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);
    if (m_initialized && m_connected) {
    	IARM_Bus_Daemon_ReleaseOwnership_Param_t req;
		req.requestor = m_member;
		req.resrcType = resrcType;
        req.rpcResult = IARM_RESULT_SUCCESS;
		retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_ReleaseOwnership, &req, sizeof(req));
        if(req.rpcResult != IARM_RESULT_SUCCESS)
        {
            retCode = req.rpcResult;
        }
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);
    /*log("Exiting IARM_BusDaemon_ReleaseOwnership\r\n");*/

    return retCode;
}
/**
 * @brief Send power pre change command
 *
 * Command is broadcasted before power change and all those modules implementing 
 * power prechange function will be called. 
 *
 *
 * @return IARM_Result_t Error Code.
 */

IARM_Result_t IARM_BusDaemon_PowerPrechange(IARM_Bus_CommonAPI_PowerPreChange_Param_t preChangeParam)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    /*log("Entering IARM_BusDaemon_PowerPrechange\r\n");*/

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {

    IARM_Bus_Daemon_PowerPreChange_Param_t param;    
    param.newState = preChangeParam.newState;
    param.curState = preChangeParam.curState;      

    log("calling Daemon  PowerPrechange\r\n");

    retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_PowerPreChange, &param, sizeof(param));

    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);
    /*log("Exiting IARM_BusDaemon_PowerPrechange\r\n");*/

    return retCode;
}

/**
 * @brief Send Deep Sleep Wakeup command
 *
 * Command is broadcasted to Deep sleep Manager for wkeup from deep sleep.
 *
 *
 * @return IARM_Result_t Error Code.
 */

IARM_Result_t IARM_BusDaemon_DeepSleepWakeup(IARM_Bus_CommonAPI_PowerPreChange_Param_t preChangeParam)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    /*log("Entering IARM_BusDaemon_DeepSleepWakeup\r\n");*/

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {

    IARM_Bus_Daemon_PowerPreChange_Param_t param;    
    param.newState = preChangeParam.newState;
    param.curState = preChangeParam.curState;      

    log("calling Daemon  IARM_BusDaemon_DeepSleepWakeup\r\n");

    retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_DeepSleepWakeup, &param, sizeof(param));

    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);
    /*log("Exiting IARM_BusDaemon_DeepSleepWakeup\r\n");*/

    return retCode;
}




/**
 * @brief Send resolution pre change command
 *
 * Command is broadcasted before resolution change and all those modules implementing 
 * resolution prechange function will be called. 
 *
 * @param [in] preChangeParam: height, width etc.
 *
 *
 * @return IARM_Result_t Error Code.
 */

IARM_Result_t IARM_BusDaemon_ResolutionPrechange(IARM_Bus_CommonAPI_ResChange_Param_t preChangeParam)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    /*log("Entering IARM_BusDaemon_ResolutionPrechange\r\n");*/

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {
        IARM_Bus_Daemon_ResolutionChange_Param_t param;
        param.width = preChangeParam.width;
        param.height = preChangeParam.height;
        retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_ResolutionPreChange, &param, sizeof(param));
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);
    /*log("Exiting IARM_BusDaemon_ResolutionPrechange\r\n");*/

    return retCode;
}

/**
 * @brief Send resolution post change command
 *
 * Command is broadcasted after resolution change and all those modules implementing 
 * resolution post change handlers will be called. 
 *
 * @param [in] preChangeParam: height, width etc.
 *
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_BusDaemon_ResolutionPostchange(IARM_Bus_CommonAPI_ResChange_Param_t postChangeParam)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    /*log("Entering IARM_BusDaemon_ResolutionPostchange\r\n");*/

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {
        IARM_Bus_Daemon_ResolutionChange_Param_t param;
        param.width = postChangeParam.width;
        param.height = postChangeParam.height;
        retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_ResolutionPostChange, &param, sizeof(param));
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
    }
    IBUS_Unlock(lock);
   /* log("Exiting IARM_BusDaemon_ResolutionPostchange\r\n");*/

    return retCode;
}


/**
 * @brief Reigster this member to the UI Manager.
 *
 * This API allows IARM member notify UI Manager of its presence.
 *
 * @param None.
 *
 * @return IARM_Result_t Error Code.
 */
static IARM_Result_t Register(void)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    if (strcmp(m_member->selfName, IARM_BUS_DAEMON_NAME) != 0) {
        /*log("Registering %s\r\n", m_member->selfName);*/
    	IARM_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_RegisterMember, (void *)m_member, (int *)&retCode);
    }
    else {
        log("NOT Registering %s\r\n", m_member->selfName);
    }
    return retCode;
}

/**
 * @brief UnReigster this member to the UI Manager.
 *
 * This API allows IARM member notify UI Manager of its exit.
 *
 * @param None.
 *
 * @return IARM_Result_t Error Code.
 */
static IARM_Result_t UnRegister(void)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    if (strcmp(m_member->selfName, IARM_BUS_DAEMON_NAME) != 0) {
    	IARM_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_UnRegisterMember, (void *)m_member, (int *)&retCode);
    }
    return IARM_RESULT_SUCCESS;
}


static void _BusCall_FuncWrapper(void *callCtx, unsigned long methodID, void *arg, unsigned int serial)
{
	/*log("Entering [%s] - callCtx[%p]\r\n", __FUNCTION__, callCtx);*/

	IARM_Bus_CallContext_t *cctx = (IARM_Bus_CallContext_t *)callCtx;
	IARM_BusCall_t handler = (IARM_BusCall_t)cctx->handler;
	IARM_Result_t retCode = handler(arg);
	/*log("Returing [%s] - [%s][%s]\r\n", __FUNCTION__, cctx->ownerName, cctx->methodName);*/

	IARM_CallReturn(cctx->ownerName, cctx->methodName, retCode, serial);
}

static void _EventHandler_FuncWrapper (void *ctx, void *arg)
{
    IARM_EventData_t *eventData = (IARM_EventData_t *)arg;
	GList *event_list = g_list_first(m_eventHandlerList);
    IARM_Bus_EventContext_t *cctx = NULL;
    
	
	if (event_list != NULL){
		do	{
			cctx = (IARM_Bus_EventContext_t *)event_list->data;

			if ((strcmp(cctx->ownerName, eventData->owner) == 0) && (cctx->eventId == eventData->id)) {
                /*log("Event Handler [%s]for Event [%d] will be  invoked\r\n", eventData->owner, eventData->id);*/
                break;
            }
        }while ((event_list = g_list_next(event_list)) != NULL);
    

		if (cctx != NULL && cctx->handler != NULL) {
			/*log("Calling for event [%s][%d]\r\n", eventData->owner, cctx->eventId);*/
			cctx->handler(eventData->owner, eventData->id, (void *)&eventData->data, eventData->len);
			
		}
		else if(cctx != NULL && eventData != NULL) {
			log("NO  handler for event [%s][%d]\r\n", eventData->owner, cctx->eventId);
		}

	}
	
}
