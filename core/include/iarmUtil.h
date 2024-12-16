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

#ifndef _IARM_UTIL_H
#define _IARM_UTIL_H
#include <time.h>
#include <sys/time.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <unistd.h>

#define __TIMESTAMP() do { /*YYMMDD-HH:MM:SS:usec*/               \
            struct tm __tm;                                             \
            struct timeval __tv;                                        \
            gettimeofday(&__tv, NULL);                                  \
            localtime_r(&__tv.tv_sec, &__tm);                           \
            printf("\r\n[tid=%ld]: %02d%02d%02d-%02d:%02d:%02d:%06d ",                 \
                                                syscall(SYS_gettid), \
                                                __tm.tm_year+1900-2000,                             \
                                                __tm.tm_mon+1,                                      \
                                                __tm.tm_mday,                                       \
                                                __tm.tm_hour,                                       \
                                                __tm.tm_min,                                        \
                                                __tm.tm_sec,                                        \
                                                (int)__tv.tv_usec);                                      \
} while(0)                              


#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif
#define container_of(ptr, type, member) ({          \
            const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
            (type *)( (char *)__mptr - offsetof(type,member) );})



#define _RETURN_IF_ERROR(cond, err) do {\
    if (!(cond)) { \
        printf("ERROR at [%s][%d]\r\n", __FILE__, __LINE__);\
        return (err); \
    } \
}while(0)

#define _DEBUG_ENTER() do {\
}while(0)

#define _DEBUG_EXIT(ret)  do {\
}while(0)

typedef void (*IARM_Bus_LogCb)(const char *);

#ifdef __cplusplus
extern "C" 
{
#endif

void IARM_Bus_RegisterForLog(IARM_Bus_LogCb cb); 

#ifdef __cplusplus
};
#endif

#endif
