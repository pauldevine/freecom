/* $Id$
 * COPY.C -- Internal Copy Command
 *
 * 1999/05/10 ska
 * rewritten, based upon previous COPY.C of FreeCom v0.76b
 *
 * Known bugs:
 *  + Multiple '+' plus signs are scanned as a single one.
 *
 * 1999/07/08 ska
 * bugfix: destination is a drive letter only
 *
 * 2000/07/17 Ron Cemer
 * bugfix: destination ending in "\\" must be a directory, but fails
 *	within dfnstat()
 *
 * 2000/07/24 Ron Cemer
 * bugfix: Suppress "Overwrite..." prompt if destination is device
 *
 * 2001/02/17 ska
 * add: interactive command flag
 
 * bugfix: copy 1 + 2 + 3 <-> only first and last file is stored
 */

#include "../config.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>

#include <portable.h>

/*#define DEBUG*/

#include "dfn.h"
#include "suppl.h"
#include "supplio.h"

#include "../include/lfnfuncs.h"
#include "../include/command.h"
#include "../include/cmdline.h"
#include "../err_fcts.h"
#include "../include/misc.h"
#include "../strings.h"
#include "../include/openf.h"

#define ASCII 1
#define BINARY 2
#define IS_DIRECTORY 5

static struct CopySource {
  struct CopySource *nxt;   /* next source */
  struct CopySource *app;   /* list of files to append */
  int flags;          /* ASCII / Binary */
  char *fnam;         /* filename */
} *head, *last, *lastApp;


static int appendToFile; /* Append the next file rather than new source */
static char *destFile;     /* destination file/directory/pattern */
static int destFlags;

static int optY, optV, optA, optB;

optScanFct(opt_copy)
{
  (void)arg;
  switch(ch) {
  case 'Y': return optScanBool(optY);
  case 'V': return optScanBool(optV);
  case 'A': case 'B': return E_Ignore;
  }
  optErr();
  return E_Useage;
}

optScanFct(opt_copy1)
{
  int ec, *opt = NULL, *optReset = NULL;

  (void)arg;
  switch(ch) {
#ifndef NDEBUG
  default:
    fprintf(stderr, "Invalid option: file '%s' in line %d\n"
      , __FILE__, __LINE__);
    abort();
#endif
  case 'A': opt = &optA; optReset = &optB; break;
  case 'B': opt = &optB; optReset = &optA; break;
  }
  if((ec = optScanBool(*opt)) == E_None
   && *opt)
    *optReset = 0;

  return ec;
}


static void initContext(void)
{
  appendToFile = 0;
  last = lastApp = 0;
}

static void killContext(void)
{
  if(last) {
    assert(head);
    do {
      if((lastApp = head->app) != 0) do {
        lastApp = (last = lastApp)->app;
        free(last);
      } while(lastApp);
      head = (last = head)->nxt;
      free(last);
    } while(head);
  }
}

/*
	faster copy, using large (far) buffers
*/

/*
	a) this copies data, using a 60K buffer
	b) if transfer is slow (or on a huge file),
	   indicate some progress
*/

static int BIGcopy(int fdout, int fdin, int asc)
{
	char far *buffer;
	unsigned size;
	unsigned rd;
	int retval = 0;
								/* stat stuff */
	unsigned startTime, lastTime=0, now, doStat = 0, deviceIn;
#ifdef __GNUC__
	/* VICTOR9000: statistics disabled to avoid 32-bit division crash */
	unsigned long copied = 0;
#else
	unsigned long copied = 0, toCopy;
#endif
	char *statString;
	char far *ctrlz;
	volatile unsigned short far *dbg = (unsigned short far *)0xF0000000UL;

	/* Debug: puts to confirm BIGcopy entry */
	// // puts("[BIG]");

#ifndef __GNUC__
	toCopy = filelength(fdin);
#endif
	// // puts("[FLEN]");

	dbg[20] = 0x0F00 | 'K';  /* BIGcopy entered */

	/* Fetch the largest available buffer */
	for(size = 60*1024u; size != 0; size -= 4*1024) {
		dbg[21] = 0x0F00 | 'L';  /* before DOSalloc */
#ifdef FARDATA
		/* use last-fit allocation to work well with large model */
		buffer = MK_SEG_PTR(void, DOSalloc(size/16,2));
#else
		buffer = MK_SEG_PTR(void, DOSalloc(size/16,0));
#endif
		dbg[22] = 0x0F00 | 'M';  /* after DOSalloc */
		if(buffer != NULL)
			goto ok;
	}
	return 3;	/* out of memory error */

ok:
	dbg[23] = 0x0F00 | 'N';  /* DOSalloc succeeded */
	dprintf( ("[MEM: BIGcopy() allocate %u bytes @ 0x%04x]\n"
	 , size, FP_SEG(buffer)) );
	deviceIn = isadev(fdin);
	statString = getString(deviceIn
		? TEXT_COPY_COPIED_NO_END
		: TEXT_COPY_COPIED);
	startTime = *(unsigned far *)MK_FP(0x40,0x6c);

	dbg[24] = 0x0F00 | 'O';  /* before read loop */
	ctrlz = 0;
	while((rd = farread(fdin, buffer, size)) != 0) {
		if(rd == 0xffff) {
			retval = 1;
			goto _exit;
		}

		if(asc) {
			ctrlz = _fmemchr(buffer, 0x1a, rd);
			if(ctrlz != 0)
				rd = (unsigned)(ctrlz - buffer);
		}
		
		if(farwrite(fdout, buffer, rd) != rd) {
			if(!isadev(fdout)) retval = 2;
			goto _exit;
		}
			
						/* statistics */
#ifdef __GNUC__
		/* VICTOR9000 FIX: Manual 32-bit addition to avoid __addsi3 library call */
		{
			union {
				unsigned long l;
				struct { unsigned int lo, hi; } w;
			} sum;
			sum.l = copied;
			sum.w.lo += rd;
			if (sum.w.lo < rd) sum.w.hi++;  /* carry */
			copied = sum.l;
		}
#else
		copied += rd;
#endif	
			
		now = *(unsigned far *)MK_FP(0x40,0x6c);
		
		if(!doStat
		 && now - startTime > 15 * 18
		 && isatty(fileno(stdout)))
			doStat = TRUE;
		
		if(now - lastTime > 18) {
#ifdef __GNUC__
			/* VICTOR9000 FIX: Skip statistics display to avoid __udivsi3 crash */
			(void)doStat;
			(void)statString;
#else
			if(doStat)
				printf(statString, copied/1024, toCopy/1024);
#endif
				
			if(cbreak) {
				retval = 3;
				goto _exit;
			}	
				
			lastTime = now;
		}
		if(ctrlz || (rd < size && !(deviceIn && asc))) break;
	}	
		
_exit:		
	if(doStat)
		printf("%30s\r","");
		
	dprintf( ("[MEM: BIGcopy() release memory @ 0x%04x]\n"
	 , FP_SEG(buffer)) );
	DOSfree(FP_SEG(buffer));
	free(statString);
	return retval;
}

static int is_valid_disk(int tstdsk)
{
  int savdsk = getdisk();
  int newdsk;

  /* Change to new disk */
  setdisk(tstdsk);
  newdsk = getdisk();

  /* Restore */
  setdisk(savdsk);

  return (newdsk == tstdsk);
}

static int copy(char *dst, char *pattern, struct CopySource *src
  , int openMode)
{ struct dos_ffblk ff;
  struct CopySource *h;
  char rDest[MAXPATH], rSrc[MAXPATH];
  int fdin, fdout;
  int rc;
  FLAG keepFTime;
#if defined(__WATCOMC__) && __WATCOMC__ < 1280
  unsigned short date, time;
#elif defined(__TURBOC__)
  struct ftime fileTime;
#else
  unsigned date, time;
#endif
  char *srcFile;
  FLAG wildcarded;
  /*FLAG isfirst = 1;*/
  FLAG singleFileCopy = src->app == NULL;
  volatile unsigned short far *dbg = (unsigned short far *)0xF0000000UL;

  assert(dst);
  assert(pattern);
  assert(src);

  // // puts("[c0]");
  dbg[15] = 0x0F00 | 'F';  /* copy() entered */

  // // puts("[c1]");
  if(strpbrk(pattern, "*?") == 0) {
    // // puts("[c2]");
  	srcFile = dfnfilename(pattern);
  	wildcarded = 0;
    // // puts("[c3]");
  } else if(dos_findfirst(pattern, &ff, FA_RDONLY | FA_ARCH) != 0) {
    // // puts("[c4]");
    error_sfile_not_found(pattern);
    return 0;
  } else {
    // // puts("[c5]");
  	srcFile = ff.ff_name;
  	wildcarded = 1;
  }

  // // puts("[c6]");
  dbg[16] = 0x0F00 | 'G';  /* after findfirst */

  // // puts("[c7]");
  do {
/*    if( wildcarded && !strpbrk( dst, "*?" ) && !isfirst ) openMode = O_APPEND; */
    // // puts("[c8]");
    fillFnam(rDest, dst, srcFile);
    // // puts("[c9]");
    if(rDest[0] == 0)
      return 0;
    // // puts("[cA]");
    h = src;
    // // puts("[cB]");

    do {  /* to prevent to open a source file for writing, e.g.
          for COPY *.c *.?    */
      // // puts("[cC]");
      fillFnam(rSrc, h->fnam, srcFile);
      // // puts("[cD]");
      if(rSrc[0] == 0) {
        return 0;
      }
      // // puts("[cE]");
      rc = samefile(rDest, rSrc);
      // // puts("[cF]");
      if(rc < 0) {
        error_out_of_memory();
        return 0;
      } else if(rc) {
        error_selfcopy(rDest);
        return 0;
      }
      // // puts("[cG]");
    } while((h = h->app) != 0);
    // // puts("[cH]");

    /* Concenation of files uses ASCII by default */
    if(src->app) {
      for(h = src; h && !h->flags; h = h->app)
        h->flags = ASCII;
      if(!destFlags) destFlags = ASCII;
    }
    // // puts("[cI]");

    if(interactive_command		/* Suppress prompt if in batch file */
       && openMode != O_APPEND && !optY
       && (fdout = dos_open(rDest, O_RDONLY)) >= 0) {
      int destIsDevice;  /* C89 requires declarations at block start */
      // // puts("[cJ]");
      destIsDevice = isadev(fdout);

      /* Debug marker: dest exists, checking source */
      { unsigned short far *s = (unsigned short far *)0xF0000000UL;
        s[1927] = 0x0700 | '8'; }

      dos_close(fdout);
      if(!destIsDevice) {	/* Devices do always exist */
        if((fdin = devopen(rSrc, O_RDONLY)) < 0) { /* Source doesn't exist */
            error_open_file( rSrc );
            return 0;
        } else {
	        dos_close(fdin);
          /* Debug marker: before userprompt */
          { unsigned short far *s = (unsigned short far *)0xF0000000UL;
            s[1928] = 0x0700 | '9'; }
          	switch(userprompt(PROMPT_OVERWRITE_FILE, rDest)) {
	    	default:	/* Error */
		    case 4:	/* Quit */
	    		  return 0;
		    case 3:	/* All */
			    optY = 1;
    		case 1: /* Yes */
	    		break;
		    case 2:	/* No */
    			continue;
    		}
        }
	  }
    }

    // // puts("[cK]");

    if(cbreak) {
      return 0;
    }
    // // puts("[cL]");

    if((fdout = devopen(rDest, openMode)) < 0) {
      // // puts("[cM]");
      error_open_file(rDest);
      return 0;
    }
    // // puts("[cN]");

    keepFTime = 1;
    if(isadev(fdout)) {
      if(destFlags & BINARY)  {
        /* in forced binary mode character devices are set to raw */
        fdsetattr(fdout, (fdattr(fdout) & 0xff) | 0x20);
      }
      keepFTime = 0;
    }
    // // puts("[cO]");
    h = src;
    keepFTime = (keepFTime && h->app == 0);
    do {
      // // puts("[cP]");
      fillFnam(rSrc, h->fnam, srcFile);
      // // puts("[cQ]");
      if(rSrc[0] == 0) {
        dos_close(fdout);
        unlink(rDest);		/* if device -> no removal, ignore error */
        return 0;
      }
      // // puts("[cR]");
      if((fdin = devopen(rSrc, O_RDONLY)) < 0) {
        error_open_file(rSrc);
        dos_close(fdout);
        unlink(rDest);		/* if device -> no removal, ignore error */
        return 0;
      }
      // // puts("[cS]");
      if(isadev(fdin)) {
		keepFTime = 0;		/* Cannot keep file time of devices */
      	if(h->flags & BINARY)
		  /* in forced binary mode character devices are set to raw */
		  fdsetattr(fdin, (fdattr(fdin) & 0xff) | 0x20);
      	else
		  /* make sure to stop at Ctrl-Z */
		  h->flags |= ASCII;
      }
      if(keepFTime)
#ifdef __TURBOC__
        if(getftime(fdin , &fileTime))
#else
        if(_dos_getftime(fdin , &date , &time))
#endif
          keepFTime = 0; /* if error: turn it off */

      displayString(TEXT_MSG_COPYING, rSrc
	   , (openMode == 'a' || h != src)? "=>>": "=>", rDest);
      if(cbreak) {
        dos_close(fdin);
        dos_close(fdout);
        unlink(rDest);		/* if device -> no removal, ignore error */
        return 0;
      }

      /* Now copy the file */
      dbg[17] = 0x0F00 | 'H';  /* before copy section */
      rc = 1;
      {
      	FLAG sizeChanged = !(h->flags & ASCII) && singleFileCopy &&
			!isadev(fdin) && !isadev(fdout);
        if(sizeChanged) {	/* faster copy, *MUCH* faster on floppies
								 change destination filesize to wanted size.
								 this a) writes all required entries to the
								 FAT (faster) determines, if there is enough
								 space on the destination device
								 no need to copy file, if it won't fit */
        					/* No test if chsize() fails for MS DOS 5/6 bug
        						see RBIL DOS-40 */
        					/* Don't use chsize() as Turbo RTL fills with
        						'\0' bytes, which is not useful here */
        	dbg[18] = 0x0F00 | 'I';  /* before lseek/truncate */
        	lseek(fdout, filelength(fdin), SEEK_SET);
        	if(truncate(fdout) != 0
        	 || lseek(fdout, 0, SEEK_SET) == -1) {
				error_write_file_disc_full(rDest, filelength(fdin));
        		rc = 0;
			} else {
				dprintf( ("[COPY chsize(%s, %lu)]\n", rDest,
				 filelength(fdin)) );
			}
		}

        dbg[19] = 0x0F00 | 'J';  /* before BIGcopy */
        // puts("[->BIG]");
        if(rc != 0)
			switch(BIGcopy(fdout, fdin, h->flags & ASCII)) {
			case 0: 
				if(sizeChanged)
					/* probably the source file got truncated */
					/* we silently ignore any failure here, because it is
						assumed that we never extend, but truncate the file
						only (or do not change the length at all) */
					truncate(fdout);
				break;
			case 1:  error_read_file(rSrc);   rc = 0; break;
			case 2:  error_write_file(rDest); rc = 0; break;
			default: error_copy();            rc = 0; break;
			}
      }
      if(cbreak)
        rc = 0;
      dos_close(fdin);
      if(!rc) {
        dos_close(fdout);
        unlink(rDest);		/* if device -> no removal, ignore error */
        return 0;
      }
    } while((h = h->app) != 0);
    rc = 0;
    if((destFlags & ASCII) && !isadev(fdout)) {   /* append the ^Z as we copied in ASCII mode */
      if (dos_write(fdout, "\x1a", 1) != 1)
		rc = 1;
    }
    if(keepFTime)
#ifdef __TURBOC__
      setftime(fdout, &fileTime);
#else
      _dos_setftime(fdout, date, time);
#endif
    if(dos_close(fdout) != 0)
      rc = 1;
    if(rc) {
      error_write_file(rDest);
      unlink(rDest);		/* if device -> no removal, ignore error */
      return 0;
    }
  } while (wildcarded && dos_findnext(&ff) == 0);
  /*} while(wildcarded && FINDNEXT(&ff) == 0 && !(isfirst = 0)); */

  dos_findclose(&ff);

  return 1;
}

static int copyFiles(struct CopySource *h)
{ int differ, rc;

  // puts("[L]");
  rc = 0;

#define dst destFile
  // puts("[M]");
  if((differ = samefile(h->fnam, dst)) < 0) {
    // puts("[N]");
    error_out_of_memory();
  }
  else if(!differ) {
    // puts("[O]");
    rc = copy(dst, h->fnam, h, O_WRONLY|O_TRUNC|O_CREAT);
    // puts("[P]");
  }
  else if(h->app) {
    // puts("[Q]");
    rc = copy(dst, h->fnam, h->app, O_WRONLY|O_APPEND);
    // puts("[R]");
  }
  else {
    // puts("[S]");
    error_selfcopy(dst);
  }
#undef dst

  // puts("[T]");
  return rc;
}

static int cpyFlags(void)
{
  return (optA? ASCII: 0) | (optB? BINARY: 0);
}

static struct CopySource *srcItem(char *fnam)
{	struct CopySource *h;

    if((h = malloc(sizeof(struct CopySource))) == 0) {
      error_out_of_memory();
      return 0;
    }

    h->fnam = fnam;
    h->nxt = h->app = 0;
    h->flags = cpyFlags();

    return h;
}

static int addSource(char *p)
{ struct CopySource *h;
  char *q;

  assert(p);
  q = strtok(p, "+");
  assert(q && *q);

  if(appendToFile) {
    appendToFile = 0;
    if(!lastApp) {
      error_leading_plus();
      return 0;
    }
  } else {      /* New entry */
    if(0 == (h = srcItem(q)))
      return 0;
    if(!last)
      last = lastApp = head = h;
    else
      last = lastApp = last->nxt = h;

    if((q = strtok(0, "+")) == 0)   /* no to-append file */
      return 1;
  }

  /* all the next files are to be appended to the source in "last" */
  assert(q);
  assert(lastApp);
  do {
    if(0 == (h = srcItem(q)))
      return 0;
    lastApp = lastApp->app = h;
  } while((q = strtok(0, "+")) != 0);

  return 1;
}


int cmd_copy(char *rest)
{ char **argv, *p;
  int argc, opts, argi;
  struct CopySource *h;
  char **argBuffer = 0;
  /* Use row 0 for debug markers so they don't scroll off */
  volatile unsigned short far *dbg = (unsigned short far *)0xF0000000UL;

  /* Debug: simple puts to confirm cmd_copy is called */
  // puts("[COPY-START]");

  dbg[0] = 0x0F00 | '0';  /* Marker: entered cmd_copy */

  /* Initialize options */
  optA = optB = optV = optY = 0;
  dbg[1] = 0x0F00 | '1';  /* Marker: options initialized */
  // puts("[1]");

  /* read the parameters from env */
  p = getEnv("COPYCMD");
  dbg[2] = 0x0F00 | '2';  /* Marker: after getEnv */
  // puts("[2]");

  argv = scanCmdline(p, opt_copy, 0, &argc, &opts);
  dbg[3] = 0x0F00 | '3';  /* Marker: after scanCmdline 1 */
  // puts("[3]");

  if (argv == 0) {
    free(p);
    return 1;
  }
  free(p);
  dbg[4] = 0x0F00 | '4';  /* Marker: after free(p) */
  // puts("[4]");

  freep(argv);    /* ignore any parameter from env var */
  dbg[5] = 0x0F00 | '5';  /* Marker: after freep */
  // puts("[5]");

  if((argv = scanCmdline(rest, opt_copy, 0, &argc, &opts)) == 0)
    return 1;
  dbg[6] = 0x0F00 | '6';  /* Marker: after scanCmdline 2 */
  // puts("[6]");

  initContext();
  dbg[7] = 0x0F00 | '7';  /* Marker: after initContext */
  // puts("[7]");

  /* Now parse the remaining arguments into the copy file
    structure */
  for(argi = 0; argi < argc; ++argi)
    if(isoption(p = argv[argi])) {    /* infix /a or /b */
      if(leadOptions(&p, opt_copy1, 0) != E_None) {
        killContext();
        freep(argv);
        return 1;
      }
      /* Change the flags of the previous argument */
      if(lastApp)
        lastApp->flags = cpyFlags();
    } else {            /* real argument */
      if(*p == '+') {       /* to previous argument */
        appendToFile = 1;
        while(*++p == '+');
        if(!*p)
          continue;
      }

      if(!addSource(p)) {
        killContext();
        freep(argv);
        return 1;
      }

    }

  if(appendToFile) {
    error_trailing_plus();
    killContext();
    freep(argv);
    return 1;
  }

  dbg[8] = 0x0F00 | '8';  /* Marker: after for loop */
  // puts("[8]");

  if(!last) {   /* Nothing to do */
    error_nothing_to_do();
    killContext();
    freep(argv);
    return 1;
  }

  dbg[9] = 0x0F00 | '9';  /* Marker: last is valid */
  // puts("[9]");
  assert(head);

  /* Check whether the source items are files or directories */
  h = head;
  argc = 0;		/* argBuffer entries */
  dbg[10] = 0x0F00 | 'A';  /* Marker: before do-while loop */
  // puts("[A]");
  do {
	struct CopySource *p = h;
	dbg[11] = 0x0F00 | 'B';  /* Marker: in outer do loop */
	// puts("[B]");
  	do {
  		char *s = strchr(p->fnam, '\0') - 1;
  		dbg[12] = 0x0F00 | 'C';  /* Marker: before dfnstat */
  		// puts("[C]");
  		if(*s == '/' || *s == '\\'		/* forcedly be directory */
  		 || 0 != (dfnstat(p->fnam) & DFN_DIRECTORY)) {
			char **buf;
			char *q;
			if(*s == ':') 
				q = dfnmerge(0, p->fnam, 0, "*", "*");
			else
				q = dfnmerge(0, 0, p->fnam, "*", "*");
			if(0 == (buf = realloc(argBuffer, (argc + 2) * sizeof(char*)))
			 || !q) {
				free(q);
				error_out_of_memory();
				goto errRet;
			}
			argBuffer = buf;
			buf[argc] = p->fnam = q;
			buf[++argc] = 0;
		} else if(*s == ':' && (s - p->fnam) > 1) {		/* Device name LPT1:, but not X: */
  			if(!isDeviceName(p->fnam)) {
				error_invalid_parameter(p->fnam);
				goto errRet;
			}
  		}
  		// puts("[D]");
  	} while(0 != (p = p->app));
  	// puts("[E]");
  } while(0 != (h = h->nxt));
  // puts("[F]");

  destFlags = last->flags;
  // puts("[G]");
	if(last != head) {
		/* The last argument is to be the destination */
		if(last->app) {	/* last argument is a + b syntax -> no dst! */
			error_copy_plus_destination();
			goto errRet;
		}
		destFile = last->fnam;
		// puts("[H]");
		h = head;         /* remove it from argument list */
		while(h->nxt != last) {
		  assert(h->nxt);
		  h = h->nxt;
		}
		free(last);
		(last = h)->nxt = 0;
		// puts("[I]");
  } else {              /* Nay */
    destFile = ".\\*.*";
  }

#define dst destFile
  /* If the destination specifies a drive, check that it is valid */
  // puts("[J]");
  if (dst[0] && dst[1] == ':' && !is_valid_disk(toupper(dst[0]) - 'A')) {
    error_invalid_drive(toupper(dst[0]) - 'A');
    return 0;
  }
#undef dst

  /* Now copy the files */
  dbg[13] = 0x0F00 | 'D';  /* Marker: before copyFiles */
  // puts("[K]");
  h = head;
  while(copyFiles(h) && (h = h->nxt) != 0);
  dbg[14] = 0x0F00 | 'E';  /* Marker: after copyFiles */

errRet:
  killContext();
  freep(argv);
  freep(argBuffer);
  return 0;
}
