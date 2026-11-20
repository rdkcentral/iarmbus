#include <string.h>

#include "safec_lib.h"

int main(void)
{
    char destination[8] = {0};
    const char *source = "iarm";
    errno_t result = strcpy_s(destination, sizeof(destination), source);

    return result == EOK && strcmp(destination, "iarm") == 0 ? 0 : 1;
}