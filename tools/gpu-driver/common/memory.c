/* Minimal freestanding module runtime. No kernel imports or C library ABI.
 * Volatile accesses prevent the compiler from replacing these implementation
 * loops with recursive calls to the very memcpy/memset being defined. */
#include <stddef.h>
#include <stdint.h>
void *memcpy(void *destination,const void *source,size_t bytes){
    volatile unsigned char *d=destination;const volatile unsigned char *s=source;
    for(size_t n=0;n<bytes;n++)d[n]=s[n];
    return destination;
}
void *memmove(void *destination,const void *source,size_t bytes){
    volatile unsigned char *d=destination;const volatile unsigned char *s=source;
    if((uintptr_t)d>(uintptr_t)s && (uintptr_t)d-(uintptr_t)s<bytes){for(size_t n=bytes;n;n--)d[n-1]=s[n-1];}
    else for(size_t n=0;n<bytes;n++)d[n]=s[n];
    return destination;
}
void *memset(void *destination,int value,size_t bytes){
    volatile unsigned char *d=destination;for(size_t n=0;n<bytes;n++)d[n]=(unsigned char)value;
    return destination;
}
int memcmp(const void *a,const void *b,size_t bytes){
    const volatile unsigned char *p=a,*q=b;
    for(size_t n=0;n<bytes;n++)if(p[n]!=q[n])return (int)p[n]-(int)q[n];
    return 0;
}
