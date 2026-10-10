#ifndef IARM_REPLY_VALIDATION_H
#define IARM_REPLY_VALIDATION_H

#include <stdint.h>
#include <string.h>

static inline int IARM_IsValidReplySize(int replySize, uint32_t capacity)
{
    return replySize >= 0 && (uint32_t)replySize <= capacity;
}

static inline int IARM_CopyValidatedReply(void *destination, uint32_t capacity, const void *reply, int replySize)
{
    if (destination == NULL || reply == NULL || !IARM_IsValidReplySize(replySize, capacity)) {
        return 0;
    }
    memcpy(destination, reply, (size_t)replySize);
    return 1;
}

#endif
