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


#ifndef _IARM_BUS_BARMGR_H
#define _IARM_BUS_BARMGR_H

#ifdef __cplusplus
extern "C" 
{
#endif
#define IARM_BUS_BARMGR_NAME 						"BarMgr"
/*
 * Declare Published Events
 */
typedef enum _BarMgr_EventId_t {
	IARM_BUS_BARMGR_EVENT_DUMMY0,
    IARM_BUS_BARMGR_EVENT_DUMMY1,
    IARM_BUS_BARMGR_EVENT_DUMMY2,

    IARM_BUS_BARMGR_EVENT_MAX,
} IARM_Bus_BarMgr_EventId_t;

/*
 * Declare Event Data
 */
typedef struct _BarMgr_EventData_t {
    union {
        struct _EventData_DUMMY_0{
        	/* Declare Event Data structure for BARMGR_EVENT_DUMMY0 */
        	int dummyData;
        } dummy0;
        struct _EventData_DUMMY_1{
        	/* Declare Event Data structure for BARMGR_EVENT_DUMMY1 */
        	int dummyData;
        } dummy1;
        struct _EventData_DUMMY_2{
        	/* Declare Event Data structure for BARMGR_EVENT_DUMMY2 */
        	int dummyData;
        } dummy2;
    } data;
}IARM_Bus_BarMgr_EventData_t;

/*
 * Declare RPC API names and their arguments
 */
#define IARM_BUS_BARMGR_API_DummyAPI0 		"DummyAPI0"
typedef struct _IARM_Bus_BarMgr_DummyAPI0_Param_t {
	/* Declare Argument Data structure for DummyAPI0 */
} IARM_Bus_BarMgr_DummyAPI0_Param_t;

#define IARM_BUS_BARMGR_API_DummyAPI1 		"DummyAPI1"
typedef struct _IARM_Bus_BarMgr_DummyAPI1_Param_t {
	/* Declare Argument Data structure for DummyAPI1 */
} IARM_Bus_BarMgr_DummyAPI1_Param_t;

#ifdef __cplusplus
}
#endif
#endif


/** @} */
/** @} */
