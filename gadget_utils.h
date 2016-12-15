#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "gadget_headers.h"

#ifndef MAXLEN
#define MAXLEN 1000
#endif

enum iofields           /*!< this enumeration lists the defined output blocks in snapshot files. Not all of them need to be present. */
{
  IO_POS,
  IO_VEL,
  IO_ID,
  IO_MASS,
  /* IO_U, */
  /* IO_RHO, */
  /* IO_HSML, */
  /* IO_POT, */
  /* IO_ACCEL, */
  /* IO_DTENTR, */
  /* IO_TSTP, */
};




struct io_header get_gadget_header(const char *fname);
int get_gadget_nfiles(const char *fname);
int64_t get_Numpart(struct io_header *header);
FILE * position_file_pointer(const char *file, const int type, const enum iofields field);
size_t get_gadget_id_bytes(const char *file);

