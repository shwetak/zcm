#include "zcm.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static int handler(zcm_t *zcm, const char *channel, char *data, size_t len, void *usr)
{
    printf("Inside handler!\n");
    return 0;
}

int main()
{
    zcm_t *zcm = zcm_create();
    zcm_subscribe(zcm, "foobar", handler, NULL);

    char data[] = "Foobar Data";

    while (1) {
        zcm_publish(zcm, "foobar", data, sizeof(data));
        usleep(250000);
    }

    return 0;
}
