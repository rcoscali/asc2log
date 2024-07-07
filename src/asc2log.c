/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * asc2log.c - convert ASC logfile to compact CAN frame logfile
 *
 * Copyright (c) 2002-2007 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Send feedback to <linux-can@vger.kernel.org>
 *
 */

#include <libgen.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include <linux/can.h>
#include <linux/can/error.h>
#include <net/if.h>

#include "lib.h"

#define BUFLEN 400 /* CAN FD mode lines can be pretty long */

/* relevant flags in Flags field */
#define ASC_F_RTR 0x00000010
#define ASC_F_FDF 0x00001000
#define ASC_F_BRS 0x00002000
#define ASC_F_ESI 0x00004000

extern int optind, opterr, optopt;

/**
 * print_usage
 *
 * Display usage help message for the command
 */
void
print_usage(char *prg)
{
  fprintf(stderr, "%s - convert ASC logfile to compact CAN frame logfile.\n", prg);
  fprintf(stderr, "Usage: %s [-v][-t]\n", prg);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "\t-h         \tdisplay this help message\n");
  fprintf(stderr, "\t-v         \tincrease verbosity\n");
  fprintf(stderr, "\t-t         \tfix time origin to 0\n");
  fprintf(stderr, "\t-N <name> \tfilter out frame names not starting with <name>\n");
  fprintf(stderr, "\t-f <canif> \tfilter out frames not from interface <canif>\n");
  fprintf(stderr, "\t-i <infile>\t(default stdin)\n");
  fprintf(stderr, "\t-o <outfile>\t(default stdout)\n");
}

/**
 * prframe
 *
 *
 *
 */
void
prframe(FILE *file,
	struct timeval *tv,
	int dev,
	struct canfd_frame *cf,
	unsigned int max_dlen,
	char *extra_info)
{
  fprintf(file, "(%lu.%06lu) ", tv->tv_sec, tv->tv_usec);

  if (dev > 0)
    fprintf(file, "can%d ", dev-1);
  else
    fprintf(file, "canX ");

  fprint_canframe(file, cf, extra_info, 0, max_dlen);
}

/**
 * get_can_id
 *
 *
 *
 */
void
get_can_id(struct canfd_frame *cf,
	   char *idstring,
	   int base)
{
  if (idstring[strlen(idstring)-1] == 'x')
    {
      cf->can_id = CAN_EFF_FLAG;
      idstring[strlen(idstring)-1] = 0;
  }
  else
    cf->can_id = 0;
    
  cf->can_id |= strtoul(idstring, NULL, base);
}

/**
 * calc_tv
 *
 *
 *
 *
 */
void
calc_tv(struct timeval *tv,
	struct timeval *read_tv,
	struct timeval *date_tv,
	char timestamps,
	int dplace,
	int fixtime)
{
  if (dplace == 4) /* shift values having only 4 decimal places */
    read_tv->tv_usec *= 100;                /* and need for 6 */

  if (dplace == 5) /* shift values having only 5 decimal places */
    read_tv->tv_usec *= 10;                /* and need for 6 */

  if (timestamps == 'a')
    { /* absolute */
      if (fixtime)
	{
	  tv->tv_sec  = read_tv->tv_sec;
	  tv->tv_usec = read_tv->tv_usec;
	}
      else
	{
	  tv->tv_sec  = date_tv->tv_sec  + read_tv->tv_sec;
	  tv->tv_usec = date_tv->tv_usec + read_tv->tv_usec;
	}
    }
  else
    { /* relative */
      if (((!tv->tv_sec) && (!tv->tv_usec)) && 
	  (date_tv->tv_sec || date_tv->tv_usec))
	{
	  if (fixtime)
	    {
	      tv->tv_sec  = 0;
	      tv->tv_usec = 0;
	    }
	  else
	    {
	      tv->tv_sec  = date_tv->tv_sec; /* initial date/time */
	      tv->tv_usec = date_tv->tv_usec;
	    }
	}

      tv->tv_sec  += read_tv->tv_sec;
      tv->tv_usec += read_tv->tv_usec;
    }

  if (tv->tv_usec >= 1000000)
    {
      tv->tv_usec -= 1000000;
      tv->tv_sec++;
    }
}

/**
 *
 *
 *
 *
 *
 */
int
eval_can(char* buf,
	 int canif,
	 int fixtime, 
	 struct timeval *date_tvp,
	 char timestamps,
	 char base,
	 int dplace,
	 FILE *outfile)
{
  int ret = 0;
  int interface;
  static struct timeval tv; /* current frame timestamp */
  static struct timeval read_tv; /* frame timestamp from ASC file */
  struct canfd_frame cf;
  struct can_frame *ccf = (struct can_frame *)&cf; /* for len8_dlc */
  char rtr;
  int dlc = 0;
  int len = 0;
  int data[8];
  char tmp1[BUFLEN];
  char dir[3]; /* 'Rx' or 'Tx' plus terminating zero */
  char *extra_info;
  int i, items;

  /* check for ErrorFrames */
  if (sscanf(buf, "%lu.%lu %d %s",
	     &read_tv.tv_sec, &read_tv.tv_usec,
	     &interface, tmp1) == 4)
    {
      if (!strncmp(tmp1, "ErrorFrame", strlen("ErrorFrame")))
	{
	  memset(&cf, 0, sizeof(cf));
	  /* do not know more than 'Error' */
	  cf.can_id = (CAN_ERR_FLAG | CAN_ERR_BUSERROR);
	  cf.len = CAN_ERR_DLC;

	  calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace, fixtime);
	  prframe(outfile, &tv, interface, &cf, CAN_MAX_DLEN, "\n");
	  fflush(outfile);
	  goto error_can;
	}
    }

  /* 0.002367 1 390x Rx d 8 17 00 14 00 C0 00 08 00 */

  /* check for CAN frames with (hexa)decimal values */
  if (base == 'h')
    items = sscanf(buf, "%lu.%lu %d %s %2s %c %x %x %x %x %x %x %x %x %x",
		   &read_tv.tv_sec, &read_tv.tv_usec, &interface,
		   tmp1, dir, &rtr, &dlc,
		   &data[0], &data[1], &data[2], &data[3],
		   &data[4], &data[5], &data[6], &data[7]);
  else
    items = sscanf(buf, "%lu.%lu %d %s %2s %c %x %d %d %d %d %d %d %d %d",
		   &read_tv.tv_sec, &read_tv.tv_usec, &interface,
		   tmp1, dir, &rtr, &dlc,
		   &data[0], &data[1], &data[2], &data[3],
		   &data[4], &data[5], &data[6], &data[7]);

  if (canif > 0 && interface != canif)
    goto error_can;
  
  if (items < 7 ) /* make sure we've read the dlc */
    goto error_can;

  /* dlc is one character hex value 0..F */
  if (dlc > CAN_MAX_RAW_DLC)
    goto error_can;

  /* retrieve real data length */
  if (dlc > CAN_MAX_DLC)
    len = CAN_MAX_DLEN;
  else
    len = dlc;

  if ((items == len + 7 ) || /* data frame */
      ((items == 6) && (rtr == 'r')) || /* RTR without DLC */
      ((items == 7) && (rtr == 'r')))   /* RTR with DLC */
    {
      /* check for CAN ID with (hexa)decimal value */
      if (base == 'h')
	get_can_id(&cf, tmp1, 16);
      else
	get_can_id(&cf, tmp1, 10);
      
      /* dlc > 8 => len == CAN_MAX_DLEN => fill len8_dlc value */
      if (dlc > CAN_MAX_DLC)
	ccf->len8_dlc = dlc;
      
      if (strlen(dir) != 2) /* "Rx" or "Tx" */
	goto error_can;
      
      /* check for signed integer overflow */
      if (dplace == 4 && read_tv.tv_usec >= INT_MAX / 100)
	goto error_can;

      if (dplace == 5 && read_tv.tv_usec >= INT_MAX / 10)
	goto error_can;
      
      if (dir[0] == 'R')
	extra_info = " R\n";
      else
	extra_info = " T\n";

      cf.len = len;
      if (rtr == 'r')
	cf.can_id |= CAN_RTR_FLAG;
      else
	for (i = 0; i < len; i++)
	  cf.data[i] = data[i] & 0xFFU;

      calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace, fixtime);
      prframe(outfile, &tv, interface, &cf, CAN_MAX_DLEN, extra_info);
      fflush(outfile);
    }
  ret = 1;
  
 error_can:
  return ret;
}

/**
 * eval_canfd
 *
 *
 *
 *
 */
int
eval_canfd(char* buf,
	   int canif,
	   int fixtime,
	   const char *frame_name,
	   struct timeval *date_tvp,
	   char timestamps,
	   int dplace,
	   FILE *outfile)
{
  int ret = 0;
  int interface;
  static struct timeval tv; /* current frame timestamp */
  static struct timeval read_tv; /* frame timestamp from ASC file */
  struct canfd_frame cf = { 0 };
  unsigned char brs, esi, ctmp;
  unsigned int flags;
  int dlc, dlen = 0;
  char tmp1[BUFLEN];
  char name[BUFLEN];
  char dir[3]; /* 'Rx' or 'Tx' plus terminating zero */
  char *extra_info;
  char *ptr;
  int i;

  /* The CANFD format is mainly in hex representation but <DataLength>
     and probably some content we skip anyway. Don't trust the docs! */

  /* 21.671796 CANFD   1 Tx         11  msgCanFdFr1                      1 0 a 16 \
     00 00 00 00 00 00 00 00 00 00 00 00 00 00 59 c0		\
     100000  214   223040 80000000 46500250 460a0250 20011736 20010205 */

  /* check for valid line without symbolic name */
  if (sscanf(buf, "%lu.%lu %*s %d %2s %s %hhx %hhx %x %d ",
	     &read_tv.tv_sec, &read_tv.tv_usec, &interface,
	     dir, tmp1, &brs, &esi, &dlc, &dlen) != 9)
    {
      /* check for valid line with a symbolic name */
      if (sscanf(buf, "%lu.%lu %*s %d %2s %s %s %hhx %hhx %x %d ",
		 &read_tv.tv_sec, &read_tv.tv_usec, &interface,
		 dir, tmp1, name, &brs, &esi, &dlc, &dlen) != 10)
	{
	  /* no valid CANFD format pattern */
	  goto error_canfd;
	}
    }

  /* Filter out frames not matching if or name */
  if (canif > 0 && interface != canif)
    goto error_canfd;
  if (frame_name && strncmp(frame_name, name, strlen(frame_name)))
    goto error_canfd;

  /* check for allowed (unsigned) value ranges */
  if ((dlen > CANFD_MAX_DLEN) || (dlc > CANFD_MAX_DLC) ||
      (brs > 1) || (esi > 1))
    goto error_canfd;

  if (strlen(dir) != 2) /* "Rx" or "Tx" */
    goto error_canfd;

  /* check for signed integer overflow */
  if (dplace == 4 && read_tv.tv_usec >= INT_MAX / 100)
    goto error_canfd;

  /* check for signed integer overflow */
  if (dplace == 5 && read_tv.tv_usec >= INT_MAX / 10)
    goto error_canfd;

  if (dir[0] == 'R')
    extra_info = " R\n";
  else
    extra_info = " T\n";

  /* don't trust ASCII content - sanitize data length */
  if (dlen != can_fd_dlc2len(can_fd_len2dlc(dlen)))
    goto error_canfd;

  get_can_id(&cf, tmp1, 16);

  /* now search for the beginning of the data[] content */
  sprintf(tmp1, " %x %x %x %2d ", brs, esi, dlc, dlen);

  /* search for the pattern generated by real data */
  ptr = strcasestr(buf, tmp1);
  if (ptr == NULL)
    goto error_canfd;

  ptr += strlen(tmp1); /* start of ASCII hex frame data */

  cf.len = dlen;

  for (i = 0; i < dlen; i++)
    {
      ctmp = asc2nibble(ptr[0]);
      if (ctmp > 0x0F)
	goto error_canfd;

      cf.data[i] = (ctmp << 4);
      
      ctmp = asc2nibble(ptr[1]);
      if (ctmp > 0x0F)
	goto error_canfd;

      cf.data[i] |= ctmp;

      ptr += 3; /* start of next ASCII hex byte */
    }

  /* skip MessageDuration and MessageLength to get Flags value */
  if (sscanf(ptr, "   %*x %*x %x ", &flags) != 1)
    goto error_canfd;

  if (flags & ASC_F_FDF)
    {
      dlen = CANFD_MAX_DLEN;
      if (flags & ASC_F_BRS)
	cf.flags |= CANFD_BRS;
      if (flags & ASC_F_ESI)
	cf.flags |= CANFD_ESI;
    }
  else
    {
      /* yes. The 'CANFD' format supports classic CAN content! */
      dlen = CAN_MAX_DLEN;
      if (flags & ASC_F_RTR)
	{
	  cf.can_id |= CAN_RTR_FLAG;
	  /* dlen is always 0 for classic CAN RTR frames
	     but the DLC value is valid in RTR cases */
	  cf.len = dlc;
	  /* sanitize payload length value */
	  if (dlc > CAN_MAX_DLEN)
	    cf.len = CAN_MAX_DLEN;
	}
      /* check for extra DLC when having a Classic CAN with 8 bytes payload */
      if ((cf.len == CAN_MAX_DLEN) && (dlc > CAN_MAX_DLEN) && (dlc <= CAN_MAX_RAW_DLC))
	{
	  struct can_frame *ccf = (struct can_frame *)&cf;

	  ccf->len8_dlc = dlc;
	}
    }

  calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace, fixtime);
  prframe(outfile, &tv, interface, &cf, dlen, extra_info);
  fflush(outfile);

  /* No support for really strange CANFD ErrorFrames format m( */
  ret = 1;
  
 error_canfd:
  return ret;
}

/**
 * get_date
 *
 *
 *
 *
 *
 */
int
get_date(struct timeval *tv,
	 char *date)
{
  struct tm tms;
  unsigned int msecs = 0;
  
  if (strcasestr(date, " pm ") != NULL)
    {
      /* assume EN/US date due to existing am/pm field */

      if (!setlocale(LC_TIME, "en_US"))
	{
	  fprintf(stderr, "Setting locale to 'en_US' failed!\n");
	  return 1;
	}

      if (!strptime(date, "%B %d %I:%M:%S %p %Y", &tms))
	{
	  /* The string might contain a milliseconds value which strptime()
	     does not support. So we read the ms value into the year variable
	     before parsing the real year value (hack) */
	  if (!strptime(date, "%B %d %I:%M:%S.%Y %p %Y", &tms))
	    return 1;
	  sscanf(date, "%*s %*d %*d:%*d:%*d.%3u ", &msecs);
	}
  }
  else
    {
      /* assume DE date due to non existing am/pm field */

      if (!setlocale(LC_TIME, "de_DE"))
	{
	  fprintf(stderr, "Setting locale to 'de_DE' failed!\n");
	  return 1;
	}

      if (!strptime(date, "%B %d %H:%M:%S %Y", &tms))
	{
	  /* The string might contain a milliseconds value which strptime()
	     does not support. So we read the ms value into the year variable
	     before parsing the real year value (hack) */
	  if (!strptime(date, "%B %d %H:%M:%S.%Y %Y", &tms))
	    return 1;
	  sscanf(date, "%*s %*d %*d:%*d:%*d.%3u ", &msecs);
	}
    }

  //printf("h %d m %d s %d ms %03d d %d m %d y %d\n",
  //tms.tm_hour, tms.tm_min, tms.tm_sec, msecs,
  //tms.tm_mday, tms.tm_mon+1, tms.tm_year+1900);

  tv->tv_sec = mktime(&tms);
  tv->tv_usec = msecs * 1000;

  if (tv->tv_sec < 0)
    return 1;

  return 0;
}

/**
 *
 *
 *
 *
 *
 *
 */
int
main(int argc, char **argv)
{
  char buf[BUFLEN], tmp1[BUFLEN], tmp2[BUFLEN];

  FILE *infile = stdin;
  FILE *outfile = stdout;
  static struct timeval tmp_tv; /* tmp frame timestamp from ASC file */
  static struct timeval date_tv; /* date of the ASC file */
  static int dplace; /* decimal place 4, 5 or 6 or uninitialized */
  static char base; /* 'd'ec or 'h'ex */
  static char timestamps; /* 'a'bsolute or 'r'elative */
  static int opt_fixtime = 0;
  static int opt_verbose = 0;
  static char *frame_name = NULL;
  static int opt_help = 0;
  static int wanted_canif = 0;
  unsigned int nframes = 0;
  int opt;

  while ((opt = getopt(argc, argv, "htf:N:i:o:v?")) != -1)
    {
      switch (opt)
	{
	case 't':
	  opt_fixtime = 1;
	  break;
	  
	case 'f':
	  wanted_canif = atoi(optarg);
	  break;
	  
	case 'N':
	  frame_name = optarg;
	  break;
	  
	case 'i':
	  infile = fopen(optarg, "r");
	  if (!infile) {
	    perror("infile");
	    return 1;
	  }
	  break;
	  
	case 'o':
	  outfile = fopen(optarg, "w");
	  if (!outfile) {
	    perror("outfile");
	    return 1;
	  }
	  break;
	  
	case 'v':
	  opt_verbose++;
	  break;
	  
	case 'h':
	  opt_help++;
	  break;
	  
	case '?':
	  print_usage(basename(argv[0]));
	  return 0;
	  break;
	  
	default:
	  fprintf(stderr, "Unknown option %c\n", opt);
	  print_usage(basename(argv[0]));
	  return 1;
	  break;
	}
  }

  if (opt_help)
    {
      print_usage(basename(argv[0]));
      exit(0);
    }

  while (fgets(buf, BUFLEN-1, infile))
    {
    if (!dplace) /* the representation of a valid CAN frame not known */
      {
	/* check for base and timestamp entries in the header */
	if ((!base) &&
	    (sscanf(buf, "base %s timestamps %s", tmp1, tmp2) == 2))
	  {
	    base = tmp1[0];
	    timestamps = tmp2[0];
	    if (opt_verbose)
	      printf("base %c timestamps %c\n", base, timestamps);
	    if ((base != 'h') && (base != 'd'))
	      {
		printf("invalid base %s (must be 'hex' or 'dez')!\n", tmp1);
		return 1;
	      }
	    if ((timestamps != 'a') && (timestamps != 'r'))
	      {
		printf("invalid timestamps %s (must be 'absolute' or 'relative')!\n", tmp2);
		return 1;
	      }
	    continue;
	  }

	/* check for the original logging date in the header */ 
	if ((!date_tv.tv_sec) &&
	    (!strncmp(buf, "date", 4)))
	  {
	    
	    if (get_date(&date_tv, &buf[9]))   /* skip 'date day ' */
	      {
		fprintf(stderr, "Not able to determine original log file date. Using current time.\n");
		/* use current date as default */
		gettimeofday(&date_tv, NULL);
	      }
	
	    if (opt_verbose)
	      printf("date %lu => %s", date_tv.tv_sec, ctime(&date_tv.tv_sec));
	    
	    continue;
	  }

	/* check for decimal places length in valid CAN frames */
	if (sscanf(buf, "%lu.%s %s ",
		   &tmp_tv.tv_sec, tmp2, tmp1) != 3)
	  continue; /* dplace remains zero until first found CAN frame */

	dplace = strlen(tmp2);
	if (opt_verbose)
	  printf("decimal place %d, e.g. '%s'\n", dplace, tmp2);
	if (dplace < 4 || dplace > 6)
	  {
	    printf("invalid dplace %d (must be 4, 5 or 6)!\n", dplace);
	    return 1;
	  }
      }

    /* the representation of a valid CAN frame is known here */
    /* so try to get CAN frames and ErrorFrames and convert them */

    /* check classic CAN format or the CANFD tag which can take both types */
    if (sscanf(buf, "%lu.%lu %s ", &tmp_tv.tv_sec,  &tmp_tv.tv_usec, tmp1) == 3)
      {
	if (!strncmp(tmp1, "CANFD", 5))
	  nframes += eval_canfd(buf, wanted_canif, opt_fixtime, frame_name, &date_tv, timestamps, dplace, outfile);
	else
	  nframes += eval_can(buf, wanted_canif, opt_fixtime, &date_tv, timestamps, base, dplace, outfile);
      }
  }

  if (opt_verbose)
    printf("%u frames converted !\n", nframes);
  else
    fprintf(stderr, "%u frames converted !\n", nframes);
  
  fclose(outfile);
  fclose(infile);
  return 0;
}
