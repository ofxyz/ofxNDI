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
	11.06.18 - Add changes for OSX (https://github.com/ThomasLengeling/ofxNDI)
	06.12.19 - Remove SSE functions for Linux
	07.12.19 - remove includes emmintrin.h, xmmintrin.h, iostream, cstdint
	16.09.24 - #define USE_CHRONO for OSX
	20.12.25 - Update to NDI version 6.2.1
	09-02-26 - Add Audio functions
			 - Add NOMINMAX define to avoid conflict for
			   std::min/std::max and Windows min/max
	23.02.26 - Add audio functions AudioFrameSequence and InterleavedToPlanar
	20-05-26 - Add MessageDialog functions

*/
#pragma once
#ifndef __ofxNDI_
#define __ofxNDI_

// For std::min
#define NOMINMAX

#include "ofxNDIplatforms.h" // Openframeworks platform definitions
#include <stdint.h> // ints of known sizes, standard library
#include <stdlib.h>
#include <string>
#include <iostream> // for cout
#include <vector>
#include <cmath>     // for std::floor, std::ceil
#include <algorithm> // for std::min
#include <numeric>   // for std::accumulate

// TODO : test includes for OSX
#if defined(TARGET_OSX)
#define USE_CHRONO
#if defined(__aarch64__)
#include "sse2neon.h"
#else
#include <x86intrin.h> // for _movsd
#endif
#elif defined(TARGET_WIN32)
#include <windows.h>
#include <intrin.h> // for _movsd
#pragma comment (lib, "winmm.lib") // for timeBeginPeriod
// #else // Linux
#endif

#include <cstring>
#include <climits>

// MessageDialog - windows only
#if defined(TARGET_WIN32)

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

// TaskDialog requires comctl32.dll version 6
#ifdef _MSC_VER
// https://learn.microsoft.com/en-us/windows/win32/controls/cookbook-overview
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#endif


//
// C++11 timer is only available for MS Visual Studio 2015 and above.
//
// Note that _MSC_VER may not correspond correctly if an earlier platform toolset
// is selected for a later compiler e.g. Visual Studio 2010 platform toolset for
// a Visual studio 2017 compiler. "#include <chrono>" will then fail.
// If this is a problem, remove _MSC_VER_ and manually enable/disable the USE_CHRONO define.
//
#if _MSC_VER >= 1900
#define USE_CHRONO
#endif

#ifdef USE_CHRONO
#include <chrono> // c++11 timer
#include <thread>
#endif

// For SetAudioType
enum ofxNDIaudiotype {
	audio_frame_v2_t = 0,
	audio_frame_interleaved_16s_t = 1,
	audio_frame_interleaved_32s = 2,
	audio_frame_interleaved_32f_t = 3
};

namespace ofxNDIutils {

	// ofxNDI version number
	std::string GetVersion();

	//
	// Image pixel copy
	//

	// Copy rgba source image to dest.
	// Images must be the same size with no line padding.
	// Option flip image vertically (invert).
	void CopyImage(const unsigned char *source, unsigned char *dest,
		unsigned int width, unsigned int height,
		bool bInvert = false);

	// Copy rgba source image to dest.
	// Source line pitch (unused).
	// Option convert bgra<>rgba.
	// Option flip image vertically (invert).
	void CopyImage(const unsigned char *source, unsigned char *dest,
		unsigned int width, unsigned int height, unsigned int stride,
		bool bSwapRB = false, bool bInvert = false);

	// Copy rgba image buffers line by line.
	// Allow for both source and destination line pitch.
	// Option flip image vertically (invert).
	void CopyImage(const void* source, void* dest,
		unsigned int width, unsigned int height,
		unsigned int sourcePitch, unsigned int destPitch,
		bool bInvert = false);

// TODO : check it works for OSX
#if defined(TARGET_WIN32) || defined(TARGET_OSX)
	void memcpy_sse2(void* dst, const void* src, size_t Size);
	void memcpy_movsd(void* dst, const void* src, size_t Size);
	void rgba_bgra_sse2(const void *source, void *dest, unsigned int width, unsigned int height, bool bInvert = false);
#endif

	void rgba_bgra(const void *rgba_source, void *bgra_dest, unsigned int width, unsigned int height, bool bInvert = false);
	void FlipBuffer(const unsigned char *src, unsigned char *dst, unsigned int width, unsigned int height);
	void rgb2rgba(const void* rgb_source, void* rgba_dest, unsigned int width, unsigned int height, bool bInvert);
	void YUV422_to_RGBA(const unsigned char* source, unsigned char* dest, unsigned int width, unsigned int height, unsigned int stride = 0);

	//
	// Timing
	//

#ifdef USE_CHRONO
	// Start timing period
	void StartTiming();
	// Stop timing and return microseconds elapsed.
	// Code console output can be enabled for quick timing tests.
	double EndTiming();
	void HoldFps(int fps);
#if defined(TARGET_WIN32)
	// Windows minimum time period
	void StartTimePeriod();
	void EndTimePeriod();
#endif

	//
	// Audio
	//

	// Create an audio frame number sequence for a given video fps
	std::vector<int> AudioFrameSequence(int audioSampleRate, double videoFps, int &maxSample, int length = 100);

	// Convert interleaved audio to a single planar buffer for NDI v2
	std::vector<float> InterleavedToPlanar(const float* interleaved, int channels, int nsamples);
	static std::vector<float> planar;

	
//
// MessageDialog - Windows only
// From SpoutUtils - SpoutMessageBox
//
#if defined(TARGET_WIN32)

	//
	// Private variables and functions
	//
	namespace {

		// Application window
		HWND hwndMain = NULL; // Taskdialog window to prevent multiple open
		HWND hwndTask = NULL; // Position for TaskDialog window centre
		POINT TDcentre = {};
		HWND hwndTop = NULL;
		bool bTopMost = false; // For topmost
		int nAllowCancel = 0; // Caption 'X' for cancel
		HICON hTaskIcon = NULL; // For custom icon
		// For custom buttons
		std::vector<int>TDbuttonID;
		std::vector<std::wstring>TDbuttonTitle;
		std::wstring wstrInstruction; // Main instruction text
		// For edit text control
		bool bEdit = false;
		HWND hEdit = NULL;
		std::string stredit;
		#define IDC_TASK_EDIT 101
		// For combo box control
		bool bCombo = false;
		HWND hCombo = NULL;
		std::vector<std::string> comboitems;
		int comboindex = 0;
		#define IDC_TASK_COMBO 102
		// For cancel button only together with custom buttons
		#define MB_CANCEL TDCBF_CANCEL_BUTTON

		// Taskdialog for MessageDialog
		int MessageTaskDialog(HWND hWnd, const char* content, const char* caption, DWORD dwButtons, DWORD dwMilliseconds);

		// TaskDialogIndirect callback to handle timer, topmost and hyperlinks
		HRESULT TDcallbackProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LONG_PTR lpRefData);

	}


	// MessageBox dialog with optional timeout.
	// The dialog closes itself if a timeout is specified.
	int MessageDialog(const char* message, DWORD dwMilliseconds = 0);

	// MessageBox with variable arguments
	int MessageDialog(const char* caption, const char* format, ...);
	
	// MessageBox with variable arguments and icon, buttons
	int MessageDialog(const char* caption, UINT uType, const char* format, ...);

	// MessageBox dialog with standard arguments.
	// Replaces an existing MessageBox call.
	// uType options : standard MessageBox buttons and icons
	// MB_USERICON - use together with MessageDialogIcon
	// Hyperlinks can be included in the content using HTML format.
	// For example : <a href=\"https://spout.zeal.co/\">Spout home page</a>
	// Only double quotes are supported and must be escaped.
	int MessageDialog(HWND hwnd, LPCSTR message, LPCSTR caption, UINT uType, DWORD dwMilliseconds = 0);

	// MessageBox dialog with standard arguments
	// including taskdialog main instruction large text
	int MessageDialog(HWND hwnd, LPCSTR message, LPCSTR caption,  UINT uType, const char* instruction, DWORD dwMilliseconds = 0);

	// MessageBox dialog with an edit control for text input
	// Can be used in place of a specific application resource dialog
	//   o For message content, the control is in the footer area
	//   o If no message, the control is in the main content area
	//   o All MessageDialog functions such as user icon and buttons are available
	int MessageDialog(HWND hwnd, LPCSTR message, LPCSTR caption, UINT uType, std::string& text);

	// MessageBox dialog with a combobox control for item selection
	// Can be used in place of a specific application resource dialog
	// Properties the same as the edit control
	int MessageDialog(HWND hwnd, LPCSTR message, LPCSTR caption, UINT uType,
		std::vector<std::string> items, int &selected);

	// Custom icon for MessageDialog from resources
	void MessageDialogIcon(HICON hIcon);

	// Custom icon for MessageDialog from file
	bool MessageDialogIcon(std::string iconfile);

	// Custom button for MessageDialog
	void MessageDialogButton(int ID, std::wstring title);

	// Window handle for MessageDialog where not specified
	void MessageDialogWindow(HWND hWnd);

	// Position to centre MessageDialog
	void MessageDialogPosition(POINT pt);

	// Allow dialog cancel
	// Adds an 'X' to the caption
	// and allows close and cancel with :
	//   Esc, Alt+F4 or the caption X
	// Closing in this way returns IDCANCEL.
	//   true  - add 'X'
	//   false - remove 'X'
	void MessageDialogCancel(bool bCancel);

#endif // End MessageDialog for Windows


#endif

}


#endif

