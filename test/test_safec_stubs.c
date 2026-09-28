#include <stdio.h>
#include <string.h>

#include "safec_lib.h"

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check failed at line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    char destination[4] = {'A', 'A', 'A', '\0'};
    const char source[5] = {'B', 'B', 'B', 'B', '\0'};

    CHECK(memcpy_s(destination, sizeof(destination), source, sizeof(source)) != EOK);
    CHECK(destination[0] == 'A');
    CHECK(memset_s(destination, sizeof(destination), 0, sizeof(destination) + 1) != EOK);
    CHECK(destination[0] == 'A');
    CHECK(strcpy_s(destination, sizeof(destination), source) != EOK);
    CHECK(destination[0] == 'A');
    return 0;
}
