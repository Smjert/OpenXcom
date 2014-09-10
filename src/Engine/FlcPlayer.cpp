/*
* Copyright 2010-2014 OpenXcom Developers.
*
* This file is part of OpenXcom.
*
* OpenXcom is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* OpenXcom is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
* Based on http://www.libsdl.org/projects/flxplay/
*/

#include "FlcPlayer.h"
#include <string.h>
#include <math.h>
#include <SDL_mixer.h>
#include <fstream>

#include "Logger.h"
#include "Exception.h"
#include "Zoom.h"
#include "Screen.h"
#include "Surface.h"
#include "Options.h"
#include "../fmath.h"
#include "Game.h"

namespace OpenXcom
{

enum FileTypes
{
	FLI_TYPE = 0xAF11,
	FLC_TYPE = 0xAF12,
};

// Taken from: http://www.compuphase.com/flic.htm
enum ChunkTypes
{
	COLOR_256 = 0x04,
	FLI_SS2 = 0x07, // or DELTA_FLC
	COLOR_64 = 0x0B,
	FLI_LC = 0x0C, // or DELTA_FLI
	BLACK = 0x0D,
	FLI_BRUN = 0x0F, // or BYTE_RUN
	FLI_COPY = 0x10,

	AUDIO_CHUNK = 0xAAAA, // This is the only exception, it's from TFTD
	PREFIX_CHUNK = 0xF100,
	FRAME_TYPE = 0xF1FA,
};

FlcPlayer::FlcPlayer() : _mainScreen(0), _fileBuf(0), _realScreen(0), _game(0)
{
}

FlcPlayer::~FlcPlayer()
{
	deInitAudio();
	deInit();
}

bool FlcPlayer::init(const char *filename, void(*frameCallBack)(), Game *game, int dx, int dy)
{
	if (_fileBuf != 0)
	{
		Log(LOG_ERROR) << "Trying to init a video player that is already initialized";
		return false;
	}

	_frameCallBack = frameCallBack;
	_realScreen = game->getScreen();
	_game = game;
	Surface* surf = _realScreen->getSurface();
	_dx = dx;
	_dy = dy;
	
	_fileBuf = NULL;
	_fileSize = 0;
	_frameCount = 0;
	_audioFrameData = 0;
	_hasAudio = false;
	_audioData.loadingBuffer = 0;
	_audioData.playingBuffer = 0;

	std::ifstream file;
	file.open(filename, std::ifstream::in | std::ifstream::binary | std::ifstream::ate);
	if (!file.is_open())
	{
		Log(LOG_ERROR) << "Could not open FLI/FLC file: " << filename;
		return false;
	}

	std::streamoff size = file.tellg();
	file.seekg(0, std::ifstream::beg);

	// TODO: substitute with a cross-platform memory mapped file?
	_fileBuf = new Uint8[size];
	_fileSize = size;
	file.read((char *)_fileBuf, size);
	file.close();

	_audioFrameData = _fileBuf + 128;

	// Let's read the first 128 bytes
	readFileHeader();

	// If it's a FLC or FLI file, it's ok
	if (_headerType == SDL_SwapLE16(FLI_TYPE) || (_headerType == SDL_SwapLE16(FLC_TYPE)))
	{
		_screenWidth = _headerWidth;
		_screenHeight = _headerHeight;
		_screenDepth = 8;

		Log(LOG_INFO) << "Playing flx, " << _screenWidth << "x" << _screenHeight << ", " << _headerFrames << " frames";
	}
	else
	{
		Log(LOG_ERROR) << "Flx file failed header check.";
		return false;
	}

	if (_realScreen->getSurface()->getSurface()->format->BitsPerPixel == 8)
	{
		_mainScreen = _realScreen->getSurface()->getSurface();
	}
	else
	{
		_mainScreen = SDL_AllocSurface(SDL_SWSURFACE, _screenWidth, _screenHeight, 8, 0, 0, 0, 0);
	}

	

	return true;
}

void FlcPlayer::deInit()
{
	if (_mainScreen != 0 && _realScreen != 0)
	{
		if (_mainScreen != _realScreen->getSurface()->getSurface())
			SDL_FreeSurface(_mainScreen);
	}

	delete[] _fileBuf;
}

void FlcPlayer::play()
{
	_quit = false;
	bool finalFrame = false;

	// Vertically center the video
	_dy = (_mainScreen->h - _headerHeight) / 2;

	_offset = _dy * _mainScreen->pitch + _mainScreen->format->BytesPerPixel * _dx;

	// Skip file header
	_videoFrameData = _fileBuf + 128;
	_audioFrameData = _videoFrameData;

	while (!_quit)
	{
		if (_frameCallBack)
			(*_frameCallBack)();
		else // TODO: support both, in the case the callback is not some audio?
			decodeAudio(2);

		if (!_quit)
			decodeVideo();
		
		int pauseStart;
		if (isEndOfFile(_videoFrameData))
		{
			pauseStart = SDL_GetTicks();
			finalFrame = true;
		}

		do
		{
			SDLPolling();
		} while (!_quit && finalFrame && pauseStart > (SDL_GetTicks() - 10000));
	}
	
}

void FlcPlayer::SDLPolling()
{
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_MOUSEBUTTONDOWN:
		case SDL_KEYDOWN:
			_quit = true;
			break;
		case SDL_VIDEORESIZE:
			if (Options::allowResize)
			{
				Options::newDisplayWidth = Options::displayWidth = std::max(Screen::ORIGINAL_WIDTH, event.resize.w);
				Options::newDisplayHeight = Options::displayHeight = std::max(Screen::ORIGINAL_HEIGHT, event.resize.h);
				if (_mainScreen != _realScreen->getSurface()->getSurface())
				{
					_realScreen->resetDisplay();
				}
				else
				{
					_realScreen->resetDisplay();
					_mainScreen = _realScreen->getSurface()->getSurface();
				}
			}
			break;
		case SDL_QUIT:
			exit(0);
		default:
			break;
		}
	}
}

void FlcPlayer::readFileHeader()
{
	readU32(_headerSize, _fileBuf);
	readU16(_headerType, _fileBuf + 4);
	readU16(_headerFrames, _fileBuf + 6);
	readU16(_headerWidth, _fileBuf + 8);
	readU16(_headerHeight, _fileBuf + 10);
	readU16(_headerDepth, _fileBuf + 12);
	readU16(_headerSpeed, _fileBuf + 16);

#ifdef DEBUG
	printf("flc.HeaderSize: %d\n", flc.HeaderSize);
	printf("flc.HeaderCheck: %d\n", flc.HeaderCheck);
	printf("_headerFrames: %d\n", _headerFrames);
	printf("flc.HeaderWidth: %d\n", flc.HeaderWidth);
	printf("flc.HeaderHeight: %d\n", flc.HeaderHeight);
	printf("flc.HeaderDepth: %d\n", flc.HeaderDepth);
	printf("flc.HeaderSpeed: %lf\n", flc.HeaderSpeed);
#endif
}

bool FlcPlayer::isValidFrame(Uint8 *frameHeader, Uint32 &frameSize, Uint16 &frameType)
{
	readU32(frameSize, frameHeader);
	readU16(frameType, frameHeader + 4);

#ifdef DEBUG
	printf("flc.FrameSize: %d\n", flc.FrameSize);
	printf("flc.FrameType: %d\n", flc.FrameType);
#endif

	return (frameType == FRAME_TYPE || frameType == AUDIO_CHUNK || frameType == PREFIX_CHUNK);
} 

void FlcPlayer::decodeAudio(int frames)
{
	int audioFramesFound = 0;

	// TODO: delete me
	int lastFrameSize = 0;
	int lastFrameType = 0;
	Uint8 *lastFramePointer = 0;

	while (audioFramesFound < frames && !isEndOfFile(_audioFrameData))
	{
		if (!isValidFrame(_audioFrameData, _audioFrameSize, _audioFrameType))
		{
			_quit = true;
			break;
		}

		lastFrameSize = _audioFrameSize;
		lastFrameType = _audioFrameType;

		switch (_audioFrameType)
		{
			case FRAME_TYPE:
			case PREFIX_CHUNK:
				_audioFrameData += _audioFrameSize;
				break;
			case AUDIO_CHUNK:
				Uint16 sampleRate;

				readU16(sampleRate, _audioFrameData + 8);

				_chunkData = _audioFrameData + 16;

				playAudioFrame(sampleRate);

				_audioFrameData += _audioFrameSize + 16;

				++audioFramesFound;

				break;
		}	
	}
}

void FlcPlayer::decodeVideo()
{
	bool videoFrameFound = false;
	
	while (!videoFrameFound)
	{
		if (!isValidFrame(_videoFrameData, _videoFrameSize, _videoFrameType))
		{
			_quit = true;
			break;
		}

		switch (_videoFrameType)
		{
		case FRAME_TYPE:

			Uint32 delay;

			readU16(_frameChunks, _videoFrameData + 6);
			readU16(_delayOverride, _videoFrameData + 8);

			if (_headerType == FLI_TYPE)
			{
				delay = _delayOverride > 0 ? _delayOverride : _headerSpeed * (1000.0 / 70.0);
			}
			else
			{
				delay = _videoDelay;
			}

			waitForNextFrame(delay);

			// Skip the frame header, we are not interested in the rest
			_chunkData = _videoFrameData + 16;
			playVideoFrame();

			_videoFrameData += _videoFrameSize;

			videoFrameFound = true;

			break;
		case AUDIO_CHUNK:
			_videoFrameData += _videoFrameSize + 16;
			break;
		case PREFIX_CHUNK:
			// Just skip it
			_videoFrameData += _videoFrameSize;

			break;
		}
	}
}

void FlcPlayer::playVideoFrame()
{
	++_frameCount;
	if (SDL_LockSurface(_mainScreen) < 0)
		return;
	int chunkCount = _frameChunks;

	for (int i = 0; i < chunkCount; ++i)
	{
		readU32(_chunkSize, _chunkData);
		readU16(_chunkType, _chunkData + 4);

		switch (_chunkType)
		{
			case COLOR_256:
				color256();
				break;
			case FLI_SS2:
				fliSS2();
				break;
			case COLOR_64:
				color64();
				break;
			case FLI_LC:
				fliLC();
				break;
			case BLACK:
				black();
				break;
			case FLI_BRUN:
				fliBRun();
				break;
			case FLI_COPY:
				fliCopy();
				break;
			case 18:
				break;
			default:
				Log(LOG_WARNING) << "Ieek an non implemented chunk type:" << _chunkType;
				break;
		}

		_chunkData += _chunkSize;
	}

	SDL_UnlockSurface(_mainScreen);

	/* TODO: Track which rectangles have really changed */
	//SDL_UpdateRect(_mainScreen, 0, 0, 0, 0);
	if (_mainScreen != _realScreen->getSurface()->getSurface())
		SDL_BlitSurface(_mainScreen, 0, _realScreen->getSurface()->getSurface(), 0);

	_realScreen->flip();
}

void FlcPlayer::playAudioFrame(Uint16 sampleRate)
{
	/* TFTD audio header (10 bytes)
	* Uint16 unknown1 - always 0
	* Uint16 sampleRate
	* Uint16 unknown2 - always 1 (Channels? bytes per sample?)
	* Uint16 unknown3 - always 10 (No idea)
	* Uint16 unknown4 - always 0
	* Uint8[] unsigned 1-byte 1-channel PCM data of length _chunkSize_ (so the total chunk is _chunkSize_ + 6-byte flc header + 10 byte audio header */

	if (!_hasAudio)
	{
		_audioData.sampleRate = sampleRate;
		_hasAudio = true;
		initAudio(AUDIO_U8, 1);
	}
	else
	{
		/* Cannot change sample rate mid-video */
		assert(sampleRate == _audioData.sampleRate);
	}

	SDL_SemWait(_audioData.sharedLock);
	AudioBuffer *loadingBuff = _audioData.loadingBuffer;
	assert(loadingBuff->currSamplePos == 0);
	int newSize = (_audioFrameSize + loadingBuff->sampleCount);
	if (newSize > loadingBuff->sampleBufSize)
	{
		/* If the sample count has changed, we need to reallocate (Handles initial state
		* of '0' sample count too, as realloc(NULL, size) == malloc(size) */
		loadingBuff->samples = (char*)realloc(loadingBuff->samples, newSize);
		loadingBuff->sampleBufSize = newSize;
	}

	/* Copy the data.... */
	memcpy(loadingBuff->samples + loadingBuff->sampleCount, _chunkData, _audioFrameSize);
	loadingBuff->sampleCount += _audioFrameSize;

	SDL_SemPost(_audioData.sharedLock);

	
}
void FlcPlayer::color256()
{
	Uint8 *pSrc;
	Uint16 numColorPackets;
	Uint16 numColors;
	Uint8 numColorsSkip;

	pSrc = _chunkData + 6;
	readU16(numColorPackets, pSrc);
	pSrc += 2;

	while (numColorPackets--) 
	{
		numColorsSkip = *(pSrc++);
		numColors = *(pSrc++);
		if (numColors == 0)
		{
			numColors = 256;
		}

		for (int i = 0; i < numColors; ++i)
		{
			_colors[i].r = *(pSrc++);
			_colors[i].g = *(pSrc++);
			_colors[i].b = *(pSrc++);
		}

		_realScreen->setPalette(_colors, numColorsSkip, numColors, true);
	}
}

void FlcPlayer::fliSS2()
{
	Uint8 *pSrc, *pDst, *pTmpDst;
	Sint8 CountData;
	Uint8 ColumSkip, Fill1, Fill2;
	Uint16 Lines;
	Sint16 Count;
	bool setLastByte = false;
	Uint8 lastByte = 0;

	pSrc = _chunkData + 6;
	pDst = (Uint8*)_mainScreen->pixels + _offset;
	readU16(Lines, pSrc);

	pSrc += 2;

	while (Lines--) 
	{
		readS16(Count, (Sint8 *)pSrc);
		pSrc += 2;

		if ((Count & 0xc000) == 0xc000) // 0xc000h = 1100000000000000
		{  
			pDst += (-Count)*_mainScreen->pitch;
			++Lines;
			continue;
		}
			
		else if ((Count & 0x8000) == 0x8000) // 0x8000h = 1000000000000000
		{  
			/* Upper bits 01 - Last pixel
			*/
			setLastByte = true;
			lastByte = (Count & 0x00FF);
			readS16(Count, (Sint8 *)pSrc);
			pSrc += 2;
		}

		if ((Count & SDL_SwapLE16(0xc000)) == 0x0000) // 0xc000h = 1100000000000000
		{      
			pTmpDst = pDst;
			while (Count--) 
			{
				ColumSkip = *(pSrc++);
				pTmpDst += ColumSkip;
				CountData = *(pSrc++);

				if (CountData > 0) 
				{
					std::copy(pSrc, pSrc + (2 * CountData), pTmpDst);
					pTmpDst += (2 * CountData);
					pSrc += (2 * CountData);
				}
				else 
				{
					if (CountData < 0) 
					{
						CountData = -CountData;

						Fill1 = *(pSrc++);
						Fill2 = *(pSrc++);
						while (CountData--)
						{
							*(pTmpDst++) = Fill1;
							*(pTmpDst++) = Fill2;
						}
					}
				}
			}

			if (setLastByte)
			{
				setLastByte = false;
				*(pDst + _mainScreen->pitch - 1) = lastByte;
			}
			pDst += _mainScreen->pitch;
		}
	}
}

void FlcPlayer::fliBRun()
{
	Uint8 *pSrc, *pDst, *pTmpDst, Fill;
	Sint8 CountData;
	int HeightCount, PacketsCount;

	HeightCount = _headerHeight;
	pSrc = _chunkData + 6; // Skip chunk header
	pDst = (Uint8*)_mainScreen->pixels + _offset;

	while (HeightCount--) 
	{
		pTmpDst = pDst;
		PacketsCount = *(pSrc++);

		int pixels = 0;
		while (pixels != _headerWidth)
		{
			CountData = *(pSrc++);
			if (CountData > 0) 
			{
				Fill = *(pSrc++);

				std::fill_n(pTmpDst, CountData, Fill);
				pTmpDst += CountData;
				pixels += CountData;
			}
			else 
			{
				if (CountData < 0) 
				{
					CountData = -CountData;

					std::copy(pSrc, pSrc + CountData, pTmpDst);
					pTmpDst += CountData;
					pSrc += CountData;
					pixels += CountData;
				}
			}
		}
		pDst += _mainScreen->pitch;
	}
}


void FlcPlayer::fliLC()
{
	Uint8 *pSrc, *pDst, *pTmpDst;
	Sint8 CountData;
	Uint8 CountSkip;
	Uint8 Fill;
	Uint16 Lines, tmp;
	int PacketsCount;

	pSrc = _chunkData + 6;
	pDst = (Uint8*)_mainScreen->pixels + _offset;

	readU16(tmp, pSrc);
	pSrc += 2;
	pDst += tmp*_mainScreen->pitch;
	readU16(Lines, pSrc);
	pSrc += 2;

	while (Lines--) 
	{
		pTmpDst = pDst;
		PacketsCount = *(pSrc++);

		while (PacketsCount--) 
		{
			CountSkip = *(pSrc++);
			pTmpDst += CountSkip;
			CountData = *(pSrc++);
			if (CountData > 0) 
			{
				while (CountData--) 
				{
					*(pTmpDst++) = *(pSrc++);
				}
			}
			else 
			{
				if (CountData < 0) 
				{
					CountData = -CountData;

					Fill = *(pSrc++);
					while (CountData--) 
					{
						*(pTmpDst++) = Fill;
					}
				}
			}
		}
		pDst += _mainScreen->pitch;
	}
}

void FlcPlayer::color64()
{
	Uint8 *pSrc;
	Uint16 NumColors, NumColorPackets;
	Uint8 NumColorsSkip;
	int i;

	pSrc = _chunkData + 6;
	readU16(NumColorPackets, pSrc);
	pSrc += 2;

	while (NumColorPackets--) 
	{
		NumColorsSkip = *(pSrc++);
		NumColors = *(pSrc++);

		if (NumColors == 0)
		{
			NumColors = 256;
		}

		for (int i = 0; i < NumColors; ++i)
		{
			_colors[i].r = *(pSrc++) << 2;
			_colors[i].g = *(pSrc++) << 2;
			_colors[i].b = *(pSrc++) << 2;
		}

		_realScreen->setPalette(_colors, NumColorsSkip, NumColors, true);
	}
}

void FlcPlayer::fliCopy()
{
	Uint8 *pSrc, *pDst;
	int Lines = _screenHeight;
	pSrc = _chunkData + 6;
	pDst = (Uint8*)_mainScreen->pixels + _offset;

	while (Lines--) 
	{
		memcpy(pDst, pSrc, _screenWidth);
		pSrc += _screenWidth;
		pDst += _mainScreen->pitch;
	}
}

void FlcPlayer::black()
{
	Uint8 *pDst;
	int Lines = _screenHeight;
	pDst = (Uint8*)_mainScreen->pixels + _offset;

	while (Lines-- > 0) 
	{
		memset(pDst, 0, _screenHeight);
		pDst += _mainScreen->pitch;
	}
} /* BLACK */

#ifndef MIN
/* FIXME: Only works on expressions with no side effects */
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif

void FlcPlayer::audioCallback(void *userData, Uint8 *stream, int len)
{
	AudioData *audio = (AudioData*)userData;

	AudioBuffer *playBuff = audio->playingBuffer;

	while (len > 0)
	{
		int samplesToCopy = 0;
		if (playBuff->sampleCount > 0)
		{
			int samplesToCopy = MIN(len, playBuff->sampleCount);
			memcpy(stream, playBuff->samples + playBuff->currSamplePos, samplesToCopy);

			playBuff->currSamplePos += samplesToCopy;
			playBuff->sampleCount -= samplesToCopy;
			len -= samplesToCopy;

			assert(playBuff->sampleCount >= 0);
		}

		if (len > 0)
		{
			/* Need to swap buffers */
			size_t samplesCopied = samplesToCopy;
			playBuff->currSamplePos = 0;

			SDL_SemWait(audio->sharedLock);
			AudioBuffer *tempBuff = playBuff;
			audio->playingBuffer = playBuff = audio->loadingBuffer;
			audio->loadingBuffer = tempBuff;
			SDL_SemPost(audio->sharedLock);

			if (playBuff->sampleCount == 0)
				break;
		}
	}
}

void FlcPlayer::initAudio(Uint16 format, Uint8 channels)
{
	int err;

	err = Mix_OpenAudio(_audioData.sampleRate, format, channels, _audioFrameSize);
	_videoDelay = 1000 / (_audioData.sampleRate / _audioFrameSize);

	if (err)
	{
		printf("Failed to open audio (%d)\n", err);
		return;
	}

	/* Start runnable */
	_audioData.sharedLock = SDL_CreateSemaphore(1);

	_audioData.loadingBuffer = new AudioBuffer();
	_audioData.loadingBuffer->currSamplePos = 0;
	_audioData.loadingBuffer->sampleCount = 0;
	_audioData.loadingBuffer->samples = (char *)malloc(_audioFrameSize);
	_audioData.loadingBuffer->sampleBufSize = _audioFrameSize;

	_audioData.playingBuffer = new AudioBuffer();
	_audioData.playingBuffer->currSamplePos = 0;
	_audioData.playingBuffer->sampleCount = 0;
	_audioData.playingBuffer->samples = (char *)malloc(_audioFrameSize);
	_audioData.playingBuffer->sampleBufSize = _audioFrameSize;

	Mix_HookMusic(FlcPlayer::audioCallback, &_audioData);
}

void FlcPlayer::deInitAudio()
{
	if (_game)
	{
		Mix_HookMusic(NULL, NULL);
		Mix_CloseAudio();
		_game->initAudio();
	}
	else
		SDL_DestroySemaphore(_audioData.sharedLock);

	if (_audioData.loadingBuffer)
	{
		free(_audioData.loadingBuffer->samples);
		delete _audioData.loadingBuffer;
	}
	_audioData.loadingBuffer = 0;

	if (_audioData.playingBuffer)
	{
		free(_audioData.playingBuffer->samples);
		delete _audioData.playingBuffer;
	}
	_audioData.playingBuffer = 0;
}

void FlcPlayer::stop()
{
	_quit = true;
}

bool FlcPlayer::isEndOfFile(Uint8 *pos)
{
	return (pos - _fileBuf) == _fileSize;
}

int FlcPlayer::getFrameCount()
{
	return _frameCount;
}

void FlcPlayer::setHeaderSpeed(int speed)
{
	_headerSpeed = speed;
}

void FlcPlayer::waitForNextFrame(Uint32 delay)
{
	static Uint32 oldTick = 0;
	int newTick;
	int currentTick;

	currentTick = SDL_GetTicks();
	if (oldTick == 0)
	{
		oldTick = currentTick;
		newTick = oldTick;
	}
	else
		newTick = oldTick + delay;

	if (_hasAudio)
	{
		while (currentTick < newTick)
		{
			while ((newTick - currentTick) > 10 && !isEndOfFile(_audioFrameData))
			{
				decodeAudio(1);
				currentTick = SDL_GetTicks();
			}
			SDL_Delay(1);
			currentTick = SDL_GetTicks();
		}
	}
	else
	{
		while (currentTick < newTick)
		{
			SDL_Delay(1);
			currentTick = SDL_GetTicks();
		}
	}
	oldTick = SDL_GetTicks();
} 

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
inline void FlcPlayer::readU16(Uint16 &dst, const Uint8 * const src)
{
	dst = (src[0] << 8) | src[1];
}
inline void FlcPlayer::readU32(Uint32 &dst, const Uint8 * const src)
{
	dst = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
}
inline void FlcPlayer::readS16(Sint16 &dst, const Sint8 * const src)
{
	dst = (src[0] << 8) | src[1];
}
inline void FlcPlayer::readS32(Sint32 &dst, const Sint8 * const src)
{
	dst = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
}
#else
inline void FlcPlayer::readU16(Uint16 &dst, const Uint8 * const src)
{
	dst = (src[1] << 8) | src[0];
}
inline void FlcPlayer::readU32(Uint32 &dst, const Uint8 * const src)
{
	dst = (src[3] << 24) | (src[2] << 16) | (src[1] << 8) | src[0];
}
inline void FlcPlayer::readS16(Sint16 &dst, const Sint8 * const src)
{
	dst = (src[1] << 8) | src[0];
}
inline void FlcPlayer::readS32(Sint32 &dst, const Sint8 * const src)
{
	dst = (src[3] << 24) | (src[2] << 16) | (src[1] << 8) | src[0];
}
#endif

}
