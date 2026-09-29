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
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>

#ifdef __cplusplus
extern "C"
{
#endif
#include <direct/interface.h>
#include <direct/messages.h>
#include <direct/list.h>
#include <direct/signals.h>
#ifdef __cplusplus
}
#endif
#include <fusiondale.h>

#include "libIARM.h"
#include "iarmUtil.h"
#include "libIARMCore.h"
#include "libIBusDaemonInternal.h"

#include "safec_lib.h"

#define _IARM_MEM_MAGIC_SIZE    8

#define _IARM_MEM_DEBUG
#define _IARM_CTX_HEADER_MAGIC  0xCABFACE1

#ifdef _IARM_MEM_DEBUG
#define _IARM_MEM_HEADER_MAGIC  0xBADBEEF0
#endif
#define IARM_CALL_PREFIX "_IARMC"
#define IARM_EVENT_PREFIX "_IARME"

#define IARM_EVENT_MAX 	0xFF
#define IARM_EventID_IsValid(eventId)  (((eventId) >= 0) && ((eventId) < IARM_EVENT_MAX))
#define IARM_GrpCtx_IsValid(cctx) ((cctx) && (cctx->isActive == _IARM_CTX_HEADER_MAGIC))

#ifdef _IARM_MEM_DEBUG
#define IARM_GetMemType(ptr)  (*((unsigned int *)(((char *)ptr) - _IARM_MEM_MAGIC_SIZE)) - _IARM_MEM_HEADER_MAGIC)
#endif


int mallocLocalCount;
int mallocShareCount;
int freeLocalCount;
int freeShareCount;

typedef struct _Component_Node_t {
    DirectLink link; 
    IComaComponent *comp;
    char name[IARM_MAX_NAME_LEN];
} Component_Node_t;

typedef struct _IARM_Ctxt_t {
     unsigned int   isActive;
     char           groupName[IARM_MAX_NAME_LEN]; 
     char           memberName[IARM_MAX_NAME_LEN];
     IFusionDale    *dale;
     IComa          *coma;
     DirectLink     *compList;
} IARM_Ctx_t;

static IARM_Ctx_t *m_grpCtx = NULL;

typedef struct _IARM_UIEvent_t {
    IARM_EventId_t  eventId;
    int eventCode;
} IARM_UIEvent_t;

static void _ListenersDone(void *gctx, ComaNotificationID notification, void *arg);
static void DumpRegisteredComponents(IARM_Ctx_t * cctx);
static void DumpMemStat(void);
static void _EventComponentStubCall(void *ctx, unsigned long methodID, void *arg, unsigned int magic);

void *IARM_GetContext(void)
{
	return (void *)m_grpCtx;
}
/**
 * @brief Allocate memory for IARM member processes.
 * 
 * This API allows IARM member process to allocate local memory or shared memory.
 * There are two types of local memory: Process local and thread local. Local memory
 * is only accessible by the process who allocates. 
 *
 * Shared library, is accessible by all processes within the same group as the process that
 * allocates the memory.
 *
 * For example,  RPC call arguments of complicated structures should be allocated from shared 
 * memory so the RPC invocation can access these arguments from a different process.
 *
 * @param [in] grpCtx Context of the process group.
 * @param [in] type Thread-Local, Process-Local or Shared memory.
 * @param [in] size Number of bytes to allocate.
 * @param [out] ptr Return allocated memory.
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Malloc(IARM_MemType_t type, size_t size, void **ptr)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    DirectResult dRet = DR_OK;
    void *alloc = NULL;

#ifdef _IARM_MEM_DEBUG
    size += _IARM_MEM_MAGIC_SIZE;
#endif
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (type == IARM_MEMTYPE_PROCESSSHARE && !IARM_GrpCtx_IsValid(cctx)) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if (retCode == IARM_RESULT_SUCCESS) {
        switch(type) {
            case IARM_MEMTYPE_PROCESSLOCAL:
                mallocLocalCount++;
                alloc = malloc(size);
                break;
            case IARM_MEMTYPE_PROCESSSHARE:
                mallocShareCount++;
                dRet = cctx->coma->Allocate(cctx->coma, size, &alloc);
                break;
            case IARM_MEMTYPE_THREADLOCAL:
                /*Not supported, fall through */
            default:
                alloc = malloc(size);
        }
    }

    if (alloc != NULL) {
#ifdef _IARM_MEM_DEBUG
        unsigned int *p = (unsigned int *)alloc;
        *p = _IARM_MEM_HEADER_MAGIC + type;
        alloc = ((char *)alloc) + _IARM_MEM_MAGIC_SIZE;
#endif
    }

    *ptr = alloc;

    if (dRet != DR_OK) {
        retCode = IARM_RESULT_IPCCORE_FAIL;
    }
    else {
        retCode = (*ptr) ? IARM_RESULT_SUCCESS : IARM_RESULT_OOM;
    }


    return retCode;
}

/**
 * @brief Free memory allocated by IARM member processes.
 * 
 * This API allows IARM member process to free local memory or shared memory. 
 * The type specified in the free() API must match that specified in the malloc()
 * call.
 *
 * @param [in] grpCtx Context of the process group. Use NULL if allocating local memory.
 * @param [in] type Thread-Local, Process-Local or Shared memory. This must match the type of the allocated memory.
 * @param [in] alloc Points to the allocated memory to be freed.
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_Free(IARM_MemType_t type, void *alloc)
{

    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    DirectResult dRet = DR_OK;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (type == IARM_MEMTYPE_PROCESSSHARE && !IARM_GrpCtx_IsValid(cctx)) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if (retCode == IARM_RESULT_SUCCESS) {
        if (alloc != NULL) {
#ifdef _IARM_MEM_DEBUG
            alloc = ((char *)alloc) - _IARM_MEM_MAGIC_SIZE;
            unsigned int *p = (unsigned int *)alloc;
            int hType  = *p - _IARM_MEM_HEADER_MAGIC;
            IARM_ASSERT(hType == type);
            if (hType != type) {
            }
#endif
            switch(type) {
                case IARM_MEMTYPE_PROCESSLOCAL:
                    freeLocalCount++;
                    free(alloc);
                    break;
                case IARM_MEMTYPE_PROCESSSHARE:
                    freeShareCount++;
                    dRet = cctx->coma->Deallocate(cctx->coma, alloc);
                    break;
                case IARM_MEMTYPE_THREADLOCAL:
                    /*Not supported, fall through */
                default:
                    free(alloc);
            }
        }

        if (dRet != DR_OK) {
            retCode = IARM_RESULT_IPCCORE_FAIL;
        }
        else {
            retCode = IARM_RESULT_SUCCESS;
        }
    }

    return retCode;
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
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (!IARM_GrpCtx_IsValid(cctx) || callName == NULL) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }
    else if ( ownerName == NULL || (strlen(ownerName) >= IARM_MAX_NAME_LEN)) {
    	retCode = IARM_RESULT_INVALID_PARAM;
    }
    else if ( callName == NULL || (strlen(callName) >= IARM_MAX_NAME_LEN)) {
    	retCode = IARM_RESULT_INVALID_PARAM;
    }

    /*log("Entering [%s] - [%s][%s][func=%p][callctx=%p]\r\n", __FUNCTION__, ownerName, callName, handler, callCtx);*/

    if (retCode == IARM_RESULT_SUCCESS) {
        DirectResult dRet = DR_OK;
        IComaComponent *comp = NULL;
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%s_%s", IARM_CALL_PREFIX, ownerName, callName);
        if (comp == NULL) {
            /*log("Creating CALL Comp\r\n");*/

            dRet = cctx->coma->CreateComponent( cctx->coma, compId, handler, /*No Evt*/0, callCtx, &comp);
            if (dRet == DR_BUSY) {
                /*XONE-13886 mitigation, unregister the component before recreating */
                __TIMESTAMP();log("XONE-13886: COMPONENT [%s] Already Exist!\r\n", compId);
                dRet = cctx->coma->GetComponent( cctx->coma, compId, 0/*WaitForever*/,  &comp);
                __TIMESTAMP();log("XONE-13886: EXISTING COMPONENT [%s] TO RELEASE! [%d]\r\n", compId, dRet);
                if (dRet == DR_OK) {
                    comp->Release(comp);
                }
            }

            dRet = DR_OK;

            if (dRet == DR_OK) {
                dRet = comp->Activate(comp);
                if (dRet == DR_OK) {
                    /* Register the component */
                    Component_Node_t *compNode = NULL;
                    IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(Component_Node_t), (void **)&compNode);
                    compNode->comp = comp;
                    strncpy(compNode->name, compId, IARM_MAX_NAME_LEN);
                    direct_list_append(&cctx->compList, &compNode->link);
                    __TIMESTAMP();log("ADDED COMPONENT %p [%s]\r\n", comp, compNode->name);
                    DumpRegisteredComponents(cctx);
                }
            }
            else {
                retCode = IARM_RESULT_IPCCORE_FAIL;
            }
        }
        else {
        }
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
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    DirectResult dRet = DR_OK;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (!IARM_GrpCtx_IsValid(cctx) || ownerName == NULL || funcName == NULL) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if (retCode == IARM_RESULT_SUCCESS) {
        IComaComponent *comp = NULL;
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%s_%s", IARM_CALL_PREFIX, ownerName, funcName);
        dRet = cctx->coma->GetComponent( cctx->coma, compId, 0/*WaitForever*/,  &comp);
        if (dRet == DR_OK) {
            if (comp) {
                comp->Call(comp, 0, arg, ret);
                comp->Release( comp);
            }
        }
        else {
            retCode = IARM_RESULT_IPCCORE_FAIL;
        }
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
 * @param [in] serial a number that must match the "serial" passed in when the RPC implementation is executed. 
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_CallReturn(const char *ownerName, const char *funcName, int ret, int serial)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    DirectResult dRet = DR_OK;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (!IARM_GrpCtx_IsValid(cctx) || ownerName == NULL || funcName == NULL) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if (retCode == IARM_RESULT_SUCCESS) {
        IComaComponent *comp = NULL;
        char compId[IARM_MAX_NAME_LEN] = {0};

        snprintf(compId, sizeof(compId), "%s_%s_%s", IARM_CALL_PREFIX, ownerName, funcName);

        dRet = cctx->coma->GetComponent( cctx->coma, compId, 0/*WaitForever*/,  &comp);
        if (dRet == DR_OK && comp != NULL) {
            dRet = comp->Return(comp, ret, serial);
            comp->Release( comp);
        }
        else {
            retCode = IARM_RESULT_IPCCORE_FAIL;
        }
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

    if (!IARM_GrpCtx_IsValid(cctx) || !IARM_EventID_IsValid(0)) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if (retCode == IARM_RESULT_SUCCESS) {

        DirectResult dRet = DR_OK;
        IComaComponent *comp = NULL;

        if (1) { /*@TODO: Only allow an eventId be registered once */
            char compId[IARM_MAX_NAME_LEN] = {0};
            snprintf(compId, sizeof(compId), "%s_%s", IARM_EVENT_PREFIX, ownerName);
            dRet = cctx->coma->CreateComponent( cctx->coma, compId, _EventComponentStubCall/*NO call*/, maxEventId, NULL, &comp);
            /*log("CREATED Event Comp %p, %x\r\n", comp, dRet);*/

            if (dRet == DR_OK) {
            	int i = 0;
            	for (i = 0; i < maxEventId; i++) {
            		dRet = comp->InitNotification(comp, i, _ListenersDone /*Used when needed for debug*/, grpCtx, (ComaNotificationFlags)0 );//CNF_DEALLOC_ARG);
            	}
                if (dRet == 0) {
                    dRet = comp->Activate(comp);
                    if (dRet == DR_OK) {
                        /* Register the component */
                        Component_Node_t *compNode = NULL;
                        IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(Component_Node_t), (void **)&compNode);
                        compNode->comp = comp;
                        strncpy(compNode->name, compId, IARM_MAX_NAME_LEN);
                        direct_list_append(&cctx->compList, &compNode->link);
                        __TIMESTAMP();log("ADDED COMPONENT %p [%s]\r\n", comp, compNode->name);

                        DumpRegisteredComponents(cctx);
                    }
                    else {
                        log(" Event Comp %p failed1 %x\r\n", comp, dRet);
                    }

                }
                else {
                    log(" Event Comp %p failed2 %x\r\n", comp, dRet);

                    retCode = IARM_RESULT_IPCCORE_FAIL;
                }
            }
            else {
                log(" Event Comp %p failed3 %x\r\n", comp, dRet);
                retCode = IARM_RESULT_IPCCORE_FAIL;
            }
        }
        else {
            retCode = IARM_RESULT_INVALID_STATE;
        }
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
 * @param [in] arg Argument to be passed to the listeners. Note that this arg must be allocated from shared memory 
 *             via IARM_Malloc(IARM_MEMTYPE_PROCESSSHARE). IARM Module will free this memor once all listeners are
 *             notified.
 *
 * @return IARM_Result_t Error Code.
 */
IARM_Result_t IARM_NotifyEvent(const char *ownerName,  IARM_EventId_t eventId, void *arg)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (!IARM_GrpCtx_IsValid(cctx) || !IARM_EventID_IsValid(eventId)) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    IARM_ASSERT((arg == NULL) || (IARM_GetMemType(arg) == IARM_MEMTYPE_PROCESSSHARE));

    if (retCode == IARM_RESULT_SUCCESS) {
        IComaComponent *comp = NULL;
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%s", IARM_EVENT_PREFIX, ownerName);
        DirectResult dRet = cctx->coma->GetComponent( cctx->coma, compId, 0/*WaitForever*/,  &comp);
        if (dRet == DR_OK && comp) {
            dRet = comp->Notify(comp, eventId, arg);
            if (dRet != DR_OK) {
                log("Notiyfing Event returned error %d\r\n", dRet);
            }
            comp->Release( comp);
        }
        else {
            retCode = IARM_RESULT_IPCCORE_FAIL;
        }
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
    DirectResult dRet = DR_OK;
    IComaComponent *comp = NULL;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (!IARM_GrpCtx_IsValid(cctx) || !IARM_EventID_IsValid(eventId)) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if (retCode == IARM_RESULT_SUCCESS) {
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%s", IARM_EVENT_PREFIX, ownerName);
        dRet = cctx->coma->GetComponent( cctx->coma, compId, 0/*WaitForever*/,  &comp);
        if (dRet == DR_OK && comp) {
            dRet = comp->Listen(comp, eventId, listener, callCtx);
            if (dRet == DR_OK) {
                /* Register the component */
                Component_Node_t *compNode = NULL;
                IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(Component_Node_t), (void **)&compNode);
                compNode->comp = comp;
                strncpy(compNode->name, compId, IARM_MAX_NAME_LEN);
                direct_list_append(&cctx->compList, &compNode->link);
                DumpRegisteredComponents(cctx);
            }
            else {
                retCode = IARM_RESULT_IPCCORE_FAIL;
            }
        }
        else {
            retCode = IARM_RESULT_IPCCORE_FAIL;
        }
    }

    return retCode;
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
    DirectResult dRet = DR_OK;
    IComaComponent *comp = NULL;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (!IARM_GrpCtx_IsValid(cctx) || !IARM_EventID_IsValid(eventId)) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }

    if (retCode == IARM_RESULT_SUCCESS) {
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%s", IARM_EVENT_PREFIX, ownerName);
        {
            /* Find the component */
            comp = NULL;
            DirectLink *registeredMember = NULL;
            DirectLink *next;
            Component_Node_t *compNode = NULL;
            direct_list_foreach_safe(registeredMember, next, cctx->compList) { 
                compNode = (Component_Node_t *)registeredMember;
                if (strcmp(compNode->name, compId) == 0) {
                    comp = compNode->comp;
                    direct_list_remove(&cctx->compList, &compNode->link);
                    IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, compNode);
                    break;
                }
            }
        }

        if (comp != NULL) {
            dRet = comp->Unlisten(comp, eventId);
            comp->Release(comp);
            if (dRet == DR_OK) {
            }
            else {
                retCode = IARM_RESULT_IPCCORE_FAIL;
            }
            DumpRegisteredComponents(cctx);
        }
        else {
            retCode = IARM_RESULT_INVALID_STATE;
        }
    }

    return retCode;
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
     DirectResult dRet = DR_OK;
     IARM_Result_t retCode = IARM_RESULT_SUCCESS;
     IARM_Ctx_t *cctx = NULL;
     
     if ( groupName == NULL || (strlen(groupName) >= IARM_MAX_NAME_LEN)) {
    	 retCode = IARM_RESULT_INVALID_PARAM;
     }
     else if ( groupName == NULL || (strlen(groupName) >= IARM_MAX_NAME_LEN)) {
    	 retCode = IARM_RESULT_INVALID_PARAM;
     }
     else {
		 retCode = IARM_Malloc(IARM_MEMTYPE_PROCESSLOCAL, sizeof(*cctx), (void **)&cctx);
		 memset(cctx, 0, sizeof(*cctx));
     }


     if (retCode == IARM_RESULT_SUCCESS) {

         dRet = FusionDaleInit(NULL, NULL);

         if (dRet == DR_OK) {
             dRet = FusionDaleCreate( &cctx->dale );

             if (dRet == DR_OK) {
                 dRet = cctx->dale->EnterComa( cctx->dale, groupName, &cctx->coma );
                 
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

                 if (dRet == DR_OK) {
                     cctx->isActive = _IARM_CTX_HEADER_MAGIC;
                     m_grpCtx = cctx;
                 }
                 else {
                     FusionDaleErrorFatal( "EnterComa", dRet );
                 }
             }
             else {
                 FusionDaleErrorFatal( "FusionDaleCreate", dRet );
             }
         }
         else {
             FusionDaleErrorFatal( "FusionDaleInit", dRet );
         }

         if (dRet != DR_OK) {
             retCode = IARM_RESULT_IPCCORE_FAIL;
         }
     }

     if (retCode == IARM_RESULT_SUCCESS) {
     }
     else {
     }

     direct_signals_shutdown(); //disabling signal handlers from DirectFB, this allows IARMBus client applications to handle signals properly.

     return retCode;
}

IARM_Result_t IARM_IsCallRegistered(const char *ownerName, const char *callName, int *isRegistered)
{
    IARM_Result_t retCode = IARM_RESULT_SUCCESS;
    IARM_Ctx_t *grpCtx = m_grpCtx;
    IARM_Ctx_t * cctx = (IARM_Ctx_t *)grpCtx;

    if (!IARM_GrpCtx_IsValid(cctx) || callName == NULL) {
        retCode = IARM_RESULT_INVALID_PARAM;
    }
    else if ( ownerName == NULL || (strlen(ownerName) >= IARM_MAX_NAME_LEN)) {
    	retCode = IARM_RESULT_INVALID_PARAM;
    }
    else if ( callName == NULL || (strlen(callName) >= IARM_MAX_NAME_LEN)) {
    	retCode = IARM_RESULT_INVALID_PARAM;
    }

    /*log("Entering [%s] - [%s][%s]\r\n", __FUNCTION__, ownerName, callName);*/

    if (retCode == IARM_RESULT_SUCCESS) {
        DirectResult dRet = DR_OK;
        IComaComponent *comp = NULL;
        char compId[IARM_MAX_NAME_LEN] = {0};
        snprintf(compId, sizeof(compId), "%s_%s_%s", IARM_CALL_PREFIX, ownerName, callName);
        if (comp == NULL) {
            /*log("Checking CALL Comp\r\n");*/

            dRet = cctx->coma->GetComponent( cctx->coma, compId, 1/*Wait 1ms*/,  &comp);
            if (dRet == DR_OK) {
            	*isRegistered = 1;
                comp->Release( comp);
            }
            else {
            	*isRegistered = 0;
            }
        }
        else {
        }
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

	 /*log("Entering [%s]\r\n", __FUNCTION__);*/

    if (retCode == IARM_RESULT_SUCCESS) {
        /* Check if resources has been cleaned up */
        {
            DirectLink *registeredMember = NULL;
            DirectLink *next = NULL;
            direct_list_foreach_safe(registeredMember, next, cctx->compList) { 
                Component_Node_t *compNode = (Component_Node_t *)registeredMember;
                __TIMESTAMP();log("REMOVING Component [%s]\r\n", compNode->name);
                direct_list_remove( &cctx->compList, &compNode->link);
                compNode->comp->Release(compNode->comp);
                IARM_Free(IARM_MEMTYPE_PROCESSLOCAL, compNode);
            }
        }
        DumpRegisteredComponents(cctx);

        /* Release fusion resources */
        /*log("3111111111111111111111111111111111111111111111111111\r\n");*/
        cctx->coma->Release( cctx->coma );
        /*log("2222222222222222222222222222222222222222222222222222\r\n");*/
        cctx->dale->Release( cctx->dale );
       /* log("3333333333333333333333333333333333333333333333333333\r\n");*/
        cctx->isActive = 0;
        /*log("4444444444444444444444444444444444444444444444444444\r\n");*/
		
		__TIMESTAMP();log("IARM_Term Released fusion resources \r\n");

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
		log("MallocShareCount %d, FreeShareCount %d\r\n", mallocShareCount, freeShareCount);
	#endif
}

static void _ListenersDone(void *gctx, ComaNotificationID notification, void *arg)
{
    if (arg != NULL) {
        IARM_Free(IARM_MEMTYPE_PROCESSSHARE, arg);
    }
}

static void DumpRegisteredComponents(IARM_Ctx_t * cctx)
{
	#if 0
		DirectLink *registeredMember = NULL;
		DirectLink *next;
		int i = 0;
		log("=================+Registered Components+===========================\r\n");
		direct_list_foreach_safe(registeredMember, next, cctx->compList) { 
			Component_Node_t *compNode = (Component_Node_t *)registeredMember;
			log("COMP#%d - %s\r\n", i, compNode->name);
			i++;
		}
		log("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\r\n\r\n");
	#endif
}

static void _EventComponentStubCall(void *ctx, unsigned long methodID, void *arg, unsigned int magic)
{
    /* This stub function is added because FusionDale's DEBUG version asserts that all components 
     * supply a "Call" method. This should never be invoked. 
     */
    int *p = (int *)0xBADEFACE;
    *p = 0;
}
