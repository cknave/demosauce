#ifndef _H_MEM_
#define _H_MEM_

#include <malloc.h>
#include <cassert>
#include <cstring>

#if defined(_WIN32)
	#define aligned_malloc(alignment, size) _aligned_malloc(size, alignment)
#elif defined(__unix__)
	#define aligned_malloc(alignment, size) memalign(alignment, size)
#else
	#error "don't have aligned malloc"
#endif

// ffmpeg/sse needs mem aligned to 16 byete bounds
static void* aligned_realloc(void* ptr, size_t size)
{
	if (ptr)
	{
		ptr = realloc(ptr, size);
		if (reinterpret_cast<size_t>(ptr) % 16 != 0)
		{
			void* tmp_ptr = aligned_malloc(16, size);
			memcpy(tmp_ptr, ptr, size);
			free(ptr);
			ptr = tmp_ptr;
		}
	}
	else
		ptr = aligned_malloc(16, size);

	assert(ptr);
	return ptr;
}

#endif // _H_MEM_
