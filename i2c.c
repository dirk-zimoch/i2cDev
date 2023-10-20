#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <limits.h>
#include <assert.h>

#include <regDev.h>
#include "i2c.h"

int i2cDebug = -1;

struct devinfo_t { short bus; short dev; } *devinfo;

static int i2cMajor = 0;

int i2cOpenBus(const char* path)
{
    glob_t globinfo;
    struct stat statinfo;
    char* p;
    int busnum;
    int fd;
    char filename[32];

    if (!devinfo)
        devinfo = (struct devinfo_t*)calloc(sysconf(_SC_OPEN_MAX)+1, sizeof(struct devinfo_t))+1; /* allow index -1 */

    if (i2cDebug > 0) printf("i2cOpenBus(%s)\n", path);
    if (!path || !path[0])
    {
        errno = EINVAL;
        return -1;
    }
    /* maybe path is a device file? */
    if (stat(path, &statinfo) == 0 && S_ISCHR(statinfo.st_mode))
    {
        if (!i2cMajor)
        {
            FILE* file = fopen("/proc/devices", "r");

            if (!file) {
                /* cannot read /proc/devices, thus cannot check major device number */
                if (i2cDebug > 0)
                    printf("i2cOpenBus: error opening /proc/devices for i2c device number: %m\n");
                i2cMajor = -1;
            }
            else
            {
                char linebuffer[32];
                char* dev_name;
                long number;
                while (fgets(linebuffer, sizeof(linebuffer), file))
                {
                    number = strtol(linebuffer, &dev_name, 10);
                    while (isblank((int)*dev_name)) dev_name++;
                    if (dev_name && strcmp("i2c\n", dev_name) == 0)
                    {
                        i2cMajor = number;
                        if (i2cDebug > 0)
                            printf("i2cOpenBus: found i2c major device number: %d\n", i2cMajor);
                        break;
                    }
                }
                fclose(file);
            }
            if (!i2cMajor)
            {
                if (i2cDebug >= 0)
                    printf("i2cOpenBus() failed: We don't seem to have i2c devices\n");
                errno = EINVAL;
                return -1;
            }
        }
        if (i2cDebug > 0) printf("i2cOpenBus: %s device major number is %d\n", path, major(statinfo.st_rdev));
        if (i2cMajor > 0 && major(statinfo.st_rdev) != i2cMajor)
        {
            if (i2cDebug >= 0) printf("i2cOpenBus: %s is not an i2c controller\n", path);
            errno = EINVAL;
            return -1;
        }
        busnum = minor(statinfo.st_rdev);
        fd = open(path, O_RDWR);
        if (i2cDebug > 0) printf("i2cOpenBus: open %s returned %d\n", path, fd);
    }
    else
    {
        /* maybe path is a number? */
        busnum = strtol(path, &p, 10);
        if (*p == 0)
        {
            if (i2cDebug > 0) printf("i2cOpenBus: %d is bus number\n", busnum);
        }
        else
        {
            /* maybe path is a sysfs path? */
            int status = glob(path, GLOB_BRACE, NULL, &globinfo);
            if (status == GLOB_NOMATCH)
            {
                if (i2cDebug >= 0) printf("i2cOpenBus: %s does not match anything\n", path);
                errno = ENOENT;
                return -1;
            }
            if (status != 0)
                return -1;
            if (i2cDebug > 0) printf("i2cOpenBus: glob found %s\n", globinfo.gl_pathv[0]);
            p = strstr(globinfo.gl_pathv[0], "/i2c-");
            if (!p)
            {
                if (i2cDebug >= 0) printf("i2cOpenBus: no /i2c- found in %s\n", globinfo.gl_pathv[0]);
                return -1;
            }
            p+=5;
            if (i2cDebug > 0) printf("i2cOpenBus: look up number in '%s'\n", p);
            busnum = strtol(p, NULL, 10);
            globfree(&globinfo);
            if (i2cDebug > 0) printf("i2cOpenBus: bus number is %d\n", busnum);
        }
        sprintf(filename, "/dev/i2c-%d", busnum);
        fd = open(filename, O_RDWR);
        if (i2cDebug > 0) printf("i2cOpenBus: open %s returned %d\n", filename, fd);
        if (fd < 0)
        {
            sprintf(filename, "/dev/i2c/%d", busnum);
            fd = open(filename, O_RDWR);
            if (i2cDebug > 0) printf("i2cOpenBus: open %s returned %d\n", filename, fd);
        }
    }
    if (fd >= 0) {
        if (ioctl(fd, I2C_TIMEOUT, 100) < 0)
        {
            if (i2cDebug >= 0) fprintf(stderr,
                "i2cOpenBus(%s): ioctl I2C_TIMEOUT failed\n",
                path);
        }
        devinfo[fd].bus = busnum;
    }
    return fd;
}

int i2cOpen(const char* path, unsigned int address)
{
    int fd;

    if (i2cDebug > 0) fprintf(stderr,
        "i2cOpen(%s,0x%x)\n", path, address);
    fd = i2cOpenBus(path);
    if (fd < 0) return fd;
    if (address > 0x77) /* in 7-bit addressing only range from 0x03 to 0x77 is allowed */
    {
        if (ioctl(fd, I2C_TENBIT, 1))
        {
            if (i2cDebug >= 0) fprintf(stderr,
                "i2cOpen(%s,0x%x): ioctl I2C_TENBIT failed\n",
                path, address);
            close(fd);
            return -1;
        }
    }
    else
    {
        if (address < 0x03) /* in 7-bit addressing 0x00, 0x01, 0x02 are reserved */
        {
            if (i2cDebug >= 0) fprintf(stderr,
                "i2cOpen(%s,0x%x): Reserved address used\n", path, address);
            close(fd);
            return -1;
        }
    }
    if (ioctl(fd, I2C_SLAVE_FORCE, address) < 0)  /* I2C_SLAVE_FORCE: use address even if a driver binds to it */
    {
        if (i2cDebug >= 0) fprintf(stderr,
            "i2cOpen(%s,0x%x): ioctl I2C_SLAVE_FORCE failed\n",
            path, address);
        close(fd);
        return -1;
    }
    devinfo[fd].dev = address;
    return fd;
}

/* Keep i2cRead and i2cWrite only for backward compatibility (i2Dev no longer uses it) */

int i2cRead(int fd, unsigned int offset, unsigned int dlen, void* value)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data = {0};

    if (dlen < 1 || dlen > 32) {
        if (i2cDebug >= 0) fprintf(stderr,
            "i2cRead: invalid dlen %d (must be 1..32)\n", dlen);
        return -1;
    }

    args.read_write = I2C_SMBUS_READ;
    args.size = I2C_SMBUS_I2C_BLOCK_DATA;
    args.data = &data;
    args.command = offset;
    data.block[0] = dlen;

    if (ioctl(fd, I2C_SMBUS, &args) < 0)
    {
        if (i2cDebug >= 0) fprintf(stderr,
            "i2cRead: ioctl(fd=%d (%d-0x%02x), I2C_SMBUS, {I2C_SMBUS_READ}) failed: %m\n",
            fd, devinfo[fd].bus, devinfo[fd].dev);
        return -1;
    }
    if (i2cDebug > 0)
    {
        fprintf(stderr,
            "i2cRead(fd=%d (%d-0x%02x), command=0x%x, dlen=%u bytes",
            fd, devinfo[fd].bus, devinfo[fd].dev, args.command, dlen);
        for (dlen = 1; dlen <= data.block[0]; dlen++) fprintf(stderr," 0x%02x", data.block[dlen]);
        fprintf(stderr,")\n");
    }
    regDevCopy(dlen, 1, data.block+1, value, NULL, REGDEV_SWAP_FROM_LE);
    return 0;
}

int i2cWrite(int fd, unsigned int offset, unsigned int dlen, int value)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;

    if (dlen < 1 || dlen > 4) {
        if (i2cDebug >= 0) fprintf(stderr,
            "i2cWrite: invalid dlen %d (must be 1..4)\n", dlen);
        return -1;
    }

    args.read_write = I2C_SMBUS_WRITE;
    args.size = I2C_SMBUS_I2C_BLOCK_DATA;
    args.data = &data;
    args.command = offset;
    data.block[0] = dlen;
    data.block[1] = value & 0xff;
    data.block[2] = (value >> 8) & 0xff;
    data.block[3] = (value >> 16) & 0xff;
    data.block[4] = (value >> 24) & 0xff;

    if (i2cDebug > 0)
    {
        fprintf(stderr,
            "i2cWrite(fd=%d (%d-0x%02x), command=0x%x, dlen=%u bytes",
            fd, devinfo[fd].bus, devinfo[fd].dev, args.command, dlen);
        for (dlen = 1; dlen <= data.block[0]; dlen++) fprintf(stderr," 0x%02x", data.block[dlen]);
        fprintf(stderr,")\n");
    }
    if (ioctl(fd, I2C_SMBUS, &args) < 0)
    {
        if (i2cDebug >= 0) fprintf(stderr,
            "i2cWrite: ioctl(fd=%d (%d-0x%02x), I2C_SMBUS, {I2C_SMBUS_WRITE}) failed: %m\n",
            fd, devinfo[fd].bus, devinfo[fd].dev);
        return -1;
    }
    return 0;
}
