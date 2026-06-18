#include <stdbool.h>
#include "board.h"
#include "app.h"

int main(void)
{
    DevicesInit();
    App_Init();

    while (true)
    {
        App_RunOnce();
    }
}
