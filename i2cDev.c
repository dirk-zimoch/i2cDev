#include <errno.h>
#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <errlog.h>
#include <iocsh.h>
#include <regDev.h>
#include <epicsExport.h>

#include "i2c.h"

/* I got this number from linux/driver/i2c/i2c-dev.c */
#define MAX_MSG_SIZE 8192

epicsExportAddress(int, i2cDebug);

struct mux
{
    int fd;
    uint8_t addr;
    uint8_t cmd;
};

struct regDevice
{
    uint8_t* buf;
    int fd;
    uint16_t bufsize;
    uint16_t addr;
    uint8_t busnum;
    uint8_t offslen;
    uint8_t nmux;
    uint8_t swap;
};

void i2cDevReport(regDevice *device, int level)
{
    printf("i2c-%u 0x%02x", device->busnum, device->addr);
    if (device->swap)
        printf(" swap");
    if (device->nmux)
    {
        struct i2c_msg* msg = (struct i2c_msg*)(device+1);
        int n;
        printf(" mux:");
        for (n = 0; n < device->nmux; n++)
            printf("%s0x%02x=0x%02x", n ? "," : "", msg[n].addr, msg[n].buf[0]);
    }
    printf("\n");
}

static void i2cDumpMsgs(const struct i2c_rdwr_ioctl_data* ioctl_data)
{
    int i, j, n = ioctl_data->nmsgs;
    struct i2c_msg* msg = ioctl_data->msgs;
    for (i = 0; i < n; i++)
    {
        printf("\t%s%0*x [%d]:",
            msg->flags & I2C_M_RD ? "R<-" : "W->",
            msg->flags & I2C_TENBIT ? 3 : 2,
            msg->addr,
            msg->len);
        for (j = 0; j < msg->len; j++)
            printf(" %02x", msg->buf[j]);
        printf("\n");
        msg ++;
    }
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
    struct i2c_msg* msg = (struct i2c_msg*)(device+1);
    struct i2c_rdwr_ioctl_data ioctl_data = {
        .msgs = msg,
        .nmsgs = device->nmux+2,
    };
    size_t remaining_bytes = nelem * dlen;
    int status = 0;
    uint8_t buf[4];
    uint8_t swap = device->swap;

    if (i2cDebug > 0)
        printf("i2cDevRead %s %zu * %u bytes\n", user, nelem, dlen);
    if (dlen == 0) return 0; /* any way to check online status ? */

    /* setup the read request */
    msg += device->nmux;
    msg->buf = buf;
    msg->len = device->offslen;

    /* setup the reply */
    msg++;
    msg->buf = pdata;

    msg->len = MAX_MSG_SIZE - MAX_MSG_SIZE % dlen;
    while (remaining_bytes)
    {
        if (remaining_bytes < MAX_MSG_SIZE)
            msg->len = remaining_bytes;

        buf[0] = offset & 0xff;
        buf[1] = (offset>>8) & 0xff;
        buf[2] = (offset>>16) & 0xff;
        buf[3] = (offset>>24) & 0xff;

        status = ioctl(device->fd, I2C_RDWR, &ioctl_data);
        if (i2cDebug > 0)
        {
            printf("i2c-%d", device->busnum);
            i2cDumpMsgs(&ioctl_data);
        }
        if (status < 0)
        {
            if (i2cDebug >= 0)
                errlogPrintf("i2cDevRead %s: ioctl(i2c-%d, I2C_RDWR, read %u bytes) failed: %m\n",
                    user, device->busnum, msg->len);
            return -status;
        }
        if (swap)
            regDevCopy(dlen, msg->len/dlen, msg->buf, msg->buf, NULL, DO_SWAP);
        offset += msg->len;
        msg->buf += msg->len;
        remaining_bytes -= msg->len;
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
    struct i2c_msg* msg = (struct i2c_msg*)(device+1);
    struct i2c_rdwr_ioctl_data ioctl_data = {
        .msgs = msg,
        .nmsgs = device->nmux+1,
    };
    size_t remaining_bytes = nelem * dlen;
    int status = 0;
    uint8_t* buf = device->buf;
    unsigned int offslen = device->offslen;

    if (i2cDebug > 0)
        printf("i2cDevWrite %s %zu * %u bytes\n", user, nelem, dlen);

    msg += device->nmux;
    if (remaining_bytes + offslen > MAX_MSG_SIZE)
        msg->len = MAX_MSG_SIZE - (MAX_MSG_SIZE - offslen) % dlen;
    else
        msg->len = remaining_bytes + offslen;
    if (msg->len > device->bufsize)
    {
        free (device->buf);
        buf = device->buf = malloc(msg->len);
        if (!buf)
        {
            device->bufsize = 0;
            errlogPrintf("i2cDevWrite %s: %m\n", user);
            return errno;
        }
        device->bufsize = msg->len;
    }

    while (remaining_bytes)
    {
        if (remaining_bytes + offslen < MAX_MSG_SIZE)
            msg->len = remaining_bytes + offslen;
        nelem = (msg->len - offslen)/dlen;

        buf[0] = offset & 0xff;
        buf[1] = (offset>>8) & 0xff;
        buf[2] = (offset>>16) & 0xff;
        buf[3] = (offset>>24) & 0xff;
        msg->buf = buf;

        if (pmask)
        {
            /* For masking we need to read old values first */
            (msg+1)->buf = buf + offslen;
            (msg+1)->len = msg->len - offslen;
            msg->len = offslen;
            ioctl_data.nmsgs++;
            status = ioctl(device->fd, I2C_RDWR, &ioctl_data);
            if (i2cDebug > 0)
            {
                printf("i2c-%d", device->busnum);
                i2cDumpMsgs(&ioctl_data);
            }
            if (status < 0)
            {
                if (i2cDebug >= 0)
                    errlogPrintf("i2cDevWrite %s: ioctl(i2c-%d, I2C_RDWR, read %u bytes) failed: %m\n",
                        user, device->busnum, msg[1].len);
                return -status;
            }
            ioctl_data.nmsgs--;
            msg->len = (msg+1)->len + offslen;
        }
        regDevCopy(dlen, nelem, pdata, buf + offslen, pmask, device->swap);
        if (i2cDebug > 0)
        {
            printf("i2c-%d", device->busnum);
            i2cDumpMsgs(&ioctl_data);
        }
        status = ioctl(device->fd, I2C_RDWR, &ioctl_data);
        if (status < 0)
        {
            if (i2cDebug >= 0)
                errlogPrintf("i2cDevWrite %s: ioctl(i2c-%d, I2C_RDWR, write %u bytes) failed: %m\n",
                    user, device->busnum, msg->len);
            return -status;
        }
        offset += msg->len - offslen;
        pdata += msg->len - offslen;
        remaining_bytes -= msg->len - offslen;
    }
    return 0;
}

struct regDevSupport i2cDevRegDev = {
    .report = i2cDevReport,
    .read = i2cDevRead,
    .write = i2cDevWrite,
};

static void i2cDevConfigureFunc(const iocshArgBuf *args)
{
    const char *name = args[0].sval;
    const char *path = args[1].sval;
    unsigned int addr = args[2].ival;

    struct stat statinfo;
    regDevice *device = NULL;
    uint8_t* muxcmd;
    struct i2c_msg *msg;
    int fd = -1;
    unsigned int i, n, size = 256, flags=0, swap=0, nmux;

    static const union {uint8_t b[2]; uint16_t u;} endianess = {.u = 0x1234};

    if (!name || !name[0] || !path || !path[0])
    {
        printf("usage: i2cDevConfigure name path device [size=integer] [swap] [muxaddr=muxval ...]\n");
        goto fail;
    }
    fd = i2cOpenBus(path);
    if (fd == -1)
    {
        errlogPrintf("i2cDevConfigure: i2cOpenBus(%s) failed: %m\n", path);
        goto fail;
    }

    /* optional arguments that are not multiplexers */
    nmux = args[3].aval.ac ? args[3].aval.ac -1 : 0;
    for (i = 1; i < args[3].aval.ac; i++)
    {
        if (strcmp(args[3].aval.av[i], "swap") == 0)
            swap = 1;
        else if (strcmp(args[3].aval.av[i], "le") == 0)
            swap = endianess.b[0] == 0x12;
        else if (strcmp(args[3].aval.av[i], "be") == 0)
            swap = endianess.b[0] == 0x34;
        else if (sscanf(args[3].aval.av[i], "size=%i", &size) == 1);
        else break;
        nmux --;
    }

    device = calloc(1, sizeof(regDevice) + (nmux + 2) * sizeof(struct i2c_msg) + nmux);
    if (!device)
    {
        errlogPrintf("i2cDevConfigure: out of memory");
        goto fail;
    }
    msg = (struct i2c_msg*)(device+1);
    muxcmd = (uint8_t*)(msg + nmux + 2);

    device->swap = swap;
    device->offslen = size > 0x10000 ? size > 0x1000000 ? 4 : 3 : size > 0x100 ? 2 : 1;
    device->addr = addr;
    if (addr > 0x3ff)
    {
        errlogPrintf("i2cDevConfigure: Invalid i2c address 0x%02x\n", addr);
        goto fail;
    }
    if (addr > 0x77) /* in 7-bit addressing only range from 0x03 to 0x77 is allowed */
        flags |= I2C_TENBIT;
    else if (addr < 0x03) /* in 7-bit addressing 0x00, 0x01, 0x02 are reserved */
        errlogPrintf("i2cDevConfigure: Warning: Reserved address 0x%02x used\n", addr);

    /* messages for multiplexers */
    for (n = 0; n < nmux; n++)
    {
        if (sscanf(args[3].aval.av[i+n], "%hi =%hhi", &msg->addr, &muxcmd[n]) != 2)
        {
            if (args[3].aval.av[i+n][0] == '#')
            {
                nmux = n;
                break;
            }
            errlogPrintf("i2cDevConfigure: unknown argument %s\n", args[3].aval.av[i+n]);
            goto fail;
        }
        msg->flags = 0;
        msg->len = 1;
        msg->buf = muxcmd+n;
        msg++;
    }
    if (nmux > I2C_RDRW_IOCTL_MAX_MSGS-2)
    {
        errlogPrintf("i2cDevConfigure: too many muxes\n");
        goto fail;
    }
    device->nmux = nmux;

    /* the read/write request */
    msg->addr = addr;
    msg->flags = flags;
    msg++;

    /* the read reply */
    msg->addr = addr;
    msg->flags = flags | I2C_M_RD;

    fstat(fd, &statinfo);
    device->fd = fd;
    device->busnum = minor(statinfo.st_rdev);
    if (regDevRegisterDevice(name, &i2cDevRegDev, device, size) != SUCCESS)
    {
        errlogPrintf("i2cDevConfigure: regDevRegisterDevice(%s) failed: %m\n", name);
        goto fail;
    }
    if (regDevInstallWorkQueue(device, 100) != SUCCESS)
    {
        errlogPrintf("i2cDevConfigure: regDevInstallWorkQueue(%s) failed: %m\n", name);
        goto fail;
    }
    return;
fail:
    if (fd > 0) close(fd);
    free(device);
#if defined(VERSION_INT)
#if EPICS_VERSION_INT >= VERSION_INT(7,0,3,1)
    iocshSetError(errno);
#endif
#endif
    return;
}

static const iocshFuncDef i2cDevConfigureDef =
    { "i2cDevConfigure", 4, (const iocshArg *[]) {
    &(iocshArg) { "name", iocshArgString },
    &(iocshArg) { "bus", iocshArgString },
    &(iocshArg) { "device", iocshArgInt },
    &(iocshArg) { "[mux=val ...]", iocshArgArgv },
}};

static void i2cRegistrar(void)
{
    iocshRegister(&i2cDevConfigureDef, i2cDevConfigureFunc);
}

epicsExportRegistrar(i2cRegistrar);

/* backward compatibility */
int i2cDevConfigure(const char* name, const char* path, unsigned int device, const char* muxes)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "i2cDevConfigure %s \"%s\" %#x %s", name, path, device, muxes?muxes:"");
    return iocshCmd(cmd);
}
