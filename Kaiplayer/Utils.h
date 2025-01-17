//  Copyright (c) 2017 - 2020, Marcin Drob

//  Kainote is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  Kainote is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with Kainote.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <wx/colour.h>
#include <wx/string.h>
#include <wx/window.h>
#include <wx/bitmap.h>
#include "Styles.h"
#include <vector>
#include "windows.h"
#include "LogHandler.h"

#undef wxBITMAP_PNG


#ifndef MIN
#define MIN(a,b) ((a)<(b))?(a):(b)
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b))?(a):(b)
#endif

#ifndef MID
#define MID(a,b,c) MAX((a),MIN((b),(c)))
#endif

inline wxColour GetColorWithAlpha(const wxColour &colorWithAlpha, const wxColour &background)
{
	int r = colorWithAlpha.Red(), g = colorWithAlpha.Green(), b = colorWithAlpha.Blue();
	int r2 = background.Red(), g2 = background.Green(), b2 = background.Blue();
	int inv_a = 0xFF - colorWithAlpha.Alpha();
	int fr = (r2* inv_a / 0xFF) + (r - inv_a * r / 0xFF);
	int fg = (g2* inv_a / 0xFF) + (g - inv_a * g / 0xFF);
	int fb = (b2* inv_a / 0xFF) + (b - inv_a * b / 0xFF);
	return wxColour(fr, fg, fb);
}

inline wxString GetTruncateText(const wxString &textToTruncate, int width, wxWindow *window)
{
	int w, h;
	window->GetTextExtent(textToTruncate, &w, &h);
	if (w > width){
		size_t len = textToTruncate.length() - 1;
		while (w > width && len > 3){
			window->GetTextExtent(textToTruncate.SubString(0, len), &w, &h);
			len--;
		}
		return textToTruncate.SubString(0, len - 2i64) + L"...";
	}
	return textToTruncate;
}

void SelectInFolder(const wxString & filename);

void OpenInBrowser(const wxString &adress);

bool IsNumberFloat(const wxString &test);

bool sortfunc(Styles *styl1, Styles *styl2);
//formating here works like this, 
//first digit - digits before dot, second digit - digits after dot, for example 5.3f;
wxString getfloat(float num, const wxString &format = L"5.3f", bool Truncate = true);
wxBitmap CreateBitmapFromPngResource(const wxString& t_name);
wxBitmap *CreateBitmapPointerFromPngResource(const wxString& t_name);
wxImage CreateImageFromPngResource(const wxString& t_name);
#define wxBITMAP_PNG(x) CreateBitmapFromPngResource(x)
#define PTR_BITMAP_PNG(x) CreateBitmapPointerFromPngResource(x)
void MoveToMousePosition(wxWindow *win);
wxString MakePolishPlural(int num, const wxString &normal, const wxString &plural24, const wxString &pluralRest);
wxRect GetMonitorWorkArea(int wmonitor, std::vector<tagRECT> *MonitorRects, const wxPoint &position, bool workArea);
wxRect GetMonitorRect1(int wmonitor, std::vector<tagRECT> *MonitorRects, const wxRect &programRect);
int FindMonitor(std::vector<tagRECT> *MonitorRects, const wxPoint &pos);
static const wxString emptyString;
bool IsNumber(const wxString &txt);

#ifdef _M_IX86
void SetThreadName(DWORD id, LPCSTR szThreadName);
#else
void SetThreadName(size_t id, LPCSTR szThreadName);
#endif


#ifndef SAFE_DELETE
#define SAFE_DELETE(x) if (x !=NULL) { delete x; x = NULL; }
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(x) if (x != NULL) { x->Release(); x = NULL; } 
#endif



#ifndef PTR
#define PTR(what,err) if(!what) {KaiLogSilent(err); return false;}
#endif

#ifndef PTR1
#define PTR1(what,err) if(!what) {KaiLogSilent(err); return;}
#endif

#ifndef HR
#define HR(what,err) if(FAILED(what)) {KaiLogSilent(err); return false;}
#endif

#ifndef HRN
#define HRN(what,err) if(FAILED(what)) {KaiLogSilent(err); return;}
#endif