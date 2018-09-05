/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Support for using spidev userspace drivers to communicate directly over SPI
 * 
 * Copyright (C) 2013 Kevin Cuzner <kevin@kevincuzner.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 * Support for inversion of reset pin, Tim Chilton 02/05/2014
 */
 
#include "linuxspi.h"

#include "ac_cfg.h"

#include "avrdude.h"
#include "avr.h"
#include "pindefs.h"

#if HAVE_SPIDEV

/**
 * Linux Kernel SPI Drivers
 * 
 * Copyright (C) 2006 SWAPP
 *      Andrea Paterniani <a.paterniani@swapp-eng.it>
 * Copyright (C) 2007 David Brownell (simplification, cleanup)
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/**
 * Data for the programmer
 */

struct pdata
{
    int port_fd;
};

typedef enum {
    LINUXSPI_GPIO_DIRECTION,
    LINUXSPI_GPIO_VALUE,
    LINUXSPI_GPIO_EXPORT,
    LINUXSPI_GPIO_UNEXPORT
} LINUXSPI_GPIO_OP;

#define PDATA(pgm) ((struct pdata *)(pgm->cookie))
#define IMPORT_PDATA(pgm) struct pdata *pdata = PDATA(pgm)

/**
 * Function Prototypes
 */

//linuxspi specific functions
static int linuxspi_spi_duplex(PROGRAMMER* pgm, unsigned char* tx, unsigned char* rx, int len);
static int linuxspi_gpio_op_wr(PROGRAMMER* pgm, LINUXSPI_GPIO_OP op, int gpio, char* val);
//interface - management
static void linuxspi_setup(PROGRAMMER* pgm);
static void linuxspi_teardown(PROGRAMMER* pgm);
//interface - prog
static int linuxspi_open(PROGRAMMER* pgm, char* port);
static void linuxspi_close(PROGRAMMER* pgm);
// dummy functions
static void linuxspi_disable(PROGRAMMER * pgm);
static void linuxspi_enable(PROGRAMMER * pgm);
static void linuxspi_display(PROGRAMMER * pgm, const char * p);
//universal
static int linuxspi_initialize(PROGRAMMER* pgm, AVRPART* p);
// SPI specific functions
static int linuxspi_cmd(PROGRAMMER * pgm, unsigned char cmd[4], unsigned char res[4]);
static int linuxspi_program_enable(PROGRAMMER * pgm, AVRPART * p);
static int linuxspi_chip_erase(PROGRAMMER * pgm, AVRPART * p);

/**
 * @brief Sends/receives a message in full duplex mode
 * @return -1 on failure, otherwise number of bytes sent/recieved
 */
static int linuxspi_spi_duplex(PROGRAMMER* pgm, unsigned char* tx, unsigned char* rx, int len)
{
    IMPORT_PDATA(pgm);
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = len,
        .delay_usecs = 0,
        .speed_hz = 0,
        .bits_per_word = 0,
    };
    
    int ret = ioctl(pdata->port_fd, SPI_IOC_MESSAGE(1), &tr);
    
    if (ret != len)
    {
        fprintf(stderr, "\n%s: error: Unable to send SPI message\n", progname);
        return -1;
    }
    
    return ret;
}

/**
 * @brief Performs an operation on a gpio. Writes to stderr if error.
 * @param op Operation to perform
 * @param gpio 
 * @return -1 if failed, 0 otherwise
 */
static int linuxspi_gpio_op_wr(PROGRAMMER* pgm, LINUXSPI_GPIO_OP op, int gpio, char* val)
{
    char* fn = malloc(PATH_MAX); //filename
    gpio &= ~PIN_INVERSE; // Remove the inversion flag

    switch(op)
    {
        case LINUXSPI_GPIO_DIRECTION:
            sprintf(fn, "/sys/class/gpio/gpio%d/direction", gpio);
            break;
        case LINUXSPI_GPIO_EXPORT:
            sprintf(fn, "/sys/class/gpio/export");
            break;
        case LINUXSPI_GPIO_UNEXPORT:
            sprintf(fn, "/sys/class/gpio/unexport");
            break;
        case LINUXSPI_GPIO_VALUE:
            sprintf(fn, "/sys/class/gpio/gpio%d/value", gpio);
            break;
        default:
            fprintf(stderr, "%s: linuxspi_gpio_op_wr(): Unknown op %d", progname, op);
            return -1;
    }
    
    FILE* f = fopen(fn, "w");
    
    int fopen_retries = 0;
    while (!f && (fopen_retries < 100))
    {
        usleep(20000);
        f = fopen(fn, "w");
        fopen_retries++;
    }

    if (!f)
    {
        fprintf(stderr, "%s: linuxspi_gpio_op_wr(): Unable to open file %s", progname, fn);
        free(fn); //we no longer need the path
        return -1;
    }
    
    if (fprintf(f, val) < 0)
    {
        fprintf(stderr, "%s: linuxspi_gpio_op_wr(): Unable to write file %s with %s", progname, fn, val);
        free(fn); //we no longer need the path
        return -1;
    }
    
    fclose(f);
    free(fn); //we no longer need the path
    
    return 0;
}

static void linuxspi_setup(PROGRAMMER* pgm)
{
    if ((pgm->cookie = malloc(sizeof(struct pdata))) == 0)
    {
        fprintf(stderr, "%s: linuxspi_setup(): Unable to allocate private memory.\n", progname);
        exit(1);
    }
    memset(pgm->cookie, 0, sizeof(struct pdata));
}

static void linuxspi_teardown(PROGRAMMER* pgm)
{
    free(pgm->cookie);
}

static int linuxspi_open(PROGRAMMER* pgm, char* port)
{
    IMPORT_PDATA(pgm);
    char* buf;
    
    if (port == 0 || strcmp(port, "unknown") == 0) //unknown port
    {
        fprintf(stderr, "%s: error: No port specified. Port should point to an SPI interface.\n", progname);
        exit(1);
    }
    
    if (pgm->pinno[PIN_AVR_RESET] == 0)
    {
        fprintf(stderr, "%s: error: No pin assigned to AVR RESET.\n", progname);
        exit(1);
    }

    //export reset pin
    buf = malloc(32);
    sprintf(buf, "%d", pgm->pinno[PIN_AVR_RESET] &~PIN_INVERSE);
    if (linuxspi_gpio_op_wr(pgm, LINUXSPI_GPIO_EXPORT, pgm->pinno[PIN_AVR_RESET], buf) < 0)
    {
        free(buf);
        return -1;
    }
    free(buf);
    
    //set reset to output active and write initial value at same time
    //this prevents glitches https://www.kernel.org/doc/Documentation/gpio/sysfs.txt
    if (linuxspi_gpio_op_wr(pgm, LINUXSPI_GPIO_DIRECTION, pgm->pinno[PIN_AVR_RESET], pgm->pinno[PIN_AVR_RESET]&PIN_INVERSE ? "high" : "low") < 0)
    {
        return -1;
    }
    
    //save the port to our data
    strcpy(pgm->port, port);

    pdata->port_fd = open(port, O_RDWR);
    if (pdata->port_fd < 0)
    {
        fprintf(stderr, "\n%s: error: Unable to open SPI port %s", progname, port);
        return -1; //error
    }
    else
    {
        long baud;
        unsigned char mode = SPI_MODE_0;
        unsigned char bits = 8;

        // Default SPI speed of 400KHz. Adjust using baud parameter (-b)
        baud = pgm->baudrate;
        if (baud == 0)
            baud = 400000;

        if (ioctl(pdata->port_fd, SPI_IOC_WR_MODE, &mode) == -1)
        {
          fprintf(stderr, "can't set SPI mode\n");
          return -1;
        }
        if (ioctl(pdata->port_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) == -1)
        {
          fprintf(stderr, "can't set SPI bits\n");
          return -1;
        }
        if (ioctl(pdata->port_fd, SPI_IOC_WR_MAX_SPEED_HZ, &baud) == -1)
        {
          fprintf(stderr, "can't set SPI speed\n");
          return -1;
        }
    }
    
    return 0;
}

static void linuxspi_close(PROGRAMMER* pgm)
{
    IMPORT_PDATA(pgm);
    char* buf;

    // Close SPI device
    if (pdata->port_fd >= 0)
    {
        close(pdata->port_fd);
        pdata->port_fd = -1;
    }

    //set reset to input
    linuxspi_gpio_op_wr(pgm, LINUXSPI_GPIO_DIRECTION, pgm->pinno[PIN_AVR_RESET], "in");
    
    //unexport reset
    buf = malloc(32);
    sprintf(buf, "%d", pgm->pinno[PIN_AVR_RESET]);
    linuxspi_gpio_op_wr(pgm, LINUXSPI_GPIO_UNEXPORT, pgm->pinno[PIN_AVR_RESET], buf);
}

static void linuxspi_disable(PROGRAMMER* pgm)
{
    //do nothing
}

static void linuxspi_enable(PROGRAMMER* pgm)
{
    //do nothing
}

static void linuxspi_display(PROGRAMMER* pgm, const char* p)
{
    //do nothing
}

static int linuxspi_initialize(PROGRAMMER* pgm, AVRPART* p)
{
    int tries, rc;
    
    if (p->flags & AVRPART_HAS_TPI)
    {
        //we do not support tpi..this is a dedicated SPI thing
        fprintf(stderr, "%s: error: Programmer %s does not support TPI\n", progname, pgm->type);
        return -1;
    }
    
    //enable programming on the part
    tries = 0;
    do
    {
        rc = pgm->program_enable(pgm, p);
        if (rc == 0 || rc == -1)
            break;
        tries++;
    }
    while(tries < 65);
    
    if (rc)
    {
        fprintf(stderr, "%s: error: AVR device not responding\n", progname);
        return -1;
    }
    
    return 0;
}

static int linuxspi_cmd(PROGRAMMER* pgm, unsigned char cmd[4], unsigned char res[4])
{
    return linuxspi_spi_duplex(pgm, cmd, res, 4);
}

static int linuxspi_program_enable(PROGRAMMER* pgm, AVRPART* p)
{
    unsigned char cmd[4];
    unsigned char res[4];
    
    if (p->op[AVR_OP_PGM_ENABLE] == NULL)
    {
        fprintf(stderr, "%s: error: program enable instruction not defined for part \"%s\"\n", progname, p->desc);
        return -1;
    }
    
    memset(cmd, 0, sizeof(cmd));
    avr_set_bits(p->op[AVR_OP_PGM_ENABLE], cmd); //set the cmd
    pgm->cmd(pgm, cmd, res);
    
    if (res[2] != cmd[1])
        return -2;
    
    return 0;
}

static int linuxspi_chip_erase(PROGRAMMER* pgm, AVRPART* p)
{
    unsigned char cmd[4];
    unsigned char res[4];
    
    if (p->op[AVR_OP_CHIP_ERASE] == NULL)
    {
        fprintf(stderr, "%s: error: chip erase instruction not defined for part \"%s\"\n", progname, p->desc);
        return -1;
    }
    
    memset(cmd, 0, sizeof(cmd));

    avr_set_bits(p->op[AVR_OP_CHIP_ERASE], cmd);
    pgm->cmd(pgm, cmd, res);
    usleep(p->chip_erase_delay);
    pgm->initialize(pgm, p);
    
    return 0;
}

void linuxspi_initpgm(PROGRAMMER * pgm)
{
    strcpy(pgm->type, "linuxspi");
    
    pgm_fill_old_pins(pgm); // TODO to be removed if old pin data no longer needed
    
    /*
     * mandatory functions
     */

    pgm->initialize     = linuxspi_initialize;
    pgm->display        = linuxspi_display;
    pgm->enable         = linuxspi_enable;
    pgm->disable        = linuxspi_disable;
    pgm->program_enable = linuxspi_program_enable;
    pgm->chip_erase     = linuxspi_chip_erase;
    pgm->cmd            = linuxspi_cmd;
    pgm->open           = linuxspi_open;
    pgm->close          = linuxspi_close;
    pgm->read_byte      = avr_read_byte_default;
    pgm->write_byte     = avr_write_byte_default;

    /*
     * optional functions
     */
    pgm->setup          = linuxspi_setup;
    pgm->teardown       = linuxspi_teardown;
}

const char linuxspi_desc[] = "SPI using Linux spidev driver";

#else

void linuxspi_initpgm(PROGRAMMER * pgm)
{
    fprintf(stderr,
      "%s: Linux SPI driver not available in this configuration\n",
      progname);
}

const char linuxspi_desc[] = "SPI using Linux spidev driver (not available)";

#endif
