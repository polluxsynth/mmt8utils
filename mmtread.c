#rtinclude <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef __TURBOC__
#include <alloc.h>
#else
#include <malloc.h>
#endif

#define  DEBUG
#undef  DEBUGARG

#ifdef __TURBOC__
#define FAR far
#define HUGE huge
#define MALLOC farmalloc
#define FREE farfree
#else
#define FAR
#define HUGE
#define MALLOC malloc
#define FREE free
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

#define MMT8_NAMELEN 14    /* # chars */


#define MMT8_MEMSIZE (0xFF00-0x400)  /* size of MMT8 memory / max dump */
#define OFFSET 0x400		     /* Start of memory dump */

typedef enum { NONE, PART, SONG } nametype;

typedef struct
{
  int partptrs[MMT8_PARTS];
  char dummy1[0xCF-0xC8];
  int freememptr;
  char dummy2[0xD3-0xD1];
  int freememsize;
  char dummy3[0x102-0xD5];
  int songptrs[MMT8_SONGS];
/*  char dummy4[0x200-0x1CA]; */
} datahead;


typedef struct
{
  int bytes;
  int trackoffset[MMT8_TRACKS];
  int bcdbeats;
  char m_channel[MMT8_TRACKS];
  char name[MMT8_NAMELEN];
} parthead;


typedef struct
{
  int bytes;
  char tempo;
  char name[MMT8_NAMELEN];
} songhead;


char FAR *mmt8mem;
long mmt8bytes;
long midibytes;
datahead FAR *mmt8head;


FILE *infile;   /* global input file */


int byteswap(int d)
{
  return (d << 8) | ((d >> 8) & 255);
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
    if (c != expected)
    {
      printf(errstring, c);
      return FAIL;
    }
  }
  return OK;
}


char r_mmthead(void)
{
  int c;

  if (r_expect(SYSEX, "Not a .syx file (F0 missing)?\n")) return FAIL;
  if (r_expect(0, "Bad Sysex ID(1)\n")) return FAIL;
  if (r_expect(0, "Bad Sysex ID(2)\n")) return FAIL;
  if (r_expect(ALESIS_SYX, "Bad Sysex ID(3)\n")) return FAIL;
  if (r_expect(MMT8_SYX, "Bad Sysex ID(4)\n")) return FAIL;

  midibytes = 5;

  return OK;
}


char r_getdata(void)
{
  int first, second;
  int count;
  long byteno;

  count = 0;
  byteno = 0;

  while (1)
  {

    if (count == 0)
    {
      if (r_getbyte(&first)) return FAIL;
      midibytes++;
#ifdef DEBUG
      printf("Input byte %ld: %02Xh\n", byteno, first);
#endif
      if (first == EOX) break; /* EOF at first byte in group => done now */
      count = 1;
    }

    if (r_getbyte(&second)) return FAIL;
    midibytes++;
#ifdef DEBUG
    printf("Input byte %ld: %02Xh\n", byteno, second);
#endif
    if (second == EOX) break; /* last byte */

    mmt8mem[byteno] = ((first << count) |
                      (second >> (7-count))) & 255;
    byteno++;

    first = second;

    count = (count+1) & 7;
  }

  mmt8bytes = byteno;

  return OK;
}


int w_byte(int c, FILE *file) /* write byte to file */
{
#ifdef FOOTBALL
  printf("w_byte: %d\n", c);
#endif

  if (putc(c & 255, file) == EOF) return FAIL;
  else return OK;
}


char w_mmthead(FILE *file) /* write sysex header to file */
{
  if (w_byte(SYSEX, file)) return FAIL;
  if (w_byte(0, file)) return FAIL;
  if (w_byte(0, file)) return FAIL;
  if (w_byte(ALESIS_SYX, file)) return FAIL;
  if (w_byte(MMT8_SYX, file)) return FAIL;
  return OK;
}


char w_mmttrail(FILE *file) /* write sysex trail to file */
{
  if (w_byte(EOX, file)) return FAIL;
  return OK;
}


char w_putdata(FILE *file) /* write memory to file */
{
  int count;
  unsigned int data;
  long byteno;
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
    {
      done = 1;
      data = 0;
    }

    if (w_byte((data >> count) & 127, file)) return FAIL;
    if (count == 7 && !done)
    {
      if (w_byte(data & 127, file)) return FAIL;
      count = 0;
    }

    count++;
  }

  return OK;
}


void showall(void)
{
  int i;
  int addr;
  static char name[MMT8_NAMELEN+1];

  for (i = 0; i < MMT8_PARTS; i++)
  {
    addr = byteswap(mmt8head->partptrs[i]);
    if (addr & 0xFF00)
    {
      memcpy(name, ((parthead *) (&mmt8mem[addr - OFFSET]))->name,
	     MMT8_NAMELEN);
      name[MMT8_NAMELEN] = '\0';
    }
    else
      strcpy(name, "<none>");

    printf("Part %02d addr: %04Xh, name \"%s\"\n", i, addr, name);
  }

  printf("Free addr: %04Xh\n", mmt8head->freememptr);
  printf("Free size: %04Xh\n", mmt8head->freememsize);

  for (i = 0; i < MMT8_PARTS; i++)
  {
    addr = byteswap(mmt8head->songptrs[i]);
    if (addr & 0xFF00)
    {
      memcpy(name, ((songhead *) (&mmt8mem[addr - OFFSET]))->name,
	     MMT8_NAMELEN);
      name[MMT8_NAMELEN] = '\0';
    }
    else
      strcpy(name, "<none>");

    printf("Song %02d addr: %04Xh, name \"%s\"\n", i, addr, name);
  }
}


char changename(nametype ntype, int no, char *newname)
{
  int addr;
  char *str;
  int i;
  char end = 0;

  if (no < 0) return FAIL;

  for (i = 0; i < MMT8_NAMELEN; i++)
  {
    if (newname[i] == '\0') end = 1;

    if (end || newname[i] == '_')
      newname[i] = ' '; /* spaces used for pad and _ */
  }

  if (ntype == PART)
  {
    if (no > MMT8_PARTS) return FAIL;
    addr = byteswap(mmt8head->partptrs[no]);
    if ((addr & 0xFF00) == 0) return FAIL;
    str = ((parthead *) (&mmt8mem[addr-OFFSET]))->name;
  }
  else
  {
    if (no > MMT8_SONGS) return FAIL;
    addr = byteswap(mmt8head->songptrs[no]);
    if ((addr & 0xFF00) == 0) return FAIL;
    str = ((songhead *) (&mmt8mem[addr-OFFSET]))->name;
  }

  memcpy(str, newname, MMT8_NAMELEN);

  return OK;
}




int main(int argc, char **argv)
{
#ifdef DEBUGARG
#endif

  int i;
  char *infilename = NULL;
  char *outfilename = NULL;
  FILE *outfile;
  char show;
  nametype ntype;
  int ref;
  static char newname[81];

  show = 0;
  ntype = NONE;

  printf("=== mmt10read - read and interpret MMT-8 data V1.0 RW 001109\n===\n");

  for (i = 1; i < argc; i++)
  {
    if (argv[i][0] == '-')
    {
      switch (toupper(argv[i][1]))
      {
	case 'H':
	  fprintf(stderr, "Usage: mmtread [options] <infile> [<outfile>]\n");
	  fprintf(stderr, "Options: -h                this list\n");
          fprintf(stderr, "         -s                show data\n");
          fprintf(stderr, "         -np<no><name>     set part name\n");
	  fprintf(stderr, "         -ns<no><name>     set song name\n");
	  fprintf(stderr, "         (underlines replaced w/ spaces)\n");
	  exit(0);
	  break;
        case 'S':
          show = 1;
          break;
        case 'N':
          if (toupper(argv[i][2]) == 'P')
            ntype = PART;
          else if (toupper(argv[i][2]) == 'S')
            ntype = SONG;
          else
          {
            fprintf(stderr, "-n requires p or s!\n");
            exit(1);
          }
          if (sscanf(&argv[i][3], "%d%s", &ref, newname) != 2)
          {
            fprintf(stderr, "%s: invalid syntax\n", &argv[i][3]);
            exit(1);
          }

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

  mmt8mem = MALLOC(MMT8_MEMSIZE);
  if (mmt8mem == NULL)
  {
    fprintf(stderr, "Not enough memory to allocate buffer!\n");
    exit(1);
  }

  mmt8head = (datahead *) mmt8mem;

  if (infilename)
  {
    infile = fopen(infilename, "rb");
    if (infile == NULL)
    {
      fprintf(stderr, "Can't open input file %s\n", infilename);
      exit(1);
    }
    printf("Reading input file %s ...\n", infilename);
  }
  else
  {
    fprintf(stderr, "No input file specified!\n");
    exit(1);
  }

  if (r_mmthead())
    exit(2);

  if (r_getdata())
  {
    fprintf(stderr, "Unexpected EOF!\n");
    exit(2);
  }

  fclose(infile);


  printf("Read %ld bytes (%ld MIDI bytes) of MMT8 data.\n",
         mmt8bytes, midibytes);


  if (show)
    showall();

  if (ntype != NONE)
    if (changename(ntype, ref, newname))
    {
      fprintf(stderr, "No such %s %d!\n",
              ntype == PART ? "part" : "song", ref);
    }



  if (outfilename)
  {
    printf("Writing output file...\n");

    outfile = fopen(outfilename, "wb");
    if (outfile == NULL)
    {
      fprintf(stderr, "Can't open output file %s\n", outfilename);
      exit(1);
    }
    if (w_mmthead(outfile) | w_putdata(outfile) | w_mmttrail(outfile))
      fprintf(stderr, "Write failure!\n");

    fclose(outfile);
  }

  return 0;
}

/**************************** END OF FILE *********************************/
