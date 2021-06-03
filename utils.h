#ifndef __STD_IO_LIB__
#define __STD_IO_LIB__
#include <stdio.h>
#include <stdlib.h>
#endif

// Decoding int in encoded 4 bytes (chars).
uint uint_from_bytes(char *args)
{
    return (args[0] << 24) | (args[1] << 16) | (args[2] << 8) | args[3];
}

// Encoding int in 4 bytes (chars) in BigEndian way.
void uint_to_bytes(char *str, uint id)
{
    str[0] = (id >> 24) & 0xFF;
    str[1] = (id >> 16) & 0xFF;
    str[2] = (id >> 8)  & 0xFF;
    str[3] = id & 0xFF;
}

void copy_range(const char *src, char *dst, size_t start, size_t count)
{
    int j = 0;
    for (int i = start; i < start + count; i++)
    {
        dst[j++] = src[i];
    }
}

void bzero_range(char *src, int start, int len)
{
    for (int i = start; i < start+len; i++)
    {
        src[i] = '\0';
    }
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