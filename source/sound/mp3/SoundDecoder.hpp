/***************************************************************************
 * Copyright (C) 2010
 * by Dimok
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 * must not claim that you wrote the original software. If you use
 * this software in a product, an acknowledgment in the product
 * documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 * must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 *
 * for WiiXplorer 2010
 ***************************************************************************/
#ifndef SOUND_DECODER_HPP
#define SOUND_DECODER_HPP

#include <asndlib.h>
#include "FileOperations/File.hpp"
#include "Tools/BufferCircle.hpp"
#include "Tools/timer.h"
#include "Tools/tools.h"
#include "stdafx.h"

enum
{
	SOUND_RAW = 0,
	SOUND_MP3,
	SOUND_OGG,
	SOUND_WAV,
	SOUND_BNS,
	SOUND_AIF
};

class SoundDecoder
{
public:
	SoundDecoder();
	SoundDecoder(const char * filepath);
	SoundDecoder(const u8 * buffer, int size);
	virtual ~SoundDecoder();
	virtual int Read(u8 * buffer, int buffer_size, int pos);
	virtual int Tell() { return CurPos; }
	virtual int Seek(int pos) { CurPos = pos; return file_fd->seek(CurPos, SEEK_SET); }
	virtual int Rewind();
	virtual u8 GetFormat() { return Format; }
	virtual u16 GetSampleRate() { return SampleRate; }
	virtual void Decode();
	virtual bool IsBufferReady() { return SoundBuffer.IsBufferReady(); }
	virtual u8 * GetBuffer() { return SoundBuffer.GetBuffer(); }
	virtual u32 GetBufferSize() { return SoundBuffer.GetBufferSize(); }
	virtual void LoadNext() { SoundBuffer.LoadNext(); }
	virtual bool IsEOF() { return EndOfFile; }
	virtual void SetLoop(bool l) { Loop = l; }
	virtual u8 GetSoundType() { return SoundType; }
	virtual void ClearBuffer() { SoundBuffer.ClearBuffer(); }
	virtual bool IsStereo() { return (GetFormat() == VOICE_STEREO_16BIT || GetFormat() == VOICE_STEREO_8BIT); }
	virtual bool Is16Bit() { return (GetFormat() == VOICE_STEREO_16BIT || GetFormat() == VOICE_MONO_16BIT); }

	void EnableUpsample(void);
protected:
	void Init();
	void Upsample(s16 *src, s16 *dst, u32 nr_src_samples, u32 nr_dst_samples);

	CFile * file_fd;
	BufferCircle SoundBuffer;
	u8 SoundType;
	u16 whichLoad;
	u16 SoundBlocks;
	int SoundBlockSize;
	int CurPos;
	bool ResampleTo48kHz;
	bool Loop;
	bool EndOfFile;
	bool Decoding;
	bool ExitRequested;
	u8 Format;
	u16 SampleRate;
	u8 *ResampleBuffer;
	u32 ResampleRatio;
};

#endif
