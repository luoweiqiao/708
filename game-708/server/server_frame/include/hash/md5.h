/*
 * Declaration of functions and data types used for MD5 sum computing
 * library functions.  Copyright (C) 1995, 1996 Free Software
 * Foundation, Inc.  NOTE: The canonical source of this file is
 * maintained with the GNU C Library.  Bugs can be reported to
 * bug-glibc@prep.ai.mit.edu.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * $Id: md5.h,v 1.1.1.1 2004/05/17 13:23:36 rcrittenden0569 Exp $
 */

/*
 * NOTE: during quick performance tests on a sun ultra and an alpha
 * 255 300, the md5 libraries performed upwards of 3mb/sec.  That
 * included disk i/o on bobcat and panther.
 */

#ifndef __MD5_H__
#define __MD5_H__

#include "fundamental/common.h"


/*
 * Size of a standard MD5 signature in bytes.  This definition is for
 * external programs only.  The MD5 routines themselves reference the
 * signature as 4 unsigned 32-bit integers.
 */
#define MD5_SIZE	16


#define MAX_MD5_UINT32		((uint32_t)4294967295U)

/*
 * The MD5 algorithm works on blocks of characters of 64 bytes.  This
 * is an internal value only and is not necessary for external use.
 */
#define MD5_BLOCK_SIZE		64


/*
 * Structure to save state of computation between the single steps.
 */
struct md5_t
{
  uint32_t	md_A;			/* accumulater 1 */
  uint32_t	md_B;			/* accumulater 2 */
  uint32_t	md_C;			/* accumulater 3 */
  uint32_t	md_D;			/* accumulater 4 */

  uint32_t	md_total[2];		/* totaling storage */
  uint32_t	md_buf_len;		/* length of the storage buffer */
  char		md_buffer[MD5_BLOCK_SIZE * 2];	/* character storage buffer */
};

/*<<<<<<<<<<  The below prototypes are auto-generated by fillproto */

/*
 * md5_init
 *
 * DESCRIPTION:
 *
 * Initialize structure containing state of MD5 computation. (RFC 1321,
 * 3.3: Step 3).  This is for progressive MD5 calculations only.  If
 * you have the complete string available, md5_buffer should be used.
 * md5_process should be called for each bunch of bytes and after the
 * last process call, md5_finish should be called to get the
 * signature.
 *
 * RETURNS:
 *
 * None.
 *
 * ARGUMENTS:
 *
 * md5_p - Pointer to md5 structure that we are initializing.
 */

void	md5_init(md5_t *md5_p);

/*
 * md5_process
 *
 * DESCRIPTION:
 *
 * This function is used to progressively calculate a MD5 signature some
 * number of bytes at a time.  If you have the complete string
 * available, md5_buffer should be used.  The MD5 structure should
 * have been initialized with md5_init and after the last process
 * call, md5_finish should be called to get the results.
 *
 * RETURNS:
 *
 * None.
 *
 * ARGUMENTS:
 *
 * md5_p - Pointer to MD5 structure which we are progressively updating.
 *
 * buffer - A buffer of bytes whose MD5 signature we are calculating.
 *
 * buf_len - The length of the buffer.
 */

void	md5_process(md5_t *md5_p, const void *buffer,
        uint32_t buf_len);

/*
 * md5_finish
 *
 * DESCRIPTION:
 *
 * Finish a progressing MD5 calculation and copy the resulting MD5
 * signature into the result buffer which should be 16 bytes
 * (MD5_SIZE).  After this call, the MD5 structure is invalid.
 *
 * RETURNS:
 *
 * None.
 *
 * ARGUMENTS:
 *
 * md5_p - Pointer to MD5 structure which we are finishing.
 *
 * signature - A 16 byte buffer that will contain the MD5 signature.
 */

void	md5_finish(md5_t *md5_p, void *signature);

/*
 * md5_buffer
 *
 * DESCRIPTION:
 *
 * This function is used to calculate a MD5 signature for a buffer of
 * bytes.  If you only have part of a buffer that you want to process
 * then md5_init, md5_process, and md5_finish should be used.
 *
 * RETURNS:
 *
 * None.
 *
 * ARGUMENTS:
 *
 * buffer - A buffer of bytes whose MD5 signature we are calculating.
 *
 * buf_len - The length of the buffer.
 *
 * signature - A 16 byte buffer that will contain the MD5 signature.
 */

void	md5_buffer(const char *buffer, uint32_t buf_len,
		   void *signature);

/*
 * md5_sig_to_string
 *
 * DESCRIPTION:
 *
 * Convert a MD5 signature in a 16 byte buffer into a hexadecimal string
 * representation.
 *
 * RETURNS:
 *
 * None.
 *
 * ARGUMENTS:
 *
 * signature - a 16 byte buffer that contains the MD5 signature.
 *
 * str - a string of charactes which should be at least 33 bytes long (2
 * characters per MD5 byte and 1 for the \0).
 *
 * str_len - the length of the string.
 */

void	md5_sig_to_string(void *signature, char *str, uint32_t str_len);

/*
 * md5_sig_from_string
 *
 * DESCRIPTION:
 *
 * Convert a MD5 signature from a hexadecimal string representation into
 * a 16 byte buffer.
 *
 * RETURNS:
 *
 * None.
 *
 * ARGUMENTS:
 *
 * signature - A 16 byte buffer that will contain the MD5 signature.
 *
 * str - A string of charactes which _must_ be at least 32 bytes long (2
 * characters per MD5 byte).
 */

void	md5_sig_from_string(void *signature, const char *str);



//#define MD5_CTX			      md5_t
//#define Hash_MD5Init(A)       md5_init(A)
//#define Hash_MD5Update(A,B,C) md5_process(A,B,C)
//#define Hash_MD5Final(A,B)    md5_finish(A,B)

#endif /* ! __MD5_H__ */
