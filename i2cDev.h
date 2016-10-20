#ifndef i2cDev_h
#define i2cDev_h
#ifdef __cplusplus
extern "C" {
#endif

extern int i2cDebug;

int i2cOpenFmt(unsigned int address, const char* pathformat, ...)
    __attribute__ ((__format__ (__printf__, 2, 3)));
int i2cOpenVar(unsigned int address, const char* pathformat, va_list ap)
    __attribute__ ((__format__ (__printf__, 2, 0)));
int i2cOpen(unsigned int address, const char* path)
    __attribute__ (( __nonnull__ (2)));
int i2cRead(int fd, unsigned int command, unsigned int dlen, void* value);
int i2cWrite(int fd, unsigned int command, unsigned int dlen, int value);

#ifdef __cplusplus
}
#endif
#endif
