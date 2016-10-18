#ifndef i2cDev_h
#define i2cDev_h
#ifdef __cplusplus
extern "C" {
#endif

extern int i2cDebug;

int i2cOpen(const char* busname, unsigned int address);
int i2cRead(int fd, unsigned int command, unsigned int dlen, void* value);
int i2cWrite(int fd, unsigned int command, unsigned int dlen, int value);
int i2cClose(int fd);

#ifdef __cplusplus
}
#endif
#endif
