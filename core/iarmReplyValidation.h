#ifndef IARM_REPLY_VALIDATION_H
#define IARM_REPLY_VALIDATION_H

#include <stddef.h>
#include <string.h>

static inline int IARM_IsValidReplySize(int replySize, size_t capacity)
{
    return replySize >= 0 && (size_t)replySize <= capacity;
}

static inline int IARM_CopyValidatedReply(void *destination, size_t capacity,
                                          const void *reply, int replySize)
{
    if (destination == NULL || !IARM_IsValidReplySize(replySize, capacity)) {
        return 0;
    }
    if (replySize == 0) {
        return 1;
    }
    if (reply == NULL) {
        return 0;
    }

    memcpy(destination, reply, (size_t)replySize);
    return 1;
}

#endif