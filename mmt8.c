/* mmt8 - read and handle mmt8 files.
 *
 * (c) Copyright Ricard Wanderlof 2000-2003.
 *
 * Can display names, and change them.
 * V1.0 RW 001109  Large(?) model
 * V1.1 RW 010826  Rebuilt for small model
 * V1.2 RW 010902  New user interface
 * V1.3 RW 010903  Cleanup, handles ctrl-c
 * V1.4 RW 030316  Help texts adapt to MIDI 1 or 0.
 */

#define MIDI 1 /* include midi code */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef __TURBOC__
#include <alloc.h>
#else
#include <malloc.h>
#endif
#if MIDI
#include <dos.h>
#endif

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


#define SYSEX  240         /* MIDI sysex code */
#define EOX    247         /* MIDI eox code */
#define ALESIS_SYX 0x0E    /* Alesis sysex ID */
#define MMT8_SYX 0x00      /* MMT8 ID */

#define MMT8_PARTS 100     /* # parts */
#define MMT8_SONGS 100     /* # songs */
#define MMT8_TRACKS 8	   /* # tracks */
#define SONG99     199     /* special, don't delete */

#define MMT8_NAMELEN 14    /* # chars */

#define MMT8_MEMSTART  0x0400
#define MMT8_MEMEND    0xFF00
#define MMT8_MEMSIZE   (MMT8_MEMEND-MMT8_MEMSTART) /* memory / max dump */
#define OFFSET         MMT8_MEMSTART     /* memory address offset */

#define ALLOCSIZE (MMT8_MEMSIZE+10)  /* size of MMT8 memory + some extra */
#define FF00      (Allocsize+OFFSET-10)

/* midi stuff */

#define INT_ENB 0x08 /* GP02 active = int o/p active */

#define COMBASE 0x3F8 /* presumed base address - recompile if different */
#define COMIRQ  4     /* presumed IRQ */

#define RHR (COMBASE)
#define THR (COMBASE)
#define IER (COMBASE+1)
#define IIR (COMBASE+2)
#define LCR (COMBASE+3)
#define MCR (COMBASE+4)
#define LSR (COMBASE+5)
#define MSR (COMBASE+6)
#define SCR (COMBASE+7)

#define DLL (COMBASE)
#define DLM (COMBASE+1)

#define RDR  1
#define OE   2
#define PE   4
#define FE   8
#define THRE 32

#define ERRBITS 14

#define KBDDATA 0x60
#define KBDSTAT 0x61

#define INTFLAGS 2 /* disable kbd interrutpt only */

#define lastscan (inportb(KBDDATA))

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
  uint bytes;
  uchar tempo;
  char name[MMT8_NAMELEN];
} songhead;

uint Allocsize;

char FAR *mmt8mem;
uint mmt8bytes;
datahead FAR *mmt8head;

char FAR *auxmem;
uint auxbytes;

long midibytes;

char *mainfilename;
char *auxfilename;

char nbuf1[81] = "";
char nbuf2[81] = "";

char changed;      /* file changed ? */
char auxchanged;

char valid;        /* file valid ? */
char auxvalid;

FILE *curfile;   /* global file */

char breaked;

/* midi stuff */

#if MIDI
char rxerrs;
char usemidi;
#endif

void farcpy(char FAR *dest, char FAR *src, uint len)
{
  while (len--)
    *dest++ = *src++;
}


void revcpy(char FAR *dest, char FAR *src, uint len)
{
  src += len;
  dest += len;
  while (len--)
    *--dest = *--src;
}


int byteswap(int d)
{
  return (d << 8) | ((d >> 8) & 255);
}


uint beats(uint bcdbeats)
{
  return (bcdbeats & 0x0F) + ((bcdbeats & 0xF0) >> 4) * 10 +
	 ((bcdbeats & 0xF00) >> 8) * 100;
}


char yesno(char *prompt)
{
  char yes;

  printf("%s (y/n) ?", prompt);
  yes = (toupper(getche()) == 'Y');
  printf("\n");
  return yes;
}


char *fname(char *filename)
{
  return filename[0] ? filename : "<none>";
}


char *skipspace(char *str)
{
  while (*str == ' ')
    str++;
  return str;
}


void swap(void)
{
  char FAR *swapfar;
  char *swapptr;
  uint swapint;
  char swapchar;

  swapfar = mmt8mem;
  mmt8mem = auxmem;
  auxmem = swapfar;

  mmt8head = (datahead FAR *) mmt8mem;

  swapptr = mainfilename;
  mainfilename = auxfilename;
  auxfilename = swapptr;

  swapint = mmt8bytes;
  mmt8bytes = auxbytes;
  auxbytes = swapint;

  swapchar = valid;
  valid = auxvalid;
  auxvalid = swapchar;

  swapchar = changed;
  changed = auxchanged;
  auxchanged = swapchar;
}


int ctrlc(void)
{
  breaked = 1;
  return 1;
}


#if MIDI

/* Init UART */
char m_init(void)
{
  if (peek(0x40, 0) != COMBASE)  /* COM1 base address vector */
    return FAIL;

  /* assume 1 MHz input clock => divide by 2 to get 500 kHz x16 clock */
  outportb(LCR,0x80);           /* divisor latch access */
  outportb(DLM, 0);             /* divisor hi */
  outportb(DLL, 2);             /* divisor lo */

  outportb(LCR, 3);             /* 8N1 */
  /* outportb(MCR, INT_ENB); */      /* set GP OUT 2 low */

  inportb(RHR);                 /* flush RHR */
  inportb(LSR);                 /* clear errors */

  return OK;
}


/* prepare for midi rx */
void m_on(void)
{
  disable();

  inportb(RHR);
  inportb(LSR);
  rxerrs = 0;

  outportb(0x21, inportb(0x21) | INTFLAGS); /* poss. disable kbd & timer */

  enable();
}


/* finish rx */
void m_off(void)
{
  disable();

  outportb(0x21, inportb(0x21) & ~INTFLAGS); /* reenable disabled ints */

  enable();
}


#endif /* MIDI */



char r_getbyte(int *c)
{
  int ch;

#if MIDI
  if (usemidi)
  {
    while ((inportb(LSR) & RDR) == 0)
      if (lastscan)
      {
	printf("Rx cancelled!\n");
	return FAIL;
      }
    rxerrs |= inportb(LSR);
    *c = inportb(RHR);
  }
  else
#endif
  {
    ch = getc(curfile);
    if (ch == EOF)
    {
      printf("Unexpected EOF\n");
      return FAIL;
    }
    *c = ch;
  }
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


char r_mmthead(void)
{
  if (r_expect(SYSEX, "Not a .syx file (F0 missing)?\n")) return FAIL;
  if (r_expect(0, "Bad Sysex ID(1)\n")) return FAIL;
  if (r_expect(0, "Bad Sysex ID(2)\n")) return FAIL;
  if (r_expect(ALESIS_SYX, "Bad Sysex ID(3)\n")) return FAIL;
  if (r_expect(MMT8_SYX, "Bad Sysex ID(4)\n")) return FAIL;

  midibytes = 5;

  return OK;
}


char r_pshead(uint no)
{
  static char *err = "Wrong file type!\n";

  if (r_expect('M', err)) return FAIL;
  if (r_expect('M', err)) return FAIL;
  if (r_expect('T', err)) return FAIL;
  if (r_expect(no >= MMT8_PARTS ? 'S' : 'P', err)) return FAIL;
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


uint r_getdata(char FAR *membuf)
{
  int first, second;
  int count;
  uint byteno;

  count = 0;
  byteno = 0;

  while (1)
  {
    if (byteno > Allocsize)
    {
      printf("File too long!\n");
      return 0;
    }

    if (count == 0)
    {
      if (r_getbyte(&first)) return 0;
      midibytes++;
#if 0
      printf("Input byte %u: %02Xh\n", byteno, first);
#endif
      if (first == EOX) break; /* EOF at first byte in group => done now */
      count = 1;
    }

    if (r_getbyte(&second)) return 0;
    midibytes++;
#if 0
    printf("Input byte %u: %02Xh\n", byteno, second);
#endif
    if (second == EOX) break; /* last byte */

    membuf[byteno] = ((first << count) | (second >> (7-count))) & 255;
    byteno++;

    first = second;

    count = (count+1) & 7;
  }

  return byteno; /* # bytes */
}


char w_byte(int c) /* write byte to file */
{
#if 0
  printf("w_byte: %d\n", c);
#endif

#if MIDI
  if (usemidi)
  {
    while ((inportb(LSR) & THRE) == 0)
      ;
    outportb(THR, c);
  }
  else
#endif

    if (putc(c & 255, curfile) == EOF)
    {
      printf("Write failure\n");
      return FAIL;
    }

  return OK;
}


char w_pshead(uint no)
{
  if (w_byte('M')) return FAIL;
  if (w_byte('M')) return FAIL;
  if (w_byte('T')) return FAIL;
  if (w_byte(no >= MMT8_PARTS ? 'S' : 'P')) return FAIL;
  return OK;
}


char w_psbuf(char FAR *buf, uint len)
{
  while (len--)
    if (w_byte(*buf++)) return FAIL;
  return OK;
}


char w_mmthead(void) /* write sysex header to file */
{
  if (w_byte(SYSEX)) return FAIL;
  if (w_byte(0)) return FAIL;
  if (w_byte(0)) return FAIL;
  if (w_byte(ALESIS_SYX)) return FAIL;
  if (w_byte(MMT8_SYX)) return FAIL;
  return OK;
}


char w_mmttrail(void) /* write sysex trail to file */
{
  if (w_byte(EOX)) return FAIL;
  return OK;
}


char w_putdata(void) /* write memory to file */
{
  int count;
  uint data;
  uint byteno;
  char done;

  byteno = 0;
  count = 1;
  data = 0;
  done = 0;

  while (!done)
  {
    data <<= 8;

    if (byteno < mmt8bytes)
      data |= mmt8mem[byteno++] & 255;
    else
      done = 1; /* was: and data = 0 */

    if (w_byte((data >> count) & 127)) return FAIL;
    if (count == 7 && !done)
    {
      if (w_byte(data & 127)) return FAIL;
      count = 0;
    }

    count++;
  }

  return OK;
}


char r_openfile(char *filename)
{
#if MIDI
  usemidi = 0;
#endif

  printf("Opening input file %s ...\n", filename);

  curfile = fopen(filename, "rb");
  printf(curfile ? "Reading file ... \n" : "Can't open!\n");

  return curfile == NULL;
}


char w_openfile(char *filename)
{
#if MIDI
  usemidi = 0;
#endif

  printf("Creating output file %s ...\n", filename);

  curfile = fopen(filename, "rb");
  if (curfile)
  {
    fclose(curfile);
    if (!yesno("File exists, overwrite"))
      return FAIL;
  }

  curfile = fopen(filename, "wb");
  printf(curfile ? "Writing file ...\n" : "Can't create file!\n");

  return curfile == NULL;
}


void closefile(void)
{
  if (curfile)
  {
    fclose(curfile);
    curfile = NULL;
  }
}


/* load main buffer from file or midi */
char loadfile(char *filename)
{
  mainfilename[0] = '\0';

  if (!mmt8mem)
  {
    if ((mmt8mem = MALLOC(Allocsize)) == NULL)
    {
      printf("Not enough memory to allocate buffer!\n");
      return FAIL;
    }
  }

  mmt8head = (datahead FAR *) mmt8mem;

#if MIDI
  usemidi = 0;
  if (!filename[0]) /* midi */
  {
    usemidi = 1;
    filename = "<midi>";
    delay(200); /* wait for key release */
    printf("Start MMT-8 dump - press ENTER to cancel!\n");
    m_on();
  }
  else
#endif
    if (r_openfile(filename))
      return FAIL;

  strcpy(mainfilename, filename);

  valid = 0;
  mmt8bytes = 0;

  if (!r_mmthead())
    mmt8bytes = r_getdata(mmt8mem);

#if MIDI
  if (usemidi)
  {
    m_off();
    if (rxerrs &= ERRBITS)
    {
      printf("Rx errs (%02Xh)!\007\n", rxerrs);
      mmt8bytes = 0;
    }
  }
  else
#endif
    closefile();

  if (mmt8bytes)
  {
    printf("Read %u bytes (%ld MIDI bytes) of MMT8 data.\n",
	   mmt8bytes, midibytes);
    valid = 1;
    changed = 0;
    return OK;
  }
  return FAIL;
}


/* adjust relevant pointers, plus freemem, freesize and mmt8bytes */
void adjptrs(uint addr, uint delta)
{
  int i;
  uint FAR *a;

  for (i = 0; i < MMT8_PARTS; i++)
  {
    a = &mmt8head->partptrs[i];
    if (byteswap(*a) >= addr)
      *a = byteswap(byteswap(*a) + delta);
  }

  for (i = 0; i < MMT8_SONGS; i++)
  {
    a = &mmt8head->songptrs[i];
    if (byteswap(*a) >= addr)
      *a = byteswap(byteswap(*a) + delta);
  }

  mmt8head->freememptr += delta;
  mmt8head->freememsize -= delta;
  mmt8bytes += delta;
}


/* ptr to MMT8 address ptr */
uint FAR *addrptr(uint no)
{
  if (no < MMT8_PARTS)
    return &mmt8head->partptrs[no];
  else
  {
    no -= MMT8_PARTS;
    return &mmt8head->songptrs[no];
  }
}


/* MMT8 address ptr */
uint psaddress(uint no)
{
  return byteswap(*addrptr(no));
}


/* make space for new part/song */
/* no. must not already exist, returns NULL if no space */
char FAR *insert(uint no, uint size)
{
  uint FAR *addrp; /* ptr to part/songptr */
  uint addr; /* addr of new part */
  uint song = 0;
  int i;

  if ((long) mmt8head->freememptr + size > FF00)
  {
    printf("Not enough memory!\n");
    return NULL;
  }

  addrp = addrptr(no);

  if (no < MMT8_PARTS)
  {
    for (i = no; i < MMT8_PARTS; i++)
    {
      addr = byteswap(mmt8head->partptrs[i]);
      if (addr & 0xFF00) /* exists */
      {
	song = MMT8_PARTS; /* inhibit song scan */
	break;
      }
    }
  }
  else
    song = no - MMT8_PARTS;

  for (i = song; i < MMT8_SONGS; i++)
  {
    addr = byteswap(mmt8head->songptrs[i]);
    if (addr & 0xFF00) /* exists */
      break;
  }

  revcpy(&mmt8mem[addr+size-OFFSET], &mmt8mem[addr-OFFSET],
	 mmt8head->freememptr - addr);
  adjptrs(addr, size);
  *addrp = byteswap(addr);

  return &mmt8mem[addr-OFFSET];
}

/* delete song/part, return FAIL if doesn't exist */
char delete(uint no)
{
  uint FAR *addrp;
  uint addr;
  uint size;

  addrp = addrptr(no);
  addr = byteswap(*addrp);
  if (addr & 0xFF00) /* exists */
  {
    *addrp = 0; /* kill address => doesn't exist */
    size = ((parthead FAR *) (&mmt8mem[addr-OFFSET]))->bytes;
    /* song size is in same relative place */
    adjptrs(addr, -size);
    farcpy(&mmt8mem[addr-OFFSET], &mmt8mem[addr+size-OFFSET],
	   mmt8head->freememptr - addr); /* adjptrs changes freememptr */
  }
  else
    return FAIL;

  return OK;
}


char printname(int no, char askfornew)
{
  uint addr;
  static char name[MMT8_NAMELEN+1];
  parthead FAR *part;
  songhead FAR *song;

  addr = psaddress(no);
  if (addr & 0xFF00)
  {
    if (no < MMT8_PARTS)
    {
      part = (parthead FAR *) &mmt8mem[addr-OFFSET];

      farcpy(name, part->name, MMT8_NAMELEN);
      name[MMT8_NAMELEN] = '\0';
      printf("Part %2d : \"%s\"  %3d beat%c (%d bytes)\n",
	     no, name, beats(part->bcdbeats)-1,
	     (part->bcdbeats == 2) ? ' ' : 's', part->bytes);
    }
    else
    {
      song = (songhead FAR *) &mmt8mem[addr-OFFSET];
      farcpy(name, song->name, MMT8_NAMELEN);
      name[MMT8_NAMELEN] = '\0';
      printf("Song %2d : \"%s\"  %3d BPM (%d bytes)\n",
	     no-MMT8_PARTS, name, song->tempo, song->bytes);
    }
    if (askfornew)
      printf("New name: \"%s\"\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b", name);
    return OK;
  }
  else
  {
    printf("Doesn't exist.\n");
    return FAIL;
  }
}

/* no is 0 or 100 for P or S */
void printused(uint no, char onlyno)
{
  int i;

  for (i = 0; i < MMT8_PARTS; i++)
  {
    if (psaddress(i+no) & 0xFF00)
      if (onlyno)
	printf("%d ", i);
      else
      {
	printname(i+no, 0);
	if (breaked)
	  break;
      }
  }

  if (onlyno)
    printf("\n");
}


char changename(uint no, char *newname)
{
  uint addr;
  char FAR *str;
  int i;
  char end = 0;

  for (i = 0; i < MMT8_NAMELEN; i++)
  {
    if (newname[i] == '\0')
      end = 1;
    if (end)
      newname[i] = ' ';
  }

  addr = psaddress(no);
  if ((addr & 0xFF00) == 0) return FAIL;
  if (no < MMT8_PARTS)
    str = ((parthead FAR *) (&mmt8mem[addr-OFFSET]))->name;
  else
    str = ((songhead FAR *) (&mmt8mem[addr-OFFSET]))->name;

  farcpy(str, newname, MMT8_NAMELEN);

  return OK;
}


uint getpsf(char *cmdstr, char *filename)
{
  char ch;
  uint no;

  if (sscanf(skipspace(cmdstr), filename ? "%c%u%s" : "%c%u",
	     &ch, &no, filename) != (filename ? 3 : 2))
    return 65535U;
  if (no > MMT8_PARTS)
    return 65535U;
  switch (toupper(ch))
  {
    case 'S': no += MMT8_PARTS; /* fall through */
    case 'P': return no;
    default: return 65535U;
  }
}


char notvalid(void)
{
  if (!valid)
    printf("No file loaded!\n");
  return !valid;
}


int main(int argc, char **argv)
{
  static char cmdstr[81];
  static char namebuf[81];
  char endcmd = 0, fail;
  uint no, other, addr, size;
  char FAR *buf, FAR *from;
  long mem;

  mainfilename = nbuf1;
  auxfilename = nbuf2;

  printf("== mmt8 - process MMT-8 midi dumps V1.4 RW 030317\n==\n");

  mem = (CORELEFT() - 1000)/2;
  if (mem < ALLOCSIZE)
  {
    printf("Warning: only %ld bytes per buf!\n", mem);
    Allocsize = (uint) mem;
  }
  else
    Allocsize = ALLOCSIZE;

  if (argc > 1)
    if (argv[1][0] == '-' && toupper(argv[1][1]) == 'H')
    {
      printf("Usage: mmtread [<mmt8file>]\n");
      exit(0);
    }
    else
      if (loadfile(argv[1]))
      exit(1);

#if MIDI
  if (m_init())
  {
    printf("Invalid combase!\n");
    exit(2);
  }
#endif

#ifdef __TURBOC__
  ctrlbrk(ctrlc);
#endif

  while (!endcmd)
  {
    fail = 0;
    printf("Mmt8>");
    gets(cmdstr);
    breaked = 0;
    switch (toupper(cmdstr[0]))
    {
      case 'L':
      case 'U':
	if (notvalid()) break;
	no = 0;
	switch (toupper(cmdstr[1]))
	{
	  case 'S': no = MMT8_PARTS; /* FT */
	  case 'P': printused(no, toupper(cmdstr[0]) == 'U'); break;
	  default: fail = 1;
	}
	break;
      case 'N':
	if (notvalid()) break;
	no = getpsf(&cmdstr[1], NULL);
	if (no == 65535U) fail = 1;
	else
	{
	  if (!printname(no, 1))
	  {
	    gets(cmdstr);
	    if (cmdstr[0])
	    {
	      changename(no, cmdstr);
	      changed = 1;
	    }
	  }
	}
	break;
      case 'D':
	if (notvalid()) break;
	no = getpsf(&cmdstr[1], NULL);
	if (no == 65535U) fail = 1;
	else
	{
	  if (no == SONG99 || delete(no))
	    printf("Doesn't exist!\n");
	  else
	  {
	    printf("Deleted.\n");
	    changed = 1;
	  }
	}
	break;
      case 'I':
	if (notvalid()) break;
	no = getpsf(&cmdstr[1], namebuf);
	if (no == 65535U) fail = 1;
	else if (psaddress(no) & 0xFF00) printf("Already exists!\n");
	else
	{
	  if (sscanf(namebuf, "%u", &other) == 1 && other < MMT8_PARTS)
	  {
	    if (no > MMT8_PARTS) other += MMT8_PARTS; /* if song */
	    swap();
	    addr = psaddress(other);
	    swap();
	    if (addr & 0xFF00)
	    {
	      from = &auxmem[addr-OFFSET];
	      size = ((parthead FAR *) from)->bytes;
	      buf = insert(no, size);
	      if (!buf) break;
	      farcpy(buf, from, size);
	    }
	    else
	    {
	      printf("No such part/song!\n");
	      break;
	    }
	  }
	  else /* from file */
	  {
	    if (r_openfile(namebuf) || r_pshead(no) || (size=r_pslen()) == 0)
	      break;
	    buf = insert(no, size);
	    if (!buf)
	    {
	      closefile();
	      break;
	    }
	    ((parthead FAR *) buf)->bytes = size;
	    if (r_psbuf(buf+2, size-2))
	    {
	      closefile();
	      delete(no);
	      break;
	    }
	    closefile();
	  }
	  printf("Inserted.\n");
	  changed = 1;
	}
	break;
      case 'X':
	if (notvalid()) break;
	no = getpsf(&cmdstr[1], namebuf);
	if (no == 65535U) fail = 1;
	else
	{
	  addr = psaddress(no);
	  if (addr & 0xFF00)
	  {
	    w_openfile(namebuf) || w_pshead(no) ||
	    w_psbuf(&mmt8mem[addr-OFFSET],
		    ((parthead FAR *) &mmt8mem[addr-OFFSET])->bytes);
	    closefile();
	  }
	  else
	    printf("Doesn't exist!\n");
	}
	break;
      case 'S':
	swap(); /* fall through */
	printf("Swapped.\n");
      case 'T':
	printf("Main:%c %s ", changed ? '*' : ' ', fname(mainfilename));
	if (valid)
	  printf("(size %u/%u, free %u) ", mmt8bytes,
		 mmt8head->freememptr-OFFSET, mmt8head->freememsize);

	printf("\nAux: %c %s ", auxchanged ? '*' : ' ', fname(auxfilename));
	if (auxvalid)
	  printf("(size %u)", auxbytes);

	printf("\n");
	break;
      case 'R':
	if (!changed || yesno("File not saved, are you sure"))
	  loadfile(skipspace(&cmdstr[1]));
	break;
      case 'W':
	if (notvalid()) break;
	if (w_openfile(cmdstr[1] ? skipspace(&cmdstr[1]) : mainfilename))
	  break;
	if (w_mmthead() || w_putdata() || w_mmttrail())
	  printf("Write failure!\n");
	closefile();
	changed = 0;
	break;
#if MIDI
      case 'M':
	if (notvalid()) break;
	if (yesno("Tx to MIDI, are you sure"))
	{
	  printf("Transmitting ...\n");
	  usemidi = 1;
	  w_mmthead();
	  w_putdata();
	  w_mmttrail();
	  usemidi = 0;
	}
	break;
#endif
      case 'Q':
	if ((!changed && !auxchanged) ||
	    yesno("Exit without save, are you sure"))
	  endcmd = 1;
	break;
      case 'H':
	printf("H                this list\n");
	printf("L[P|S]           list used parts/songs\n");
	printf("U[P|S]           show used parts/songs\n");
	printf("N[P|S]<no>       set part/song name\n");
	printf("D[P|S]<no>       delete part/song\n");
	printf("I[P|S]<no><file> insert part/song from file\n");
	printf("I[P|S]<no1><no2> insert part/song no1 from aux no2\n");
	printf("X[P|S]<no><file> extract part/song to file\n");
	printf("W[<filename>]    write to [new] file\n");
#if MIDI
	printf("R[<filename>]    read <file> or midi\n");
	printf("M                send to midi\n");
#else
	printf("R<filename>      read <file>\n");
#endif
	printf("S                swap main and aux\n");
	printf("T                display statistics\n");
	printf("Q                quit\n");
      case ' ':
      case '\0':
	break;
      default:
	fail = 1;
    }
    if (fail) printf("???\n");
  }

  return 0;
}

/**************************** END OF FILE *********************************/
