// ledctl.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

void print_i2c_smbus_data(const union i2c_smbus_data *data) {
    printf("i2c_smbus_data:\n");
    if (!data) {
        printf("i2c_smbus_data: (null pointer)\n");
        return;
    }

    /* Print as single byte */
    printf("byte: 0x%02x (%u)\n", data->byte, data->byte);

    /* Print as 16-bit word */
    printf("word: 0x%04x (%u)\n", data->word, data->word);

    /* Print block contents */
    __u8 len = data->block[0];
    if (len > I2C_SMBUS_BLOCK_MAX) {
        len = I2C_SMBUS_BLOCK_MAX; /* clamp to avoid overflow */
    }
    printf("block length: %u\n", len);
    printf("block data: ");
    for (__u8 i = 1; i <= len; i++) {
        printf("0x%02x ", data->block[i]);
    }
    printf("\n");
}

void print_i2c_smbus_ioctl_data(const struct i2c_smbus_ioctl_data *ioctl_data) {
    printf("i2c_smbus_ioctl_data:\n");
    if (!ioctl_data) {
        printf("i2c_smbus_ioctl_data: (null pointer)\n");
        return;
    }

    /* Read/write direction */
    const char *rw = (ioctl_data->read_write == I2C_SMBUS_READ) ? "READ" :
                     (ioctl_data->read_write == I2C_SMBUS_WRITE) ? "WRITE" : "UNKNOWN";
    printf("read_write: %s (%u)\n", rw, ioctl_data->read_write);

    /* Command byte */
    printf("command: 0x%02x (%u)\n", ioctl_data->command, ioctl_data->command);

    /* Size / transaction type */
    printf("size (transaction type): ");
    switch (ioctl_data->size) {
        case I2C_SMBUS_QUICK:       printf("I2C_SMBUS_QUICK\n"); break;
        case I2C_SMBUS_BYTE:        printf("I2C_SMBUS_BYTE\n"); break;
        case I2C_SMBUS_BYTE_DATA:   printf("I2C_SMBUS_BYTE_DATA\n"); break;
        case I2C_SMBUS_WORD_DATA:   printf("I2C_SMBUS_WORD_DATA\n"); break;
        case I2C_SMBUS_PROC_CALL:   printf("I2C_SMBUS_PROC_CALL\n"); break;
        case I2C_SMBUS_BLOCK_DATA:  printf("I2C_SMBUS_BLOCK_DATA\n"); break;
        case I2C_SMBUS_I2C_BLOCK_DATA: printf("I2C_SMBUS_I2C_BLOCK_DATA\n"); break;
        default: printf("UNKNOWN (%u)\n", ioctl_data->size); break;
    }

    /* Data payload */
    if (ioctl_data->data) {
        printf("data:\n");
        print_i2c_smbus_data(ioctl_data->data);
    } else {
        printf("data: (null)\n");
    }
    printf("===============================\n");
}

static int set_addr(int fd, int addr) {
    // Debug: Log parameters
    printf("DEBUG: set_addr() called with fd=%d, addr=0x%02X\n", fd, addr);
    
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        return -1;
    }
    return 0;
}

static int write_byte( int fd, int addr, unsigned char data ) {
    // Debug: Log parameters
    printf("DEBUG: write_byte() called with fd=%d, addr=0x%02X, data=0x%02X\n", fd, addr, data);
    
    if (set_addr(fd, addr) < 0) {
        return -1;
    }
    // Prepare SMBus ioctl payload
    union i2c_smbus_data smbus_data;
    smbus_data.byte = data;
    struct i2c_smbus_ioctl_data args = {
        .read_write = I2C_SMBUS_WRITE,
        .command = 0x01,
        .size = I2C_SMBUS_BYTE_DATA,
        .data = &smbus_data,
    };

    print_i2c_smbus_ioctl_data(&args);


    if (ioctl(fd, I2C_SMBUS, &args) < 0) {
        return -1;
    }
    return 0;
}

static int write_word( int fd, int addr, unsigned int data ) {
    // Debug: Log parameters
    printf("DEBUG: write_word() called with fd=%d, addr=0x%02X, data=0x%04X\n", fd, addr, data);
    
    if (set_addr(fd, addr) < 0) {
        return -1;
    }
    // Prepare SMBus ioctl payload
    union i2c_smbus_data smbus_data;
    smbus_data.word = data;
    struct i2c_smbus_ioctl_data args = {
        .read_write = I2C_SMBUS_WRITE,
        .command = 0,
        .size = I2C_SMBUS_WORD_DATA,
        .data = &smbus_data,
    };

    print_i2c_smbus_ioctl_data(&args);

    if (ioctl(fd, I2C_SMBUS, &args) < 0) {
        return -1;
    }
    return 0;
}

static int smbus_quick_probe(int fd, int rw) {
    // rw: I2C_SMBUS_WRITE (0) or I2C_SMBUS_READ (1)
    struct i2c_smbus_ioctl_data args = {
        .read_write = rw,
        .command = 0,
        .size = I2C_SMBUS_QUICK,
        .data = NULL,
    };
    if (ioctl(fd, I2C_SMBUS, &args) < 0) {
        return -1;
    }
    return 0;
}

static int smbus_receive_byte_probe(int fd, unsigned char *val_out) {
    union i2c_smbus_data data;
    struct i2c_smbus_ioctl_data args = {
        .read_write = I2C_SMBUS_READ,
        .command = 0,
        .size = I2C_SMBUS_BYTE,
        .data = &data,
    };
    if (ioctl(fd, I2C_SMBUS, &args) < 0) {
        return -1;
    }
    if (val_out) *val_out = data.byte;
    return 0;
}

static int has_device_at_addr(int fd, unsigned long funcs, const unsigned char addr) {
    if (set_addr(fd, addr) < 0)
        return 0; // cannot talk to this address on this adapter

    // Prefer QUICK if available (least intrusive)
    if (funcs & I2C_FUNC_SMBUS_QUICK) {
        if (smbus_quick_probe(fd, I2C_SMBUS_WRITE) == 0)
            return 1;

        // Some devices respond only to read quick
        if (smbus_quick_probe(fd, I2C_SMBUS_READ) == 0)  
            return 1;

        return 0;
    }

    // Fallback to SMBUS RECEIVE BYTE if supported
    if (funcs & I2C_FUNC_SMBUS_BYTE) {
        unsigned char dummy = 0;
        if (smbus_receive_byte_probe(fd, &dummy) == 0) return 1;
        return 0;
    }

    // If neither QUICK nor BYTE is supported, we don’t attempt unsafe probes.
    return 0;
}

// Perform an SMBus I2C block write of 3 bytes (R,B,G).
static int write_led_colour(int fd, unsigned char bank_addr, unsigned int led_addr,
                                  unsigned char r, unsigned char b, unsigned char g)
{
    if( write_word(fd, bank_addr, led_addr) < 0) {
        perror("write_word(led_addr)");
        return -1;
    }

    if (set_addr(fd, bank_addr) < 0) {
        return -1;
    }

    // Prepare SMBus ioctl payload (no command byte semantics per your spec).
    union i2c_smbus_data data;
    // Per kernel ABI, block[0] holds length, block[1..n] data
    data.block[0] = 3;
    data.block[1] = r;
    data.block[2] = b; // Note: RBG order as specified
    data.block[3] = g;

    struct i2c_smbus_ioctl_data args = {
        .read_write = I2C_SMBUS_WRITE,
        .command    = 0x03,
        .size       = I2C_SMBUS_BLOCK_DATA,
        .data       = &data,
    };

    if (ioctl(fd, I2C_SMBUS, &args) < 0) {
        perror("ioctl(I2C_SMBUS, I2C_SMBUS_BLOCK_DATA)");
    }

    return 0;
}

// Map colour string to R,B,G bytes (RBG order).
static int colour_from_name(const char *name, unsigned char *r, unsigned char *b, unsigned char *g) {
    if (!name) return -1;
    if (strcasecmp(name, "red") == 0)   { *r = 0xFF; *b = 0x00; *g = 0x00; return 0; }
    if (strcasecmp(name, "blue") == 0)  { *r = 0x00; *b = 0xFF; *g = 0x00; return 0; }
    if (strcasecmp(name, "black") == 0) { *r = 0x00; *b = 0x00; *g = 0x00; return 0; }
    return -1;
}

// Compute LED address: base 0x0081 plus n * 0x0300, n in [0..7].
static unsigned int led_addr_for_index(int n) {
    return 0x0081u + (unsigned int)n * 0x0300u;
}

// On hardware reboot the LEDs are in "breathing mode". This mode is controlled by the hardware 
// so we need to set the leds into direct control mode in order to control the colours manually.
int set_led_direct_control(int fd, unsigned char bank_addr, unsigned int control_code) {
    // Debug: Log all parameters
    printf("DEBUG: set_led_direct_control() called with fd=%d, bank_addr=0x%02X, control_code=0x%04X\n", 
           fd, bank_addr, control_code);
    
    // Select the 0x2080 register (LED mode control)
    if( write_word(fd, bank_addr, control_code) < 0) {
        perror("write_word(0x2080)");
        return -1;
    }
    // Now write a byte 0x01 to set direct control mode
    if( write_byte(fd, bank_addr, 0x01) < 0) {
        perror("write_byte(0x01)");
        return -1;
    }

    return 0;
}


int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s {red|blue|black}\n", argv[0]);
        return EXIT_FAILURE;
    }

    unsigned char r=0, b=0, g=0;
    if (colour_from_name(argv[1], &r, &b, &g) != 0) {
        fprintf(stderr, "Invalid colour '%s'. Use one of: red, blue, black.\n", argv[1]);
        return EXIT_FAILURE;
    }

    // Discover adapters
    glob_t gl = {0};
    int ret = glob("/dev/i2c-*", 0, NULL, &gl);
    if (ret != 0 || gl.gl_pathc == 0) {
        fprintf(stderr, "No I2C adapters found at /dev/i2c-*\n");
        globfree(&gl);
        return EXIT_FAILURE;
    }

    int fd = -1;
    const char *chosen = NULL;
    unsigned long funcs = 0;

    // Find first adapter with 0x70 present (7-bit probe)
    for (size_t i = 0; i < gl.gl_pathc; ++i) {
        const char *dev = gl.gl_pathv[i];
        int tmpfd = open(dev, O_RDWR | O_CLOEXEC);
        if (tmpfd < 0) {
            fprintf(stderr, "Warning: cannot open %s: %s\n", dev, strerror(errno));
            continue;
        }

        // Query adapter functionality to see what probing methods are supported. e.g.
        // I2C_FUNC_SMBUS_QUICK → supports SMBus “quick” transactions
        // I2C_FUNC_SMBUS_BYTE → supports single-byte read/write
        // I2C_FUNC_I2C → supports plain I²C transfers
        if (ioctl(tmpfd, I2C_FUNCS, &funcs) < 0) {
            fprintf(stderr, "Warning: cannot query funcs on %s: %s\n", dev, strerror(errno));
            close(tmpfd);
            continue;
        }

        // Ensure we're in 7-bit mode for probing
        int ten = 0; 
        ioctl(tmpfd, I2C_TENBIT, ten);

        if( has_device_at_addr(tmpfd, funcs, 0x70) ) {
            fd = tmpfd;
            chosen = dev;
            break;
        }
        close(tmpfd);
    }

    if (fd < 0) {
        fprintf(stderr, "No adapter with a responding device at 0x70 was found.\n");
        globfree(&gl);
        return EXIT_FAILURE;
    }

    printf("Using adapter: %s (found device at 0x70)\n", chosen);

    // Cycle through bank devices 0x70..0x73
    const unsigned char banks[] = { 0x70, 0x71, 0x72, 0x73 };

    for (size_t bi = 0; bi < ARRAY_SIZE(banks); ++bi) {
        unsigned char bank_addr = banks[bi];

        if (!has_device_at_addr(fd, funcs, bank_addr)) {
            fprintf(stderr, "Warning: No response from bank device at 0x%02x on %s; skipping.\n",
                    bank_addr, chosen);
            continue;
        }

        printf("Bank 0x%02x present; setting 8 LEDs to %s (R=%u B=%u G=%u)\n",
               bank_addr, argv[1], r, b, g);

        // Ensure direct control mode (required before writing LED colours)
        set_led_direct_control(fd, bank_addr, 0x2080);
        set_led_direct_control(fd, bank_addr, 0xA080);

        // For each of the eight LEDs, address via 10-bit LED address and write RBG block
        for (int n = 0; n < 8; ++n) {

            unsigned int led = led_addr_for_index(n);
            if (write_led_colour(fd, bank_addr, led, r, b, g) == 0) {
                printf("  LED%u @ 0x%03X set.\n", (unsigned)(n+1), led);
            } else {
                fprintf(stderr, "  Failed setting LED%u @ 0x%03X\n", (unsigned)(n+1), led);
            }
        }
    }

    close(fd);
    globfree(&gl);
    return EXIT_SUCCESS;
}
