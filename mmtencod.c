/* MMTENCOD.C - encode mmt8 data */
/* (c) Copyright 2001 Ricard Wanderlof
/* V1.0  010909  Built from mmtdecod.c */
/* V1.1  010924  Implemented song */
/* V1.2  010928  Overflow, count and general cleanup */

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

/* pattern codes */
#define MMT8_END  0x80
#define MMT8_PRGM 122
#define MMT8_AFTR 123
#define MMT8_BEND 124
#define MMT8_SYSX 125

#define MK_CLK(BEAT, TICK) ((BEAT) * MMT8_PPQN + (TICK))

#define MK_BEAT(CLK)       ((CLK) / MMT8_PPQN)
#define MK_TICK(CLK)       ((CLK) % MMT8_PPQN)

#define MMT8_MEMSTART  0x0400
#define MMT8_MEMEND    0xFF00
#define MMT8_MEMSIZE   (MMT8_MEMEND-MMT8_MEMSTART) /* memory / max dump */

#define SLACK 10    /* extra slack in allocated buffer */
#define ALLOCSIZE (MMT8_MEMSIZE - sizeof(datahead) + SLACK)

typedef unsigned int uint;
typedef unsigned char uchar;

typedef struct
{
  uint partptrs[MMT8_PARTS];
  char dummy1[0xCF-0xC8];
  uint freememptr;
  char dummy2[0xD3-0xD1];
  uint freememsize;
  char dummy3[0x102-0xD5];
  uint songptrs[MMT8_SONGS];
  char dummy4[0x200-0x1CA];
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
  uint bytes;
  uchar tempo;
  char name[MMT8_NAMELEN];
} songhead;


typedef union
{
  parthead part;
  songhead song;
} head;

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

typedef enum { NONE, PART, SONG } nametype;


uchar FAR *buffer;
uchar FAR *bufp;
head  FAR *bufhead;
uchar FAR *trackp;
uint bufbytes;
uint Allocsize;
uint linecount;

FILE *infile;
FILE *outfile;

char inbuf[81];

typedef enum { S_OBJ, S_SONG, S_TRACK, S_EVENT } read_s;

read_s readstate;
uint readtrack;
uint readbeats;
nametype readtype;

typedef struct
{
  char *str;
  int match;
} templ;

#define T_NOTE    0
#define T_CTRL    1
#define T_BEND    2
#define T_AFTR    3
#define T_PRGM    4
#define T_SYX3    5
#define T_SYX2    6
#define T_SYX1    7
#define T_END     8
#define TEMPLATES 9

templ p_templates[TEMPLATES] =
{
  { "AT %u/%u CH %u NOTE %u VEL %u DURA %u/%u", 5 },
  { "AT %u/%u CH %u CTRL %u VAL %u", 3 },
  { "AT %u/%u CH %u BEND %d", 2 },
  { "AT %u/%u CH %u AFTR %u", 2 },
  { "AT %u/%u CH %u PRGM %u", 2 },
  { "AT %u/%u SYSX %u %u %u", 3 },
  { "AT %u/%u SYSX %u %u", 2 },
  { "AT %u/%u SYSX %u", 1 },
  { "AT %u/%u", 0 },
};

#define AT_OFFSET 9 /* size of AT %u/%u */

char nmap[TEMPLATES] = /* map template no to number for non-note events */
  {   0,   0, 124, 123, 122, 125, 125, 125, 0 };
 /* note ctrl bend aftr prgm syx1 syx2 syx3 end */


#define T_PART  0
#define T_SONG  1
#define T_OBJS  2

char *t_templates[T_OBJS] =
{
  "PART NAME \"%c%c%c%c%c%c%c%c%c%c%c%c%c%c\" BEATS %u",
  "SONG NAME \"%c%c%c%c%c%c%c%c%c%c%c%c%c%c\" TEMPO %u"
};


void farcpy(char FAR *dest, char FAR *src, uint len)
{
  while (len--)
    *dest++ = *src++;
}


uint byteswap(uint d)
{
  return (d << 8) | ((d >> 8) & 255);
}


uint bcdb(uint beats)
{
  uint bcd;

  bcd = beats % 10;
  beats /= 10;
  bcd |= (beats % 10) << 4;
  beats /= 10;
  bcd |= (beats % 10) << 8;
  return bcd;
}


char yesno(char *prompt)
{
  char yes;

  printf("%s (y/n) ?", prompt);
  yes = (toupper(getche()) == 'Y');
  printf("\n");
  return yes;
}


char w_byte(int c) /* write byte to outfile */
{
#if 0
  printf("w_byte: %d\n", c);
#endif

  if (putc(c & 255, outfile) == EOF)
  {
    printf("Write failure\n");
    return FAIL;
  }

  return OK;
}


char w_pshead(nametype ntype)
{
  if (w_byte('M')) return FAIL;
  if (w_byte('M')) return FAIL;
  if (w_byte('T')) return FAIL;
  if (w_byte((ntype == SONG) ? 'S' : 'P')) return FAIL;
  return OK;
}


char w_psbuf(char FAR *buf, uint len)
{
  while (len--)
    if (w_byte(*buf++)) return FAIL;
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


char *skipspace(char *str)
{
  while (*str == ' ')
    str++;
  return str;
}


char skipcomment(char *buf)
{
  if (buf[0] == '\0' || buf[0] == ';')
    return 1;
  return 0;
}


/* Set correct trackoffset for next track or size of whole part, depending
 * on which track is being compiled.
 */
void fixlength(void)
{
  uint trackoffs = (uint) (bufp - (char FAR *) bufhead);

  if (readtrack > 1)
    /* start of next (lower) track */
    bufhead->part.trackoffset[MMT8_TRACKS+1-readtrack] = trackoffs;
  else
    bufhead->part.bytes = trackoffs;
}


char r_event(char *buf)
{
  int template, conv;
  uint at1, at2, chan, val1, val2, dura1, dura2;
  uint when, dura;
  static uint prevwhen = 0;
  char number, amount, ch, atexists;
  ev FAR *eventp = (ev FAR *) bufp;

  atexists = (buf[0] && buf[0] == 'A' && buf[1] == 'T');

  for (template = 0; template < TEMPLATES; template++)
  {
    if (atexists)
    {
      conv = sscanf(buf, p_templates[template].str,
		    &at1, &at2, &chan, &val1, &val2, &dura1, &dura2);
      if (conv != p_templates[template].match + 2)
	continue;
      when = MK_CLK(at1, at2);
    }
    else
    {
      conv = sscanf(buf, p_templates[template].str+AT_OFFSET-1,
		    &chan, &val1, &val2, &dura1, &dura2);
      if (conv != p_templates[template].match)
	continue;
    }
    /* now have valid template# and # converted args [after AT] in conv */

    amount = 0; dura = 0;
    ch = (chan - 1) & 15;

    switch (template)
    {
      case T_END:
	if (!atexists ||                                      /* no AT */
	    strncmp(skipspace(inbuf+AT_OFFSET), "END", 3)) /* no END */
	  continue; /* end is last template => exit from loop => error */
	number = 0x80;
	ch = 0x80;
	break;
      case T_NOTE:
	number = val1 & 127; /* note number */
	amount = val2 & 127; /* vel */
	dura = byteswap(MK_CLK(dura1, dura2));
	break;
      case T_CTRL:
	number = val1 & 127; /* controller no */
	amount = val2 | 128; /* value */
	break;
      case T_AFTR:
	number = MMT8_AFTR;
	amount = val1 | 128;
	break;
      case T_PRGM:
	number = MMT8_PRGM;
	amount = val1 | 128;
	break;
      case T_BEND:
	number = MMT8_BEND;
	amount = 128; /* bit 7 set */
	val1 += 8192;
	dura = (val1 & 127) + ((val1 << 1) & 0x7F00);
	break;
      case T_SYX3:
	number = MMT8_SYSX;
	amount = chan | 128; /* 1. val */
	ch = val1 & 127;   /* 2. val */
	dura = (val2 & 127) << 8; /* 3. val */
	break;
      case T_SYX2:
	number = MMT8_SYSX;
	amount = chan | 128;
	ch = val1 & 127;
	dura = 32768U;
	break;
      case T_SYX1:
	number = MMT8_SYSX;
	amount = chan | 128;
	ch = 128;
	break;
      default:
	printf("Invalid template %d?!\n", template);
	return FAIL;
    }
    if (atexists)
    {
      eventp->seven.number = number | 0x80;
      eventp->seven.start = when;
      eventp->seven.ch = ch;
      eventp->seven.amount = amount;
      eventp->seven.dura = dura;
      bufp += sizeof(long_ev);
    }
    else
    {
      eventp->five.number = number;
      eventp->five.ch = ch;
      eventp->five.amount = amount;
      eventp->five.dura = dura;
      bufp += sizeof(short_ev);
    }
    if (template == T_END)
    {
      prevwhen = 0; /* prepare for next track */
      fixlength();
      if (readtrack > 1)
      {
	readstate = S_TRACK;
	readtrack--;
      }
      else
	readstate = S_OBJ;
      if (MK_CLK(readbeats, 0) != when)
      {
	printf("End time differs from part length!\n");
	return FAIL;
      }
    }
    else if (atexists)
    {
      if (prevwhen && when <= prevwhen)
      {
	printf("Non-advancing event time!\n");
	prevwhen = when; /* only cause error once! */
	return FAIL;
      }
      prevwhen = when;
    }
    return OK;
  } /* endfor */

  printf("Unknown event type\n");
  return FAIL;
}


char r_track(char *buf)
{
  uint track, ch;

  if (sscanf(buf, "TRACK %u CH %u", &track, &ch) != 2 || track != readtrack)
  {
    printf("Expecting track specifier for track %u!\n", readtrack);
    return FAIL;
  }
  bufhead->part.m_channel[MMT8_TRACKS - readtrack] = ch;
  readstate = S_EVENT;
  trackp = bufp;
  return OK;
}


char r_song(char *buf)
{
  uint partno;
  char tr[8];
  char tracks = 0;
  int i;
  int bit = 1;

  if (sscanf(buf, "PART %u TRACKS %c%c%c%c%c%c%c%c", &partno,
	     &tr[0], &tr[1], &tr[2], &tr[3],
	     &tr[4], &tr[5], &tr[6], &tr[7]) != 9)
  {
    if (strncmp(buf, "END", 3))
    {
      printf("Expecting track step specifier!\n");
      return FAIL;
    }
    *bufp++ = 255;
    bufhead->song.bytes = (uint) (bufp - (char FAR *) bufhead);
    readstate = S_OBJ;
    return OK;
  }

  for (i = 0; i < 8; i++)
  {
    if (tr[i] == '1'+i)
      tracks |= bit;
    else if (tr[i] != '-')
    {
      printf("Bad tracks specifier!\n");
      return FAIL;
    }
    bit <<= 1;
  }

  *bufp++ = partno % 100;
  *bufp++ = tracks;

  return OK;
}


char r_object(char *buf)
{
  char name[MMT8_NAMELEN];
  uint val;
  int template;

  bufhead = (head FAR *) bufp;

  for (template = 0; template < T_OBJS; template++)
  {
    if (sscanf(buf, t_templates[template],
	       &name[0], &name[1], &name[2], &name[3],
	       &name[4], &name[5], &name[6], &name[7],
	       &name[8], &name[9], &name[10], &name[11],
	       &name[12], &name[13], &val) != 15)
      continue;

    if (template == T_PART)
    {
      farcpy(bufhead->part.name, name, MMT8_NAMELEN);
      readbeats = val;
      bufhead->part.bcdbeats = bcdb(readbeats+1);
      bufp += sizeof(parthead);
      bufhead->part.trackoffset[0] = sizeof(parthead); /* start of track 8 */
      readtype = PART;
      readstate = S_TRACK;
      readtrack = MMT8_TRACKS;
      return OK;
    }
    else /* T_SONG */
    {
      farcpy(bufhead->song.name, name, MMT8_NAMELEN);
      bufhead->song.tempo = val;
      bufp += sizeof(songhead);
      readtype = SONG;
      readstate = S_SONG;
      return OK;
    }
  } /* eofor */
  printf("No song or part!\n");
  return FAIL;
}


char r_line(char *buf)
{
  switch (readstate)
  {
    case S_OBJ: return r_object(buf);
    case S_SONG: return r_song(buf);
    case S_TRACK: return r_track(buf);
    case S_EVENT: return r_event(buf);
    default: printf("Invalid state %d\n", readstate);
  }
  return FAIL;
}


char r_infile(void)
{
  bufp = buffer;
  readstate = S_OBJ;

  while (fgets(inbuf, 80, infile))
  {
    linecount++;
    if (!skipcomment(inbuf))
    {
      if (r_line(inbuf))
	printf(" In line %d: %s", linecount, inbuf);
      if (bufp - buffer > Allocsize - SLACK)
      {
	printf("Resulting object too large!\n");
	return FAIL;
      }
    }
  }

  if (readstate != S_OBJ)
  {
    printf("unexpected EOF\n");
    return FAIL;
  }

  return OK;
}


int main(int argc, char **argv)
{
  int i;
  char *infilename = NULL;
  char *outfilename = NULL;
  long mem;

  printf("== mmtencod - compile MMT-8 data V1.2 RW 010928\n==\n");

  for (i = 1; i < argc; i++)
  {
    if (argv[i][0] == '-')
    {
      switch (toupper(argv[i][1]))
      {
	case 'H':
	  fprintf(stderr, "Usage: mmtencod [options] <infile> [<outfile>]\n");
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
    if (r_openfile(infilename, "rt"))
      exit(1);
  }
  else
  {
    fprintf(stderr, "No input file specified!\n");
    exit(1);
  }

  /* read text file and interpret */

  r_infile();

  r_closefile();

  printf("Read %u lines.\n", linecount);

  bufbytes = bufhead->part.bytes; /* same for song */

  /* write output file */

  if (outfilename)
  {
    printf("Writing output file...\n");

    if (w_openfile(outfilename, "wb"))
      exit(2);
  }
  else
  {
    printf("No output file.\n");
    exit(0);
  }

  /* write buffer */

  if (w_pshead(readtype) || w_psbuf(buffer, bufbytes))
    printf("Error writing output file!\n");
  else
    printf("Wrote %u bytes of %s data.\n",
	   bufbytes, readtype == PART ? "part" : "song");

  w_closefile();


  return 0;
}

/**************************** END OF FILE *********************************/

