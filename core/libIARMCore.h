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
* @file libIARMCore.h
*
* @brief IARM-Bus IARM core library API.
*
* This API defines the core operations for IARM
*
* @par Document
* Document reference.
*
* @par Open Issues (in no particular order)
* -# None
*
* @par Assumptions
* -# None
*
* @par Abbreviations
* - BE:       ig-Endian.
* - cb:       allback function (suffix).
* - DS:      Device Settings.
* - FPD:     Front-Panel Display.
* - HAL:     Hardware Abstraction Layer.
* - LE:      Little-Endian.
* - LS:      Least Significant.
* - MBZ:     Must be zero.
* - MS:      Most Significant.
* - RDK:     Reference Design Kit.
* - _t:      Type (suffix).
*
* @par Implementation Notes
* -# None
*
*/

/** @defgroup IARM_BUS IARM_BUS
*    @ingroup IARM_BUS
*
*  IARM-Bus is a platform agnostic Inter-process communication (IPC) interface. It allows
*  applications to communicate with each other by sending Events or invoking Remote
*  Procedure Calls. The common programming APIs offered by the RDK IARM-Bus interface is
*  independent of the operating system or the underlying IPC mechanism.
*
*  Two applications connected to the same instance of IARM-Bus are able to exchange events
*  or RPC calls. On a typical system, only one instance of IARM-Bus instance is needed. If
*  desired, it is possible to have multiple IARM-Bus instances. However, applications
*  connected to different buses will not be able to communicate with each other.
*/

/** @addtogroup IARM_BUS_IARM_CORE_API IARM-Core library.
*  @ingroup IARM_BUS
*
*  Described herein are the functions that are part of the
*  IARM Core library.
*
*  @{
*/

#ifndef _LIB_IARMCORE_H
#define _LIB_IARMCORE_H

#include "libIARM.h"

#ifdef __cplusplus
extern "C" 
{
#endif
#include <stdbool.h>
#define IARM_ASSERT(cond) while(!(cond)) \
{ \
    printf("IARM_ASSERT Failed at [%s @ %d]\r\n", __func__, __LINE__);\
    break; \
}\

/*! IARM Event data*/
typedef struct _IARM_EventData_t {
	char owner[IARM_MAX_NAME_LEN];   /*!< Owner of the event*/
	IARM_EventId_t id;               /*!< Event id*/
	size_t len;                      /*!< Size of event data*/
	char data[];                     /*!< Event data*/
} IARM_EventData_t;

typedef enum _IARM_MemType_t 
{
    IARM_MEMTYPE_THREADLOCAL = 1, /*!< Thread local memory */
    IARM_MEMTYPE_PROCESSLOCAL,	/*!< Process local memory */
    IARM_MEMTYPE_PROCESSSHARE,	/*!< Process shared memory */
} IARM_MemType_t;

/**
 * @brief Callback prototype for IARM RPC Call.
 * 
 */
typedef void (*IARM_Call_t) (void *ctx, unsigned long methodID, void *arg, void *magic);

/**
 * @brief Callback prototype for IARM listener interface.
 * 
 */
typedef void (*IARM_Listener_t) (void *ctx, void *arg);

/**
 * @brief Initialize the IARM module for the calling process.
 * 
 * This API is used to initialize the IARM module for the calling process, and regisers the calling process to
 * be visible to other processes that it wishes to communicate to. The registered process is uniquely identified
 * by (groupName, memberName).
 *
 * @param [in] groupName The IPC group this process wishes to participate.
 * @param [in] memberName The name of the calling process.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_Init(const char *groupName, const char *memberName);

/**
 * @brief Allocate memory for IARM member processes.
 * 
 * This API allows IARM member process to allocate local memory or shared memory.
 * There are two types of local memory: Process local and thread local. Local memory
 * is only accessible by the process who allocates. 
 *
 * Shared memory, is accessible by all processes within the same group as the process that
 * allocates the memory.
 *
 * For example,  RPC call arguments of complicated structures should be allocated from shared 
 * memory so the RPC invocation can access these arguments from a different process.
 *
 * @param [in] type Thread-Local, Process-Local or Shared memory.
 * @param [in] size Number of bytes to allocate.
 * @param [out] ptr Return allocated memory.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_Malloc(IARM_MemType_t type, size_t size, void **ptr);

/**
 * @brief Free memory allocated by IARM member processes.
 * 
 * This API allows IARM member process to free local memory or shared memory. 
 * The type specified in the free() API must match that specified in the malloc()
 * call.
 *
 * @param [in] type Thread-Local, Process-Local or Shared memory. This must match the type of the allocated memory.
 * @param [in] alloc Points to the allocated memory to be freed.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_Free(IARM_MemType_t type, void *alloc);


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
 * @param [in] ownerName The name of this member process that implements the Call.
 * @param [in] callName The name of the function that this member offers as RPC-Call.
 * @param [in] handler The function that can be called by (ownerName, callName)
 * @param [in] ctx Local context to passed in when the function is called. 
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_RegisterCall(const char *ownerName, const char *callName, IARM_Call_t handler, void *ctx);

/**
 * @brief Make a RPC call 
 * 
 * Invoke the RPC function by specifying its names.  This call will block until
 * the specified RPC function is published.
 *
 * @param [in] ownerName The name of member process implementing the RPC call.
 * @param [in] funcName The name of the function to call. 
 * @param [in] arg Supply the argument to be used by the RPC call.
 * @param [out] ret Returns the return value of the RPC call.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_Call(const char *ownerName,  const char *funcName, void *arg, int *ret);

/**
 * @brief Make a RPC call with Timeout
 *
 * Invoke the RPC function by specifying its names.  This call will block until
 * the specified RPC function is published.
 *
 * @param [in] ownerName The name of member process implementing the RPC call.
 * @param [in] funcName The name of the function to call.
 * @param [in] arg Supply the argument to be used by the RPC call.
 * @param [in] timeout millisecond time interval to be used for the RPC call.
 * @param [out] ret Returns the return value of the RPC call.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_CallWithTimeout(const char *ownerName,  const char *funcName, void *arg, int timeout, int *ret);

/**
 * @brief Check whether a RPC call is registered or not.
 * 
 * RPC calls should be registered so that other processes can call. 
 *
 * @param [in] ownerName The name of owner process implementing the RPC call.
 * @param [in] callName The name of the function to be checked.
 * @param [out] isRegistered returns the status whether the RPC call is registered.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_IsCallRegistered(const char *ownerName, const char *callName, int *isRegistered);

#if 1
/**
 * @brief Explicitly mark the return/finish of a RPC-Call 
 * 
 * This API must be called by the implementation of the RPC-Call to submit the return value to the caller.
 * Otherwise the caller will be blocked forever waiting for the return value.
 *
 * This API does not need to be called at the end of the RPC invokation. It can be called anytime by anybody 
 * after the RPC call is requested, to unblocked the caller.
 *
 * @param [in] ownerName The name of owner process implementing the RPC call.
 * @param [in] funcName The name of the function to call. 
 * @param [in] ret return value of the executed RPC-Call.
 * @param [in] callMsg must match the "serial" passed in when the RPC implementation is executed.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_CallReturn(const char *ownerName, const char *funcName, void *arg, int ret, void *callMsg);

#else
/**
 * @brief Explicitly mark the return/finish of a RPC-Call 
 * 
 * This API must be called by the implementation of the RPC-Call to submit the return value to the caller.
 * Otherwise the caller will be blocked forever waiting for the return value.
 *
 * This API does not need to be called at the end of the RPC invokation. It can be called anytime by anybody 
 * after the RPC call is requested, to unblocked the caller.
 *
 * @param [in] ownerName The name of owner process implementing the RPC call.
 * @param [in] funcName The name of the function to call. 
 * @param [in] ret return value of the executed RPC-Call.
 * @param [in] serial a number that must match the "serial" passed in when the RPC implementation is executed. 
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */

IARM_Result_t IARM_CallReturn(const char *ownerName,  const char *funcName, int ret, int serial);
#endif
/**
 * @brief Register a event that can be listened to by other processes.
 * 
 * All events published within a process group are uniquely identified by an eventId.
 * A process uses this API to publish an event so this event can be listened by
 * other processes.
 * 
 * An event must be published before it can be "notified"
 *
 * @param [in] ownerName The name of the member process that owns the event.
 * @param [in] eventId The ID of the event. This ID is unique across all processes. 
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_RegisterEvent(const char *ownerName,  IARM_EventId_t eventId);

/**
 * @brief Notify listeners of event
 * 
 * This API is used to notify all listeners of a certain event.
 *
 * @param [in] ownerName The name of the member process that owns the event.
 * @param [in] eventId The ID of the event.
 * @param [in] arg Argument to be passed to the listeners. Note that this arg must be allocated from shared memory 
 *             via IARM_Malloc(IARM_MEMTYPE_PROCESSSHARE). IARM Module will free this memor once all listeners are
 *             notified.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_NotifyEvent(const char *ownerName,  IARM_EventId_t eventId, void *arg);

/**
 * @brief Register to listen for an event. 
 * 
 * This API is used to register the calling process for a certain event.
 * If the event is not yet published, this call be be blocked until the event
 * is published.
 *
 * @param [in] ownerName The name of the member process that owns the event.
 * @param [in] eventId The ID of the event.
 * @param [in] listener Callback function when the event is received. 
 * @param [in] ctx Local context used when calling the listener's callback function.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_RegisterListner(const char *ownerName,  IARM_EventId_t eventId, IARM_Listener_t listener, void *ctx);

/**
 * @brief UnRegister to listen for an event. 
 * 
 * This API is used to unregister the calling process for a certain event.
 *
 * @param [in] ownerName The name of the member process that owns the event.
 * @param [in] eventId The ID of the event.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_UnRegisterListner(const char *ownerName,  IARM_EventId_t eventId);

/**
 * @brief Terminate the IARM module for the calling process.
 * 
 * This API is used to  terminate the IARM module for the calling process, and cleanup resources
 * used by MAF.
 *
 * @return IARM_Result_t Error Code.
 * @retval IARM_RESULT_SUCCESS on success
 */
IARM_Result_t IARM_Term(void);

#ifdef __cplusplus
}
#endif
#endif

/* End of IARM_BUS_IARM_CORE_API doxygen group */
/**
 * @}
 */
