/*
	NDI utility functions

	using the NDI SDK to receive frames from the network

	https://ndi.video

	Copyright (C) 2016-2026 Lynn Jarvis.

	http://www.spout.zeal.co

	=========================================================================
	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
	=========================================================================

	16.10.16 - Create file
	22.02.17 - Changed lower size limit for SSE copy to 640x480
	30.03.18 - const source for memcpy_sse2 and rgba_bgra_sse2

	Changes with update to 3.5
	11.06.18 - __movsd for OSX (https://github.com/ThomasLengeling/ofxNDI)
			 - _rotl replacement for OSX

	03.12-19 - changes for Ubuntu/ARM (https://github.com/IDArnhem/ofxNDI)
			 - TODO bring up to date with Spout SDK
	04.12.19 - Cleanup
			 - TODO - use TARGET_LINUX_ARM for SSE functions?
	05.12.19 - Clean all functions
			 - justify targets with compiler definitions
	07.12.19 - rgba_bgra use more portable known-size types
			   unsigned __int32 > uint32_t (https://github.com/IDArnhem/ofxNDI)
	22.08.21 - CopyImage overloads 
			     Previous version compatibility with rgba<>bgra and invert options
				 Same size as dest with invert option
				 Line-by-line with source and dest pitch
	15.11.21 - Add timing functions
	18.11.21 - unsigned __int32 > uint32_t (https://github.com/leadedge/ofxNDI/issues/23)
			   Replace CopyMemory with memcpy for MacOS compatibility
	23.04.22 - CopyImage - Use size_t cast for memcpy functions
			   to avoid warning C26451: Arithmetic overflow
	09.06.22 - rgba_bgra unsigned __int32 > uint32_t (https://github.com/leadedge/ofxNDI/issues/34)
	10.06.22 - rgba_bgra_sse2 remove uint32_t declarations for src and dst
	12.06.24 - Add HoldFps with timeBeginPeriod/timeEndPeriod for Windows
	14.05.24 - Corrected #ifdef WIN32 -> #if defined(TARGET_WIN32)
	15.05.24 - Remove anonymous namespace for UINT PeriodMin
	15.05.24 - Correct missing #endif for #ifdef USE_CHRONO
			   Remove extra #endif at file end
	19.05.24 - Add GetVersion() - return addon version number string
	30.05.24 - Revise YUV422_to_RGBA conversion equations
	16.09.24 - change UINT to uint32_t PeriodMin
	12-04-25 - Add rgb2rgba
	20.12.25 - Update to NDI version 6.2.1.0
	01.01.26 - Revise YUV422_to_RGBA with lookup tables
			   Gain 7.9 > 5.5 msec at 1920x1080
			   ofxNDI version 2.001.000
	09-02-26 - Add Audio functions
	03-03-26 - ofxNDI version 2.002.000
	20-05-26 - Add MessageDialog functions
			   from SpoutUtils - SpoutMessageBox
			   Add MessageDialogCancel to add a caption 'X'
			   Update ofxNDI version to 2.003.000

*/
#include "ofxNDIutils.h"

// _rotl replacement
// Other solutions possible
// https://stackoverflow.com/questions/776508/best-practices-for-circular-shift-rotate-operations-in-c
#define BitsCount( val ) ( sizeof( val ) * CHAR_BIT )
#define Shift( val, steps ) ( steps % BitsCount( val ) )
#define ROL( val, steps ) ( ( val << Shift( val, steps ) ) | ( val >> ( BitsCount( val ) - Shift( val, steps ) ) ) )
#define ROR( val, steps ) ( ( val >> Shift( val, steps ) ) | ( val << ( BitsCount( val )

namespace ofxNDIutils {

	// ofxNDI version number string
	// Major, minor, release
	std::string ofxNDIversion = "2.003.000";

#ifdef USE_CHRONO
	// Timing counters
	std::chrono::steady_clock::time_point start;
	std::chrono::steady_clock::time_point end;
	// For HoldFps
	std::chrono::steady_clock::time_point FrameStartPtr;
	std::chrono::steady_clock::time_point FrameEndPtr;
	uint32_t PeriodMin = 0;
#endif

#if defined (__APPLE__)

	static inline void *__movsd(void *d, const void *s, size_t n) {
#if defined(__aarch64__)
        return memcpy(d, s, n);
#else
		asm volatile ("rep movsb"
			: "=D" (d),
			"=S" (s),
			"=c" (n)
			: "0" (d),
			"1" (s),
			"2" (n)
			: "memory");
		return d;
#endif
    }
#endif

	//
	// Image pixel copy
	//

#if defined(TARGET_WIN32) || defined (TARGET_OSX)

	// movsd requires 4 byte aligned data
	void memcpy_movsd(void* dst, const void* src, size_t Size)
	{
		// one DWORD per rep move
		const unsigned long *pSrc = static_cast<const unsigned long *>(src); // Source buffer
		unsigned long *pDst = static_cast<unsigned long *>(dst); // Dest buffer
		__movsd(pDst, pSrc, Size >> 2); //Size divided by 4 (4 bytes per rep move)
	}

	//
	// Fast memcpy
	//
	// Original source - William Chan
	// (dead link) http://williamchan.ca/portfolio/assembly/ssememcpy/
	// See also :
	//	http://stackoverflow.com/questions/1715224/very-fast-memcpy-for-image-processing
	//	http://www.gamedev.net/topic/502313-special-case---faster-than-memcpy/
	//	and others.
	//
	// Approx 1.7 times speed of memcpy (0.84 msec per frame 1920x1080)
	//
	void memcpy_sse2(void* dst, const void* src, size_t Size)
	{
		char * pSrc = (char *)src;				  // Source buffer
		char * pDst = (char *)dst;				  // Destination buffer
		unsigned int n = (unsigned int)Size >> 7; // Counter = size divided by 128 (8 * 128bit registers)

		__m128i Reg0, Reg1, Reg2, Reg3, Reg4, Reg5, Reg6, Reg7;
		for (unsigned int Index = n; Index > 0; --Index) {

			// SSE2 prefetch
			_mm_prefetch(pSrc + 256, _MM_HINT_NTA);
			_mm_prefetch(pSrc + 256 + 64, _MM_HINT_NTA);

			// move data from src to registers
			// 8 x 128 bit (16 bytes each)
			// Increment source pointer by 16 bytes each
			// for a total of 128 bytes per cycle
			Reg0 = _mm_load_si128((__m128i *)(pSrc));
			Reg1 = _mm_load_si128((__m128i *)(pSrc + 16));
			Reg2 = _mm_load_si128((__m128i *)(pSrc + 32));
			Reg3 = _mm_load_si128((__m128i *)(pSrc + 48));
			Reg4 = _mm_load_si128((__m128i *)(pSrc + 64));
			Reg5 = _mm_load_si128((__m128i *)(pSrc + 80));
			Reg6 = _mm_load_si128((__m128i *)(pSrc + 96));
			Reg7 = _mm_load_si128((__m128i *)(pSrc + 112));

			// move data from registers to dest
			_mm_stream_si128((__m128i *)(pDst), Reg0);
			_mm_stream_si128((__m128i *)(pDst + 16), Reg1);
			_mm_stream_si128((__m128i *)(pDst + 32), Reg2);
			_mm_stream_si128((__m128i *)(pDst + 48), Reg3);
			_mm_stream_si128((__m128i *)(pDst + 64), Reg4);
			_mm_stream_si128((__m128i *)(pDst + 80), Reg5);
			_mm_stream_si128((__m128i *)(pDst + 96), Reg6);
			_mm_stream_si128((__m128i *)(pDst + 112), Reg7);

			pSrc += 128;
			pDst += 128;
		}
	} // end memcpy_sse2


	//
	// Adapted from : https://searchcode.com/codesearch/view/5070982/
	// 
	// Copyright (c) 2002-2010 The ANGLE Project Authors. All rights reserved.
	// Use of this source code is governed by a BSD-style license that can be
	// found in the LICENSE file.
	//
	// https://chromium.googlesource.com/angle/angle/+/master/LICENSE
	//
	// All instructions SSE2.
	//
	void rgba_bgra_sse2(const void *source, void *dest, unsigned int width, unsigned int height, bool bInvert)
	{
		unsigned int y = 0;
		__m128i brMask = _mm_set1_epi32(0x00ff00ff); // argb

		for (y = 0; y < height; y++) {

			// Start of buffer
			auto src = static_cast<const uint32_t*>(source); // unsigned int = 4 bytes
			auto dst = static_cast<uint32_t*>(dest);

			// Cast first to avoid warning C26451: Arithmetic overflow
			unsigned long H1YxW = (unsigned long)((height - 1 - y) * width);
			unsigned long YxW = (unsigned long)(y * width);

			// Increment to current line
			if (bInvert)
				src += H1YxW;
			else
				src += YxW;

			dst += YxW; // dest is not inverted

			// Make output writes aligned
			unsigned int x;
			for (x = 0; ((reinterpret_cast<intptr_t>(&dst[x]) & 15) != 0) && x < width; x++) {
				auto rgbapix = src[x];
				// rgbapix << 16		: a r g b > g b a r
				//        & 0x00ff00ff  : r g b . > . b . r
				// rgbapix & 0xff00ff00 : a r g b > a . g .
				// result of or			:           a b g r
#if defined(TARGET_WIN32)
// _rotl is available
				dst[x] = (_rotl(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#else
// _rotl replacement
				dst[x] = (ROL(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#endif
			}

			for (; x + 3 < width; x += 4) {
				__m128i sourceData = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[x]));
				// Mask out g and a, which don't change
				__m128i gaComponents = _mm_andnot_si128(brMask, sourceData);
				// Mask out b and r
				__m128i brComponents = _mm_and_si128(sourceData, brMask);
				// Swap b and r
				__m128i brSwapped = _mm_shufflehi_epi16(_mm_shufflelo_epi16(brComponents, _MM_SHUFFLE(2, 3, 0, 1)), _MM_SHUFFLE(2, 3, 0, 1));
				__m128i result = _mm_or_si128(gaComponents, brSwapped);
				_mm_store_si128(reinterpret_cast<__m128i*>(&dst[x]), result);
			}

			// Perform leftover writes
			for (; x < width; x++) {
				auto rgbapix = src[x];
#if defined(TARGET_WIN32)
				// _rotl is available
				dst[x] = (_rotl(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#else
				// _rotl replacement
				dst[x] = (ROL(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#endif
			}

		}
	} // end rgba_bgra_sse2

#endif // endif TARGET_WIN32 || TARGET_OSX

	// Without SSE
	void rgba_bgra(const void *rgba_source, void *bgra_dest,
		unsigned int width, unsigned int height, bool bInvert)
	{

		for (unsigned int y = 0; y < height; y++) {

			// Start of buffer
			auto source = static_cast<const uint32_t*>(rgba_source);; // unsigned int = 4 bytes
			auto dest = static_cast<uint32_t*>(bgra_dest);

			// Cast first to avoid warning C26451: Arithmetic overflow
			unsigned long H1YxW = (unsigned long)((height - 1 - y) * width);
			unsigned long YxW = (unsigned long)(y * width);

			// Increment to current line
			if (bInvert) {
				source += H1YxW;
				dest += YxW; // dest is not inverted
			}
			else {
				source += YxW;
				dest += YxW;
			}

			for (unsigned int x = 0; x < width; x++) {
				auto rgbapix = source[x];
#if defined(TARGET_WIN32)
				// _rotl is available
				dest[x] = (_rotl(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#else
				// _rotl replacement
				dest[x] = (ROL(rgbapix, 16) & 0x00ff00ff) | (rgbapix & 0xff00ff00);
#endif
			}

		}

	} // end rgba_bgra


	// Flip a buffer in place
	void FlipBuffer(const unsigned char *src,
		unsigned char *dst,
		unsigned int width,
		unsigned int height)
	{
		const unsigned char * From = src;
		unsigned char * To = dst;
		unsigned int pitch = width * 4; // RGBA default
		unsigned int line_s = 0;
		unsigned int line_t = (height - 1)*pitch;

		for (unsigned int y = 0; y < height; y++) {
			// @zilog no SSE stuff for aarch64 build
#if defined(TARGET_WIN32) || defined (TARGET_OSX)
			if (width <= 512 || height <= 512) // too small for assembler
				memcpy((void *)(To + line_t), (void *)(From + line_s), pitch);
			else if ((pitch % 16) == 0) // use sse assembler function
				memcpy_sse2((void *)(To + line_t), (void *)(From + line_s), pitch);
			else if ((pitch % 4) == 0) // use 4 byte move assembler function
				memcpy_movsd((unsigned long *)(To + line_t), (unsigned long *)(From + line_s), pitch);
			else
#endif
				memcpy((void *)(To + line_t), (void *)(From + line_s), pitch);

			line_s += pitch;
			line_t -= pitch;
		}
	} // end FlipBuffer

	//
	// Flip an image vertically
	//
	// http://www.codeproject.com/Questions/369873/How-can-i-flip-the-image-Vertically-using-cplusplu
	//
	bool FlipVertical(unsigned char *inbuf, long widthBytes, long height)
	{
		unsigned char *tb1;
		unsigned char *tb2;
		long bufsize;
		long row_cnt;
		long off1 = 0;
		long off2 = 0;

		if (inbuf == NULL)
			return false;

		bufsize = widthBytes * 4;

		tb1 = (unsigned char *)malloc(bufsize);
		if (tb1 == NULL) {
			return false;
		}

		tb2 = (unsigned char *)malloc(bufsize);
		if (tb2 == NULL) {
			free((void *)tb1);
			return false;
		}

		for (row_cnt = 0; row_cnt < (height + 1) / 2; row_cnt++)
		{
			off1 = row_cnt * bufsize;
			off2 = ((height - 1) - row_cnt)*bufsize;
			memcpy((void *)tb1, (void *)(inbuf + off1), bufsize * sizeof(unsigned char));
			memcpy((void *)tb2, (void *)(inbuf + off2), bufsize * sizeof(unsigned char));
			memcpy((void *)(inbuf + off1), (void *)tb2, bufsize * sizeof(unsigned char));
			memcpy((void *)(inbuf + off2), (void *)tb1, bufsize * sizeof(unsigned char));
		}

		free((void*)tb1);
		free((void*)tb2);

		return true;
	}

	// ofxNDI version number string
	// Major, minor, release
	std::string GetVersion()
	{
		return ofxNDIversion;
	}

	// Copy rgba source image to dest.
	// Images must be the same size with no line padding.
	// Option flip image vertically (invert).
	void CopyImage(const unsigned char *source, unsigned char *dest,
		unsigned int width, unsigned int height, bool bInvert)
	{
		CopyImage(source, dest,	width, height, width*4, false, bInvert);

	} // end CopyImage

	// Copy rgba source image to dest.
	// Source line pitch (unused).
	// Option convert bgra<>rgba.
	// Option flip image vertically (invert).
	void CopyImage(const unsigned char *source, unsigned char *dest,
		unsigned int width, unsigned int height, unsigned int stride,
		bool bSwapRB, bool bInvert)
	{
		if (source == nullptr || dest == nullptr)
			return;

		// user requires bgra->rgba or rgba->bgra conversion from source to dest
		if (bSwapRB) {
#if defined(TARGET_WIN32) || defined (TARGET_OSX)
			rgba_bgra_sse2((const void *)source, (void *)dest, width, height, bInvert);
#else
			rgba_bgra((const void *)source, (void *)dest, width, height, bInvert);
#endif
			return;
		}

		if (bInvert) { // Flip the image in place
			FlipBuffer(source, dest, width, height);
		}
		else {
#if defined(TARGET_WIN32) || defined (TARGET_OSX)
			// Small image just use memcpy
			if (width < 512 || height < 256) {
				memcpy((void *)dest, (const void *)source, (size_t)height* (size_t)stride);
			}
			else if ((stride % 16) == 0) { // 16 byte aligned
				memcpy_sse2((void *)dest, (const void *)source, (size_t)height* (size_t)stride);
			}
			else if ((stride % 4) == 0) { // 4 byte aligned
				memcpy_movsd((void*)dest, (const void *)source, (size_t)height* (size_t)stride);
			}
#else
			// @zilog no SSE stuff for aarch64 build
			memcpy((void *)dest, (const void *)source, (size_t)height* (size_t)stride);
#endif
		}
	} // end CopyImage


	// Copy rgba image buffers line by line.
	// Allow for both source and destination line pitch.
	// Option flip image vertically (invert).
	void CopyImage(const void* rgba_source, void* rgba_dest,
		unsigned int width, unsigned int height,
		unsigned int sourcePitch, unsigned int destPitch,
		bool bInvert)
	{
		// For all rows
		for (unsigned int y = 0; y < height; y++) {
			// Start of buffers
			auto source = static_cast<const uint32_t *>(rgba_source); // unsigned int = 4 bytes
			auto dest = static_cast<uint32_t *>(rgba_dest);
			// Increment to current line
			// Pitch is line length in bytes. Divide by 4 to get the width in rgba pixels.
			if (bInvert) {
				source += (unsigned long)((height - 1 - y)*sourcePitch / 4);
				dest   += (unsigned long)(y * destPitch / 4); // dest is not inverted
			}
			else {
				source += (unsigned long)(y * sourcePitch / 4);
				dest   += (unsigned long)(y * destPitch / 4);
			}

			// Copy the line
			memcpy((void *)dest, (const void *)source, (size_t)width * 4);
		}
	}

	// Copy rgb source to rgba dest
	void rgb2rgba(const void* rgb_source, void* rgba_dest, unsigned int width, unsigned int height, bool bInvert)
	{
		// Start of buffers
		auto rgb = static_cast<const unsigned char*>(rgb_source); // rgb/bgr
		auto rgba = static_cast<unsigned char*>(rgba_dest); // rgba/bgra
		if (!rgb || !rgba)
			return;

		const uint64_t rgbsize = (uint64_t)width * (uint64_t)height * 3;
		const uint64_t rgbpitch = (uint64_t)width * 3;
		if (bInvert) {
			rgb += rgbsize; // end of rgb buffer
			rgb -= rgbpitch; // beginning of the last rgb line
		}

		for (unsigned int y = 0; y < height; y++) {
			for (unsigned int x = 0; x < width; x++) {
				// rgb source - rgba dest
				*(rgba + 0) = *(rgb + 0); // red
				*(rgba + 1) = *(rgb + 1); // grn
				*(rgba + 2) = *(rgb + 2); // blu
				*(rgba + 3) = (unsigned char)255; // alpha
				rgb  += 3;
				rgba += 4;
			}
			if (bInvert)
				rgb -= rgbpitch * 2L; // move up a line for invert
		}

	} // end rgb2rgba

	//
	//        YUV422_to_RGBA
	//

	//
	// Lookup tables to avoid repeat calculations
	//
	// Initialize once only for repeats
	static bool tablesInitialized = false;
	static int YTable[256];
	static int UToB[256];
	static int UToG[256];
	static int VToR[256];
	static int VToG[256];
	
	//
	// Color space conversion
	//
	// https://github.com/rzwm/YUVRGBFormulaGenerator
	//
	// BT.601 : 16-235 > 0-255
	// R = 1.164384(Y - 16) + 1.596027(V - 128)
	// G = 1.164384(Y - 16) - 0.391762(U - 128) - 0.812968(V - 128)
	// B = 1.164384(Y - 16) + 2.017232(U - 128)
	// R = (297(Y - 16) + 407(V - 128) + 127) / 255
	// G = (297(Y - 16) - 100(U - 128) - 207(V - 128) + 127) / 255
	// B = (297(Y - 16) + 514(U - 128) + 127) / 255
	//
	// BT.709 : 16-235 > 0-255
	// R = 1.164384(Y - 16) + 1.792741(V - 128)
	// G = 1.164384(Y - 16) - 0.213249(U - 128) - 0.532909(V - 128)
	// B = 1.164384(Y - 16) + 2.112402(U - 128)
	// R = (297(Y - 16) + 457(V - 128) + 127) / 255
	// G = (297(Y - 16) - 54(U - 128) - 136(V - 128) + 127) / 255
	// B = (297(Y - 16) + 539(U - 128) + 127) / 255
	//
	static void InitYUVTables(bool bt709)
	{
		for (int i = 0; i < 256; i++) {

			int y = i - 16;
			if (y < 0) y = 0;
			YTable[i] = 297 * y;

			int u = i - 128;
			int v = i - 128;

			if (bt709) {
				VToR[i] =  457 * v;
				VToG[i] = -136 * v;
				UToG[i] =  -54 * u;
				UToB[i] =  539 * u;
			}
			else {
				VToR[i] =  407 * v;
				VToG[i] = -207 * v;
				UToG[i] = -100 * u;
				UToB[i] =  514 * u;
			}
		}
	}

	// Clamp out of range values 0-255
	inline unsigned char clamp8(int v) {
		return (unsigned char)((v & ~255) ? (v < 0 ? 0 : 255) : v);
	}

	//
	//        YUV422_to_RGBA
	//
	// Y sampled at every pixel
	// U and V sampled at every second pixel 
	//
	// 5.5 msec
	void YUV422_to_RGBA(const unsigned char* yuvsource,	unsigned char* rgbadest,
		unsigned int width, unsigned int height, unsigned int stride)
	{
		// SD BT.601 for widths <= 720
		// HD BT.709 default
		// UHD BT.2020 - use RGBA receiver preference
		bool b709 = (width > 720);

		if (!tablesInitialized) {
			InitYUVTables(b709);
			tablesInitialized = true;
		}

		// YUV data (NDIlib_FourCC_type_UYVA) is half width 
		unsigned int w = width/2;
		if (stride == 0) stride = w*4;

		const unsigned char* yuv = yuvsource;
		unsigned char* rgba = rgbadest;
		unsigned int padding = stride-w*4;

		for (unsigned int y = 0; y < height; y++) {
			const unsigned char* rowEnd = yuv + w*4;
			while (yuv < rowEnd) {

				int u  = *yuv++;
				int y0 = *yuv++;
				int v  = *yuv++;
				int y1 = *yuv++;

				//
				// uyvy to rgb with color space conversion
				//

				// Tables avoid repeat calculations
				int y0v = YTable[y0];
				int y1v = YTable[y1];

				// rgba pixel 1
				int r = (y0v + VToR[v] + 127) >> 8;
				int g = (y0v + UToG[u] + VToG[v] + 127) >> 8;
				int b = (y0v + UToB[u] + 127) >> 8;

				*rgba++ = clamp8(r);
				*rgba++ = clamp8(g);
				*rgba++ = clamp8(b);
				*rgba++ = 255;

				// rgba pixel 2
				r = (y1v + VToR[v] + 127) >> 8;
				g = (y1v + UToG[u] + VToG[v] + 127) >> 8;
				b = (y1v + UToB[u] + 127) >> 8;

				*rgba++ = clamp8(r);
				*rgba++ = clamp8(g);
				*rgba++ = clamp8(b);
				*rgba++ = 255;
			}
			yuv += padding; // if any
		}
	} // end YUV422_to_RGBA


	//
	// Timing
	//

#ifdef USE_CHRONO
	// Timing functions
	void StartTiming() {
		start = std::chrono::steady_clock::now();
	}

	double EndTiming() {
		end = std::chrono::steady_clock::now();
		double elapsed = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
		// printf("    elapsed [%6.2f] msec\n", elapsed / 1000.0);
		// printf("elapsed [%.3f] u/sec\n", elapsed);
		return elapsed / 1000.0; // msec
	}

	// -----------------------------------------------
	// Function: HoldFps
	// Frame rate control
	//
	// Hold a desired frame rate if the application does not already
	// have frame rate control. Must be called every frame.
	//
	// Note that this function is affected by changes to Windows timer 
	// resolution since Windows 10 Version 2004 (April 2020)
	// https://randomascii.wordpress.com/2020/10/04/windows-timer-resolution-the-great-rule-change/
	//
	// timeBeginPeriod / timeEndPeriod avoid loss of precision
	// https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
	// Microsoft remark :
	//   Call this function immediately before using timer services, and call the timeEndPeriod
	//   function immediately after you are finished using the timer services. An application 
	//   can make multiple timeBeginPeriod calls as long as each call is matched with a call
	//   to timeEndPeriod.
	// 
	void HoldFps(int fps)
	{
		// Unlikely but return anyway
		if (fps <= 0)
			return;

		// Reduce Windows timer period to minimum
#if defined(TARGET_WIN32)
		StartTimePeriod();
#endif

		// Target frame time
		const double target = (1000000.0/static_cast<double>(fps))/1000.0; // msec

		// Time now end point
		FrameEndPtr = std::chrono::steady_clock::now();

		// Milliseconds elapsed
		const double elapsedTime = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(FrameEndPtr - FrameStartPtr).count()/1000000.0);

		// Sleep to reach the target frame time
		if (elapsedTime < target) {
			std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(target - elapsedTime)));
		}

		// Set start time for the next frame
		FrameStartPtr = std::chrono::steady_clock::now();

		// Reset Windows timer period
#if defined(TARGET_WIN32)
		EndTimePeriod();
#endif

	}

#if defined(TARGET_WIN32)
	// -----------------------------------------------
	// Reduce Windows timing period to the minimum
	// supported by the system (usually 1 msec)
	void StartTimePeriod()
	{
		TIMECAPS tc={};
		PeriodMin = 0; // To allow for errors
		MMRESULT mres = timeGetDevCaps(&tc, sizeof(TIMECAPS));
		if (mres == MMSYSERR_NOERROR) {
			mres = timeBeginPeriod(tc.wPeriodMin);
			if (mres == TIMERR_NOERROR)
				PeriodMin = tc.wPeriodMin;
		}
	}


	// -----------------------------------------------
	// Reset Windows timing period
	void EndTimePeriod()
	{
		if (PeriodMin > 0) {
			timeEndPeriod(PeriodMin);
			PeriodMin = 0;
		}
	}
#endif

	//
	// Audio
	//

	//--------------------------------------------------------------
	// Create an audio frame number sequence for a given video fps
	//
	// TODO - check and revise for correct average value
	//
	// The average of the sequence should be as close as possible
	// to the Audio rate / Video fps
	//
	// Typical values
	// Audio Rate | Video fps | Audio/Video | Sequence                 | Avg/Frame |
	// ---------- | --------- | ----------- | ------------------------ | --------- |
	// 48000      | 29.97     | 1601.602    | 1602,1601,1602,1601,1602 | 1601.6    |
	// 44100      | 29.97     | 1470.147    | 1471,1470,1471,1470,1471 | 1470.2    |
	//
	std::vector<int> AudioFrameSequence(int audioSampleRate, double videoFps, int &maxSample, int sequenceLength)
	{
		std::vector<int> sequence;

		if (videoFps <= 0.0)
			return sequence;

		// Ideal fractional number of samples per video frame
		double S = audioSampleRate/videoFps;
		int a = static_cast<int>(std::floor(S));
		int b = a + 1;

		// Fractional part
		double frac = S-(double)a;

		// If very small, no sequence is needed.
		// Just return one number
		if (frac < 1e-6) {
			sequence.push_back(a);
			maxSample = a;
			return sequence;
		}

		 // limit the sequence length (default 100 and maximum 1000)
		int64_t N = std::min(sequenceLength, 1000);
		int64_t numB = static_cast<int64_t>(std::round(N*frac));
		int64_t numA = N-numB;

		// Alternate b and a for minimal jitter
		while (numB > 0 || numA > 0) {
			if (numB > 0) {
				sequence.push_back(b);
				numB--;
			}
			if (numA > 0) {
				sequence.push_back(a);
				numA--;
			}
		}

		// Find the maximum sample number for buffer allocation
		auto smax = std::max_element(sequence.begin(), sequence.end());
		maxSample = *smax;

		return sequence;
	}

	//
	// Testing function
	//
	// Convert interleaved audio to a single planar buffer for NDI v2
	// Returns a std::vector<float> containing all channels contiguously
	// Channel 0 first, channel 1 next, etc.
	// Interleaved : L R L R L R L R ...
	// Planar:       L L L L ... R R R R ...
	std::vector<float> InterleavedToPlanar(const float* interleaved, int nChannels, int nSamples)
	{
		// Resize the namespace buffer (same size > no action)
		planar.resize(nChannels*nSamples);
		for (int c = 0; c < nChannels; c++) {
			for (int f = 0; f < nSamples; f++) {
				planar[c*nSamples+f] = interleaved[f*nChannels+c];
			}
		}
		return planar;
	}



//
// MessageDialog - Windows only
// From SpoutUtils - SpoutMessageBox
//
#if defined(TARGET_WIN32)

	//
	// Group: MessageDialog
	//

	// ---------------------------------------------------------
	// Function: MessageDialog
	// MessageBox dialog with optional timeout.
	// The dialog closes itself if a timeout is specified.
	int MessageDialog(const char* message, DWORD dwMilliseconds)
	{
		if (!message)
			return 0;
		
		return MessageTaskDialog(NULL, message, "Message", MB_OK, dwMilliseconds);

	}

	// ---------------------------------------------------------
	// Function: MessageDialog
	// MessageBox with variable arguments
	int MessageDialog(const char* caption, const char* format, ...)
	{
		std::string strmessage;
		std::string strcaption;

		// Construct the message
		va_list args;
		va_start(args, format);
		char logChars[1024]{}; // The log string
		vsprintf_s(logChars, 1024, format, args);
		strmessage = logChars;
		va_end(args);

		if (caption && *caption)
			strcaption = caption;
		else
			strcaption = "Message";

		return MessageTaskDialog(NULL, strmessage.c_str(), strcaption.c_str(), MB_OK, 0);
	
	}

	// ---------------------------------------------------------
	// Function: MessageDialog
	// MessageBox with variable arguments and icon, buttons
	int MessageDialog(const char* caption, UINT uType, const char* format, ...)
	{
		std::string strmessage;
		std::string strcaption;

		// Construct the message
		va_list args;
		va_start(args, format);
		char logChars[1024]{}; // The log string
		vsprintf_s(logChars, 1024, format, args);
		strmessage = logChars;
		va_end(args);

		if (caption && *caption)
			strcaption = caption;
		else
			strcaption = "Message";

		return MessageTaskDialog(NULL, strmessage.c_str(), strcaption.c_str(), uType, 0);
	}

	// ---------------------------------------------------------
	// Function: MessageDialog
	// Messagebox with standard arguments and optional timeout
	// Replaces an existing MessageBox call.
	int MessageDialog(HWND hwnd, LPCSTR message, LPCSTR caption, UINT uType, DWORD dwMilliseconds)
	{
		return MessageTaskDialog(hwnd, message, caption, uType, dwMilliseconds);
	}

	// ---------------------------------------------------------
	// Function: MessageDialog
	// MessageBox dialog with standard arguments
	// including taskdialog main instruction large text
	int MessageDialog(HWND hwnd, LPCSTR message, LPCSTR caption, UINT uType, const char* instruction, DWORD dwMilliseconds)
	{
		// Set global main instruction
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, instruction, (int)strlen(instruction), NULL, 0);
		wstrInstruction.resize(size_needed);
		MultiByteToWideChar(CP_UTF8, 0, instruction, (int)strlen(instruction), &wstrInstruction[0], size_needed);

		return MessageTaskDialog(hwnd, message, caption, uType, dwMilliseconds);

	}
	
	// ---------------------------------------------------------
	// Function: MessageDialog
	// MessageBox dialog with edit control for text input
	// Can be used in place of a specific application resource dialog
	//   o For message content, the control is in the footer area
	//   o If no message, the control is in the main content area
	//   o All MessageDialog functions such as user icon and buttons are available
	int MessageDialog(HWND hwnd, LPCSTR message, LPCSTR caption, UINT uType, std::string& text)
	{
		// For edit control creation
		bEdit = true;

		// A timeout value of 1000000 signals the Taskdialog callback of message content.
		// The dialog times out after 1000 seconds but is effectively modal.
		DWORD dwTimeout = 0;
		std::string content = "";
		if (message && *message) {
			dwTimeout = 1000000;
			content = message;
		}

		// Set initial text for edit control
		stredit = text;
		int iret = MessageTaskDialog(hwnd, content.c_str(), caption, uType, dwTimeout);
		// Get text from global edit control string
		text = stredit;
		stredit.clear();
		bEdit = false;
		return iret;
	}

	// ---------------------------------------------------------
	// Function: MessageDialog
	// MessageBox dialog with a combobox control for item selection
	// Can be used in place of a specific application resource dialog
	// Properties the same as the edit control
	int MessageDialog(HWND hwnd, LPCSTR message, LPCSTR caption, UINT uType,
		std::vector<std::string> items, int& index)
	{
		// For combobox creation
		bCombo = true;

		// Timeout value to signal the Taskdialog callback of message content.
		DWORD dwTimeout = 0;
		std::string content = "";
		if (message && *message) {
			dwTimeout = 1000000;
			content = message;
		}

		// Set taskdialog combo box items vector and selected index
		comboitems = items;
		comboindex = index;
		int iret = MessageTaskDialog(hwnd, content.c_str(), caption, uType, dwTimeout);
		index = comboindex;
		comboitems.clear();
		comboindex = 0;
		bCombo = false;
		return iret;

	}

	// ---------------------------------------------------------
	// Function: MessageDialogIcon
	// Custom icon for MessageDialog from resources
	// Use together with MB_USERICON
	void MessageDialogIcon(HICON hIcon)
	{
		hTaskIcon = hIcon;
	}

	// ---------------------------------------------------------
	// Function: MessageDialogIcon
	// Custom icon for MessageDialog from file
	// Use together with MB_USERICON
	bool MessageDialogIcon(std::string iconfile)
	{
		hTaskIcon = reinterpret_cast<HICON>(LoadImageA(nullptr, iconfile.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE));
		return (hTaskIcon != nullptr);
	}

	// ---------------------------------------------------------
	// Function: MessageDialogButton
	// Custom button for MessageDialog
	void MessageDialogButton(int ID, std::wstring title)
	{
		TDbuttonID.push_back(ID);
		TDbuttonTitle.push_back(title);
	}

	// ---------------------------------------------------------
	// Function: MessageDialogWindow
	// Window handle for MessageDialog where not specified
	void MessageDialogWindow(HWND hWnd)
	{
		hwndMain = hWnd;
	}

	// ---------------------------------------------------------
	// Function: MessageDialogPosition
	// Position to centre MessageDialog
	void MessageDialogPosition(POINT pt)
	{
		TDcentre = pt;
	}

	// ---------------------------------------------------------
	// Function: MessageDialogCancel
	// Adds an 'X' to the caption for MB_OK and MB_YESNO
	// to align with the behaviour of a conventional MessageBox.
	// Allows close and cancel with :
	//   Esc, Alt+F4 or the caption X
	// Closing in this way returns IDCANCEL
	//       true    - add 'X'
	//       false   - remove 'X'
	//       default - add 'X' only if there is a CANCEL button
	void MessageDialogCancel(bool bCancel)
	{
		if(bCancel)
			nAllowCancel =  1; // Add 'X'
		else
			nAllowCancel = -1; // Remove 'X'
	}


	//
	// Private functions
	//
	namespace {

		//
		//
		//
		// MessageBox replacement
		// 
		// https://learn.microsoft.com/en-us/windows/win32/api/commctrl/ns-commctrl-taskdialogconfig
		//
		int MessageTaskDialog(HWND hWnd, const char* content, const char* caption, DWORD dwButtons, DWORD dwMilliseconds) {

			// Return if TaskDialog is already open
			if (hwndTask) {
				return 0;
			}

			// Window handle is HWND passed in or specified by MessageDialogWindow
			if (hwndMain)
				hWnd = hwndMain;

			// hinstance of the window
			HINSTANCE hInst = nullptr;
			if (hWnd) hInst = (HINSTANCE)GetWindowLongPtrA(hWnd, GWLP_HINSTANCE);

			// Use a custom icon if set
			if (hTaskIcon) dwButtons |= MB_USERICON;

			//
			// Drop through for modal TaskDialogIndirect
			// or MessageBox for compilers other than Visual Studio
			//

			//
			// Visual Studio TaskDialogIndirect
			//

			// User buttons
			TASKDIALOG_BUTTON buttons[10]{};

			// Use a wide string to avoid a pre-sized buffer
			std::wstring wstrTemp;
			if (content) {
				int size_needed = MultiByteToWideChar(CP_UTF8, 0, content, (int)strlen(content), NULL, 0);
				wstrTemp.resize(size_needed);
				MultiByteToWideChar(CP_UTF8, 0, content, (int)strlen(content), &wstrTemp[0], size_needed);
			}


			// Caption (default caption is the executable name)
			std::wstring wstrCaption;
			if (caption) {
				int size_needed = MultiByteToWideChar(CP_UTF8, 0, caption, (int)strlen(caption), NULL, 0);
				wstrCaption.resize(size_needed);
				MultiByteToWideChar(CP_UTF8, 0, caption, (int)strlen(caption), &wstrCaption[0], size_needed);
			}

			// Hyperlinks can be included in the content using HTML format.
			// For example : 
			// <a href=\"https://spout.zeal.co/\">Spout home page</a>
			// Only double quotes are supported and must be escaped.

			// Topmost global flag
			bTopMost = ((dwButtons & MB_TOPMOST) != 0);
			LONG dwl = (LONG)dwButtons;
			if (bTopMost)
				dwl = dwl ^ MB_TOPMOST;

			//
			// Buttons
			//
			DWORD dwb = dwl & 0x0F; // buttons code
			DWORD dwCommonButtons = MB_OK;
			//
			// User buttons
			//
			if (TDbuttonID.size() > 0) {
				int i = 0;
				for (i=0; i < (int)TDbuttonID.size(); i++) {
					buttons[i].nButtonID = TDbuttonID[i];
					buttons[i].pszButtonText = TDbuttonTitle[i].c_str();
				}
				if ((dwButtons & MB_CANCEL) == MB_CANCEL) {
					buttons[i].nButtonID = IDCANCEL;
					buttons[i].pszButtonText = L"Cancel";
				}
				else {
					// Final button default is OK
					// YES/NO etc have to be added as buttons
					buttons[i].nButtonID = IDOK;
					buttons[i].pszButtonText = L"OK";
				}
			}
			else {
				//
				// Common buttons
				//
				// https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-taskdialog
				// TDCBF_OK_BUTTON     1
				// TDCBF_YES_BUTTON    2
				// TDCBF_NO_BUTTON     4
				// TDCBF_CANCEL_BUTTON 8
				// MB_OK          0x00
				// MB_OKCANCEL    0x01
				// MB_YESNOCANCEL 0x03
				// MB_YESNO       0x04
				if (dwb == MB_YESNO) { // 4
					dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
				}
				else if (dwb == MB_YESNOCANCEL) { // 3
					dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON | TDCBF_CANCEL_BUTTON;
				}
				else if (dwb == MB_OKCANCEL) { // 1
					dwCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;
				}
				else {
					dwCommonButtons = MB_OK;
				}
			}
			
			//
			// Icons
			//
			// Icons available
			// TD_WARNING_ICON, TD_ERROR_ICON, TD_INFORMATION_ICON, TD_SHIELD_ICON
			//
			// Icons to allow for
			// MB_ICONSTOP         0x10
			// MB_ICONERROR        0x10
			// MB_ICONHAND         0x10
			// MB_ICONQUESTION     0x20
			// MB_ICONEXCLAMATION  0x30
			// MB_ICONWARNING      0x30
			// MB_ICONINFORMATION  0x40
			// MB_ICONASTERISK     0x40
			// MB_USERICON         0x80
			//
			HICON hMainIcon = NULL; // No user icon
			WCHAR* wMainIcon = nullptr; // No resource icon
			dwl = dwl & 0xF0; // remove buttons for icons
			if (dwl == MB_USERICON && hTaskIcon) {
				// Private SpoutUtils icon handle set by MessageDialogIcon
				hMainIcon = hTaskIcon;
				wMainIcon = nullptr;
			}
			else {
				switch (dwl) {
					case MB_ICONINFORMATION: // 0x40
						wMainIcon = TD_INFORMATION_ICON;
						break;
					case MB_ICONWARNING: // 0x30
						wMainIcon = TD_WARNING_ICON;
						break;
					case MB_ICONQUESTION: // 0x20
						wMainIcon = TD_INFORMATION_ICON;
						break;
					case MB_ICONERROR: // 0x10
						wMainIcon = TD_ERROR_ICON;
						break;
					default:
						// No icon specified
						wMainIcon = nullptr;
						break;
				}
			}

			int nButtonPressed        = 0;
			int nRadioButton          = 0;
			TASKDIALOGCONFIG config   = { 0 };
			config.cbSize             = sizeof(config);
			config.hwndParent         = hWnd;
			config.hInstance          = hInst;
			config.pszWindowTitle     = wstrCaption.c_str();
			config.hMainIcon          = hMainIcon;
			if (!hMainIcon)
				config.pszMainIcon    = wMainIcon; // Important to remove this
			config.pszMainInstruction = wstrInstruction.c_str();
			if (content) {
				config.pszContent         = wstrTemp.c_str();
			}

			// User buttons in TASKDIALOG_BUTTON buttons
			// Otherwise use common buttons
			config.nDefaultButton = IDOK;

			if (TDbuttonID.size() > 0) {
				config.pButtons = buttons;
				config.cButtons = (UINT)TDbuttonID.size() + 1; // Includes OK button
			}
			else {
				config.dwCommonButtons = dwCommonButtons;
			}

			config.cxWidth            = 0; // auto width - requires TDF_SIZE_TO_CONTENT

			// TDF_POSITION_RELATIVE_TO_WINDOW Indicates that the task dialog is
			// centered relative to the window specified by hwndParent.
			// If hwndParent is NULL, the dialog is centered on the monitor.
			config.dwFlags  = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT | TDF_CALLBACK_TIMER | TDF_ENABLE_HYPERLINKS;
			if ((dwButtons & MB_RIGHT) == MB_RIGHT)
				config.dwFlags |= TDF_RTL_LAYOUT;

			if (hMainIcon)
				config.dwFlags        |= TDF_USE_HICON_MAIN; // User icon
			config.pfCallback         = reinterpret_cast<PFTASKDIALOGCALLBACK>(TDcallbackProc);
			config.lpCallbackData     = reinterpret_cast<LONG_PTR>(&dwMilliseconds);

			// Adds an 'X' to the caption without a CANCEL button
			// Allows close and cancel with : Esc, Alt+F4 or 'X'
			// Closing in this way returns IDCANCEL.
			if(nAllowCancel == 1)
				config.dwFlags  |= TDF_ALLOW_DIALOG_CANCELLATION;

			if (bTopMost) {
				// Get the first visible window in the Z order
				hwndTop = GetForegroundWindow();
				HWND hwndParent = GetParent(hwndTop); // Is it a dialog
				if (hwndParent) hwndTop = hwndParent;
				// Is it topmost ?
				if ((GetWindowLong(hwndTop, GWL_EXSTYLE) & WS_EX_TOPMOST) > 0) {
					// Move it down
					SetWindowPos(hwndTop, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				}
				else {
					hwndTop = NULL;
				}
			}

			TaskDialogIndirect(&config, &nButtonPressed, &nRadioButton, NULL);

			if (bTopMost && hwndTop) {
				// Reset the window that was topmost before
				SetWindowPos(hwndTop, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				hwndTop = NULL;
			}

			// Clear global main instruction
			wstrInstruction.clear();

			// Clear custom buttons
			TDbuttonID.clear();
			TDbuttonTitle.clear();

			// Clear custom icon handle set by MessageDialogIcon and activated by MB_USERICON
			// Use before calling any of the MessageDialog functions
			hTaskIcon = nullptr;

			// Clear dialog user position
			TDcentre.x = 0;
			TDcentre.y = 0;

			// Clear caption cancel option
			nAllowCancel = 0;

			// Return button pressed
			// IDCANCEL, IDNO, IDOK, IDRETRY, IDYES
			// or custom button ID
			return nButtonPressed;
		}

		HRESULT TDcallbackProc(HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)
		{
			if (uNotification == TDN_CREATED) {

				// Taskdialog window open
				hwndTask = hwnd;

				// For general use
				RECT rect{};
				int x, y, w, h = 0;

				// hInstance of task dialog
				HINSTANCE hInstTD = (HINSTANCE)GetWindowLongPtrA(hwnd, GWLP_HINSTANCE);

				// Timeout
				DWORD* pTimeout = reinterpret_cast<DWORD*>(dwRefData); // = tc.lpCallbackData

				// Remove icons from the caption
				// An icon appears in the caption when using MB_OKCANCEL
				// or when an icon is set for the taskdialog content
				SendMessage(hwnd, WM_SETICON, ICON_BIG, NULL);
				SendMessage(hwnd, WM_SETICON, ICON_SMALL, NULL);

				// TODO
				// For an icon in the caption instead of the dialog window
				// Disable :
				//     if (hTaskIcon) dwButtons |= MB_USERICON;
				// Add :
				//     SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hTaskIcon);
				//     SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hTaskIcon);

				// Remove 'X' from the caption
				if (nAllowCancel == -1) {
			        LONG style = GetWindowLong(hwnd, GWL_STYLE);
					style &= ~WS_SYSMENU; // Remove system menu (includes 'X')
					SetWindowLong(hwnd, GWL_STYLE, style);
				}

				// Dialog Window size and position
				GetWindowRect(hwnd, &rect);
				x = rect.left;
				y = rect.top;
				w = rect.right - rect.left;
				h = rect.bottom - rect.top;

				// Centre the taskdialog window on the point
				// if MessageDialogPosition has been used
				if (TDcentre.x > 0 || TDcentre.y > 0) {
					// Offset to the centre of the window
					x = TDcentre.x - (w / 2);
					y = TDcentre.y - (h / 2);
				}

				if (bTopMost)
					SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOSIZE);
				else
					SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, w, h, SWP_NOSIZE);

				// Edit text control
				if (bEdit) {

					// Position in the main content by default
					GetClientRect(hwnd, &rect);

					// Taskdialog client size is larger with an icon
					h = rect.bottom - rect.top;
					x = rect.left + 70;
					y = rect.top + 3;
					// Allow for increased height with an icon
					if (h > 90) y += 20;
					w = 320;
					h = 24;

					// Look for a timeout of 1000000 as a signal for message content
					// and position in the the footer area if so.
					if (*pTimeout && *pTimeout == 1000000) {
						x = rect.left + 10;
						y = rect.bottom - 38;
						w = 220;
					}

					hEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
						WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
						x, y, w, h, hwnd, (HMENU)IDC_TASK_EDIT, hInstTD, NULL);

					// Set an initial entry in the edit box
					if (!stredit.empty()) {
						SetWindowTextA(hEdit, (LPCSTR)stredit.c_str());
						// Select all text in the edit field
						SendMessage(hEdit, EM_SETSEL, 0, 0x7FFF0000L);
					}

					// Position on top of content
					BringWindowToTop(hEdit);

					// Set keyboard focus to allow user entry
					SetFocus(hEdit);

				}

				// Combo box control
				if (bCombo) {

					// Position in the main content by default
					GetClientRect(hwnd, &rect);

					// Taskdialog client size
					// h = rect.bottom-rect.top;
					h = rect.bottom - rect.top;
					x = rect.left + 20;
					y = rect.top;
					w = 395;

					// Find combo box width from the longest item string
					LONG maxw = 0L;
					if (comboitems.size() > 0) {
						HDC hdc = GetDC(hwnd);
						HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
						SelectObject(hdc, hFont);
						for (int i = 0; i < (int)comboitems.size(); i++) {
							// Text width for the current string
							SIZE size;
							GetTextExtentPoint32A(hdc, (LPCSTR)comboitems[i].c_str(), (int)comboitems[i].size(), &size);
							if (size.cx > maxw)
								maxw = size.cx;
						}
						ReleaseDC(hwnd, hdc);
						// Add padding for the combo box button
						maxw += 35;
					}

					// Adjust to the maximum width required
					w = 0;
					if (maxw > 0)
						w = (int)maxw;
					if (w < 200) w = 200; // Minimum combo width
					int dw = rect.right - rect.left;
					if (w < dw) {
						// If the width is less than the dialog adjust the x position 
						x = (dw - w) / 2;
						if (*pTimeout && *pTimeout == 1000000) {
							// Position in the footer area if there is message content
							// Less width due to buttons
							x = rect.left + 10;
							y = rect.bottom - 40;
							w = 220; // Fixed width
						}
						else if (h > 90) { // Increased client size for icon
							// Allow for increased height and position further right
							y += 20;
							if (x < 20) {
								x += 40;
								w -= 40;
							}
						}
					}
					else {
						// If the width is larger, reduce to fit the dialog
						w = dw - 4;
						x = 2;
					}

					// Combo box inital height. Changed by content.
					h = 100;

					// Use CBS_DROPDOWNLIST style for list only
					hCombo = CreateWindowExA(WS_EX_CLIENTEDGE, "COMBOBOX", "",
						CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
						x, y, w, h, hwnd, (HMENU)IDC_TASK_COMBO, hInstTD, NULL);

					// Add combo box items
					if (comboitems.size() > 0) {
						for (int i = 0; i < (int)comboitems.size(); i++) {
							SendMessageA(hCombo, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)comboitems[i].c_str());
						}
						// Display an initial item in the selection field
						SendMessageA(hCombo, CB_SETCURSEL, (WPARAM)comboindex, (LPARAM)0);
					}

					// Remove icons from the caption
					SendMessage(hwnd, WM_SETICON, ICON_BIG, NULL);
					SendMessage(hwnd, WM_SETICON, ICON_SMALL, NULL);

					// Position on top of content
					BringWindowToTop(hCombo);

				}

			}

			if (uNotification == TDN_DESTROYED) {

				// Taskdialog window closed
				hwndTask = nullptr;

				if (bEdit) {
					// Get text from edit control
					char text[MAX_PATH]{};
					GetWindowTextA(hEdit, text, MAX_PATH);
					// Move to global string for return
					stredit = text;
				}

				if (bCombo) {
					// Get currently selected index
					// Allow for error if the user edits the list item
					int index = (int)SendMessageA(hCombo, (UINT)CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
					if (index != CB_ERR) {
						comboindex = index;
					}
				}
			}

			// Timeout
			if (uNotification == TDN_TIMER) {
				DWORD* pTimeout = reinterpret_cast<DWORD*>(dwRefData);  // = tc.lpCallbackData
				DWORD timeElapsed = static_cast<DWORD>(wParam);
				if (*pTimeout && timeElapsed >= *pTimeout) {
					*pTimeout = 0; // Make sure we don't send the button message multiple times.
					SendMessage(hwnd, TDM_CLICK_BUTTON, IDOK, 0);
				}
			}

			// Hyperlink
			//   TDN_HYPERLINK_CLICKED indicates that a hyperlink has been selected.
			//   lParam - Pointer to a wide-character string containing the URL of the hyperlink.
			if (uNotification == TDN_HYPERLINK_CLICKED) {
				SHELLEXECUTEINFOW sei{};
				sei.cbSize = sizeof(sei);
				sei.hwnd = NULL;
				sei.lpVerb = L"open";
				sei.lpFile = (LPCWSTR)lParam;
				sei.nShow = SW_SHOWNORMAL;
				if (!ShellExecuteExW(&sei)) {
					return S_FALSE;
				}
				SendMessage(hwnd, TDM_CLICK_BUTTON, IDOK, 0);
			}

			return 0; // S_OK

		}

	} // endif private namespace

#endif // End MessageDialog for Windows

#endif

} // end namespace

