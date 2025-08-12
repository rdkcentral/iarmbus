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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <search.h>

#include <glib.h>

#include "libIBus.h"
#include "libIBusDaemon.h"
#include "libIARMCore.h"
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
static IARM_Result_t RegisterPreChange(IARM_Bus_CallContext_t *callCtx);

static void _BusCall_FuncWrapper(void *callCtx, unsigned long methodID, void *arg, void *serial);
static void _EventHandler_FuncWrapper (void *ctx, void *arg);

#define MAX_LOG_BUFF 200

IARM_Bus_LogCb logCb = NULL;

void IARM_Bus_RegisterForLog(IARM_Bus_LogCb cb)
{
    logCb = cb;
} 
int log(const char *format, ...)
{
    char tmp_buff[MAX_LOG_BUFF];
    va_list args;
    va_start(args, format);
    vsnprintf(tmp_buff,MAX_LOG_BUFF-1,format, args);
    va_end(args);
    if(logCb != NULL)
    {
        logCb(tmp_buff);
    }
    else
    {
        return printf(tmp_buff);
    }
    return 0;	
}

IARM_Result_t IARM_Bus_Init(const char *name)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    errno_t rc = -1;

    IARM_ASSERT(!m_initialized && !m_connected);

    IBUS_Lock(lock);

	if (!m_initialized && !m_connected) {

		void *gctx = NULL;
        retCode = IARM_Init(IARM_BUS_NAME, name);
        if (IARM_RESULT_SUCCESS == retCode) {
            IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(IARM_Bus_Member_t), (void **) &m_member);

	    rc = sprintf_s(m_member->selfName,sizeof(m_member->selfName), "%s", name);
	    if(rc < EOK)
	    {
		    ERR_CHK(rc);
	    }

            m_member->pid = getpid();
            m_member->gctx = gctx;

            log("setting init done\r\n");
            m_initialized = 1;
        }
        else {
            log("%s init failed\r\n", __FUNCTION__);
        }
	}
	else {
    	retCode = IARM_RESULT_INVALID_STATE;
		log("%s [%s] Component Already registered with IARM; Invalid state\n", __FUNCTION__, name);
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
            //log("Removing registered event handlers %p\r\n",m_eventHandlerList);
 			GList *list;
			for (list = m_eventHandlerList; list != NULL ; list = list->next)
			{
				IARM_Bus_EventContext_t *cctx = (IARM_Bus_EventContext_t *)list->data;
				IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, (void *)cctx);
				
			}
            g_list_free(m_eventHandlerList);
			m_eventHandlerList = NULL;
		}    	

    	{
      
			//log("Removing registered calls ..%p\r\n",m_registeredCallList);
			GList *list;
			for (list = m_registeredCallList; list != NULL ; list = list->next)
			{
				IARM_Bus_CallContext_t *cctx = (IARM_Bus_CallContext_t *)list->data;
				IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, (void *)cctx);
				
			}
            g_list_free(m_registeredCallList);
			m_registeredCallList = NULL;
    	}

        IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, m_member);
        m_member = NULL;
		m_initialized = 0;
		retCode = IARM_Term();
        if (retCode != IARM_RESULT_SUCCESS) {
            log("%s Term Failed\n", __FUNCTION__);
        }
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
		log("%s invalid state\n", __FUNCTION__);
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
		log("%s invalid state\n", __FUNCTION__);
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
		log("%s invalid state\n", __FUNCTION__);
    }
	else if (strlen(ownerName) > IARM_MAX_NAME_LEN) {
        retCode = IARM_RESULT_INVALID_PARAM;
		log("%s invalid component name\n", __FUNCTION__);
	}
    else if (data == NULL) {
        retCode = IARM_RESULT_INVALID_PARAM;
		log("%s invalid input data\n", __FUNCTION__);
    }
	else {
        IARM_EventData_t *eventData = NULL;
        IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(IARM_EventData_t) + len, (void **)&eventData);
		strncpy(eventData->owner, ownerName, IARM_MAX_NAME_LEN -1);
		eventData->owner[IARM_MAX_NAME_LEN -1] = '\0';
		eventData->id = eventId;
		eventData->len = len;
		
		rc = memcpy_s(&eventData->data, len, data, len);
		if(rc!=EOK)
		{
			ERR_CHK(rc);
		}

		//log("[%s\r\n", __FUNCTION__);
        retCode = IARM_NotifyEvent(ownerName, (IARM_EventId_t)eventId, (void *)eventData);
        if (retCode != IARM_RESULT_SUCCESS) {
            log("%s failed to send notification\n", __FUNCTION__);
        }
        IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, eventData);
    }
    IBUS_Unlock(lock);

    return retCode;
}


static gint _Is_Context_Handler_Matching(gconstpointer pa, gconstpointer pb)
{
    const IARM_Bus_EventContext_t *a = (IARM_Bus_EventContext_t *)pa;
    const IARM_Bus_EventContext_t *b = (IARM_Bus_EventContext_t *)pb;

    return (a->eventId != b->eventId) || (a->handler != b->handler) || strncmp(a->ownerName, b->ownerName,IARM_MAX_NAME_LEN);
}

static gint _Is_Context_Matching(gconstpointer pa, gconstpointer pb)
{
    const IARM_Bus_EventContext_t *a = (IARM_Bus_EventContext_t *)pa;
    const IARM_Bus_EventContext_t *b = (IARM_Bus_EventContext_t *)pb;

    return (a->eventId != b->eventId) || strncmp(a->ownerName, b->ownerName,IARM_MAX_NAME_LEN);
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

	//log("Entering [%s] - [%s][%d][%p]\r\n", __FUNCTION__, ownerName, eventId, handler);

	IARM_ASSERT(m_initialized && m_connected);

	if (0 && strcmp(ownerName, m_member->selfName) == 0) {
		log("Cannot Listen for own event!\r\n");
		retCode = IARM_RESULT_INVALID_PARAM;
        return retCode;
	}

    if (NULL == handler) {
        log("Cannot Register for NULL Handler!\r\n");
        retCode = IARM_RESULT_INVALID_PARAM;
         return retCode;
    }

    IBUS_Lock(lock);
    if (retCode == IARM_RESULT_SUCCESS && m_initialized && m_connected) {
        {
            IARM_Bus_EventContext_t *cctx = NULL;
    		retCode = IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(*cctx), (void **) &cctx);
    		strncpy(cctx->ownerName, ownerName, IARM_MAX_NAME_LEN);
    		cctx->eventId = eventId;
    		cctx->handler = handler;

            if(g_list_find_custom(m_eventHandlerList,cctx,_Is_Context_Handler_Matching))
            {
        	    log("Error: Owner [%s] tries to resgister duplicate handler for events ID [%d]\r\n", ownerName, eventId);
                IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, (void *)cctx);
                retCode = IARM_RESULT_INVALID_PARAM;
            }
            else
            {
    	        //log("Adding Event Handler for [%s][%d] \r\n", ownerName, eventId);   
                if( NULL ==  g_list_find_custom(m_eventHandlerList,cctx,_Is_Context_Matching) )
                {
                    m_eventHandlerList = g_list_prepend(m_eventHandlerList, cctx);
                    retCode = IARM_RegisterListner(ownerName, (IARM_EventId_t) eventId, _EventHandler_FuncWrapper, NULL);
                    if (retCode != IARM_RESULT_SUCCESS) {
                        log("%s failed add listener for [%s][%d] with IARM Core Handler \r\n", __FUNCTION__, ownerName, eventId);
                    }
                    //log("Added event  [%s][%d] with IARM Core Handler \r\n", ownerName, eventId);
                }
                else
                {
                    m_eventHandlerList = g_list_prepend(m_eventHandlerList, cctx);
                }
            }
        }
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s failed for eventID:%d of %s as this process is in invalid state isInitialized:%d isConnected:%d \n", __FUNCTION__, eventId, ownerName, m_initialized, m_connected);
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
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

	if (m_initialized && m_connected) {
        IARM_Bus_EventContext_t *cctx = NULL;
        retCode = IARM_RESULT_INVALID_PARAM;
		GList *event_list = g_list_first(m_eventHandlerList);
		while(event_list != NULL) {
			cctx = (IARM_Bus_EventContext_t *)event_list->data;
            event_list = g_list_next(event_list);
			if (strncmp(cctx->ownerName, ownerName,IARM_MAX_NAME_LEN) == 0 && cctx->eventId == eventId) {
				//log("Event Handler [%s][%d] is removed\r\n", ownerName, eventId);
                m_eventHandlerList = g_list_remove(m_eventHandlerList, (void *)cctx);
			    IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, (void *)cctx);
			}
		}
        retCode = IARM_UnRegisterListner(ownerName, (IARM_EventId_t)eventId);
        if (retCode != IARM_RESULT_SUCCESS)
             log("%s failed remove listener for [%s][%d] with IARM Core Handler \r\n", __FUNCTION__, ownerName, eventId);
        else
             log("%s Listener removed for [%s][%d] with IARM Core Handler \r\n", __FUNCTION__, ownerName, eventId); 
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s failed for eventID:%d of %s as this process is in invalid state isInitialized:%d isConnected:%d \n", __FUNCTION__, eventId, ownerName, m_initialized, m_connected);
    }

    IBUS_Unlock(lock);
    return retCode;
}


/**
 * @brief Remove specific handler  registered for the given event.
 *
 * This API remove the specific handlers.   
 * @param[in] ownerName The well-known name of the application.
 * @param [in] eventId The event whose listener to remove.
 * @param [in] handler The event handler to remove.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Bus_RemoveEventHandler(const char *ownerName, IARM_EventId_t eventId, IARM_EventHandler_t handler)
{
    IARM_Result_t retCode = IARM_RESULT_IPCCORE_FAIL;

    IARM_ASSERT(m_initialized && m_connected);

    if (NULL == handler) {
        log("Cannot Remove Handle  for NULL Handler!\r\n");
        retCode = IARM_RESULT_INVALID_PARAM;
         return retCode;
    }

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {
        
        IARM_Bus_EventContext_t *cctx = NULL;
        GList *event_list = g_list_first(m_eventHandlerList);
        retCode = IARM_RESULT_INVALID_PARAM;
        while(event_list != NULL) {
            cctx = (IARM_Bus_EventContext_t *)event_list->data;
            if (strncmp(cctx->ownerName, ownerName,IARM_MAX_NAME_LEN) == 0 && (cctx->eventId == eventId)
                && (cctx->handler == handler)) 
            {
               log("Event Handler with Handler [%p] for [%s] event [%d] is removed\r\n",handler,ownerName, eventId);
                
                /* Remove from the Master Event List */
                m_eventHandlerList = g_list_remove(m_eventHandlerList, (void *)cctx);
                retCode = IARM_RESULT_SUCCESS;

                /* Now search that if we have still same event registered with diff handler */
                if( NULL == g_list_find_custom(m_eventHandlerList,cctx,_Is_Context_Matching) )
                {
                    retCode = IARM_UnRegisterListner(ownerName, (IARM_EventId_t)eventId);
                    if (retCode != IARM_RESULT_SUCCESS)
                        log("%s Failed Remove event  [%s][%d] with IARM Core \r\n", __FUNCTION__, ownerName, eventId);
                    else
                        log("%s Removed event  [%s][%d] with IARM Core \r\n", __FUNCTION__, ownerName, eventId);
                }
                IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, (void *)cctx);
                break;
            }
            else
            {
                event_list = g_list_next(event_list);
            }   
        }

    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s failed for eventID:%d of %s as this process is in invalid state isInitialized:%d isConnected:%d \n", __FUNCTION__, eventId, ownerName, m_initialized, m_connected);
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

    IBUS_Lock(lock);
    if (m_initialized && m_connected) {

        IARM_Bus_Daemon_CheckRegistration_Param_t req;

        strncpy(req.memberName,memberName,IARM_MAX_NAME_LEN - 1);

        req.isRegistered = 0;

        IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_CheckRegistration, (void *)&req, sizeof(IARM_Bus_Daemon_CheckRegistration_Param_t));

        if(req.isRegistered == 0) //on failure case, we need to return failure.
        {
		    log("%s failed to communicate with daemon\n", __FUNCTION__);
            retCode = IARM_RESULT_IPCCORE_FAIL;
        }
    }
    else {
		log("%s invalid state\n", __FUNCTION__);
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
		log("%s invalid input passed\n", __FUNCTION__);
        return retCode;
    }

    IBUS_Lock(lock);
	count++;


    if (retCode == IARM_RESULT_SUCCESS && m_initialized && m_connected) {
    	//log("Entering [%s] - [%s][%p]\r\n", __FUNCTION__, methodName, handler);

        IARM_Bus_CallContext_t *cctx = NULL;
		retCode = IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(*cctx), (void **) &cctx);
		strncpy(cctx->ownerName, m_member->selfName, IARM_MAX_NAME_LEN -1);
		cctx->ownerName[IARM_MAX_NAME_LEN -1] = '\0';
		strncpy(cctx->methodName, methodName, IARM_MAX_NAME_LEN -1);
		cctx->methodName[IARM_MAX_NAME_LEN -1] = '\0';
		cctx->handler = handler;
        retCode = IARM_RegisterCall(m_member->selfName, methodName, _BusCall_FuncWrapper, (void *)cctx/*callCtx*/);
        {
           //log("Adding registered call context [%p] to list [%p]\r\n", cctx, m_registeredCallList);
            m_registeredCallList = g_list_prepend(m_registeredCallList, cctx);
        }
        if (IARM_RESULT_SUCCESS != retCode)
           log("%s failed register call [%s]\n", __FUNCTION__, methodName);
        
        RegisterPreChange(cctx);           
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s invalid state\n", __FUNCTION__);
    }
    IBUS_Unlock(lock);
    return retCode;
}


IARM_Result_t IARM_Bus_Call_with_IPCTimeout(const char *ownerName,  const char *methodName, void *arg, size_t argLen, int timeout)
{
    errno_t rc = -1;
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

	IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {
        void *argOut = NULL;
        
        //log("Final call to %s-%s\r\n", ownerName, methodName);

        /* even if there is no arg we still need to send _IARM_MEM_EXTRA_ALLOC_SIZE byte header allocated by IARM_Malloc, in this case use 1 byte dummy arg */
        retCode = IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, (arg != NULL) ? argLen : 1, (void **)&argOut);
        
        if (retCode == IARM_RESULT_SUCCESS) {
            if(arg != NULL)
            {
                
		rc = memcpy_s(argOut,argLen, arg, argLen);
		if(rc!=EOK)
		{
			ERR_CHK(rc);
		}

            }
            IARM_Result_t retVal = IARM_RESULT_SUCCESS;
            retCode = IARM_CallWithTimeout(ownerName, methodName, argOut, timeout, (int *)&retVal);
            if ((retCode == IARM_RESULT_SUCCESS) && (argOut != NULL))
            {
                
	    	rc = memcpy_s(arg,argLen,argOut,argLen);
		if(rc!=EOK)
                {
                        ERR_CHK(rc);
                }

            }
            IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, argOut);
            if(retCode == IARM_RESULT_SUCCESS)
            {
                retCode = retVal;
            }
            if(retCode != IARM_RESULT_SUCCESS)
		        log("%s failed to complete the method invocation %s with retCode %d \n", __FUNCTION__, methodName, retCode);
        }
        else
		    log("%s failed to allocated memory for the method invocation %s with retCode %d \n", __FUNCTION__, methodName, retCode);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s invalid state\n", __FUNCTION__);
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
        
        //log("Final call to %s-%s\r\n", ownerName, methodName);

        /* even if there is no arg we still need to send _IARM_MEM_EXTRA_ALLOC_SIZE byte header allocated by IARM_Malloc, in this case use 1 byte dummy arg */
        retCode = IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, (arg != NULL) ? argLen : 1, (void **)&argOut);
        
        if (retCode == IARM_RESULT_SUCCESS) {
            if(arg != NULL)
            {
                
		rc = memcpy_s(argOut,argLen, arg, argLen);
		if(rc!=EOK)
		{
			ERR_CHK(rc);
		}

            }
            IARM_Result_t retVal = IARM_RESULT_SUCCESS;
            retCode = IARM_Call(ownerName, methodName, argOut, (int *)&retVal);
            if ((retCode == IARM_RESULT_SUCCESS) && (argOut != NULL))
            {
                
		rc = memcpy_s(arg,argLen,argOut,argLen);
		if(rc!=EOK)
                {
                        ERR_CHK(rc);
                }

            }
            IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, argOut);
            if(retCode == IARM_RESULT_SUCCESS)
            {
                retCode = retVal;
            }
            else
                log("%s failed to invoke %s with retCode %d \n", __FUNCTION__, methodName, retCode);

            if(retCode != IARM_RESULT_SUCCESS)
                log("%s provider returned error (%d) for the method %s \n", __FUNCTION__, retCode, methodName);
        }
        else
            log("%s failed to allocated memory for the method invocation %s with retCode %d \n", __FUNCTION__, methodName, retCode);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s failed to call %s as this process is in invalid state isInitialized:%d isConnected:%d \n", __FUNCTION__, methodName, m_initialized, m_connected);
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
        retCode = IARM_RegisterEvent(m_member->selfName, maxEventId);
        if(retCode != IARM_RESULT_SUCCESS)
            log("%s failed to register event %d with retCode %d \n", __FUNCTION__, maxEventId, retCode);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s invalid state\n", __FUNCTION__);
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
    errno_t rc = -1;
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    //log("Entering IARM_BusDaemon_RequestOwnership\r\n");

	IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

    if( (resrcType < 0) || (resrcType >= IARM_BUS_RESOURCE_MAX))
    {
        IBUS_Unlock(lock);
        return IARM_RESULT_INVALID_PARAM;
    }

    if (m_initialized && m_connected) {
    	int isRegistered = 0;
    	IARM_IsCallRegistered(m_member->selfName, IARM_BUS_DAEMON_API_ReleaseOwnership, &isRegistered);
    	if (isRegistered) {
    //		log("Requestor [%s] has ReleaseOwnership\r\n", m_member->selfName);
        	retCode = IARM_RESULT_SUCCESS;
    	}
    	else {
    		log("Requestor [%s] does NOT has ReleaseOwnership\r\n", m_member->selfName);
        	retCode = IARM_RESULT_INVALID_STATE;
    	}

    }

    if (retCode == IARM_RESULT_SUCCESS && m_initialized && m_connected) {
        IARM_Bus_Daemon_RequestOwnership_Param_t  req;
        
	rc = memcpy_s(&req.requestor,sizeof(req.requestor), m_member, sizeof(IARM_Bus_Member_t));
	if(rc!=EOK)
	{
		ERR_CHK(rc);
	}

		req.resrcType = resrcType;
        req.rpcResult = IARM_RESULT_SUCCESS;

		retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_RequestOwnership, &req, sizeof(req));
        if(req.rpcResult != IARM_RESULT_SUCCESS)
        {
            retCode = req.rpcResult;
        }
        if(retCode != IARM_RESULT_SUCCESS)
            log("%s failed to request ownership with retCode %d \n", __FUNCTION__, retCode);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s invalid state\n", __FUNCTION__);
    }
    IBUS_Unlock(lock);
   // log("Exiting IARM_BusDaemon_RequestOwnership\r\n");

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
    errno_t rc = -1;
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    /*log("Entering IARM_BusDaemon_ReleaseOwnership\r\n");*/

    IARM_ASSERT(m_initialized && m_connected);


    if( (resrcType < 0) || (resrcType >= IARM_BUS_RESOURCE_MAX))
    {
		log("%s invalid input\n", __FUNCTION__);
        return IARM_RESULT_INVALID_PARAM;
    }

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {
    	IARM_Bus_Daemon_ReleaseOwnership_Param_t req;
        
	rc = memcpy_s(&req.requestor,sizeof(req.requestor), m_member, sizeof(IARM_Bus_Member_t));
	if(rc!=EOK)
	{
		ERR_CHK(rc);
	}

		req.resrcType = resrcType;
        req.rpcResult = IARM_RESULT_SUCCESS;
		retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_ReleaseOwnership, &req, sizeof(req));
        if(req.rpcResult != IARM_RESULT_SUCCESS)
        {
            retCode = req.rpcResult;
        }
        if(retCode != IARM_RESULT_SUCCESS)
            log("%s failed to request ownership with retCode %d \n", __FUNCTION__, retCode);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s invalid state\n", __FUNCTION__);
    }
    IBUS_Unlock(lock);
   /* log("Exiting IARM_BusDaemon_ReleaseOwnership\r\n");*/

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
    //log("Entering IARM_BusDaemon_PowerPrechange\r\n");

    IARM_ASSERT(m_initialized && m_connected);

    IBUS_Lock(lock);

    if (m_initialized && m_connected) {

    IARM_Bus_Daemon_PowerPreChange_Param_t param;    
    param.newState = preChangeParam.newState;
    param.curState = preChangeParam.curState;      

    //log("calling Daemon  PowerPrechange\r\n");

	retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_PowerPreChange, &param, sizeof(param));

    if(retCode != IARM_RESULT_SUCCESS)
        log("%s failed to invoke PowerPreChange method with retCode %d \n", __FUNCTION__, retCode);

    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s invalid state\n", __FUNCTION__);
    }
    IBUS_Unlock(lock);
    //log("Exiting IARM_BusDaemon_PowerPrechange\r\n");

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

    if(retCode != IARM_RESULT_SUCCESS)
        log("%s failed to invoke DeepSleepWakeup method with retCode %d \n", __FUNCTION__, retCode);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s invalid state\n", __FUNCTION__);
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

        if(retCode != IARM_RESULT_SUCCESS)
            log("%s failed to invoke ResolutionPreChange method with retCode %d \n", __FUNCTION__, retCode);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s invalid state\n", __FUNCTION__);
    }
    IBUS_Unlock(lock);
   /* log("Exiting IARM_BusDaemon_ResolutionPrechange\r\n");*/

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
        if(retCode != IARM_RESULT_SUCCESS)
            log("%s failed to invoke ResolutionPostChange method with retCode %d \n", __FUNCTION__, retCode);
    }
    else {
        retCode = IARM_RESULT_INVALID_STATE;
		log("%s invalid state\n", __FUNCTION__);
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
    IARM_Result_t rc = IARM_RESULT_SUCCESS;
    if (strcmp(m_member->selfName, IARM_BUS_DAEMON_NAME) != 0) {
        log("Registering %s\r\n", m_member->selfName);
    	rc = IARM_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_RegisterMember, (void *)m_member, (int *)&retCode);
        if((rc != IARM_RESULT_SUCCESS) || (retCode != IARM_RESULT_SUCCESS))
            log(" %s failed to invoke RegisterMember method  rc:%d retCode:%d \n", __FUNCTION__,rc,retCode);
    }
    else {
        log("NOT Registering %s\r\n", m_member->selfName);
    }
    if(rc != IARM_RESULT_SUCCESS) retCode = rc;
  
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
    IARM_Result_t rc = IARM_RESULT_SUCCESS;
    if (strcmp(m_member->selfName, IARM_BUS_DAEMON_NAME) != 0) {
    	IARM_Call(IARM_BUS_DAEMON_NAME, IARM_BUS_DAEMON_API_UnRegisterMember, (void *)m_member, (int *)&retCode);
        if((rc != IARM_RESULT_SUCCESS) || (retCode != IARM_RESULT_SUCCESS))
            log("%s failed to invoke UnRegisterMember method\n", __FUNCTION__);
    }
    return IARM_RESULT_SUCCESS;
}

/**
 * @brief Reigster Prechange API with Daemon.
 *
 * This API allows Daemon to have all Prechange call Records.
 *
 * @param None.
 *
 * @return IARM_Result_t Error Code.
 */
static IARM_Result_t RegisterPreChange(IARM_Bus_CallContext_t *callCtx)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;

    if (strcmp(m_member->selfName, IARM_BUS_DAEMON_NAME) != 0) {

        IARM_Bus_Daemon_RegisterPreChange_Param_t callParam;
        
        if ( (strcmp(callCtx->methodName,IARM_BUS_COMMON_API_ResolutionPreChange)  == 0) ||
             (strcmp(callCtx->methodName,IARM_BUS_COMMON_API_ResolutionPostChange)  == 0) ||
             (strcmp(callCtx->methodName,IARM_BUS_COMMON_API_PowerPreChange)  == 0) ||
             (strcmp(callCtx->methodName,IARM_BUS_COMMON_API_DeepSleepWakeup)  == 0) ||
             (strcmp(callCtx->methodName,IARM_BUS_COMMON_API_SysModeChange)  == 0) ) 
        {    
            //log("Registering Prechange %s-%s\r\n", callCtx->ownerName,callCtx->methodName);   
            strncpy(callParam.ownerName,callCtx->ownerName, IARM_MAX_NAME_LEN-1);
	    callParam.ownerName[IARM_MAX_NAME_LEN-1] = '\0';
            strncpy(callParam.methodName,callCtx->methodName, IARM_MAX_NAME_LEN-1);
	    callParam.methodName[IARM_MAX_NAME_LEN-1] = '\0';   //CID:136737 - Buffer size
            retCode = IARM_Bus_Call(IARM_BUS_DAEMON_NAME,IARM_BUS_DAEMON_API_RegisterPreChange, &callParam, sizeof(callParam));
            if(retCode != IARM_RESULT_SUCCESS)
                log("%s failed to invoke RegisterPreChange method with retCode %d \n", __FUNCTION__, retCode);
        }
        else {
            //log("Not Registering Prechange %s -%s\r\n", callCtx->ownerName,callCtx->methodName);
        }

    }
    return retCode;
}


static void _BusCall_FuncWrapper(void *callCtx, unsigned long methodID, void *arg, void *serial)
{
	//log("Entering [%s] - callCtx[%p]\r\n", __FUNCTION__, callCtx);

	IARM_Bus_CallContext_t *cctx = (IARM_Bus_CallContext_t *)callCtx;
	IARM_BusCall_t handler = (IARM_BusCall_t)cctx->handler;
	IARM_Result_t retCode = handler(arg);
	//log("Returing [%s] - [%s][%s]\r\n", __FUNCTION__, cctx->ownerName, cctx->methodName);

	IARM_CallReturn(cctx->ownerName, cctx->methodName, arg, retCode, serial);
}

static void _EventHandler_FuncWrapper (void *ctx, void *arg)
{
    IARM_EventData_t *eventData = (IARM_EventData_t *)arg;
    IARM_Bus_EventContext_t *cctx = NULL;

    IBUS_Lock(lock);

    GList *event_list = g_list_first(m_eventHandlerList);

    if (event_list != NULL  && eventData != NULL)
    {
        do
        {
            cctx = (IARM_Bus_EventContext_t *)event_list->data;
            if (cctx != NULL)
            {
                if ((strncmp(cctx->ownerName, eventData->owner,IARM_MAX_NAME_LEN) == 0)
                        && (cctx->eventId == eventData->id))
		{
                    //log("Event Handler [%s]for Event [%d] will be  invoked\r\n", eventData->owner, eventData->id);
                    if (cctx->handler != NULL)
                        cctx->handler(eventData->owner, eventData->id, (void *)&eventData->data, eventData->len);
                }
            } else {
                    log("Event Handler got cctx NULL.\r\n");
            }
        } while ((event_list = g_list_next(event_list)) != NULL);
    }
    IBUS_Unlock(lock);
}

#define PID_BUF_SIZE 100
/**
 * @brief Write PID file
 *
 * This API allows Daemon to write PID file
 *
 * @param full pathname to pidfile to write
 */
void IARM_Bus_WritePIDFile(const char *path)
{
    char buf[PID_BUF_SIZE];

    log("Writing PID file %s\n", path);
    int fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        log("ERROR opening PID file %s\n", path);
    }
    else
    {
        int len = snprintf(buf, PID_BUF_SIZE, "%ld\n", (long) getpid());
        if ((len <= 0) || (write(fd, buf, len) != len))
        {
            log("ERROR writing to PID file %s\n", path);
        }
        close(fd);
    }
} 
