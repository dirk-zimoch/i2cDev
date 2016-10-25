#ifndef i2cDev_h
#define i2cDev_h

#ifdef __cplusplus
extern "C" {
#endif

int i2cDevConfigure(const char* name, const char* path, unsigned int device, unsigned int maxreg);

#ifdef __cplusplus
}
#endif
#endif
