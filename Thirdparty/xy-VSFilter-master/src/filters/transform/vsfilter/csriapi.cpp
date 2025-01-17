/* 
 *	Copyright (C) 2007 Niels Martin Hansen
 *	http://aegisub.net/
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include <atlpath.h>
#include "resource.h"
#include "..\..\..\subtitles\VobSubFile.h"
#include "..\..\..\subtitles\RTS.h"
#include "..\..\..\subtitles\SSF.h"
#include "..\..\..\subpic\simplesubpicproviderimpl.h"

#define CSRIAPI extern "C" __declspec(dllexport)
#define CSRI_OWN_HANDLES
typedef const char *csri_rend;
extern "C" struct csri_vsfilter_inst {
	CRenderedTextSubtitle *rts;
	SimpleSubPicProvider* spp;
	CCritSec *cs;
	CSize script_res;
	CSize screen_res;
	CRect video_rect;
	enum csri_pixfmt pixfmt;
	
};
typedef struct csri_vsfilter_inst csri_inst;
#include "csri.h"
static csri_rend csri_vsfilter = "vsfilter";


CSRIAPI csri_inst *csri_open_file(csri_rend *renderer, const char *filename, struct csri_openflag *flags)
{
	//AMTRACE((TEXT(__FUNCTION__),0));
	int namesize;
	wchar_t *namebuf;

	namesize = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
	if (!namesize)
		return 0;
	namesize++;
	namebuf = new wchar_t[namesize];
	MultiByteToWideChar(CP_UTF8, 0, filename, -1, namebuf, namesize);

	csri_inst *inst = new csri_inst();
	inst->cs = new CCritSec();
	inst->rts = new CRenderedTextSubtitle(inst->cs);
	inst->spp=NULL;
	if (inst->rts->Open(CString(namebuf), DEFAULT_CHARSET)) {
		delete[] namebuf;
		inst->rts->AddRef();
		return inst;
	} else {
		delete[] namebuf;
		delete inst->rts;
		delete inst->cs;
		delete inst;
		return 0;
	}
}


CSRIAPI csri_inst *csri_open_mem(csri_rend *renderer, const void *data, size_t length, struct csri_openflag *flags)
{
	//AMTRACE((TEXT(__FUNCTION__),0));
	// This is actually less effecient than opening a file, since this first writes the memory data to a temp file,
	// then opens that file and parses from that.
	csri_inst *inst = new csri_inst();
	//if(!inst){return 0;}
	inst->cs = new CCritSec();
	//if(!inst->cs){return 0;}
	//CAutoLock cAutoLock(inst->cs);
	inst->rts = new CRenderedTextSubtitle(inst->cs);
	inst->spp=NULL;
	if (inst->rts->Open((BYTE*)data, (int)length, DEFAULT_CHARSET, _T("CSRI memory subtitles"))) {
		inst->rts->AddRef();
		return inst;
	} else {
		delete inst->rts;
		delete inst->cs;
		delete inst;
		return 0;
	}
}


CSRIAPI void csri_close(csri_inst *inst)
{
	if (!inst) return;
	if(inst->spp){inst->spp->Release();}
	if(inst->rts){inst->rts->Release();}
	delete inst->cs;
	delete inst;
}


CSRIAPI int csri_request_fmt(csri_inst *inst, const struct csri_fmt *fmt)
{
	if (!inst) return -1;
	//CAutoLock cAutoLock(inst->cs);
	if (!fmt->width || !fmt->height)
		return -1;
	//CAutoLock cAutoLock(inst->cs);
	ColorType col;
	// Check if pixel format is supported
	switch (fmt->pixfmt) {
		case CSRI_F_BGR_:
			col=MSP_RGB32;
			break;
		case CSRI_F_BGR:
			col=MSP_RGB24;
			break;
		case CSRI_F_YUY2:
			col=MSP_YUY2;
			break;
		case CSRI_F_YV12:
			col=MSP_YV12;
			break;
		case CSRI_F_YV12A:
			col=MSP_NV12;
			break;
		case CSRI_F_BGRA:
			col = MSP_RGBA;
			break;

		default:
			return -1;
		break;
	}
	inst->pixfmt=fmt->pixfmt;
	inst->screen_res = CSize(fmt->width, fmt->height);
	inst->video_rect = CRect(0, 0, fmt->width, fmt->height);
	HRESULT hr=S_OK;
	inst->spp=new SimpleSubPicProvider(col,inst->screen_res,inst->video_rect,NULL,&hr);
	inst->spp->AddRef();
	hr = inst->spp->SetSubPicProvider((ISubPicProviderEx*)inst->rts);
	hr = inst->spp->SetFPS(25.f);
	return (hr==S_OK)?0 : -1;
}


CSRIAPI void csri_render(csri_inst *inst, struct csri_frame *frame, double time)
{
	if(!inst){return;}
	//CAutoLock cAutoLock(inst->cs);
	SubPicDesc spd;
	
	switch (inst->pixfmt) {
		case CSRI_F_BGR_:
			spd.type = MSP_RGB32;
			spd.bpp = 32;
			spd.bits = frame->planes[0];
			spd.pitch = frame->strides[0];
			break;

		case CSRI_F_BGR:
			spd.type = MSP_RGB24;
			spd.bpp = 24;
			spd.bits = frame->planes[0];
			spd.pitch = frame->strides[0];
			break;

		case CSRI_F_YUY2:
			spd.type = MSP_YUY2;
			spd.bpp = 16;
			spd.bits = frame->planes[0];
			spd.pitch = frame->strides[0];
			break;

		case CSRI_F_YV12A:
			spd.type = MSP_NV12;
			spd.bpp = 8;
			spd.bits = frame->planes[0];
			spd.pitch = frame->strides[0];
			break;

		case CSRI_F_YV12:
			spd.type = MSP_YV12;
			spd.bpp = 8;
			spd.bits = frame->planes[0];
			spd.pitch = frame->strides[0];
			break;

		case CSRI_F_BGRA:
			spd.type = MSP_RGBA;
			spd.bpp = 32;
			spd.bits = frame->planes[0];
			spd.pitch = frame->strides[0];
			break;
		default:
			return;
		break;
	}
	spd.w = inst->screen_res.cx;
	spd.h = inst->screen_res.cy;
	spd.vidrect = inst->video_rect;

	//inst->rts->Render(spd, (REFERENCE_TIME)(time*10000000), 25.0, inst->video_rect);
	CComPtr<ISimpleSubPic> pSubPic;
	if(!inst->spp->LookupSubPic((time*10000000), &pSubPic)){return;}
	pSubPic->AlphaBlt(&spd);
}


// No extensions supported
CSRIAPI void *csri_query_ext(csri_rend *rend, csri_ext_id extname)
{
	return 0;
}

// Get info for renderer
static struct csri_info csri_vsfilter_info = {
#ifdef _DEBUG
	"vsfilter_textsub_debug", // name
	"2.39", // version (assumed version number, svn revision, patchlevel)
#else
	"vsfilter_textsub", // name
	"2.39", // version (assumed version number, svn revision, patchlevel)
#endif
	// 2.38-0611 is base svn 611
	// 2.38-0611-1 is with clipfix and fax/fay patch
	// 2.38-0611-2 adds CSRI
	// 2.38-0611-3 fixes a bug in CSRI and adds fontcrash-fix and float-pos
	// 2.38-0611-4 fixes be1-dots and ugly-fade bugs and adds xbord/ybord/xshad/yshad/blur tags and extends be
	// 2.39 merges with guliverkli2 fork
	"VSFilter/TextSub (guliverkli2)", // longname
	"Gabest", // author
	"Copyright (c) 2003-2008 by Gabest and others" // copyright
};
CSRIAPI struct csri_info *csri_renderer_info(csri_rend *rend)
{
	return &csri_vsfilter_info;
}
// Only one supported, obviously
CSRIAPI csri_rend *csri_renderer_byname(const char *name, const char *specific)
{
	if (strcmp(name, csri_vsfilter_info.name))
		return 0;
	if (specific && strcmp(specific, csri_vsfilter_info.specific))
		return 0;
	return &csri_vsfilter;
}
// Still just one
CSRIAPI csri_rend *csri_renderer_default()
{
	return &csri_vsfilter;
}
// And no further
CSRIAPI csri_rend *csri_renderer_next(csri_rend *prev)
{
	return 0;
}

