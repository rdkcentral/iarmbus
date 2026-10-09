#include "libIARMCore.h"
#include "libIBus.h"

IARM_Result_t initResult = IARM_RESULT_SUCCESS;
IARM_Result_t termResult = IARM_RESULT_SUCCESS;
int initCalls = 0;
int termCalls = 0;
int connectCalls = 0;
int disconnectCalls = 0;
int eventRegistrationCalls = 0;
int methodRegistrationCalls = 0;

extern "C" IARM_Result_t __wrap_IARM_Init(const char *, const char *)
{
    ++initCalls;
    return initResult;
}

extern "C" IARM_Result_t __wrap_IARM_Term(void)
{
    ++termCalls;
    return termResult;
}

extern "C" IARM_Result_t __wrap_IARM_Bus_Connect(void)
{
    ++connectCalls;
    return IARM_RESULT_SUCCESS;
}

extern "C" IARM_Result_t __wrap_IARM_Bus_Disconnect(void)
{
    ++disconnectCalls;
    return IARM_RESULT_SUCCESS;
}

extern "C" IARM_Result_t __wrap_IARM_Bus_RegisterEvent(IARM_EventId_t)
{
    ++eventRegistrationCalls;
    return IARM_RESULT_SUCCESS;
}

extern "C" IARM_Result_t __wrap_IARM_Bus_RegisterCall(const char *, IARM_BusCall_t)
{
    ++methodRegistrationCalls;
    return IARM_RESULT_SUCCESS;
}