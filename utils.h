#ifndef __STD_IO_LIB__
#define __STD_IO_LIB__
#include <stdio.h>
#include <stdlib.h>
#endif

uint id_from_bytes(char *args)
{
    return (args[0] << 24) | (args[1] << 16) | (args[2] << 8) | args[3];
}

void id_to_bytes(char *str, uint id)
{
    str[0] = (id >> 24) & 0xFF;
    str[1] = (id >> 16) & 0xFF;
    str[2] = (id >> 8)  & 0xFF;
    str[3] = id & 0xFF;
}

void trim(char *str, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (str[i] == '\n' || str[i] == '\0')
        {
            str[i] = '\0';
            break;
        }
    }
}