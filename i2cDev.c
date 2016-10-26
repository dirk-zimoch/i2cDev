#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <epicsTypes.h>
#include <iocsh.h>
#include <regDev.h>
#include <epicsExport.h>

#include "i2c.h"

epicsExportAddress(int, i2cDebug);

struct regDevice
{
    int fd;
    unsigned int addr;
};

void i2cDevReport(regDevice *device, int level)
{
    int n;
    char filename[32];
    sprintf(filename, "/proc/self/fd/%d", device->fd);
    n = readlink(filename, filename, sizeof(filename)-1);
    if (n < 0) n = 0;
    printf("i2c fd=%d %.*s addr=0x%02x\n", device->fd, n, filename, device->addr);
}

int i2cDevRead(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    int priority,
    regDevTransferComplete callback,
    const char* user)
{
    int i;
    
    if (dlen == 0) return 0; /* any way to check online status ? */
    if (dlen > 2)
    {
        if (i2cDebug>=0) fprintf(stderr,
            "%s %s: only 1 or 2 bytes supported\n", user, regDevName(device));
        return -1;
    }
    for (i = 0; i < nelem; i++)
    {
        if (i2cRead(device->fd, offset + i, dlen, pdata) != 0)
            return -1;
        pdata = (void*) (((size_t) pdata) + dlen);
    }
    return 0;
}

int i2cDevWrite(
    regDevice *device,
    size_t offset,
    unsigned int dlen,
    size_t nelem,
    void* pdata,
    void* pmask,
    int priority,
    regDevTransferComplete callback,
    const char* user)
{
    int i;
    int value = 0;
    
    if (dlen > 2)
    {
        if (i2cDebug>=0) fprintf(stderr,
            "%s %s: only 1 or 2 bytes supported\n", user, regDevName(device));
        return -1;
    }
    for (i = 0; i < nelem; i++)
    {
        switch (dlen)
        {
            case 1:
                value = ((epicsUInt8*) pdata)[i];
                break;
            case 2:
                value = ((epicsUInt16*) pdata)[i];
                break;
        }
        if (i2cWrite(device->fd, offset + i, dlen, value) != 0)
            return -1;
    }
    return 0;
}

struct regDevSupport i2cDevRegDev = {
    .report = i2cDevReport,
    .read = i2cDevRead,
    .write = i2cDevWrite,
};

int i2cDevConfigure(const char* name, const char* path, unsigned int address, unsigned int maxreg)
{
    regDevice *device = NULL;
    int fd;
    
    if (!name || !path || !name[0] || !path[0])
    {
        printf("usage: i2cDevConfigure name path device [maxreg]\n");
        return -1;
    }
    fd = i2cOpen(path, address);
    device = malloc(sizeof(regDevice));
    if (!device)
    {
        perror("malloc regDevice");
        close(fd);
        return -1;
    }
    device->fd = fd;
    device->addr = address;
    if (regDevRegisterDevice(name, &i2cDevRegDev, device, maxreg ? maxreg + 1 : 0) != SUCCESS)
    {
        perror("regDevRegisterDevice() failed");
        close(fd);
        free(device);
        return -1;
    }
    if (regDevInstallWorkQueue(device, 100) != SUCCESS)
    {
        perror("regDevInstallWorkQueue() failed");
        return -1;
    }
    return 0;
    return -1;
}

static const iocshFuncDef i2cDevConfigureDef =
    { "i2cDevConfigure", 4, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "bus", iocshArgString },
    &(iocshArg) { "device", iocshArgInt },
    &(iocshArg) { "maxreg", iocshArgInt },
}};

static void i2cDevConfigureFunc(const iocshArgBuf *args)
{
    i2cDevConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}

static void i2cRegistrar(void)
{
    iocshRegister(&i2cDevConfigureDef, i2cDevConfigureFunc);
}

epicsExportRegistrar(i2cRegistrar);
