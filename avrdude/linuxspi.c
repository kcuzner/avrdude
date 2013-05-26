/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Support for bitbanging GPIO pins using the /sys/class/gpio interface
 * 
 * Copyright (C) 2013 Radoslav Kolev <radoslav@kolev.info>
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

/**
 * Data for the programmer
 */

struct pdata
{
    /**
     * Port string to open
     */
    char* port;
};

#define PDATA(pgm) ((struct pdata *)(pgm->cookie))
#define IMPORT_PDATA(pgm) struct pdata *pdata = PDATA(pgm)

/**
 * Function Prototypes
 */

//linuxspi specific functions
static int linuxspi_open_spi(PROGRAMMER* pgm); //returns a file descriptor or -1
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
static int linuxspi_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                 unsigned int page_size,
                                 unsigned int addr, unsigned int n_bytes);
static int linuxspi_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned int page_size,
                                  unsigned int addr, unsigned int n_bytes);
static int linuxspi_set_sck_period(PROGRAMMER *pgm, double sckperiod);

static int linuxspi_open_spi(PROGRAMMER* pgm)
{
    int fd = open(pgm->port, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "\n%s: error: Unable to open SPI port %s", progname, pgm->port);
        return -1; //error
    }
    
    //set mode
    
    //set speed
    
    //set bits per word

    
    return fd;
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
    if (PDATA(pgm)->port != 0)
        free(PDATA(pgm)->port);
    free(pgm->cookie);
}

static int linuxspi_open(PROGRAMMER* pgm, char* port)
{
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
    
    // TODO Remove this repetitive code from here and also the close function
    
    //export reset pin
    FILE* f = fopen("/sys/class/gpio/export", "w");
    if (f == 0)
    {
        fprintf(stderr, "%s: error: Failed to open /sys/class/gpio/export", progname);
        exit(1);
    }
    if (fprintf(f, "%d", pgm->pinno[PIN_AVR_RESET]) < 0)
    {
        fprintf(stderr, "%s: error: Failed to export GPIO %d", progname, pgm->pinno[PIN_AVR_RESET]);
        exit(1);
    }
    fclose(f);
    
    //set reset to output
    char* buf = malloc(PATH_MAX);
    if (buf == 0)
    {
        fprintf(stderr, "%s: linuxspi_open(): Unable to allocate private memory.\n", progname);
        exit(1);
    }
    sprintf(buf, "/sys/class/gpio/gpio%d/direction", pgm->pinno[PIN_AVR_RESET]);
    f = fopen(buf, "w");
    if (f == 0)
    {
        fprintf(stderr, "%s: error: Failed to open /sys/class/gpio/gpio%d/direction", progname, pgm->pinno[PIN_AVR_RESET]);
        exit(1);
    }
    if (fprintf(f, "out") < 0)
    {
        fprintf(stderr, "%s: error: Failed to set direction on GPIO %d", progname, pgm->pinno[PIN_AVR_RESET]);
        exit(1);
    }
    fclose(f);
    
    
    //set reset low
    sprintf(buf, "/sys/class/gpio/gpio%d/value", pgm->pinno[PIN_AVR_RESET]);
    f = fopen(buf, "w");
    if (f == 0)
    {
        fprintf(stderr, "%s: error: Failed to open /sys/class/gpio/gpio%d/value", progname, pgm->pinno[PIN_AVR_RESET]);
        exit(1);
    }
    if (fprintf(f, "0") < 0)
    {
        fprintf(stderr, "%s: error: Failed to set value on GPIO %d", progname, pgm->pinno[PIN_AVR_RESET]);
        exit(1);
    }
    fclose(f);
    
    free(buf);
    
    
    //save the port to our data
    strcpy(pgm->port, port);
    
    return 0;
}

static void linuxspi_close(PROGRAMMER* pgm)
{
    char* buf = malloc(PATH_MAX);
    
    //set reset to input
    sprintf(buf, "/sys/class/gpio/gpio%d/direction", pgm->pinno[PIN_AVR_RESET]);
    FILE* f = fopen(buf, "w");
    if (f == 0)
    {
        fprintf(stderr, "%s: error: Failed to open /sys/class/gpio/gpio%d/direction", progname, pgm->pinno[PIN_AVR_RESET]);
        exit(1);
    }
    if (fprintf(f, "in") < 0)
    {
        fprintf(stderr, "%s: error: Failed to set direction on GPIO %d", progname, pgm->pinno[PIN_AVR_RESET]);
        exit(1);
    }
    fclose(f);
    
    //unexport reset
    f = fopen("/sys/class/gpio/unexport", "w");
    if (f == 0)
    {
        fprintf(stderr, "%s: error: Failed to open /sys/class/gpio/unexport", progname);
        exit(1);
    }
    if (fprintf(f, "%d", pgm->pinno[PIN_AVR_RESET]) < 0)
    {
        fprintf(stderr, "%s: error: Failed to unexport GPIO %d", progname, pgm->pinno[PIN_AVR_RESET]);
        exit(1);
    }
    fclose(f);
    
    free(buf);
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
    return 0;
}

static int linuxspi_cmd(PROGRAMMER* pgm, unsigned char cmd[4], unsigned char res[4])
{
    int fd = linuxspi_open_spi(pgm);
    
    if (fd < 0)
        return -1;
    
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)cmd,
        .rx_buf = (unsigned long)res,
        .len = 4,
        .delay_usecs = 1,
        .speed_hz = 1000000,
        .bits_per_word = 8,
    };
    
    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) != 4)
    {
        fprintf(stderr, "\n%s: error: Unable to send SPI message\n", progname);
        return -1;
    }
    
    close(fd);
    
    return 0;
}

static int linuxspi_program_enable(PROGRAMMER* pgm, AVRPART* p)
{
    return -1;
}

static int linuxspi_chip_erase(PROGRAMMER* pgm, AVRPART* p)
{
    return -1;
}

static int linuxspi_paged_load(PROGRAMMER* pgm, AVRPART* p, AVRMEM* m, 
                               unsigned int page_size, unsigned int addr, unsigned int n_bytes)
{
    return -1;
}

static int linuxspi_paged_write(PROGRAMMER* pgm, AVRPART* p, AVRMEM* m,
                                unsigned int page_size, unsigned int addr, unsigned int n_bytes)
{
    return -1;
}

static int linuxspi_set_sck_period(PROGRAMMER* pgm, double sckperiod)
{
    return -1;
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

    pgm->paged_write    = linuxspi_paged_write;
    pgm->paged_load     = linuxspi_paged_load;
    pgm->setup          = linuxspi_setup;
    pgm->teardown       = linuxspi_teardown;
    pgm->set_sck_period = linuxspi_set_sck_period;
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
