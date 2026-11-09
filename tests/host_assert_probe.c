#include "host_assert.h"
int main(void)
{
    assert(0 && "intentional host assertion probe");
    return 0;
}
