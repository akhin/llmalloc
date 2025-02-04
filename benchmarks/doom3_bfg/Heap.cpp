/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2012 Robert Beckebans
Copyright (C) 2012 Daniel Gibson

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop

//===============================================================
//
//	memory allocation all in one place
//
//===============================================================
#include <stdlib.h>
#undef new

/*
==================
Mem_Alloc16
==================
*/
// RB: 64 bit fixes, changed int to size_t

#define USE_MIMALLOC
//#define USE_LLMALLOC
#define SAMPLING_INTERVAL 100000

#ifdef USE_LLMALLOC
#define USE_ALLOC_HEADERS
#include "llmalloc.h"
#define SAMPLES_PATH  "c:\\tmp\\samples_llmalloc.txt"
#else
#ifdef USE_MIMALLOC
#pragma comment(lib, "mimalloc.lib")
#include <mimalloc.h>
#include <mimalloc-override.h>
#define SAMPLES_PATH  "c:\\tmp\\samples_mimalloc.txt"
#else
#define SAMPLES_PATH  "c:\\tmp\\samples_msvc.txt"
#endif
#endif




void* Mem_Alloc16( const size_t size, const memTag_t tag )
// RB end
{
	unsigned int model_specific_register_contents;
	static unsigned long long start = __rdtscp(&model_specific_register_contents);
	static unsigned long long end = 0;
	static unsigned int g_measurement_counter = 0;
	//////////////////////////////////////////////////////////////////////////////////
	if (!size)
	{
		return nullptr;
	}

	void* ret = nullptr;

	#ifdef USE_LLMALLOC
	static std::atomic<bool> llmalloc_initialised = false;

	if (llmalloc_initialised == false)
	{
		llmalloc::ScalableMallocOptions options;
		llmalloc::ScalableMalloc::get_instance().create(options);
		llmalloc_initialised = true;
	}

	ret = llmalloc::ScalableMalloc::get_instance().allocate(size);
	#else
	#ifdef USE_MIMALLOC
	ret = mi_malloc(size);
	#else
	const size_t paddedSize = (size + 15) & ~15;
	#ifdef _WIN32
	ret = _aligned_malloc(paddedSize, 16);
	#elif __linux__
	posix_memalign(&ret, 16, paddedSize);
	#endif
	#endif
	#endif

	if (g_measurement_counter >= SAMPLING_INTERVAL)
	{
        // No allocations happening in this code block
		auto current = __rdtscp(&model_specific_register_contents);
		FILE* file = std::fopen(SAMPLES_PATH, "a");

		if (file != nullptr)
		{
			fprintf(file, "%llu\n", current - start);
			fclose(file);
		}
		start = __rdtscp(&model_specific_register_contents);
		g_measurement_counter = 0;
	}
	else
	{
		g_measurement_counter++;
	}

	return ret;
}

/*
==================
Mem_Free16
==================
*/
void Mem_Free16( void* ptr )
{
	#ifdef USE_LLMALLOC
	llmalloc::ScalableMalloc::get_instance().deallocate(ptr);
	#else
	#ifdef USE_MIMALLOC
	mi_free(ptr);
	#else
	if( ptr == NULL )
	{
		return;
	}
	#ifdef _WIN32
	_aligned_free( ptr );
	#else // not _WIN32
	// DG: Linux/POSIX compatibility
	// can use normal free() for aligned memory
	free( ptr );
	// DG end
	#endif 
	#endif 
	#endif
}

/*
==================
Mem_ClearedAlloc
==================
*/
void* Mem_ClearedAlloc( const size_t size, const memTag_t tag )
{
	void* mem = Mem_Alloc( size, tag );
	SIMDProcessor->Memset( mem, 0, size );
	return mem;
}

/*
==================
Mem_CopyString
==================
*/
char* Mem_CopyString( const char* in )
{
	char* out = ( char* )Mem_Alloc( strlen( in ) + 1, TAG_STRING );
	strcpy( out, in );
	return out;
}

