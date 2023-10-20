EPICS i2c Driver
================

This is a driver for regDev
([github](https://github.com/paulscherrerinstitute/regdev),
[PSI Internal](https://git.psi.ch/epics_driver_modules/regDev)).
It allows to read and write data from i2c devices, currently only on Linux.

The driver supports i2c devices with 7 bit or 10 bit addresses on busses
that are available to the operating system as `/dev/i2c-*` devices or in the
sys file system. It allows the use of i2c bus multiplexers. Up to 40
multiplexers may be chained.

A "device" from the point of view of this driver consists of an i2c bus
(controller), a device address, and an optional chain of multiplexers.

Before each access to the device, the multiplexers (if any) are programmed
accordingly. Then data is written to the device and finally data may be read
back from the device (for read access only). The i2c bus is kept locked (using
the repeated start method) during the whole process, so that the communication
is safe from interruption by other accesses. This driver uses the `I2C_RDWR`
kernel API.


## Device setup

```
i2cDevConfigure name bus device [size=...] [swap] [mux=cmd] [mux=cmd] ...
```

`name` is used by records to refer to the device. It must be unique among all
regDev device on the IOC.

`bus` refers to the i2c bus (i.e. the controller). It may be in one of three
forms:
 - a device path like `/dev/i2c-0`
 - an integer number `n`, which is short for `/dev/i2c-n`
 - a sysfs glob pattern
 
A sysfs pattern can help to unambigously identify a device while the device
numbers `n` assigned by the kernel may change, e.g. between reboots.
An example for an i2c device on IOxOS IFC boards would be:
`/sys/devices/{,*/}*localbus/*000a0.pon-i2c/i2c-*`

`device` is the i2c address of the device (integer between 3 and 0x3ff).
A device address above 0x77 assumes 10-bit address mode.

`size` defines the size of the device address space in bytes, up to 0x100000000.
(That is the addresses/offset of data inside the device, not the device's i2c
address.) It is used to calculate the number of address offset bytes:
A size of up to 0x100, 0x10000, 0x1000000, or 0x10000000 bytes results in 1, 2,
3, or 4 offset bytes. The default is 0x100, i.e. one address offset byte.

`swap` is one of the following strings
 - `le` The i2c data is in little endian order. Swap on big endian hosts.
 - `be` The i2c data is in big endian order. Swap on litle endian hosts.
 - `swap` Unconditially swap the i2c data.
The default is not to swap.

`muxes` defines the multiplexer chain as a list of `mux=cmd` pairs where `mux`
is the (7 bit) i2c address of the multiplexer and `cmd` is the value of the
command byte sent to the multiplexer for programming it to reach the correct
i2c bus branch. The multiplexer chain must be last in the argument list.

## Record setup

Any record type supported by regdev is available, including string and array
recors and records that use bit masks.

Use `field(DTYP, "regDev")`.

The `INP`/`OUT` link has the form `"@name:offs T=type [options]"` where `name`
is the device name configured above and `offs` is the address/offset withing the
device in bytes.

For more details see the regDev documentation.


## Assumptions and limitations

As i2c defines little in the way of protocols, some assumptions have to be made.

Multiplexers are assumed to use 7 bit address and to be switched with a one
byte message.

This driver assumes that the device provides addressable memory which can be
read or written sequentially.

To write data, the driver sends up to 4 bytes address offset (that is memory
address within the device, not the device's i2c address) in little endian byte
order followed by an amount of data that depending on the connected record.

To read, the driver first sends up to 4 bytes address offset and then reads an
amount of bytes that depending on the connected record.

### Endianess

Multi-byte data may be in little endian or big endian byte order which may
differ from the host byte order. Therefore, multi-byte data is optionally
swapped, element-wise in the case of arrays. Strings as arrays of single bytes
are never swapped.

### Long arrays

Whenever possible, an array is transferred in one message. But a single i2c
message is limited to 8192 bytes by the Linux kernel driver. If an array is too
long for a single message, the driver will use multiple messages and increases
the address offset accordingly. It is assumed that the device can handle this
case transparently.

As the bus is unlocked in between these partial transfers, it is
possible that another messages alters the device state in between parts.
Other records using the same regDev device will not be able to interrupt but
record using different regDev devices which are mapped to the same i2c device
may. As well may an other i2c master in a multi-master setup.

### Masked writing

If a bit mask is used when writing data, the driver will first read the current
data from the device, then modify the masked bits and finally write the data
back. In case of arrays, the mask is applied element-wise and data transfer may
be broken down into several parts if the array is long.

The bus is unlocked between reading the old data and writing back the
modified data. Thus it is possible that some other messages alter the device
state in between, as describes above for long arrays.


## C API

This driver exports the following C functions:

```
int i2cOpenBus(const char* path);
int i2cOpen(const char* path, unsigned int address);
int i2cRead(int fd, unsigned int command, unsigned int dlen, void* value);
int i2cWrite(int fd, unsigned int command, unsigned int dlen, int value);
int i2cDevConfigure(const char* name, const char* path, unsigned int device, const char* muxes);
```

`i2cOpenBus` takes a device file, a numeric string or a sysfs pattern.
If the specified i2c bus/controller exists and is accessible, it returns a file
descriptor to be used for further oprations. It sets an i2c timeout of 100 ms.

`i2cOpen` calls `i2cOpenBus` and then binds a device addess to the file
descriptor. If the address is above 0x77, then 10-bit address mode is used.
This function is no longer used by the regDev driver and is only maintained for
backward compatibility.

`i2cRead` reads `dlen` (up to 32) bytes from a device opened with `i2cOpen`.
As this funtion uses the SMBus `I2C_SMBUS_I2C_BLOCK_DATA` kernel API, `offset`
is limited to one byte (the "command" in SMBus terminology). The byte order is
little endian. 
On big endian hosts, the `dlen` number of bytes will be swapped. Arrays are not
supported. This function is no longer used by the regDev driver and is only
maintained for backward compatibility. Earlier versions of this driver had used
the `I2C_SMBUS_I2C_BYTE_DATA` and `I2C_SMBUS_I2C_WORD_DATA` APIs and have thus
been limited to maximal two bytes.

`i2cWrite` writes `dlen` (up to 4) bytes to a device opened with `i2cOpen`.
The same limitations as for `i2cRead` apply. `dlen` is limited to 4 because the
value is an `int`, not a pointer. As with `i2cRead`, this funtion is no longer
used by the regDev driver and is only maintained for backward compatibility.

`i2cDevConfigure` allows to configure an i2c regDev device from a C program
instead of the ioc shell. It too is only maintained for backward compatibility.
As it calls the iocsh version, any optional parameters can now be passed in the
`muxes` string, incluing size and swap.

## License

This driver is published under GPL 3.0 or higher.

----

2016-2023 dirk.zimoch@psi.ch
