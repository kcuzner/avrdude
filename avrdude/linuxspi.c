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

#if HAVE_LINUXSPI

void linuxspi_initpgm(PROGRAMMER * pgm)
{
  fprintf(stderr,
	  "%s: Linux SPI driver not available in this configuration\n",
	  progname);
}

const char linuxspi_desc[] = "SPI using Linux spidev driver (not available)";

#else

void linuxspi_initpgm(PROGRAMMER * pgm)
{
  fprintf(stderr,
	  "%s: Linux SPI driver not available in this configuration\n",
	  progname);
}

const char linuxspi_desc[] = "SPI using Linux spidev driver (not available)";

#endif
