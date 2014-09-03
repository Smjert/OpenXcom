/*      vi:set nowrap ts=2 sw=2:
*/
/** COPYING:


Don't care ware... ;)

Something like: "you can do with it what you want to do with it. But if
your cola vaporises you can't sue me. But if you make changes to the code
you can always send to diffs to me"

grt,

- Jasper

 +-----
 | Beheer Commisaris      | Homepage: http://www.il.fontys.nl/~jasper
 | IGV Interlink          | PGP-key:  finger jasper@il.fontys.nl      |
                          | E-mail:   jasper@il.fontys.nl             |
                                                                  ----+
*/

#define version "0.2"
/*
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
/*
*/
#include <SDL.h>
/*
*/
#include "Flc.h"
#include "Logger.h"
#include "Exception.h"
#include "Zoom.h"
#include "Screen.h"
#include "Surface.h"
#include "Options.h"
#include "../fmath.h"

namespace OpenXcom
{

namespace Flc
{

struct Flc_t flc;

#if 0
void SDLInit(char *header)
{ /* Initialize SDL
  */
  printf("SDL: Version %d.%d.%d.\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);

  if(SDL_Init(SDL_INIT_VIDEO)<0) {
    fprintf(stderr, "SDL: Couldn't initialize: %s\n",SDL_GetError());
    exit(2);
  } else {
    fprintf(stdout, "SDL: Video initialization succeeded.\n");
  }
  atexit(SDL_Quit);
/* Init screen
*/
  if((flc.mainscreen=SDL_SetVideoMode(flc.screen_w, flc.screen_h, flc.screen_depth, (SDL_HWPALETTE|SDL_FULLSCREEN))) == NULL){
    fprintf(stderr, "SDL: Couldn't set video mode %dx%dx%d: %s\n", flc.screen_w, flc.screen_h, flc.screen_depth, SDL_GetError());
    exit(3);
  }

/* Set titlebar and iconbar name
*/
  SDL_WM_SetCaption(header, header);
} /* SDLInit */

#endif


#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define ReadU16(tmp1, tmp2) { Uint8 *sp = (Uint8 *)tmp2, *dp = (Uint8 *)tmp1; dp[0]=sp[1]; dp[1]=sp[0]; }
#define ReadU32(tmp1, tmp2) { Uint8 *sp = (Uint8 *)tmp2, *dp = (Uint8 *)tmp1; dp[0]=sp[3]; dp[1]=sp[2]; dp[2]=sp[1]; dp[3]=sp[0]; }
#else
#define ReadU16(tmp1, tmp2) /* (Uint16) */ (*(tmp1) = ((Uint8)*(tmp2+1)<<8)+(Uint8)*(tmp2));
#define ReadU32(tmp1, tmp2) /* (Uint32) */ (*(tmp1) = (((((((Uint8)*(tmp2+3)<<8)+((Uint8)*(tmp2+2)))<<8)+((Uint8)*(tmp2+1)))<<8)+(Uint8)*(tmp2)));
#endif

void FlcReadFile(Uint32 size)
{ 
if(size>flc.membufSize) {
    if(!(flc.pMembuf=(Uint8*)realloc(flc.pMembuf, size+1))) {
      //printf("Realloc failed: %d\n", size);
      Log(LOG_FATAL) << "Realloc failed: " << size;
      throw Exception("Realloc failed!");
    }
  }

  if(fread(flc.pMembuf, sizeof(Uint8), size, flc.file)==0) {
    //printf("Can't read flx file");
    Log(LOG_ERROR) << "Can't read flx file :(";
		return;
  }
} /* FlcReadFile */

int FlcCheckHeader(const char *filename)
{ 
if((flc.file=fopen(filename, "rb"))==NULL) {
    Log(LOG_ERROR) << "Could not open flx file: " << filename;
		return -1;
  }

  FlcReadFile(128);

  ReadU32(&flc.HeaderSize, flc.pMembuf);
  ReadU16(&flc.HeaderCheck, flc.pMembuf+4);
  ReadU16(&flc.HeaderFrames, flc.pMembuf+6);
  ReadU16(&flc.HeaderWidth, flc.pMembuf+8);
  ReadU16(&flc.HeaderHeight, flc.pMembuf+10);
  ReadU16(&flc.HeaderDepth, flc.pMembuf+12);
  ReadU16(&flc.HeaderSpeed, flc.pMembuf+16);

#ifdef DEBUG
  printf("flc.HeaderSize: %d\n", flc.HeaderSize);
  printf("flc.HeaderCheck: %d\n", flc.HeaderCheck);
  printf("flc.HeaderFrames: %d\n", flc.HeaderFrames);
  printf("flc.HeaderWidth: %d\n", flc.HeaderWidth);
  printf("flc.HeaderHeight: %d\n", flc.HeaderHeight);
  printf("flc.HeaderDepth: %d\n", flc.HeaderDepth);
  printf("flc.HeaderSpeed: %lf\n", flc.HeaderSpeed);
#endif


  if((flc.HeaderCheck==SDL_SwapLE16(0x0AF12)) || (flc.HeaderCheck==SDL_SwapLE16(0x0AF11))) { 
    flc.screen_w=flc.HeaderWidth;
    flc.screen_h=flc.HeaderHeight;
	Log(LOG_INFO) << "Playing flx, " << flc.screen_w << "x" << flc.screen_h << ", " << flc.HeaderFrames << " frames";
    flc.screen_depth=8;
	if(flc.HeaderCheck == SDL_SwapLE16(0x0AF11)){
      flc.HeaderSpeed*=1000.0/70.0;
    }
    return(0);
  }
  return(1);

} /* FlcCheckHeader */

int FlcCheckFrame()
{ 
flc.pFrame=flc.pMembuf+flc.FrameSize-16;
  ReadU32(&flc.FrameSize, flc.pFrame+0);
  ReadU16(&flc.FrameCheck, flc.pFrame+4);
  ReadU16(&flc.FrameChunks, flc.pFrame+6);
  ReadU16(&flc.DelayOverride, flc.pFrame+8); // not actually used in UFOINT.FLI, it turns out

#ifdef DEBUG
  printf("flc.FrameSize: %d\n", flc.FrameSize);
  printf("flc.FrameCheck: %d\n", flc.FrameCheck);
  printf("flc.FrameChunks: %d\n", flc.FrameChunks);
  printf("flc.DelayOverride: %d\n", flc.DelayOverride);
#endif

  flc.pFrame+=16;
  if(flc.FrameCheck==0x0f1fa) { 
    return(0);
  }

  flc.DelayOverride = 0; // not FRAME_TYPE means the value we read wasn't a delay at all

  if(flc.FrameCheck==0x0f100) { 
#ifdef DEBUG
    printf("Ani info!!!\n");
#endif	
    return(0);
  }

  return(1);
//#else
//  return(0);
//#endif
} /* FlcCheckFrame */

void COLORS256()
{ Uint8 *pSrc;
  Uint16 NumColorPackets;
  Uint16 NumColors;
  Uint8 NumColorsSkip;
  int i;

  pSrc=flc.pChunk+6;
  ReadU16(&NumColorPackets, pSrc);
  pSrc+=2;
  while(NumColorPackets--) {
    NumColorsSkip=*(pSrc++);
    if(!(NumColors=*(pSrc++))) {
      NumColors=256;
    }
    i=0;
    while(NumColors--) {
      flc.colors[i].r=*(pSrc++);
      flc.colors[i].g=*(pSrc++);
      flc.colors[i].b=*(pSrc++);
      i++;
    }
	flc.realscreen->setPalette(flc.colors, NumColorsSkip, i);
	SDL_SetColors(flc.mainscreen, flc.colors, NumColorsSkip, i);
	flc.realscreen->getSurface(); // force palette update to really happen
  }
} /* COLORS256 */

void SS2()
{ Uint8 *pSrc, *pDst, *pTmpDst;
  Sint8 CountData;
  Uint8 ColumSkip, Fill1, Fill2;
  Uint16 Lines, Count;

  pSrc=flc.pChunk+6;
  pDst=(Uint8*)flc.mainscreen->pixels + flc.offset;
  ReadU16(&Lines, pSrc);
  
  pSrc+=2;
  while(Lines--) {
    ReadU16(&Count, pSrc);
    pSrc+=2;

    while(Count & 0xc000) {
/* Upper bits 11 - Lines skip 
*/
      if((Count & 0xc000)==0xc000) {  // 0xc000h = 1100000000000000
        pDst+=(0x10000-Count)*flc.mainscreen->pitch;
      }

      if((Count & 0xc000)==0x4000) {  // 0x4000h = 0100000000000000
/* Upper bits 01 - Last pixel
*/
#ifdef DEBUG
            printf("Last pixel not implemented");
#endif
      }
      ReadU16(&Count, pSrc);
      pSrc+=2;
    }

	if((Count & SDL_SwapLE16(0xc000))==0x0000) {      // 0xc000h = 1100000000000000
      pTmpDst=pDst;
      while(Count--) {
        ColumSkip=*(pSrc++);
        pTmpDst+=ColumSkip;
        CountData=*(pSrc++);
        if(CountData>0) {
          while(CountData--) {
            *(pTmpDst++)=*(pSrc++);
            *(pTmpDst++)=*(pSrc++);
          }
        } else { 
          if(CountData<0) {
            CountData=(0x100-CountData);
            Fill1=*(pSrc++);
            Fill2=*(pSrc++);
            while(CountData--) {
              *(pTmpDst++)=Fill1;
              *(pTmpDst++)=Fill2;
            }
          }
        }
      }
      pDst+=flc.mainscreen->pitch;
    } 
  }
} /* SS2 */

void DECODE_BRUN()
{ Uint8 *pSrc, *pDst, *pTmpDst, Fill;
  Sint8 CountData;
  int HeightCount, PacketsCount;

  HeightCount=flc.HeaderHeight;
  pSrc=flc.pChunk+6;
  pDst=(Uint8*)flc.mainscreen->pixels + flc.offset;
  while(HeightCount--) {
    pTmpDst=pDst;
    PacketsCount=*(pSrc++);
    while(PacketsCount--) {
      CountData=*(pSrc++);
      if(CountData>0) {
        Fill=*(pSrc++);
        while(CountData--) {
          *(pTmpDst++)=Fill;
        }
      } else { 
        if(CountData<0) {
          CountData=(0x100-CountData);
          while(CountData--) {
          *(pTmpDst++)=*(pSrc++);
          }
        }
      }
    }
    pDst+=flc.mainscreen->pitch;
  }
} /* DECODE_BRUN */


void DECODE_LC() 
{ Uint8 *pSrc, *pDst, *pTmpDst;
  Sint8 CountData;
  Uint8 CountSkip;
  Uint8 Fill;
  Uint16 Lines, tmp;
  int PacketsCount;

  pSrc=flc.pChunk+6;
  pDst=(Uint8*)flc.mainscreen->pixels + flc.offset;

  ReadU16(&tmp, pSrc);
  pSrc+=2;
  pDst+=tmp*flc.mainscreen->pitch;
  ReadU16(&Lines, pSrc);
  pSrc+=2;
  while(Lines--) {
    pTmpDst=pDst;
    PacketsCount=*(pSrc++);
    while(PacketsCount--) {
      CountSkip=*(pSrc++);
      pTmpDst+=CountSkip;
      CountData=*(pSrc++);
      if(CountData>0) {
        while(CountData--) {
          *(pTmpDst++)=*(pSrc++);
        }
      } else { 
        if(CountData<0) {
          CountData=(0x100-CountData);
          Fill=*(pSrc++);
          while(CountData--) {
            *(pTmpDst++)=Fill;
          }
        }
      }
    }
    pDst+=flc.mainscreen->pitch;
  }
} /* DECODE_LC */

void DECODE_COLOR()
{ Uint8 *pSrc;
  Uint16 NumColors, NumColorPackets;
  Uint8 NumColorsSkip;
  int i;

  pSrc=flc.pChunk+6;
  ReadU16(&NumColorPackets, pSrc);
  pSrc+=2;
  while(NumColorPackets--) {
    NumColorsSkip=*(pSrc++);
    if(!(NumColors=*(pSrc++))) {
      NumColors=256;
    }
    i=0;
    while(NumColors--) {
      flc.colors[i].r=*(pSrc++)<<2;
      flc.colors[i].g=*(pSrc++)<<2;
      flc.colors[i].b=*(pSrc++)<<2;
      i++;
    }
	flc.realscreen->setPalette(flc.colors, NumColorsSkip, i);
    SDL_SetColors(flc.mainscreen, flc.colors, NumColorsSkip, i);
	flc.realscreen->getSurface(); // force palette update to really happen
  }
} /* DECODE_COLOR  */


void DECODE_COPY()
{ Uint8 *pSrc, *pDst;
  int Lines = flc.screen_h;
  pSrc=flc.pChunk+6;
  pDst=(Uint8*)flc.mainscreen->pixels + flc.offset;
  while(Lines-- > 0) {
    memcpy(pDst, pSrc, flc.screen_w);
    pSrc+=flc.screen_w;
    pDst+=flc.mainscreen->pitch;
  }
} /* DECODE_COPY */

void BLACK()
{ Uint8 *pDst;
  int Lines = flc.screen_h;
  pDst=(Uint8*)flc.mainscreen->pixels + flc.offset;
  while(Lines-- > 0) {
    memset(pDst, 0, flc.screen_w);
    pDst+=flc.mainscreen->pitch;
  }
} /* BLACK */

#ifndef MIN
/* FIXME: Only works on expressions with no side effects */
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif

static void FlcWakeAudioWaiter(void)
{
  SDL_SemPost(flc.audioWaiter);
}

static void FlcWaitForNextAudioFrame(void)
{
  SDL_SemWait(flc.audioWaiter);
}

void FlcAudioCallback(void *userData, Uint8 *stream, int len)
{
   struct Flc_t *f = (struct Flc_t*)userData;
   int b = f->playingSampleBuffer;
   size_t samplesToCopy = MIN(len, f->sampleCount[b] - f->samplePosition[b]);
   memcpy(stream, &f->samples[b][f->samplePosition[b]], samplesToCopy);
   f->samplePosition[b] += samplesToCopy;
   if (len > samplesToCopy) {
     /* Need to swap buffers */
	 size_t samplesCopied = samplesToCopy;
	 f->playingSampleBuffer = (f->playingSampleBuffer+1)%2;
	 b = f->playingSampleBuffer;
	 assert(f->samplePosition[b] == 0);
	 samplesToCopy = MIN(len - samplesCopied, f->sampleCount[b]);
	 memcpy(stream + samplesCopied, &f->samples[b][0], samplesToCopy);
	 f->samplePosition[b] += samplesToCopy;

	 /* We use the audio thread as sync - when we progress over the end of the  */
	 FlcWakeAudioWaiter();
   }
}

void FlcInitAudio(int sampleRate, Uint16 format, Uint8 channels)
{
	int err;
	memset(&flc.requestedAudioSpec, 0, sizeof(flc.requestedAudioSpec));
	memset(&flc.returnedAudioSpec, 0, sizeof(flc.returnedAudioSpec));
	flc.requestedAudioSpec.freq = sampleRate;
	flc.requestedAudioSpec.channels = channels;
	flc.requestedAudioSpec.format = format;
	flc.requestedAudioSpec.callback = FlcAudioCallback;
	flc.requestedAudioSpec.userdata = &flc;
	/* Arbitrary buffer size of 8k */
	flc.requestedAudioSpec.samples = (1ul<<12);

	err = SDL_OpenAudio(&flc.requestedAudioSpec, &flc.returnedAudioSpec);

	if (err)
	{
		printf("Failed to open audio (%d)\n", err);
		return;
	}

	/* We do not support channel/format/frequency changing */
	assert(flc.returnedAudioSpec.format == flc.requestedAudioSpec.format);
	assert(flc.returnedAudioSpec.freq == flc.requestedAudioSpec.freq);
	assert(flc.returnedAudioSpec.channels == flc.requestedAudioSpec.channels);

	/* Start runnable */
	flc.audioWaiter = SDL_CreateSemaphore(1);

}

void FlcDeInitAudio(void)
{
	SDL_PauseAudio(1);
	SDL_CloseAudio();
	if (flc.samples[0]) {
		free(flc.samples[0]);
		flc.samples[0] = NULL;
	}
	if (flc.samples[1]) {
		free(flc.samples[1]);
		flc.samples[1] = NULL;
	}
	SDL_DestroySemaphore(flc.audioWaiter);

}

void DECODE_TFTD_AUDIO(size_t chunkSize)
{
  /* TFTD audio header (10 bytes)
   * Uint16 unknown1 - always 0
   * Uint16 sampleRate
   * Uint16 unknown2 - always 1 (Channels? bytes per sample?)
   * Uint16 unknown3 - always 10 (No idea)
   * Uint16 unknown4 - always 0
   * Uint8[] unsigned 1-byte 1-channel PCM data of length _chunkSize_ (so the total chunk is _chunkSize_ + 6-byte flc header + 10 byte audio header */
  Uint16 sampleRate;
  ReadU16(&sampleRate, flc.pChunk+8);
  int loadingSampleBuffer = (flc.playingSampleBuffer + 1)  % 2;

  if (chunkSize != flc.sampleCount[loadingSampleBuffer]) {
    /* If the sample count has changed, we need to reallocate (Handles initial state
	 * of '0' sample count too, as realloc(NULL, size) == malloc(size) */
	flc.samples[loadingSampleBuffer] = (char*)realloc(flc.samples[loadingSampleBuffer], chunkSize);
	flc.sampleCount[loadingSampleBuffer] = chunkSize;
  }

  /* Copy the data.... */
  memcpy(flc.samples[loadingSampleBuffer], flc.pChunk+16, chunkSize);
  /* And reset the position to the beginning */
  flc.samplePosition[loadingSampleBuffer] = 0;

  /* Start the music! */
  if (!flc.hasAudio) {
	flc.hasAudio = true;
	flc.sampleRate = sampleRate;
	FlcInitAudio(sampleRate, AUDIO_U8, 1);
  } else {
	/* Cannot change sample rate mid-video */
	assert(sampleRate == flc.sampleRate);
  }

}

void FlcDoOneFrame()
{ int ChunkCount; 
  ChunkCount=flc.FrameChunks;
  flc.pChunk=flc.pMembuf;
  if ( SDL_LockSurface(flc.mainscreen) < 0 )
    return;
  // if (!ChunkCount) printf("Empty frame! %d\n", flc.FrameCount); // this is normal and used for delays
  while(ChunkCount--) {
    ReadU32(&flc.ChunkSize, flc.pChunk+0);
    ReadU16(&flc.ChunkType, flc.pChunk+4);

#ifdef DEBUG
    printf("flc.ChunkSize: %d\n", flc.ChunkSize);
    printf("flc.ChunkType: %d aka %x\n", flc.ChunkType, flc.ChunkType);
	if (flc.DelayOverride) printf("DelayOverride: %d\n", flc.DelayOverride);
#endif

    switch(flc.ChunkType) {
      case 4:
        COLORS256();
      break;
      case 7:
        SS2();
      break;
      case 11:
        DECODE_COLOR();
      break;
      case 12:
        DECODE_LC();
      break;
      case 13:
        BLACK();
      break;
      case 15:
        DECODE_BRUN();
      break;
      case 16:
        DECODE_COPY();
      break;
      case 18:
#ifdef DEBUG
        printf("Chunk 18 not yet done.\n");
#endif
      break;
      case 0xaaaa:
	    DECODE_TFTD_AUDIO(flc.ChunkSize);
		/* EVIL HACK - the tftd audio chunk lies about it's size, it does not
		 * take into account the 6-byte flc header plus the 10 byte audio header*/
		flc.pChunk += 16;
		break;
      default:
        Log(LOG_WARNING) << "Ieek an non implemented chunk type:" << flc.ChunkType;
    }
    flc.pChunk+=flc.ChunkSize;
  }
  SDL_UnlockSurface(flc.mainscreen);
} /* FlcDoOneFrame */

void SDLWaitFrame(void)
{ 
//#ifndef __NO_FLC
static double oldTick=0.0;
  Uint32 currentTick;
  double waitTicks;
  double delay = flc.DelayOverride ? flc.DelayOverride : flc.HeaderSpeed;

	if ( AreSame(oldTick, 0.0) ) oldTick = SDL_GetTicks();

	currentTick=SDL_GetTicks(); 
	waitTicks=(oldTick+=(delay))-currentTick;


	do {
		waitTicks = (oldTick + delay - SDL_GetTicks());

		if(waitTicks > 0.0) {
			//SDL_Delay((int)Round(waitTicks)); // biased rounding? mehhh?
			SDL_Delay(1);
		}
	} while (waitTicks > 0.0); 
//#endif
} /* SDLWaitFrame */

void FlcInitFirstFrame()
{ flc.FrameSize=16;
  flc.FrameCount=0;
  if(fseek(flc.file, 128, SEEK_SET)) {
    //printf("Fseek read failed\n");
    throw Exception("Fseek read failed for flx file");
  };
  FlcReadFile(flc.FrameSize);
} /* FlcInitFirstFrame */

int FlcInit(const char *filename)
{ flc.pMembuf=NULL;
  flc.membufSize=0;

  if(FlcCheckHeader(filename)) {
    Log(LOG_ERROR) << "Flx file failed header check.";
    //exit(1);
	return -1;
  }
  if (flc.realscreen->getSurface()->getSurface()->format->BitsPerPixel == 8)
  {
	  flc.mainscreen = flc.realscreen->getSurface()->getSurface();
  } else
  {
	  flc.mainscreen = SDL_AllocSurface(SDL_SWSURFACE, flc.screen_w, flc.screen_h, 8, 0, 0, 0, 0);
  }
  return 0;
  //SDLInit(filename);
} /* FlcInit */

void FlcDeInit()
{ 
	if (flc.mainscreen != flc.realscreen->getSurface()->getSurface()) SDL_FreeSurface(flc.mainscreen);
	fclose(flc.file);
	free(flc.pMembuf);
} /* FlcDeInit */

void FlcMain(void (*frameCallBack)())
{ flc.quit=false;
  SDL_Event event;
  
  FlcInitFirstFrame();
  flc.offset = flc.dy * flc.mainscreen->pitch + flc.mainscreen->format->BytesPerPixel * flc.dx;
  while(!flc.quit) {
	if (frameCallBack) (*frameCallBack)();
    flc.FrameCount++;
    if(FlcCheckFrame()) {
      if (flc.FrameCount<=flc.HeaderFrames) {
        Log(LOG_ERROR) << "Frame failure -- corrupt file?";
	return;
      } else {
        if(flc.loop)
          FlcInitFirstFrame();
        else {
          SDL_Delay(1000);
          flc.quit=true;
        }
        continue;
      }
    }

    FlcReadFile(flc.FrameSize);

	if(flc.FrameCheck!=SDL_SwapLE16(0x0f100)) {
	  FlcDoOneFrame();
	  if (flc.hasAudio)
	    FlcWaitForNextAudioFrame();
	  else
        SDLWaitFrame();
      /* TODO: Track which rectangles have really changed */
      //SDL_UpdateRect(flc.mainscreen, 0, 0, 0, 0);
      if (flc.mainscreen != flc.realscreen->getSurface()->getSurface())
        SDL_BlitSurface(flc.mainscreen, 0, flc.realscreen->getSurface()->getSurface(), 0);
      flc.realscreen->flip();
    }

	bool finalFrame = !flc.loop && (flc.FrameCount == flc.HeaderFrames);
	Uint32 pauseStart = 0;
	if (finalFrame) pauseStart = SDL_GetTicks();

	do 
	{
		while(SDL_PollEvent(&event)) {
		  switch(event.type) {
			case SDL_MOUSEBUTTONDOWN:
			case SDL_KEYDOWN:
			  return;
			break;
			case SDL_VIDEORESIZE:
				if (Options::allowResize)
				{
					Options::newDisplayWidth = Options::displayWidth = std::max(Screen::ORIGINAL_WIDTH, event.resize.w);
					Options::newDisplayHeight = Options::displayHeight = std::max(Screen::ORIGINAL_HEIGHT, event.resize.h);
					if (flc.mainscreen != flc.realscreen->getSurface()->getSurface())
					{
						flc.realscreen->resetDisplay();
					}
					else
					{
						flc.realscreen->resetDisplay();
						flc.mainscreen = flc.realscreen->getSurface()->getSurface();
					}
				}
				break;
			case SDL_QUIT:
			  exit(0);
			default:
			break;
		  }
		}
		if (finalFrame) SDL_Delay(50);
	} while (!flc.quit && finalFrame && SDL_GetTicks() - pauseStart < 10000); // 10 sec pause but we're actually just fading out and going to main menu when the music ends
	if (finalFrame) flc.quit = true;
  }
  if (flc.hasAudio)
    FlcDeInitAudio();
//#endif
} /* FlcMain */


#if 0
void FlxplayHelp()
{ printf("FLX player (%s) with SDL output (jasper@il.fontys.nl)\n", version);
  printf("View readme file for more information\n\n");
  printf("flxplay [-l] [filename]\n");
  exit(1);
} /* FlxplayHelp */


main(int argc, char **argv)
{ int c;

  flc.loop=0;
  for(c = 1; argv[c] && (argv[c][0] == '-'); ++c) {
    if(strcmp(argv[c], "-l") == 0) {
      printf("Looping mode\n");
      flc.loop = 1;
    } else {
      FlxplayHelp();
    }
  }
  if(!argv[c]) {
    FlxplayHelp();
  }

  FlcInit(argv[c]);
  FlcMain();
  FlcDeInit();
  exit(0);
} /* main */
#endif

}

}
