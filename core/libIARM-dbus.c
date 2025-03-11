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

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include "iarmUtil.h"

#include "safec_lib.h"

#ifdef __cplusplus
extern "C"
{
#endif
#ifdef __cplusplus
}
#endif
#include <glib.h>
#include "libIARM.h"
#include "libIARMCore.h"
#include "libIBusDaemonInternal.h"
#include <dbus/dbus.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "iarmUtil.h"

#define _IARM_MEM_MAGIC_SIZE    8
#define _IARM_MEM_PREFIX_SIZE   sizeof(size_t)

#define _IARM_MEM_DEBUG
#define _IARM_CTX_HEADER_MAGIC  0xCABFACE1

#ifdef _IARM_MEM_DEBUG
#define _IARM_MEM_HEADER_MAGIC  0xBADBEEF0
#define _IARM_MEM_EXTRA_ALLOC_SIZE (_IARM_MEM_MAGIC_SIZE + _IARM_MEM_PREFIX_SIZE)
#else
#define _IARM_MEM_EXTRA_ALLOC_SIZE (_IARM_MEM_PREFIX_SIZE)
#endif
#define IARM_CALL_PREFIX "_IARMC"
#define IARM_EVENT_PREFIX "_IARME"

#define IARM_EVENT_MAX 	0xFF
#define IARM_EventID_IsValid(eventId)  (((eventId) >= 0) && ((eventId) < IARM_EVENT_MAX))
#define IARM_GrpCtx_IsValid(cctx) ((cctx) && (cctx)->isActive == _IARM_CTX_HEADER_MAGIC)

#ifdef _IARM_MEM_DEBUG
#define IARM_GetMemType(ptr)  (*((unsigned int *)(((char *)ptr) - _IARM_MEM_MAGIC_SIZE)) - _IARM_MEM_HEADER_MAGIC)
#endif

#define IARM_GetSize(ptr)  (*((size_t *)(((char *)ptr) - _IARM_MEM_EXTRA_ALLOC_SIZE)))

#define METHOD_CALL_EXIT (1)
#define DISPATCH_EXIT (1<<1)

#define IARM_BUS_NAME_MAX_LEN 100

int mallocLocalCount;
int freeLocalCount;
typedef struct _Component_Node_t {
    GList link; 
    char name[IARM_MAX_NAME_LEN];
} Component_Node_t;

typedef struct _IARM_Ctxt_t {
     pthread_mutex_t mutexConn;
     pthread_cond_t  condConn;
     pthread_t       thread;
     pthread_t       threadMethodCall;
     char            groupName[IARM_MAX_NAME_LEN];
     char            memberName[IARM_MAX_NAME_LEN];
     GList           *compList;
     GList *         eventRegistry;
     DBusConnection  *conn;
     DBusConnection  *connMethodCall;
     DBusConnection  *connEvent;
     unsigned int    isActive;
     int             exitStatus;
     char            busName[IARM_BUS_NAME_MAX_LEN];
} IARM_Ctx_t;

static IARM_Ctx_t *m_grpCtx = NULL;
typedef struct _IARM_UICall_t {
    void *cctx;
    char callName[IARM_MAX_NAME_LEN];
    IARM_Call_t handler;
    void *callCtx;
} IARM_UICall_t;

typedef struct _IARM_UIEvent_t {
    void *cctx;
    IARM_EventId_t  eventId;
    IARM_Listener_t listener;
    char ownerName[IARM_MAX_NAME_LEN]; 
    void *callCtx;
} IARM_UIEvent_t;

static void DumpRegisteredComponents(IARM_Ctx_t * cctx);
static void DumpMemStat(void);

void *IARM_GetContext(void)
{
	return (void *)m_grpCtx;
}
/**
 * @brief Allocate memory for IARM member processes.
 * 
 * This API allows IARM member process to allocate local memory.
 * There are two types of local memory: Process local and thread local. Local memory
 * is only accessible by the process who allocates. 
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] type Thread-Local, Process-Local or Shared memory.
 * @param [in] size Number of bytes to allocate.
 * @param [out] ptr Return allocated memory.
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Malloc(IARM_MemType_t type, size_t size, void **ptr)
{
    IARM_Result_t retCode = IARM_RESULT_OOM;
    void *alloc = NULL;
    size_t requestedSize = size;

    size += _IARM_MEM_PREFIX_SIZE; /* DBus needs to know the size of the allocation so that it can pass the
                                      object by value therefore we code it in the prefix */
#ifdef _IARM_MEM_DEBUG
    size += _IARM_MEM_MAGIC_SIZE;
#endif

    switch(type)
    {
            case IARM_MEMTYPE_PROCESSLOCAL:
                mallocLocalCount++;
                alloc = malloc(size);
                break;
            case IARM_MEMTYPE_THREADLOCAL:
                /*Not supported, fall through */
            default:
                alloc = malloc(size);
        }

    if (alloc != NULL)
    {
        size_t *p = (size_t *)alloc;
        *p = requestedSize;
        alloc = ((char *)alloc) + _IARM_MEM_PREFIX_SIZE;
       
#ifdef _IARM_MEM_DEBUG
        unsigned int *m = (unsigned int *)alloc;
        *m = _IARM_MEM_HEADER_MAGIC + type;
        alloc = ((char *)alloc) + _IARM_MEM_MAGIC_SIZE;
#endif

    *ptr = alloc;
        retCode = IARM_RESULT_SUCCESS;
    }


    return retCode;
}

/**
 * @brief Free memory allocated by IARM member processes.
 * 
 * This API allows IARM member process to free local memory. 
 * The type specified in the free() API must match that specified in the malloc()
 * call.
 *
 * @param [in] grpCtx Context of the process group. Use NULL if allocating local memory.
 * @param [in] type Thread-Local or Process-Local. This must match the type of the allocated memory.
 * @param [in] alloc Points to the allocated memory to be freed.
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Free(IARM_MemType_t type, void *alloc)
{
    if (alloc != NULL)
    {

#ifdef _IARM_MEM_DEBUG
            alloc = ((char *)alloc) - _IARM_MEM_MAGIC_SIZE;
            unsigned int *p = (unsigned int *)alloc;
            int hType  = *p - _IARM_MEM_HEADER_MAGIC;
/*        IARM_ASSERT(hType == type);*/
        if (hType != type)
        {
            }
#endif
        alloc = ((char *)alloc) - _IARM_MEM_PREFIX_SIZE; /* dBus size header */
        switch(type)
        {
                case IARM_MEMTYPE_PROCESSLOCAL:
                    freeLocalCount++;
                    free(alloc);
                    break;
                case IARM_MEMTYPE_THREADLOCAL:
                    /*Not supported, fall through */
                default:
                    free(alloc);
            }
        }

    return IARM_RESULT_SUCCESS;
}

DBusHandlerResult dbusCallHandler(DBusConnection *connection, DBusMessage *msg, void *user_data)
{
    IARM_UICall_t *callInfo = (IARM_UICall_t *)user_data;

    // check if the message is a signal from the correct interface and with the correct name
    if (dbus_message_has_interface(msg, "iarm.signal.Type"))
    {
        IARM_UIEvent_t *eventInfo = (IARM_UIEvent_t *)user_data;
        IARM_Ctx_t *cctx = (IARM_Ctx_t *)eventInfo->cctx;

        DBusMessageIter arglist, arraylist;
        unsigned int eventId;
        int size;
        void *eventArg;
        char *pOwnerName;

        // filter out our own notifications
        if (dbus_message_has_member(msg, cctx->memberName))
        {
            /* Allow Member to listen to its own events */
            //goto ignore;
        }
        
        if(!dbus_message_iter_init(msg, &arglist))
        {
            log("%s Error dbus_message_iter_init failed\n", __FUNCTION__); 
            goto ignore;
        }
            
        if (dbus_message_iter_get_arg_type(&arglist) != DBUS_TYPE_UINT32)
        {   
            log("%s Error eventId not found, type is incorrect\n", __FUNCTION__); 
            goto ignore;
        }

        dbus_message_iter_get_basic(&arglist, &eventId);

        if(eventId != eventInfo->eventId)
        {
            goto ignore;
        }

        dbus_message_iter_next(&arglist);

        if (dbus_message_iter_get_arg_type(&arglist) != DBUS_TYPE_STRING)
        {   
            log("%s Error owner name not found, type is incorrect\n", __FUNCTION__); 
            goto ignore;
        }

        dbus_message_iter_get_basic(&arglist, &pOwnerName);

        if(strncmp(pOwnerName,eventInfo->ownerName,IARM_MAX_NAME_LEN) !=0 )
        {
            goto ignore;
        }
        dbus_message_iter_next(&arglist);

        if (dbus_message_iter_get_arg_type(&arglist) != DBUS_TYPE_UINT32)
        {   
            log("%s Error size not found, type is incorrect\n", __FUNCTION__); 
            goto ignore;
        }

        dbus_message_iter_get_basic(&arglist, &size);

        dbus_message_iter_next(&arglist);

        if (dbus_message_iter_get_arg_type(&arglist) != DBUS_TYPE_ARRAY ||
            dbus_message_iter_get_element_type(&arglist) != DBUS_TYPE_BYTE)
        {   
            log("%s Error event argument is malformed\n", __FUNCTION__); 
            goto ignore;
        }
                    
        dbus_message_iter_recurse(&arglist, &arraylist);
        dbus_message_iter_get_fixed_array(&arraylist, &eventArg, &size);
        eventInfo->listener(eventInfo->callCtx, eventArg);

        /* TODO: Add return DBUS_HANDLER_RESULT_HANDLED; here */
    }
    else if (dbus_message_is_method_call(msg, "iarm.method.Type", callInfo->callName))
    {
        DBusMessageIter arglist, arraylist;
        int size;
        unsigned char *callArg;
        
        if(!dbus_message_iter_init(msg, &arglist))
        {
            log("%s ERROR dbus_message_iter_init failed\n", __FUNCTION__);
            goto ignore;
        }
                    
        if (dbus_message_iter_get_arg_type(&arglist) != DBUS_TYPE_UINT32)
        {   
            log("%s ERROR Call argument size not found\n", __FUNCTION__);
            goto ignore;
        }

        dbus_message_iter_get_basic(&arglist, &size);
        dbus_message_iter_next(&arglist);
        
        if (dbus_message_iter_get_arg_type(&arglist) != DBUS_TYPE_ARRAY ||
            dbus_message_iter_get_element_type(&arglist) != DBUS_TYPE_BYTE)
        {   
            log("%s Error method call argument is malformed\n", __FUNCTION__); 
            goto ignore;
        }
                    
        dbus_message_iter_recurse(&arglist, &arraylist);
        dbus_message_iter_get_fixed_array(&arraylist, (void *)&callArg, &size);
        callArg += _IARM_MEM_EXTRA_ALLOC_SIZE;
        callInfo->handler(callInfo->callCtx, 0, (void *)callArg, (void *)msg);
        return DBUS_HANDLER_RESULT_HANDLED;   
        }
    else if (!dbus_message_has_interface(msg, "iarm.method.Type"))
    {
        return DBUS_HANDLER_RESULT_HANDLED;   
    }

ignore:
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;   
}

/**
 * @brief Register a RPC call so other processes can call.
 * 
 * A process publishes a RPC function using this API.  The RPC call, uniquely 
 * identified by (groupName, memberName, callName), can be invoked using the
 * three names together.
 *
 * A RPC function implemented by one process must publish it first before other
 * process can make a RPC call. Otherwise, the caller will be blocked until
 * the RPC call is published.
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] ownerName The name of this member process that implements the Call.
 * @param [in] callName The name of the function that this member offers as RPC-Call.
 * @param [in] handler The function that can be called by (ownerName, callName)
 * @param [in] callCtx Local context to passed in when the function is called. 
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_RegisterCall(const char *ownerName, const char *callName, IARM_Call_t handler, void *callCtx)
{
    errno_t rc = -1;
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;
    IARM_UICall_t *callInfo;

   // log("%s registers %s %s \n", __FUNCTION__, ownerName, callName);

    if (!IARM_GrpCtx_IsValid(cctx) || callName == NULL) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }
    else if ( ownerName == NULL || (strlen(ownerName) >= IARM_MAX_NAME_LEN)) {
    	retCode = IARM_RESULT_INVALID_PARAM;
    }
    else if ( callName == NULL || (strlen(callName) >= IARM_MAX_NAME_LEN)) {
    	retCode = IARM_RESULT_INVALID_PARAM;
    }

    //log("Entering [%s] - [%s][%s][func=%p][callctx=%p]\r\n", __FUNCTION__, ownerName, callName, handler, callCtx);

    if (retCode == IARM_RESULT_SUCCESS) {
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%s_%s", IARM_CALL_PREFIX, ownerName, callName);

        if((callInfo = (IARM_UICall_t *) malloc(sizeof(IARM_UICall_t))) == NULL)
        {
            log("%s ERROR malloc failed\n", __FUNCTION__);
            return  IARM_RESULT_INVALID_PARAM;
        }

        memset(callInfo, 0, sizeof(IARM_UICall_t));
        
        callInfo->cctx = cctx;
        callInfo->callCtx = callCtx;
        callInfo->handler = handler;
        
	rc = strcpy_s(callInfo->callName,sizeof(callInfo->callName), callName);
	if(rc!=EOK)
	{
		ERR_CHK(rc);
	}

        if(!dbus_connection_add_filter(cctx->conn, &dbusCallHandler, (void *)callInfo, &free))
        {
            log("%s ERROR dbus_connection_add_filter failed\n", __FUNCTION__);
            return IARM_RESULT_INVALID_PARAM;
        }
        
                    /* Register the component */
                    Component_Node_t *compNode = NULL;
                    IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(Component_Node_t), (void **)&compNode);
			        /*compNode->comp = comp;*/
					memset(compNode->name, 0, IARM_MAX_NAME_LEN);
                    strncpy(compNode->name, compId, IARM_MAX_NAME_LEN- 1);
					cctx->compList = g_list_append(cctx->compList, &compNode->link);
					//log("ADDED COMPONENT [%s]\r\n", compNode->name);
                    DumpRegisteredComponents(cctx);
                }

    return retCode;
}

/**
 * @brief Make a RPC call 
 * 
 * Invoke the RPC function by specifying its names.  This call will block until
 * the specified RPC function is published.
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] ownerName The name of member process implementing the RPC call.
 * @param [in] callName The name of the function to call. 
 * @param [in] arg Supply the argument to be used by the RPC call.
 * @param [out] ret Returns the return value of the RPC call.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Call(const char *ownerName,  const char *funcName, void *arg, int *ret)
{
    return IARM_CallWithTimeout(ownerName, funcName, arg, IARM_METHOD_IPC_TIMEOUT_DEFAULT, ret);
}


/**
 * @brief Make a RPC call with timeout
 *
 * Invoke the RPC function by specifying its names.  This call will block until
 * the specified RPC function is published.
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] ownerName The name of member process implementing the RPC call.
 * @param [in] callName The name of the function to call.
 * @param [in] arg Supply the argument to be used by the RPC call.
 * @param [in] timeout millisecond time interval to be used for the RPC call.
 * @param [out] ret Returns the return value of the RPC call.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_CallWithTimeout(const char *ownerName,  const char *funcName, void *arg, int timeout, int *ret)
{
    errno_t rc = -1;
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    DBusMessage* msg;
    DBusMessageIter arglist, arraylist;
    DBusMessage *replyMsg;
    DBusError error;
    uint32_t size;
    unsigned char *byteIndex = (unsigned char *)arg, *returnArg;

    if (!IARM_GrpCtx_IsValid(cctx) || ownerName == NULL || funcName == NULL)
    {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    IARM_ASSERT((arg == NULL) || (IARM_GetMemType(arg) == IARM_MEMTYPE_PROCESSLOCAL));

    if (retCode == IARM_RESULT_SUCCESS)
    {
        char serverName[IARM_MAX_NAME_LEN] = {0};

        rc = sprintf_s(serverName, sizeof(serverName), "process.iarm.%s", ownerName);
	if(rc < EOK)
	{
		ERR_CHK(rc);
	}

        // create a new method call and check for errors
        msg = dbus_message_new_method_call(serverName, // target for the method call
                                           "/iarm/method/Object", // object to call on
                                           "iarm.method.Type", // interface to call on
                                           funcName); // method name

        if (NULL == msg)
        {
            log("%s Error dbus_message_new_method_call failed\n", __FUNCTION__);
            return IARM_RESULT_INVALID_PARAM;
        }

        /* append arguments onto signal */
        dbus_message_iter_init_append(msg, &arglist);

        size = (uint32_t) IARM_GetSize(arg);
        size += _IARM_MEM_EXTRA_ALLOC_SIZE; /* include prefix */
        byteIndex -= _IARM_MEM_EXTRA_ALLOC_SIZE;

        if (!dbus_message_iter_append_basic(&arglist, DBUS_TYPE_UINT32 , &size))
        {
            log("%s Error dbus_message_iter_append_basic failed\n", __FUNCTION__);
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }

        if (!dbus_message_iter_open_container(&arglist, DBUS_TYPE_ARRAY, "y", &arraylist))
        {
            log("%s Error dbus_message_iter_open_container failed\n", __FUNCTION__);
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }

        if (!dbus_message_iter_append_fixed_array (&arraylist, DBUS_TYPE_BYTE, (void *)&byteIndex, size))
        {
            log("%s Error dbus_message_iter_append_fixed_array failed\n", __FUNCTION__);
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }

        if (!dbus_message_iter_close_container(&arglist, &arraylist))
        {
            log("%s Error dbus_message_iter_close_container failed\n", __FUNCTION__);
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }

        /* Block and timeout after 5 sec as default */
        if (timeout <= IARM_METHOD_IPC_TIMEOUT_DEFAULT)
            timeout = 5000; /* 5 sec*/
        else if (timeout == IARM_METHOD_IPC_TIMEOUT_INFINITE)
            timeout = DBUS_TIMEOUT_INFINITE;

        //log("%s called %s %s sender %s\n", __FUNCTION__, ownerName, funcName, dbus_message_get_sender(msg));

         dbus_error_init(&error);
        replyMsg = dbus_connection_send_with_reply_and_block(cctx->connMethodCall,msg, timeout, &error);
        if (!replyMsg) 
        {
            if (dbus_error_is_set(&error) == TRUE) {
               log("dbus_connection_send_with_reply_and_block failed with error %s \r\n", error.message);
               dbus_error_free(&error);
            } else {
                log("dbus_connection_send_with_reply_and_block failed \r\n");
            }
            log("IARM_Call failed for call %s-%s \r\n",ownerName,funcName);
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_STATE;
        }

        // free message
        dbus_message_unref(msg);

     
        if(!dbus_message_iter_init(replyMsg, &arglist))
        {
            log("%s Error dbus_message_iter_init failed\n", __FUNCTION__); 
            // free reply message
            dbus_message_unref(replyMsg);
            return IARM_RESULT_INVALID_PARAM;
        }

                   
        if (dbus_message_iter_get_arg_type(&arglist) != DBUS_TYPE_INT32)
        {   
           // log("%s Error Return type is not INT32\n", __FUNCTION__); 
            // free reply message
            dbus_message_unref(replyMsg);
            return IARM_RESULT_INVALID_PARAM;
        }

        dbus_message_iter_get_basic(&arglist, ret);

        dbus_message_iter_next(&arglist);
        
        if (dbus_message_iter_get_arg_type(&arglist) != DBUS_TYPE_ARRAY ||
            dbus_message_iter_get_element_type(&arglist) != DBUS_TYPE_BYTE)
        {   
            log("%s Error Reply argument is malformed\n", __FUNCTION__); 
        }

        size -= _IARM_MEM_EXTRA_ALLOC_SIZE; /* doesn't include prefix this time */

        dbus_message_iter_recurse(&arglist, &arraylist);
        dbus_message_iter_get_fixed_array(&arraylist, (void *)&returnArg, (int *)&size);
        
	rc = memcpy_s(arg, size, returnArg, size);
        if(rc!=EOK)
        {
                ERR_CHK(rc);
        }

	returnArg = (unsigned char *) arg;

         // free reply message
        dbus_message_unref(replyMsg);
        //printf("%s call exiting %s %s return %d \n", __FUNCTION__, ownerName, funcName, *ret); 
    }

    return retCode;
}


/**
 * @brief Explicitly mark the return/finish of a RPC-Call 
 * 
 * This API must be called by the implementation of the RPC-Call to submit the return value to the caller.
 * Otherwise the caller will be blocked forever waiting for the return value.
 *
 * This API does not need to be called at the end of the RPC invokation. It can be called anytime by anybody 
 * after the RPC call is requested, to unblocked the caller.
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] ownerName The name of owner process implementing the RPC call.
 * @param [in] callName The name of the function to call. 
 * @param [in] ret return value of the executed RPC-Call.
 * @param [in] callMsg must match the "serial" passed in when the RPC implementation is executed.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_CallReturn(const char *ownerName, const char *funcName, void *arg, int ret, void *callMsg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;
    DBusMessage *msg = (DBusMessage *)callMsg, *reply;
    DBusMessageIter arraylist;
    DBusMessageIter returnVal;
    dbus_uint32_t serial = 0;
    size_t size;

   //log("%s %s %s return val %d serial is %p\n", __FUNCTION__, ownerName, funcName, ret, (void *)callMsg); 

    if (!IARM_GrpCtx_IsValid(cctx) || ownerName == NULL || funcName == NULL)
    {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if (retCode == IARM_RESULT_SUCCESS)
    {

       // create a reply from the message
       reply = dbus_message_new_method_return(msg);

       // add the arguments to the reply
       dbus_message_iter_init_append(reply, &returnVal);
       
        if (!dbus_message_iter_append_basic(&returnVal, DBUS_TYPE_INT32, &ret))
        { 
            log("%s Error dbus_message_iter_append_basic failed\n", __FUNCTION__); 
            dbus_message_unref(reply);
            return IARM_RESULT_INVALID_PARAM;
        }
       
        size = (size_t)IARM_GetSize(arg);        

        if (!dbus_message_iter_open_container(&returnVal, DBUS_TYPE_ARRAY, "y", &arraylist))
        {
            log("%s Error dbus_message_iter_open_container failed\n", __FUNCTION__); 
            dbus_message_unref(reply);
            return IARM_RESULT_INVALID_PARAM;
        }

        if (!dbus_message_iter_append_fixed_array (&arraylist, DBUS_TYPE_BYTE, &arg, size))
        {
            log("%s Error dbus_message_iter_append_fixed_array failed\n", __FUNCTION__); 
            dbus_message_unref(reply);
            return IARM_RESULT_INVALID_PARAM;
        }

        if (!dbus_message_iter_close_container(&returnVal, &arraylist))
        {
            log("%s Error dbus_message_iter_close_container failed\n", __FUNCTION__); 
            dbus_message_unref(reply);
            return IARM_RESULT_INVALID_PARAM;
        }

        // send the reply && flush the connection
        if (!dbus_connection_send(cctx->conn, reply, &serial))
        {
            log("%s Error dbus_connection_send failed\n", __FUNCTION__); 
            dbus_message_unref(reply);
            return IARM_RESULT_INVALID_PARAM;
        }

        // free the reply
        dbus_message_unref(reply);
    }
    return retCode;
}

/**
 * @brief Register a event that can be listened to by other processes.
 * 
 * All events published within a process group are uniquely identified by an eventId.
 * A process uses this API to publish an event so this event can be listened by
 * other processes.
 * 
 * An event must be published before it can be "notified"
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] ownerName The name of the member process that owns the event.
 * @param [in] eventId The ID of the event. This ID is unique across all processes. 
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_RegisterEvent(const char *ownerName, int maxEventId)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (!IARM_GrpCtx_IsValid(cctx) || !IARM_EventID_IsValid(0))
    {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if (retCode == IARM_RESULT_SUCCESS)
    {

            char compId[IARM_MAX_NAME_LEN] = {0};
            snprintf(compId, sizeof(compId), "%s_%s", IARM_EVENT_PREFIX, ownerName);

            
                        /* Register the component */
                        Component_Node_t *compNode = NULL;
                        IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(Component_Node_t), (void **)&compNode);
				        /*compNode->comp = comp;*/
						memset(compNode->name, 0, IARM_MAX_NAME_LEN);
                        strncpy(compNode->name, compId, IARM_MAX_NAME_LEN - 1);
         				cctx->compList = g_list_append(cctx->compList, &compNode->link);				
						DumpRegisteredComponents(cctx);
                    }

    return retCode;
}


/**
 * @brief Notify listeners of event
 * 
 * This API is used to notify all listeners of a certain event.
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] eventId The ID of the event.
 * @param [in] arg Argument to be passed to the listeners. IARM Module will free this memor once all listeners are
 *             notified.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_NotifyEvent(const char *ownerName,  IARM_EventId_t eventId, void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    // log("%s called for eventId %d\n", __FUNCTION__, eventId); 
     
    if (!IARM_GrpCtx_IsValid(cctx) || !IARM_EventID_IsValid(eventId))
    {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    IARM_ASSERT((arg == NULL) || (IARM_GetMemType(arg) == IARM_MEMTYPE_PROCESSLOCAL));

    if (retCode == IARM_RESULT_SUCCESS)
    {
        DBusMessage* msg;
        DBusMessageIter arglist, arraylist;
        dbus_uint32_t serial = 0;
        uint32_t size;

        // create a signal & check for errors 
        msg = dbus_message_new_signal(  "/iarm/signal/Object", // object name of the signal
                                            "iarm.signal.Type", // interface name of the signal
                                            cctx->memberName);
            
        if (NULL == msg) 
        { 
            log("%s Error dbus_message_new_signal failed\n", __FUNCTION__); 
            return IARM_RESULT_INVALID_PARAM;
        }

        /* append arguments onto signal */
        dbus_message_iter_init_append(msg, &arglist);

        if (!dbus_message_iter_append_basic(&arglist, DBUS_TYPE_UINT32 , &eventId))
        {
            log("%s Error dbus_message_iter_append_basic failed\n",__FUNCTION__); 
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }

        if (!dbus_message_iter_append_basic(&arglist, DBUS_TYPE_STRING, &ownerName))
        {
            log("%s Error dbus_message_iter_append_basic (STRING) failed\n",__FUNCTION__); 
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }
        size = (uint32_t)IARM_GetSize(arg);

        if (!dbus_message_iter_append_basic(&arglist, DBUS_TYPE_UINT32 , &size))
        {
            log("%s Error dbus_message_iter_append_basic failed\n", __FUNCTION__); 
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }

        if (!dbus_message_iter_open_container(&arglist, DBUS_TYPE_ARRAY, "y", &arraylist))
        {
            log("%s Error dbus_message_iter_open_container failed\n", __FUNCTION__); 
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }

        if (!dbus_message_iter_append_fixed_array (&arraylist, DBUS_TYPE_BYTE, &arg, size))
        {
            log("%s Error dbus_message_iter_append_fixed_array failed\n", __FUNCTION__); 
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }

        if (!dbus_message_iter_close_container(&arglist, &arraylist))
        {
            log("%s Error dbus_message_iter_close_container failed\n", __FUNCTION__); 
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }
        
        /* send the message and flush the connection */
        if (!dbus_connection_send(cctx->connEvent, msg, &serial))
        {
            log("%s Error dbus_connection_send failed\n", __FUNCTION__); 
            dbus_message_unref(msg);
            return IARM_RESULT_INVALID_PARAM;
        }
        dbus_connection_flush(cctx->connEvent);
            
        dbus_message_unref(msg);
    }

    return retCode;
}

/**
 * @brief Register to listen for an event. 
 * 
 * This API is used to register the calling process for a certain event.
 * If the event is not yet published, this call be be blocked until the event
 * is published.
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] eventId The ID of the event.
 * @param [in] listener Callback function when the event is received. 
 * @param [in] callCtx Local context used when calling the listener's callback function.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_RegisterListner(const char *ownerName, IARM_EventId_t eventId, IARM_Listener_t listener, void *callCtx)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;
    IARM_UIEvent_t *eventInfo;

    if (IARM_GrpCtx_IsValid(cctx) && IARM_EventID_IsValid(eventId))
    {
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%d", "IARM_Event", eventId);

       //log("%s for event %d\n", __FUNCTION__, eventId);

       
        if((eventInfo = (IARM_UIEvent_t *) malloc(sizeof(IARM_UIEvent_t))) == NULL)
        {
            log("%s Error malloc failed\n", __FUNCTION__);
            return  IARM_RESULT_INVALID_PARAM;
        }
        
        eventInfo->cctx = cctx;
        eventInfo->callCtx = callCtx;
        eventInfo->eventId = eventId;
        eventInfo->listener = listener;
        strncpy(eventInfo->ownerName, ownerName, IARM_MAX_NAME_LEN-1);
	eventInfo->ownerName[IARM_MAX_NAME_LEN-1] = '\0';  //CID:136714 - Buffer size

        if(!dbus_connection_add_filter(cctx->conn, &dbusCallHandler, (void *)eventInfo, &free))
        {
            free(eventInfo);
            log("%s Error add filter failed\n", __FUNCTION__);
            return IARM_RESULT_INVALID_PARAM;
        }
        cctx->eventRegistry = g_list_prepend(cctx->eventRegistry,eventInfo);

                /* Register the component */
                Component_Node_t *compNode = NULL;
                IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(Component_Node_t), (void **)&compNode);
		        /*compNode->comp = comp;*/
                memset(compNode->name, 0, IARM_MAX_NAME_LEN);
                strncpy(compNode->name, compId, IARM_MAX_NAME_LEN-1);
            	cctx->compList = g_list_append(cctx->compList, &compNode->link);				
				DumpRegisteredComponents(cctx);
            }

    return retCode;
}

static gint _Is_Context_Matching(gconstpointer pa, gconstpointer pb)
{
    const IARM_UIEvent_t *a = (IARM_UIEvent_t *)pa;
    const IARM_UIEvent_t *b = (IARM_UIEvent_t *)pb;

    return (a->eventId != b->eventId) || strncmp(a->ownerName, b->ownerName,IARM_MAX_NAME_LEN);
}

/**
 * @brief UnRegister to listen for an event. 
 * 
 * This API is used to unregister the calling process for a certain event.
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] eventId The ID of the event.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_UnRegisterListner(const char *ownerName, IARM_EventId_t eventId)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;
    GList *registeredMember = NULL;
    Component_Node_t *compNode = NULL;

    //log("%s eventId is %d\n", __FUNCTION__, eventId);

    if (!IARM_GrpCtx_IsValid(cctx) || !IARM_EventID_IsValid(eventId))
    {
        log("%s Error Invalid Parameter\n", __FUNCTION__);
        return IARM_RESULT_INVALID_PARAM;
    }
    else
    {
        IARM_UIEvent_t eventInfo, *pEventInfo;
        GList *pData; 
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%d", "IARM_Event", eventId);

        eventInfo.eventId = eventId;
        strncpy(eventInfo.ownerName, ownerName, IARM_MAX_NAME_LEN -1);
	eventInfo.ownerName[IARM_MAX_NAME_LEN -1] = '\0';  //CID:136922 - Buffer size

        if((pData = g_list_find_custom(cctx->eventRegistry, &eventInfo, _Is_Context_Matching)) == NULL)
        {
            log("%s ERROR Listener does not exist\n", __FUNCTION__);
            return IARM_RESULT_INVALID_PARAM;
        }

        pEventInfo = (IARM_UIEvent_t *)pData->data;  

        dbus_connection_remove_filter(cctx->conn, &dbusCallHandler,pEventInfo);

        cctx->eventRegistry = g_list_remove(cctx->eventRegistry,pEventInfo);
		
		/* Find the component */
		for(registeredMember = (cctx->compList); registeredMember != NULL;registeredMember = registeredMember->next)
        { 
			compNode = container_of((GList *)registeredMember->data, Component_Node_t, link);
      		if (strcmp(compNode->name, compId) == 0)
       		{
               		cctx->compList = g_list_remove(cctx->compList, &compNode->link);
              		IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, compNode);
              		break;
			}
		}
	}
    return retCode;
}

void *dispatchThread(void *arg)
{
    IARM_Ctx_t *cctx = (IARM_Ctx_t *)arg;

    //log("%s %s launched\n", __FUNCTION__, cctx->memberName);
    /* just loop, dispatching messages until the connection is closed */
    while (dbus_connection_read_write_dispatch (cctx->conn, 500)){}
   // log("%s %s connection closed\n", __FUNCTION__, cctx->memberName);
    pthread_mutex_lock(&(cctx->mutexConn));
    cctx->exitStatus = cctx->exitStatus|DISPATCH_EXIT;
    pthread_cond_signal(&(cctx->condConn));
    pthread_mutex_unlock(&(cctx->mutexConn));
    return NULL;
}

void *dispatchThreadMethodCall(void *arg)
{
    IARM_Ctx_t *cctx = (IARM_Ctx_t *)arg;

  //  log("%s %s launched\n", __FUNCTION__, cctx->memberName);
    /* just loop, dispatching messages until the connection is closed */
    while (dbus_connection_read_write_dispatch (cctx->connMethodCall, 50)){}

    //log("%s %s connection closed\n", __FUNCTION__, cctx->memberName);
    pthread_mutex_lock(&(cctx->mutexConn));
    cctx->exitStatus = cctx->exitStatus|METHOD_CALL_EXIT;
    pthread_cond_signal(&(cctx->condConn));
    pthread_mutex_unlock(&(cctx->mutexConn));
    return NULL;
}


/**
 * @brief Initialize the IARM module for the calling process.
 * 
 * This API is used to initialize the IARM module for the calling process, and regisers the calling process to
 * be visible to other processes that it wishes to communicate to. The registered process is uniquely identified
 * by (groupName, memberName).
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] groupName The IPC group this process wishes to participate.
 * @param [in] memberName The name of the calling process.
 * @param [out] grpCtx (Group) Context used when communicating with other members of the same group. 
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Init(const char *groupName, const char *memberName)
{
    errno_t rc = -1;
    IARM_Result_t retCode = IARM_RESULT_IPCCORE_FAIL;
     IARM_Ctx_t *cctx = NULL;
    DBusError err;
    int ret;
    char busName[IARM_BUS_NAME_MAX_LEN] = {0};
     
     //log("%s called \n", __FUNCTION__); 
     

    if (!dbus_threads_init_default())
    {
        log("%s Error dbus_threads_init_default failed", __FUNCTION__);
    }
     
    if ( groupName == NULL || (strlen(groupName) >= IARM_MAX_NAME_LEN))
    {
    	 retCode = IARM_RESULT_INVALID_PARAM;
     }
    else if ( memberName == NULL || (strlen(memberName) >= IARM_MAX_NAME_LEN))
    {
    	 retCode = IARM_RESULT_INVALID_PARAM;
     }
     else
    {
		 retCode = IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(*cctx), (void **)&cctx);
     }

    log("%s group name = %s member name = %s\n", __FUNCTION__, groupName, memberName);

    if (retCode == IARM_RESULT_SUCCESS)
    {            
        memset(cctx, 0, sizeof(IARM_Ctx_t));
	
        cctx->isActive = _IARM_CTX_HEADER_MAGIC;
           
        pthread_mutex_init(&(cctx->mutexConn),NULL);
        pthread_cond_init(&(cctx->condConn),NULL);

        dbus_error_init(&err);

        /* connect to the bus and check for errors */
        cctx->conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);

        if ((dbus_error_is_set(&err)) || (NULL == cctx->conn))
        { 
            log("an error occurred: %s\n", err.message);
            log("%s Error Cannot connect to the Dbus (first instance)\n", __FUNCTION__);
            dbus_error_free(&err); 
            retCode = IARM_RESULT_IPCCORE_FAIL;
            goto error;
        }

        dbus_connection_set_exit_on_disconnect(cctx->conn,FALSE);


        dbus_error_init(&err);

        /* connect to the bus and check for errors */
        cctx->connEvent = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);

        if ((dbus_error_is_set(&err)) || (NULL == cctx->connEvent))
        { 
            log("an error occurred: %s\n", err.message);
            log("%s Error Cannot connect to the Dbus (first instance)\n", __FUNCTION__);
            dbus_error_free(&err); 
            retCode = IARM_RESULT_IPCCORE_FAIL;
            goto error;
        }

        dbus_connection_set_exit_on_disconnect(cctx->connEvent,FALSE);


        dbus_error_free(&err);

        /* to avoid deadlock in nested RPC calls use seperate connection */
        cctx->connMethodCall = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
        
        if ((dbus_error_is_set(&err)) || (NULL == cctx->connMethodCall))
        { 
            log("an error occurred: %s\n", err.message);
            log("%s Error Cannot connect to the Dbus (second instance)\n", __FUNCTION__);
            dbus_error_free(&err); 
            retCode = IARM_RESULT_IPCCORE_FAIL;
            goto error;
        }

        dbus_connection_set_exit_on_disconnect(cctx->connMethodCall,FALSE);

        //strcpy(busName, "process.iarm.");
        //strcpy(&busName[13], memberName);
        snprintf(busName, IARM_BUS_NAME_MAX_LEN - 1, "%s%s", "process.iarm.", memberName);
        dbus_error_free(&err);
        ret = dbus_bus_request_name(cctx->conn, busName, DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_ALLOW_REPLACEMENT, &err);

        if ((dbus_error_is_set(&err)) || (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret))
        {
            log("%s Error Request to set the Dbus name failed\n", __FUNCTION__);
            dbus_error_free(&err); 
            retCode = IARM_RESULT_IPCCORE_FAIL;
            goto error;
        }

        rc = strcpy_s(cctx->busName,sizeof(cctx->busName), busName);
	if(rc!=EOK)
	{
		ERR_CHK(rc);
	}

        dbus_error_free(&err); 
        snprintf(busName, IARM_BUS_NAME_MAX_LEN - 1, "%s%s%s", "process.iarm.", memberName, ".Event");
        ret = dbus_bus_request_name(cctx->connEvent, busName, DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_ALLOW_REPLACEMENT, &err);
        if ((dbus_error_is_set(&err)) || (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret))
        {
            log("%s Error Request to set the Dbus name on connEvent failed\n", __FUNCTION__);
            dbus_error_free(&err); 
            retCode = IARM_RESULT_IPCCORE_FAIL;
            goto error;
        }

        dbus_error_free(&err); 
        snprintf(busName, IARM_BUS_NAME_MAX_LEN - 1, "%s%s%s", "process.iarm.", memberName, ".Method");
        ret = dbus_bus_request_name(cctx->connMethodCall, busName, DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_ALLOW_REPLACEMENT, &err);
        if ((dbus_error_is_set(&err)) || (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret))
        {
            log("%s Error Request to set the Dbus name on connMethodCall failed\n", __FUNCTION__);
            dbus_error_free(&err); 
            retCode = IARM_RESULT_IPCCORE_FAIL;
            goto error;
        }

        dbus_error_free(&err); 

        /* Add filter rules for methods and events */
        dbus_bus_add_match(cctx->conn, "interface='iarm.signal.Type'", NULL);
        dbus_bus_add_match(cctx->conn, "interface='iarm.method.Type'", NULL);

                 rc = strcpy_s(cctx->groupName,sizeof(cctx->groupName), groupName);
		 if(rc!=EOK)
		 {
			 ERR_CHK(rc);
		 }
                 rc = strcpy_s(cctx->memberName,sizeof(cctx->memberName), memberName);
		 if(rc!=EOK)
                 {	
                         ERR_CHK(rc);
                 }

                     m_grpCtx = cctx;
        retCode = IARM_RESULT_SUCCESS;
        pthread_create(&(cctx->thread), NULL, &dispatchThread, (void *)cctx);
        //pthread_create(&(cctx->threadMethodCall), NULL, &dispatchThreadMethodCall, (void *)cctx); 
        //log("%s exit  \n", __FUNCTION__); 
         }

    return retCode;

error:
    IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, cctx);
     return retCode;
}

IARM_Result_t IARM_IsCallRegistered(const char *ownerName, const char *callName, int *isRegistered)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (!IARM_GrpCtx_IsValid(cctx) || callName == NULL)
    {
        retCode = IARM_RESULT_INVALID_PARAM;
    }
    else if ( ownerName == NULL || (strlen(ownerName) >= IARM_MAX_NAME_LEN))
    {
    	retCode = IARM_RESULT_INVALID_PARAM;
    }
    else if ( callName == NULL || (strlen(callName) >= IARM_MAX_NAME_LEN))
    {
    	retCode = IARM_RESULT_INVALID_PARAM;
    }

    //log("Entering [%s] - [%s][%s]\r\n", __FUNCTION__, ownerName, callName);

    if (retCode == IARM_RESULT_SUCCESS)
    {
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%s_%s", IARM_CALL_PREFIX, ownerName, callName);

            	*isRegistered = 1;
    }

    return retCode;

}

/**
 * @brief Terminate the IARM module for the calling process.
 * 
 * This API is used to  terminate the IARM module for the calling process, and cleanup resources
 * used by MAF.
 *
 * @param [in] grpCtx Context of the process group.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Term(void)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;


    if (!IARM_GrpCtx_IsValid(cctx)) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    //log("Entering [%s]\r\n", __FUNCTION__);

    if (retCode == IARM_RESULT_SUCCESS) {
        cctx->isActive = 0;

        dbus_connection_close(cctx->conn);
        dbus_connection_close(cctx->connMethodCall);
        dbus_connection_close(cctx->connEvent);


        pthread_mutex_lock (&(cctx->mutexConn));
        /* Check if resources has been cleaned up */
        {
            GList *registeredMember = NULL;
            GList *next = NULL;
			for(registeredMember = (cctx->compList); registeredMember != NULL; registeredMember = registeredMember->next)
			{ 
				Component_Node_t *compNode = container_of((GList *)registeredMember->data, Component_Node_t, link);
				//log("REMOVING Component [%s]\r\n", compNode->name);
				cctx->compList = g_list_remove( cctx->compList, &compNode->link);
				IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, compNode);
			}
        }
        DumpRegisteredComponents(cctx);
        while( cctx->exitStatus != (DISPATCH_EXIT))
        {
              pthread_cond_wait(&(cctx->condConn),&(cctx->mutexConn));
        }
        pthread_mutex_unlock (&(cctx->mutexConn));

        pthread_mutex_destroy(&(cctx->mutexConn));
        pthread_cond_destroy(&(cctx->condConn));
        IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, cctx);
        DumpMemStat();
        m_grpCtx = NULL;
    }
    return retCode;
}


static void DumpMemStat(void)
{
    #if 0
		log("MallocLocalCount %d, FreeLocalCount %d\r\n", mallocLocalCount, freeLocalCount);
	#endif
}

static void DumpRegisteredComponents(IARM_Ctx_t * cctx)
{
#if 0	
	Component_Node_t *compNode = NULL;
	GList *l = NULL;
	
	int i = 0;
	log("=================+Registered Components+===========================\r\n");
	for (l = cctx->compList; l != NULL; l = l->next)
	{
		compNode = container_of((GList *)l->data, Component_Node_t, link);
	
		log("COMP#%d - %s\r\n", i, compNode->name);
		i++;
	}
	log("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\r\n\r\n");
#endif
}


