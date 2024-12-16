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
* @file libIBusDaemonInternal.h
*
* @brief IARM-Bus Daemon API
*
* This API defines the operations for IARM bus Daemon.
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

/** @defgroup IARM_BUS IARM-Bus Daemon
*   @ingroup IARM_RDK
*
* 
*/

/** @addtogroup IARM_BUS_DAEMON_INTERNAL_API IARM-BUS Daemon Internal API.
*  @ingroup IARM_BUS
*
* Described here are the operations exposed by libIBusDaemon, which is the
* main daemon process of IARM.
*
*  @{
*/

#ifndef _IARM_BUS_DAEMON_INTERNAL_H
#define _IARM_BUS_DAEMON_INTERNAL_H

#include <libIBus.h>

#include "libIBusDaemon.h"

#if 1
#include <glib.h>
#else
#include <direct/list.h>
#endif

/*! Published Names */
typedef struct _IARM_Bus_Member_t
{
#if 1
    GList           link;                            /*!< must be first member */
#else
    DirectLink      link;                            /*!< must be first member */
#endif
    void            *gctx;                           /*!< group context */
    int             pid;                             /*!< pid of the IARM member process*/
    char            selfName[IARM_MAX_NAME_LEN];     /*!< IARM member name */
} IARM_Bus_Member_t;

#define IARM_BUS_DAEMON_API_RequestOwnership 		"RequestOwnership" /*!< Request Ownership of a resource*/
#if 1
/*! Parameter for Request Ownership call*/
typedef struct _IARM_Bus_Daemon_RequestOwnership_Param_t {
  IARM_Bus_Member_t             requestor;           /*!< IARM member who requested for the resource*/ 
  IARM_Bus_ResrcType_t          resrcType;           /*!< Type of resource requested for */
  IARM_Result_t                 rpcResult ;          /*!< RPC Result code  */
} IARM_Bus_Daemon_RequestOwnership_Param_t;

#define IARM_BUS_DAEMON_API_ReleaseOwnership 		"ReleaseOwnership" /*!< Release ownership of a resource */

/*! Parameter for Release Ownership call*/
typedef struct _IARM_Bus_Daemon_ReleaseOwnership_Param_t {
  IARM_Bus_Member_t             requestor;            /*!< IARM member who requested for the resource*/ 
  IARM_Bus_ResrcType_t          resrcType;            /*!< Type of resource requested for */
  IARM_Result_t                 rpcResult ;           /*!< RPC Result code  */
} IARM_Bus_Daemon_ReleaseOwnership_Param_t;
#else
/*! Parameter for Request Ownership call*/
typedef struct _IARM_Bus_Daemon_RequestOwnership_Param_t {
  IARM_Bus_Member_t            *requestor;           /*!< IARM member who requested for the resource*/ 
  IARM_Bus_ResrcType_t          resrcType;           /*!< Type of resource requested for */
} IARM_Bus_Daemon_RequestOwnership_Param_t;

#define IARM_BUS_DAEMON_API_ReleaseOwnership 		"ReleaseOwnership" /*!< Release ownership of a resource */

/*! Parameter for Release Ownership call*/
typedef struct _IARM_Bus_Daemon_ReleaseOwnership_Param_t {
  IARM_Bus_Member_t            *requestor;            /*!< IARM member who requested for the resource*/ 
  IARM_Bus_ResrcType_t          resrcType;            /*!< Type of resource requested for */

} IARM_Bus_Daemon_ReleaseOwnership_Param_t;
#endif



/*! Parameter for Resolution change call*/
typedef struct _IARM_Bus_Daemon_ResolutionChange_Param_t {
  int height;          /*!<  Resolution height*/ 
  int width;           /*!<  Resolution width*/
} IARM_Bus_Daemon_ResolutionChange_Param_t;

#define IARM_BUS_DAEMON_API_ResolutionPreChange  	"DaemonResolutionPreChange" /*!< Well know name for resolution prechange */

#define IARM_BUS_DAEMON_API_ResolutionPostChange   "DaemonResolutionPostChange" /*!< Well know name for resolution postchange */


#define IARM_BUS_DAEMON_API_DeepSleepWakeup        "DaemonDeepSleepWakeup"      /*!< Well know name for DeepSleepWakeup*/

#define IARM_BUS_DAEMON_API_PowerPreChange        "DaemonPowerPreChange"      /*!< Well know name for power prechange*/
/*! Parameter for Power Pre-Change call*/
typedef struct _IARM_Bus_Daemon_PowerPreChange_Param_t {
  IARM_Bus_PWRMgr_PowerState_t  newState;                          /*!< New state after power change*/			
  IARM_Bus_PWRMgr_PowerState_t  curState;                          /*!< Current power state*/
} IARM_Bus_Daemon_PowerPreChange_Param_t;

#define IARM_BUS_DAEMON_API_RegisterMember 			"RegisterMember"         /*!< Register a new member to IARM*/
#define IARM_BUS_DAEMON_API_UnRegisterMember 		"UnRegisterMember"       /*!< Unregister a member from IARM*/

/*! data associated with Sys mod-change call*/
typedef struct _IARM_Bus_Daemon_SysModeChange_Param_t{
	IARM_Bus_Daemon_SysMode_t oldMode;                    
	IARM_Bus_Daemon_SysMode_t newMode;                   
}IARM_Bus_Daemon_SysModeChange_Param_t;

#define IARM_BUS_DAEMON_API_SysModeChange               "DaemonSysModeChange" /*!< Well known name for Sys mode change*/

/*! Parameter for Check registration call*/
typedef struct _IARM_Bus_Daemon_CheckRegistration_Param_t {
  char                  memberName[IARM_MAX_NAME_LEN];
  char                  isRegistered;
} IARM_Bus_Daemon_CheckRegistration_Param_t ;

#define IARM_BUS_DAEMON_API_CheckRegistration 		"CheckRegistration"      /*!< Check whether a member is registered with IARM*/


/*! Parameter for Check registration call*/
typedef struct _IARM_Bus_Daemon_RegisterPreChange_Param_t {
  char ownerName[IARM_MAX_NAME_LEN];
  char methodName[IARM_MAX_NAME_LEN];
} IARM_Bus_Daemon_RegisterPreChange_Param_t ;
#define IARM_BUS_DAEMON_API_RegisterPreChange     "RegisterPreChange"     /*!< Register a new PreChange member and method  to IARM*/


int log(const char *format, ...);

#endif

/* End of IARM_BUS_DAEMON_INTERNAL_API doxygen group */
/**
 * @}
 */
