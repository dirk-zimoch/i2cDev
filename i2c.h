#ifndef i2c_h
#define i2c_h

#ifdef __cplusplus
extern "C" {
#endif

extern int i2cDebug;

int i2cOpenBus(const char* path);
int i2cOpen(const char* path, unsigned int address);
int i2cRead(int fd, unsigned int command, unsigned int dlen, void* value);
int i2cWrite(int fd, unsigned int command, unsigned int dlen, int value);

#ifdef __cplusplus
}
#endif
#endif
