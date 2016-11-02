#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "i2c.h"
#include "i2cDev.h"

int i2cDebug;

int i2cOpenBus(const char* path)
{
    glob_t globinfo;
    struct stat statinfo;
    char* p;
    int busnum;
    int fd;
    char filename[32];
    
    if (i2cDebug > 0) printf("i2cOpenBus(%s)\n", path);
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
            if (i2cDebug > 0) printf("i2cOpenBus: %s device major number is %d\n", path, major(statinfo.st_rdev));
            if (major(statinfo.st_rdev) != 89)
            {
                if (i2cDebug >= 0) printf("i2cOpenBus: %s is not an i2c device\n", path);
                errno = EINVAL;
                return -1;
            }
        }
        fd = open(path, O_RDWR);
        if (i2cDebug > 0) printf("i2cOpenBus: open %s returned %d\n", path, fd);
        if (fd >= 0) return fd;
    }

    /* maybe path is a number? */
    busnum = strtol(path, &p, 10);
    if (*p == 0)
    {
        if (i2cDebug > 0) printf("i2cOpenBus: %d is bus number\n", busnum);
    }
    if (*p != 0)
    {
        /* maybe path is a sysfs path? */
        int status = glob(path, 0, NULL, &globinfo);
        if (status == GLOB_NOMATCH)
        {
            if (i2cDebug >= 0) printf("i2cOpenBus: %s is no valid glob pattern\n", path);
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
    if (fd >= 0) return fd;
    sprintf(filename, "/dev/i2c/%d", busnum);
    fd = open(filename, O_RDWR);
    if (i2cDebug > 0) printf("i2cOpenBus: open %s returned %d\n", filename, fd);
    return fd;
}

int i2cOpen(const char* path, unsigned int address)
{
    int fd;

    if (i2cDebug > 0) fprintf(stderr,
        "i2cOpen(%s,0x%x)\n", path, address);
    fd = i2cOpenBus(path);
    if (fd == -1) return -1;
    if (address > 0x3f)
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
    if (ioctl(fd, I2C_SLAVE_FORCE, address) < 0)
    {
        if (i2cDebug >= 0) fprintf(stderr,
            "i2cOpen(%s,0x%x): ioctl I2C_SLAVE_FORCE failed\n",
            path, address);
        close(fd);
        return -1;
    }
    if (ioctl(fd, I2C_TIMEOUT, 100) < 0)
    {
        if (i2cDebug >= 0) fprintf(stderr,
            "i2cOpen(%s,0x%x): ioctl I2C_TIMEOUT failed\n",
            path, address);
    }
    return fd;
}

int i2cRead(int fd, unsigned int command, unsigned int dlen, void* value)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;
    
    if (i2cDebug > 0) fprintf(stderr,
        "i2cRead(fd=%d,command=0x%x,den=%u)\n",
        fd, command, dlen);
    args.read_write = I2C_SMBUS_READ;
    args.size = 1 + dlen;
    args.data = &data;
    args.command = command;
    if (ioctl(fd, I2C_SMBUS, &args) < 0)
    {
        if (i2cDebug >= 0) fprintf(stderr,
            "i2cRead: ioctl(fd=%d, I2C_SMBUS, {I2C_SMBUS_READ, size=%u, command=0x%x}) failed: %m\n",
            fd, args.size, args.command);
        return -1;
    }
    if (dlen == 1)
    {
        if (i2cDebug > 0) fprintf(stderr,
            "i2cRead: (fd=%d,command=0x%x,den=%u) got byte 0x%02x\n",
            fd, command, dlen, data.byte);
        *((uint8_t*) value) = data.byte;
    }
    else
    {
        if (i2cDebug > 0) fprintf(stderr,
            "i2cRead: (fd=%d,command=0x%x,den=%u) got word 0x%04x\n",
            fd, command, dlen, data.word);
        *((uint16_t*) value) = data.word;
    }
    return 0;
}

int i2cWrite(int fd, unsigned int command, unsigned int dlen, int value)
{
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data data;

    if (i2cDebug > 0) fprintf(stderr,
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
        if (i2cDebug >= 0) fprintf(stderr,
            "i2cRead: ioctl(fd=%d, I2C_SMBUS, {I2C_SMBUS_WRITE, size=%u, command=0x%x}) failed: %m\n",
            fd, args.size, args.command);
        return -1;
    }
    return 0;
}
