#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "iarmReplyValidation.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "check failed at line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    unsigned char destination[4] = {0xaa, 0xaa, 0xaa, 0xaa};
    const unsigned char reply[5] = {1, 2, 3, 4, 5};
    const unsigned char unchanged[4] = {0xaa, 0xaa, 0xaa, 0xaa};

    CHECK(IARM_CopyValidatedReply(destination, sizeof(destination), reply, 0));
    CHECK(IARM_CopyValidatedReply(destination, sizeof(destination), reply, 4));
    CHECK(memcmp(destination, reply, sizeof(destination)) == 0);

    memcpy(destination, unchanged, sizeof(destination));
    CHECK(!IARM_CopyValidatedReply(destination, sizeof(destination), reply, 5));
    CHECK(memcmp(destination, unchanged, sizeof(destination)) == 0);
    CHECK(!IARM_CopyValidatedReply(destination, sizeof(destination), reply, -1));
    CHECK(memcmp(destination, unchanged, sizeof(destination)) == 0);
    CHECK(!IARM_CopyValidatedReply(NULL, sizeof(destination), reply, 4));
    CHECK(!IARM_CopyValidatedReply(destination, sizeof(destination), NULL, 4));
    return 0;
}
