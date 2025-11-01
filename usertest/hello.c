/*
 * Simple test program for testos
 * This will be compiled as a static musl binary
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    printf("Hello from user mode!\n");
    printf("argc = %d\n", argc);
    
    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }
    
    printf("Test completed successfully.\n");
    
    return 0;
}
