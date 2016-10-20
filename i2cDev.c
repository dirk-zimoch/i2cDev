#define _GNU_SOURCE
#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <epicsTypes.h>
#include <iocsh.h>
#include <regDev.h>
#include <epicsExport.h>

#include "i2cDev.h"

int i2cDebug;
epicsExportAddress(int, i2cDebug);

int i2cOpenBus(const char* path)
{
    glob_t globinfo;
    struct stat statinfo;
    char* p;
    int busnum;
    int fd;
    char filename[80];
    
    if (i2cDebug) printf("i2cOpenBus(%s)\n", path);
    if (!path || !path[0])
    {
        errno = EINVAL;
        return -1;
    }
    /* maybe path is a device file? */
    if (stat(path, &statinfo) == 0)
    {
        /* file exists */
        if (S_ISCHR(statinfo.st_mode))
        {
            if (i2cDebug) printf("i2cOpenBus: %s device major number is %d\n", path, major(statinfo.st_rdev));
            if (major(statinfo.st_rdev) != 89)
            {
                if (i2cDebug) printf("i2cOpenBus: %s is not an i2c device\n", path);
                errno = EINVAL;
                return -1;
            }
        }
        fd = open(path, O_RDWR);
        if (i2cDebug) printf("i2cOpenBus: open %s returned %d\n", path, fd);
        if (fd >= 0) return fd;
    }

    /* maybe path is a number? */
    busnum = strtol(path, &p, 10);
    if (*p == 0)
    {
        if (i2cDebug) printf("i2cOpenBus: %d is bus number\n", busnum);
    }
    if (*p != 0)
    {
        /* maybe path is a sysfs path? */
        int status = glob(path, GLOB_BRACE|GLOB_TILDE, NULL, &globinfo);
        if (status == GLOB_NOMATCH)
        {
            if (i2cDebug) printf("i2cOpenBus: %s is no valid glob pattern\n", path);
            errno = ENOENT;
            return -1;
        }
        if (status != 0)
            return -1;
        if (i2cDebug) printf("i2cOpenBus: glob found %s\n", globinfo.gl_pathv[0]);
        p = strstr(globinfo.gl_pathv[0], "/i2c-");
        if (!p)
        {
            if (i2cDebug) printf("i2cOpenBus: no /i2c- found in %s\n", globinfo.gl_pathv[0]);
            return -1;
        }
        p+=5;
        if (i2cDebug) printf("i2cOpenBus: look up number in '%s'\n", p);
        busnum = strtol(p, NULL, 10);
        globfree(&globinfo);
        if (i2cDebug) printf("i2cOpenBus: bus number is %d\n", busnum);
    }
    sprintf(filename, "/dev/i2c-%d", busnum);
    fd = open(filename, O_RDWR);
    if (i2cDebug) printf("i2cOpenBus: open %s returned %d\n", filename, fd);
    if (fd >= 0) return fd;
    sprintf(filename, "/dev/i2c/%d", busnum);
    fd = open(filename, O_RDWR);
    if (i2cDebug) printf("i2cOpenBus: open %s returned %d\n", filename, fd);
    return fd;
}

int i2cOpenFmt(unsigned int address, const char* pathformat, ...)
{
    int fd;
    va_list ap;
    
    va_start(ap, pathformat);
    fd = i2cOpenVar(address, pathformat, ap);
    va_end(ap);
    return fd;
}

int i2cOpenVar(unsigned int address, const char* pathformat, va_list ap)
{
    char *path;
    int fd;
    
    vasprintf(&path, pathformat, ap);
    fd = i2cOpen(address, path);
    free(path);
    return fd;
}

int i2cOpen(unsigned int address, const char* path)
{
    int fd;

    if (i2cDebug) fprintf(stderr,
        "i2cOpen(%s,0x%x)\n", path, address);
    fd = i2cOpenBus(path);
    if (fd == -1) return -1;
    if (address > 0x3f)
    {
        if (ioctl(fd, I2C_TENBIT, 1))
        {
            if (i2cDebug) fprintf(stderr,
                "i2cOpen(%s,0x%x): ioctl I2C_TENBIT failed\n",
                path, address);
            close(fd);
            return -1;
        }
    }   
    if (ioctl(fd, I2C_SLAVE_FORCE, address) < 0)
    {
        if (i2cDebug) fprintf(stderr,
            "i2cOpen(%s,0x%x): ioctl I2C_SLAVE_FORCE failed\n",
            path, address);
        close(fd);
        return -1;
    }
    return fd;
}

static char* fdname(int fd, char* buffer, int size)
{
    char procname[32];
    sprintf(procname, "/proc/self/fd/%d", fd);
    size = readlink(procname, buffer, size-1);
    if (size < 0) return NULL;
    buffer[size] = 0;
    return buffer;
}

int i2cRead(int fd, unsigned int command, unsigned int dlen, void* value)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;
    
    if (i2cDebug) fprintf(stderr,
        "i2cRead(fd=%d,command=0x%x,den=%u)\n",
        fd, command, dlen);
    args.read_write = I2C_SMBUS_READ;
    args.size = 1 + dlen;
    args.data = &data;
    args.command = command;
    if (ioctl(fd, I2C_SMBUS, &args) < 0)
    {
        if (i2cDebug)
        {
            char filename[32];
            fprintf(stderr, "i2cRead: ioctl(fd=%d=%s, I2C_SMBUS, {I2C_SMBUS_READ, size=%u, command=0x%x}) failed: %m\n",
                fd, fdname(fd, filename, sizeof(filename)), args.size, args.command);
        }
        return -1;
    }
    if (dlen == 1)
        *((epicsUInt8*) value) = data.byte;
    else 
        *((epicsUInt16*) value) = data.word;
    return 0;
}

int i2cWrite(int fd, unsigned int command, unsigned int dlen, int value)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;

    if (i2cDebug) fprintf(stderr,
        "i2cWrite(fd=%d,command=0x%x,den=%u,value=0x%x)\n",
        fd, command, dlen, value);
    args.read_write = I2C_SMBUS_WRITE;
    args.size = 1 + dlen;
    args.data = &data;
    args.command = command;
    if (dlen == 1) data.byte = value;
    if (dlen == 2) data.word = value;
    if (ioctl(fd, I2C_SMBUS, &args) < 0)
    {
        if (i2cDebug)
        {
            char filename[32];
            fprintf(stderr, "i2cRead: ioctl(fd=%d=%s, I2C_SMBUS, {I2C_SMBUS_WRITE, size=%u, command=0x%x}) failed: %m\n",
                fd, fdname(fd, filename, sizeof(filename)), args.size, args.command);
        }
        return -1;
    }
    return 0;
}

struct regDevice
{
    int fd;
    unsigned int addr;
};

void i2cDevReport(regDevice *device, int level)
{
    char filename[32];
    printf("i2c %s 0x%02x\n", fdname(device->fd, filename, sizeof(filename)), device->addr);
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
        if (i2cDebug) fprintf(stderr, "%s %s: only 1 or 2 bytes supported\n", user, regDevName(device));
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
        if (i2cDebug) fprintf(stderr, "%s %s: only 1 or 2 bytes supported\n", user, regDevName(device));
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
        printf("usage: i2cDevConfigure name path address [maxreg]\n");
        return -1;
    }
    fd = i2cOpen(address, path);
    device = malloc(sizeof(regDevice));
    if (!device)
    {
        perror("malloc regDevice");
        goto fail;
    }
    device->fd = fd;
    device->addr = address;
    if (regDevRegisterDevice(name, &i2cDevRegDev, device, maxreg ? maxreg + 1 : 0) != SUCCESS)
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
    close(fd);
    free(device);
    return -1;
}

static const iocshFuncDef i2cDevConfigureDef =
    { "i2cDevConfigure", 4, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "i2cDev_bus", iocshArgString },
    &(iocshArg) { "address", iocshArgInt },
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
