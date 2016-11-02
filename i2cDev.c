#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#include <epicsTypes.h>
#include <epicsMutex.h>
#include <iocsh.h>
#include <regDev.h>
#include <epicsExport.h>

#include "i2c.h"

epicsExportAddress(int, i2cDebug);

struct mux
{
    int fd;
    unsigned char addr;
    unsigned char cmd;
};

struct regDevice
{
    epicsMutexId buslock;
    int fd;
    unsigned char busnum;
    unsigned char addr;
    unsigned char nmux;
    struct mux mux[0];
};

void i2cDevReport(regDevice *device, int level)
{
    printf("i2c-%u:0x%02x", device->busnum, device->addr);
    if (device->nmux)
    {
        int n;
        printf(" mux:");
        for (n = 0; n < device->nmux; n++)
            printf(" 0x%02x=0x%02x", device->mux[n].addr, device->mux[n].cmd);
    }
    printf("\n");
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
    epicsMutexLock(device->buslock);
    for (i = 0; i < device->nmux; i++)
    {
        if (i2cWrite(device->mux[i].fd, device->mux[i].cmd, 0, 0) != 0)
           goto fail;
    }
    for (i = 0; i < nelem; i++)
    {
        if (i2cRead(device->fd, offset + i, dlen, pdata) != 0)
            goto fail;
        pdata = (void*) (((size_t) pdata) + dlen);
    }
    epicsMutexUnlock(device->buslock);
    return 0;
fail:
    epicsMutexUnlock(device->buslock);
    return -1;
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
    epicsMutexLock(device->buslock);
    for (i = 0; i < device->nmux; i++)
    {
        if (i2cWrite(device->mux[i].fd, device->mux[i].cmd, 0, 0) != 0)
            goto fail;
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
            goto fail;
    }
    epicsMutexUnlock(device->buslock);
    return 0;
fail:
    epicsMutexUnlock(device->buslock);
    return -1;
}

struct regDevSupport i2cDevRegDev = {
    .report = i2cDevReport,
    .read = i2cDevRead,
    .write = i2cDevWrite,
};

static epicsMutexId fdlocks[256];

int i2cDevConfigure(const char* name, const char* path, unsigned int address, const char* muxes)
{
    struct stat statinfo;
    regDevice *device = NULL;
    int nmux = 0, n = 0;
    unsigned char muxid[255], muxcmd[255];
    
    if (!name || !name[0] || !path || !path[0])
    {
        printf("usage: i2cDevConfigure name path device [maxreg]\n");
        return -1;
    }
    if (muxes)
    {
        while (nmux < 255 && sscanf(muxes+=n, "%hhi =%hhi%n%*[, ]%n", &muxid[nmux], &muxcmd[nmux], &n, &n) >= 2)
            nmux++;
        if (*muxes != 0)
            fprintf(stderr, "rubish at end of line: %s\n", muxes);
    }
    
    device = calloc(1, sizeof(regDevice) + nmux * sizeof(struct mux));
    if (!device)
    {
        perror("malloc regDevice");
        return -1;
    }
    device->nmux = nmux;    
    device->addr = address;
    device->fd = i2cOpen(path, address);
    if (device->fd == -1) goto fail;
    
    for (n = 0; n < device->nmux; n++)
    {
        device->mux[n].addr = muxid[n];
        device->mux[n].cmd = muxcmd[n];
        device->mux[n].fd = i2cOpen(path, muxid[n]);
        if (device->mux[n].fd < 0) goto fail;
    }
    fstat(device->fd, &statinfo);
    device->busnum = minor(statinfo.st_rdev);
    if (!fdlocks[device->busnum])
    {
        fdlocks[device->busnum] = epicsMutexCreate();
        if (!fdlocks[device->busnum])
        {
            perror("epicsMutexCreate() failed");
            goto fail;
        }
    }
    device->buslock = fdlocks[device->busnum];
    if (regDevRegisterDevice(name, &i2cDevRegDev, device, 0) != SUCCESS)
    {
        perror("regDevRegisterDevice() failed");
        goto fail;
    }
    if (regDevInstallWorkQueue(device, 100) != SUCCESS)
    {
        perror("regDevInstallWorkQueue() failed");
        return -1;
    }
    return 0;
fail:
    if (device->fd > 0)
        close(device->fd);
    for (n = 0; n < device->nmux; n++)
    {
        if (device->mux[n].fd > 0)
            close(device->mux[n].fd);
    }
    free(device);
    return -1;
}

static const iocshFuncDef i2cDevConfigureDef =
    { "i2cDevConfigure", 4, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "bus", iocshArgString },
    &(iocshArg) { "device", iocshArgInt },
    &(iocshArg) { "muxdev=val ...", iocshArgArgv },
}};

static void i2cDevConfigureFunc(const iocshArgBuf *args)
{
    char* muxes = NULL;
    if (args[3].aval.ac > 1)
    {
        int i, l = 1;
        for (i = 1; i < args[3].aval.ac; i++)
            l += strlen(args[3].aval.av[i])+1;
        muxes = malloc(l);
        l = 0;
        for (i = 1; i < args[3].aval.ac; i++)
            l += sprintf(muxes + l, "%s ", args[3].aval.av[i]);
    }
    i2cDevConfigure(args[0].sval, args[1].sval, args[2].ival, muxes);
    free(muxes);
}

static void i2cRegistrar(void)
{
    iocshRegister(&i2cDevConfigureDef, i2cDevConfigureFunc);
}

epicsExportRegistrar(i2cRegistrar);
