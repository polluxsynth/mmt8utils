/* MMTDECOD.C - decode and print out mmt8 data */
/* (c) Copyright 2001 Ricard Wanderlof
/* V1.0  RW 0011?? */
/* V1.1  RW 010828  Small model version */
/* V1.2  RW 010906  Rebuilt for .mmp and .mms files */
/* V1.3  RW 010914  Reversed track order for compatibility with mmtencod */
/* V1.31 RW 010925  Fixed bug in tracknums */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <alloc.h>

#ifdef __TURBOC__
#define FAR far
#define MALLOC farmalloc
#define FREE farfree
#define CORELEFT farcoreleft
#else
#define FAR
#define MALLOC malloc
#define FREE free
#define CORELEFT coreleft
#endif

#define FAIL   1           /* general status return code */
#define OK     0

/* MMT-8 stuff */

#define SYSEX  240         /* MIDI sysex code */
#define EOX    247         /* MIDI eox code */
#define ALESIS_SYX 0x0E    /* Alesis sysex ID */
#define MMT8_SYX 0x00      /* MMT8 ID */

#define MMT8_PARTS 100     /* # parts */
#define MMT8_SONGS 100     /* # songs */
#define MMT8_TRACKS 8	   /* # tracks */

#define MMT8_NAMELEN 14    /* # chars */

#define MMT8_PPQN 96

#define MMT8_MEMSTART  0x0400
#define MMT8_MEMEND    0xFF00
#define MMT8_MEMSIZE   (MMT8_MEMEND-MMT8_MEMSTART) /* memory / max dump */

#define ALLOCSIZE (MMT8_MEMSIZE+10)  /* size of MMT8 memory + some extra */

typedef unsigned int uint;
typedef unsigned char uchar;

typedef enum { NONE, PART, SONG } nametype;

typedef struct
{
  uint partptrs[MMT8_PARTS];
  char dummy1[0xCF-0xC8];
  uint freememptr;
  char dummy2[0xD3-0xD1];
  uint freememsize;
  char dummy3[0x102-0xD5];
  uint songptrs[MMT8_SONGS];
/*  char dummy4[0x200-0x1CA]; */
} datahead;


typedef struct
{
  uint bytes;
  uint trackoffset[MMT8_TRACKS];
  uint bcdbeats;
  char m_channel[MMT8_TRACKS];
  char name[MMT8_NAMELEN];
} parthead;

typedef struct
{
  uchar number;
  uint start;
  uchar amount;
  uchar ch;
  uint dura;
} long_ev;

typedef struct
{
  uchar number;
  uchar amount;
  uchar ch;
  uint dura;
} short_ev;

typedef union
{
  short_ev five;
  long_ev seven;
} ev;


typedef struct
{
  uint bytes;
  uchar tempo;
  char name[MMT8_NAMELEN];
} songhead;


uchar FAR *buffer;
uint bufbytes;
uint Allocsize;

FILE *infile;
FILE *outfile;


void farcpy(char FAR *dest, char FAR *src, int len)
{
  while (len--)
    *dest++ = *src++;
}


uint byteswap(uint d)
{
  return (d << 8) | ((d >> 8) & 255);
}


char yesno(char *prompt)
{
  char yes;

  printf("%s (y/n) ?", prompt);
  yes = (toupper(getche()) == 'Y');
  printf("\n");
  return yes;
}



char r_getbyte(int *c)
{
  int ch;

  ch = getc(infile);
  if (ch == EOF)
  {
    printf("Unexpected EOF\n");
    return FAIL;
  }
  *c = ch;
  return OK;
}


char r_expect(int expected, const char *errstring)
{
  int c;

  if (!r_getbyte(&c))
  {
    if (c == expected)
      return OK;
    else
      printf(errstring, c);
  }
  return FAIL;
}


char r_pshead(nametype *ntype)
{
  static char *err = "Wrong file type!\n";
  int ch;

  if (r_expect('M', err)) return FAIL;
  if (r_expect('M', err)) return FAIL;
  if (r_expect('T', err)) return FAIL;
  if (r_getbyte(&ch)) return FAIL;
  switch (ch)
  {
    case 'P': *ntype = PART; break;
    case 'S': *ntype = SONG; break;
    default: printf(err); return FAIL;
  }
  return OK;
}


uint r_pslen(void)
{
  int ch;
  int len;

  if (r_getbyte(&len)) return 0;
  if (r_getbyte(&ch)) return 0;
  len |= (ch << 8);
  return len;
}


char r_psbuf(char FAR *buf, uint len)
{
  int ch;

  while (len--)
  {
    if (r_getbyte(&ch)) return FAIL;
    *buf++ = ch;
  }
  return OK;
}


char r_openfile(char *filename, char *mode)
{
  printf("Opening input file %s ...\n", filename);

  infile = fopen(filename, mode);
  printf(infile ? "Reading file ... \n" : "Can't open!\n");

  return infile == NULL;
}


char w_openfile(char *filename, char *mode)
{
  printf("Creating output file %s ...\n", filename);

  outfile = fopen(filename, "rb");
  if (outfile)
  {
    fclose(outfile);
    if (!yesno("File exists, overwrite"))
      return FAIL;
  }

  outfile = fopen(filename, mode);
  printf(outfile ? "Writing file ...\n" : "Can't create file!\n");

  return outfile == NULL;
}


void r_closefile(void)
{
  if (infile)
    fclose(infile);
  infile = NULL;
}


void w_closefile(void)
{
  if (outfile)
    fclose(outfile);
  outfile = NULL;
}


/* decode routines */

int bendamt(uint val)
{
  val = (val & 0x7F) | ((val & 0x7F00) >> 1);
  return (int) val - 8192;
}


char *printev(uchar number, uchar ch, uchar amount, uint dura)
{
  char *retval = NULL;

  if (number == 0x80) /* END = 0x80 */
  {
    fprintf(outfile, "END");
    if (amount != 0 | ch != 0x80 | dura != 0)
      retval = "Bad end format";
  }
  else /* regular event */
  {
    number &= 127; /* was if &0x80, &= start = 65k */

    if (amount & 128) /* non-note */
    {
      amount &= 127;
      if (number <= 121) /* controller */
	fprintf(outfile, "CH %-2d CTRL %-3d VAL %-3d ", ch+1, number, amount);
      else
	switch (number)
	{
	  case 122:
	    fprintf(outfile, "CH %-2d PRGM %-3d ", ch+1, amount);
	    break;
	  case 123:
	    fprintf(outfile, "CH %-2d AFTR %-3d ", ch+1, amount);
	    break;
	  case 124:
	    fprintf(outfile, "CH %-2d BEND %-5d ", ch+1, bendamt(dura));
	    break;
	  case 125:
	    fprintf(outfile, "SYSX %-3d ", amount);
	    if (ch & 128) fprintf(outfile, "EOX ");
	    else
	    {
	      fprintf(outfile, "%-3d ", ch & 127);
	      if (dura & 32768U) fprintf(outfile, "EOX ");
	      else fprintf(outfile, "%-3d ", (dura >> 8) & 127);
	    }
	    break;
	  default:
	    retval = "Unrecognized event type";
	    break;
	}
    }
    else /* note */
    {
      dura = byteswap(dura);
      fprintf(outfile, "CH %-2d NOTE %-3d VEL %-3d DURA %3u/%-3u ",
	      ch+1, number, amount, dura / MMT8_PPQN, dura % MMT8_PPQN);
    }

  } /* endif-else regular event */

  fprintf(outfile, "\n");

  return retval;
}


void printtrack(uchar FAR *trackptr, int number, int ch, uint len)
{
  ev FAR *eventptr;
  char *error;
  uint start;

  fprintf(outfile, "TRACK %d CH %d\n", number, ch);
  fprintf(outfile, "; len %u bytes\n", len);

  do
  {
    eventptr = (ev FAR *) trackptr;
    if (*trackptr & 0x80) /* long event type */
    {
      if (len < 7)
      {
	fprintf(outfile, "; Not enough track length for long event.\n");
	return;
      }
      len -= 7;
      trackptr += 7;

      start = eventptr->seven.start;
      fprintf(outfile, "AT %3u/%-4u", start / MMT8_PPQN, start % MMT8_PPQN);
      error = printev(eventptr->seven.number, eventptr->seven.ch,
		      eventptr->seven.amount, eventptr->seven.dura);
      if (error)
      {
	fprintf(outfile, "; %s (%02Xh %02Xh %02Xh %02Xh %02Xh %02Xh %02Xh)\n",
		trackptr[0], trackptr[1], trackptr[2], trackptr[3],
		trackptr[4], trackptr[5], trackptr[6]);
      }
    }
    else /* short event type */
    {
      if (len < 5)
      {
	fprintf(outfile, "; Not enough track length for short event.\n");
	return;
      }
      len -= 5;
      trackptr += 5;
      fprintf(outfile, "%11s", "");
      error = printev(eventptr->five.number, eventptr->five.ch,
		      eventptr->five.amount, eventptr->five.dura);
      if (error)
      {
	fprintf(outfile, "; %s (%02Xh %02Xh %02Xh %02Xh %02Xh)\n",
		trackptr[0], trackptr[1], trackptr[2], trackptr[3],
		trackptr[4]);
      }
    }
  } while (eventptr->five.number != 0x80); /* end */
}


char *namestring(char FAR *name)
{
  static char str[MMT8_NAMELEN+1];

  farcpy(str, name, MMT8_NAMELEN);
  str[MMT8_NAMELEN] = '\0';
  return str;
}



uint beats(uint bcdbeats)
{
  return (bcdbeats & 0x0F) + ((bcdbeats & 0xF0) >> 4) * 10 +
	 ((bcdbeats & 0xF00) >> 8) * 100;
}


/* Create tracks specifier string for song step */
char *tracknums(char tracks)
{
  int i;
  int bit = 1;
  static char buf[MMT8_TRACKS+1];

  for (i = 0; i < 8; i++)
  {
    buf[i] = (tracks & bit) ? '1'+i : '-';
    bit <<= 1;
  }
  buf[i] = '\0';

  return buf;
}



void printpart(parthead FAR *partptr)
{
  int track;

  fprintf(outfile, "PART NAME \"%s\"  BEATS %u\n",
	  namestring(partptr->name), beats(partptr->bcdbeats)-1);
  fprintf(outfile, "; len %u bytes\n", partptr->bytes);

  for (track = MMT8_TRACKS; track > 0; track--)
    printtrack(&buffer[partptr->trackoffset[MMT8_TRACKS - track]],
	       track, partptr->m_channel[MMT8_TRACKS - track],
	       ((track == 1) ? partptr->bytes :
			       partptr->trackoffset[MMT8_TRACKS+1-track]) -
	       partptr->trackoffset[MMT8_TRACKS - track]);
}


void printsong(songhead FAR *songptr)
{
  uchar FAR *data;
  int len = songptr->bytes;

  fprintf(outfile, "SONG NAME \"%s\"  TEMPO %u\n",
	  namestring(songptr->name), songptr->tempo);
  fprintf(outfile, "; len %u bytes\n", len);

  len -= sizeof(songhead);
  data = ((char FAR *) songptr) + sizeof(songhead);

  while (len--)
  {
    if (data[0] == 255)
    {
      fprintf(outfile, "END\n");
      if (len)
      {
	fprintf(outfile, "; premature song end\n");
	break;
      }
    }
    else
    {
      if (len < 1)
      {
	fprintf(outfile, "; not enough song length for event\n");
	break;
      }
      len--;
      fprintf(outfile, "PART %2u  TRACKS %s\n", data[0], tracknums(data[1]));
      data += 2;
    }
  }
  if (data[0] != 255)
    fprintf(outfile, "; song end missing\n");
}


int main(int argc, char **argv)
{
  int i;
  char *infilename = NULL;
  char *outfilename = NULL;
  nametype ntype;
  long mem;

  ntype = NONE;

  printf("== mmtdecod - read and interpret MMT-8 data V1.31 RW 010925\n==\n");

  for (i = 1; i < argc; i++)
  {
    if (argv[i][0] == '-')
    {
      switch (toupper(argv[i][1]))
      {
	case 'H':
	  fprintf(stderr, "Usage: mmtread [options] <infile> [<outfile>]\n");
	  fprintf(stderr, "Options: -h                this list\n");
	  exit(0);
	  break;
	default:
	  fprintf(stderr, "Unknown option: %s\n", argv[i]);
	  exit(1);
	  break;
      }
    }
    else if (infilename == NULL)
      infilename = argv[i];
    else if (outfilename == NULL)
      outfilename = argv[i];
  } /* eofor args */


  mem = CORELEFT()-1000;
  if (mem < ALLOCSIZE)
  {
    printf("Warning: only %ld bytes for buf!\n", mem);
    Allocsize = (uint) mem;
  }
  else
    Allocsize = ALLOCSIZE;

  buffer = MALLOC(Allocsize);
  if (buffer == NULL)
  {
    fprintf(stderr, "Buffer allocation failed!\n");
    exit(1);
  }

  if (infilename)
  {
    if (r_openfile(infilename, "rb"))
      exit(1);
  }
  else
  {
    fprintf(stderr, "No input file specified!\n");
    exit(1);
  }

  if (r_pshead(&ntype) || (bufbytes = r_pslen()) == 0)
    exit(2);

  if (bufbytes > Allocsize)
  {
    printf("File size (%u) too long for buffer (%u)!\n",
	   bufbytes, Allocsize);
    exit(2);
  }

  ((parthead FAR *) buffer)->bytes = bufbytes; /* same for songhead */

  if (r_psbuf(buffer+2, bufbytes-2))
    exit(2);

  r_closefile();

  printf("Read %u bytes of %s data.\n",
	 bufbytes, ntype == PART ? "part" : "song");

  if (outfilename)
  {
    printf("Writing output file...\n");

    if (w_openfile(outfilename, "wt"))
      exit(2);
  }
  else
    outfile = stdout;

  if (ntype == PART)
    printpart((parthead FAR *) buffer);
  else
    printsong((songhead FAR *) buffer);

  w_closefile();

  return 0;
}

/**************************** END OF FILE *********************************/

