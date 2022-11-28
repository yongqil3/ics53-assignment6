#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Md5.c"

void print_hash(unsigned char *digest)
{
    unsigned char * temp = digest;
    printf("Hash: ");
    while(*temp)
    {
        printf("%02x", *temp);
        temp++;

    }
    printf("\n");
}

void main(void)
{
    char * filename = "1.txt";
    char * ch = "f";
    unsigned char digest[16]; //  a buffer to store the hash into
    MDFile(filename, digest); // function that calculates the hash of the file
    print_hash(digest);
    printf("Modifying file...\n");
    FILE * f = fopen(filename, "a");
    fwrite(ch, 1,1,f); // write a single character to the file
    fclose(f);
    MDFile(filename, digest); // calculate the hash again
    print_hash(digest);
}