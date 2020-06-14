//  Copyright (c) 2020, Marcin Drob

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


#include "RendererDirectShow.h"
#include "kainoteApp.h"
#include "CsriMod.h"
#include "OpennWrite.h"

#pragma comment(lib, "Dxva2.lib")
const IID IID_IDirectXVideoProcessorService = { 0xfc51a552, 0xd5e7, 0x11d9, { 0xaf, 0x55, 0x00, 0x05, 0x4e, 0x43, 0xff, 0x02 } };

RendererDirectShow::RendererDirectShow(VideoCtrl *control)
	: RendererVideo(control)
	, vplayer(NULL)
{

}

RendererDirectShow::~RendererDirectShow()
{
	SAFE_DELETE(vplayer);
}

bool RendererDirectShow::InitRendererDX()
{
	HR(m_D3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &m_BlackBarsSurface), _("Nie mo�na stworzy� powierzchni"));
	HR(DXVA2CreateVideoService(m_D3DDevice, IID_IDirectXVideoProcessorService, (VOID**)&m_DXVAService),
		_("Nie mo�na stworzy� DXVA processor service"));
	DXVA2_VideoDesc videoDesc;
	videoDesc.SampleWidth = m_Width;
	videoDesc.SampleHeight = m_Height;
	videoDesc.SampleFormat.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG2;
	videoDesc.SampleFormat.NominalRange = DXVA2_NominalRange_0_255;
	videoDesc.SampleFormat.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709;//EX_COLOR_INFO[g_ExColorInfo][0];
	videoDesc.SampleFormat.VideoLighting = DXVA2_VideoLighting_dim;
	videoDesc.SampleFormat.VideoPrimaries = DXVA2_VideoPrimaries_BT709;
	videoDesc.SampleFormat.VideoTransferFunction = DXVA2_VideoTransFunc_709;
	videoDesc.SampleFormat.SampleFormat = DXVA2_SampleProgressiveFrame;
	videoDesc.Format = D3DFMT_X8R8G8B8;
	videoDesc.InputSampleFreq.Numerator = 60;
	videoDesc.InputSampleFreq.Denominator = 1;
	videoDesc.OutputFrameFreq.Numerator = 60;
	videoDesc.OutputFrameFreq.Denominator = 1;

	UINT count, count1;//, count2;
	GUID* guids = NULL;

	HR(m_DXVAService->GetVideoProcessorDeviceGuids(&videoDesc, &count, &guids), _("Nie mo�na pobra� GUID�w DXVA"));
	D3DFORMAT* formats = NULL;
	//D3DFORMAT* formats2 = NULL;
	bool isgood = false;
	GUID dxvaGuid;
	DXVA2_VideoProcessorCaps DXVAcaps;
	HRESULT hr;
	for (UINT i = 0; i < count; i++){
		hr = m_DXVAService->GetVideoProcessorRenderTargets(guids[i], &videoDesc, &count1, &formats);
		if (FAILED(hr)){ KaiLog(_("Nie mo�na uzyska� format�w DXVA")); continue; }
		for (UINT j = 0; j < count1; j++)
		{
			if (formats[j] == D3DFMT_X8R8G8B8)
			{
				isgood = true; //break;
			}

		}

		CoTaskMemFree(formats);
		if (!isgood){ KaiLog(_("Ten format nie jest obs�ugiwany przez DXVA")); continue; }
		isgood = false;

		hr = m_DXVAService->GetVideoProcessorCaps(guids[i], &videoDesc, D3DFMT_X8R8G8B8, &DXVAcaps);
		if (FAILED(hr)){ KaiLog(_("GetVideoProcessorCaps zawiod�o")); continue; }
		if (DXVAcaps.NumForwardRefSamples > 0 || DXVAcaps.NumBackwardRefSamples > 0){
			continue;
		}

		//if(DXVAcaps.DeviceCaps!=4){continue;}//DXVAcaps.InputPool
		hr = m_DXVAService->CreateSurface(m_Width, m_Height, 0, m_D3DFormat, D3DPOOL_DEFAULT, 0,
			DXVA2_VideoSoftwareRenderTarget, &m_MainSurface, NULL);
		if (FAILED(hr)){ KaiLog(wxString::Format(_("Nie mo�na stworzy� powierzchni DXVA %i"), (int)i)); continue; }

		hr = m_DXVAService->CreateVideoProcessor(guids[i], &videoDesc, D3DFMT_X8R8G8B8, 0, &m_DXVAProcessor);
		if (FAILED(hr)){ KaiLog(_("Nie mo�na stworzy� processora DXVA")); continue; }
		dxvaGuid = guids[i]; isgood = true;
		break;
	}
	CoTaskMemFree(guids);
	PTR(isgood, L"Nie ma �adnych guid�w");

	return true;
}

void RendererDirectShow::Render(bool redrawSubsOnFrame, bool wait)
{
	wxCriticalSectionLocker lock(m_MutexRendering);
	m_VideoResized = false;
	HRESULT hr = S_OK;

	if (m_DeviceLost)
	{
		if (FAILED(hr = m_D3DDevice->TestCooperativeLevel()))
		{
			if (D3DERR_DEVICELOST == hr ||
				D3DERR_DRIVERINTERNALERROR == hr){
				return;
			}

			if (D3DERR_DEVICENOTRESET == hr)
			{
				Clear();
				InitDX();
				RecreateSurface();
				if (m_Visual){
					m_Visual->SizeChanged(wxRect(m_BackBufferRect.left, m_BackBufferRect.top,
						m_BackBufferRect.right, m_BackBufferRect.bottom), m_D3DLine, m_D3DFont, m_D3DDevice);
				}
				m_DeviceLost = false;
				Render(true, false);
				return;
			}
			return;
		}
		m_DeviceLost = false;
	}

	hr = m_D3DDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

	DXVA2_VideoProcessBltParams blt = { 0 };
	DXVA2_VideoSample samples = { 0 };
	LONGLONG start_100ns = m_Time * 10000;
	LONGLONG end_100ns = start_100ns + 170000;
	blt.TargetFrame = start_100ns;
	blt.TargetRect = m_WindowRect;

	// DXVA2_VideoProcess_Constriction
	blt.ConstrictionSize.cx = m_WindowRect.right - m_WindowRect.left;
	blt.ConstrictionSize.cy = m_WindowRect.bottom - m_WindowRect.top;
	DXVA2_AYUVSample16 color;

	color.Cr = 0x8000;
	color.Cb = 0x8000;
	color.Y = 0x0F00;
	color.Alpha = 0xFFFF;
	blt.BackgroundColor = color;

	// DXVA2_VideoProcess_YUV2RGBExtended
	blt.DestFormat.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_Unknown;
	blt.DestFormat.NominalRange = DXVA2_NominalRange_0_255;//EX_COLOR_INFO[g_ExColorInfo][1];
	blt.DestFormat.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709;
	blt.DestFormat.VideoLighting = DXVA2_VideoLighting_dim;
	blt.DestFormat.VideoPrimaries = DXVA2_VideoPrimaries_BT709;
	blt.DestFormat.VideoTransferFunction = DXVA2_VideoTransFunc_709;

	blt.DestFormat.SampleFormat = DXVA2_SampleProgressiveFrame;
	// Initialize main stream video sample.
	//
	samples.Start = start_100ns;
	samples.End = end_100ns;

	// DXVA2_VideoProcess_YUV2RGBExtended
	samples.SampleFormat.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG2;
	samples.SampleFormat.NominalRange = DXVA2_NominalRange_0_255;
	samples.SampleFormat.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709;//EX_COLOR_INFO[g_ExColorInfo][0];
	samples.SampleFormat.VideoLighting = DXVA2_VideoLighting_dim;
	samples.SampleFormat.VideoPrimaries = DXVA2_VideoPrimaries_BT709;
	samples.SampleFormat.VideoTransferFunction = DXVA2_VideoTransFunc_709;

	samples.SampleFormat.SampleFormat = DXVA2_SampleProgressiveFrame;

	samples.SrcSurface = m_MainSurface;

	samples.SrcRect = m_MainStreamRect;

	samples.DstRect = m_BackBufferRect;

	// DXVA2_VideoProcess_PlanarAlpha
	samples.PlanarAlpha = DXVA2_Fixed32OpaqueAlpha();

	hr = m_DXVAProcessor->VideoProcessBlt(m_BlackBarsSurface, &blt, &samples, 1, NULL);
	
	hr = m_D3DDevice->BeginScene();

#if byvertices


	// Render the vertex buffer contents
	hr = m_D3DDevice->SetStreamSource(0, vertex, 0, sizeof(CUSTOMVERTEX));
	hr = m_D3DDevice->SetVertexShader(NULL);
	hr = m_D3DDevice->SetFVF(D3DFVF_CUSTOMVERTEX);
	hr = m_D3DDevice->SetTexture(0, texture);
	hr = m_D3DDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0, 2);
#endif

	if (m_Visual){ m_Visual->Draw(m_Time); }

	if (cross){
		DRAWOUTTEXT(m_D3DFont, coords, crossRect, (crossRect.left < vectors[0].x) ? 10 : 8, 0xFFFFFFFF)
			hr = m_D3DLine->SetWidth(3);
		hr = m_D3DLine->Begin();
		hr = m_D3DLine->Draw(&vectors[0], 2, 0xFF000000);
		hr = m_D3DLine->Draw(&vectors[2], 2, 0xFF000000);
		hr = m_D3DLine->End();
		hr = m_D3DLine->SetWidth(1);
		D3DXVECTOR2 v1[4];
		v1[0] = vectors[0];
		v1[0].x += 0.5f;
		v1[1] = vectors[1];
		v1[1].x += 0.5f;
		v1[2] = vectors[2];
		v1[2].y += 0.5f;
		v1[3] = vectors[3];
		v1[3].y += 0.5f;
		hr = m_D3DLine->Begin();
		hr = m_D3DLine->Draw(&v1[0], 2, 0xFFFFFFFF);
		hr = m_D3DLine->Draw(&v1[2], 2, 0xFFFFFFFF);
		hr = m_D3DLine->End();
	}

	if (videoControl->m_FullScreenProgressBar){
		DRAWOUTTEXT(m_D3DFont, m_ProgressBarTime, m_ProgressBarRect, DT_LEFT | DT_TOP, 0xFFFFFFFF)
			hr = m_D3DLine->SetWidth(1);
		hr = m_D3DLine->Begin();
		hr = m_D3DLine->Draw(&vectors[4], 5, 0xFF000000);
		hr = m_D3DLine->Draw(&vectors[9], 5, 0xFFFFFFFF);
		hr = m_D3DLine->End();
		hr = m_D3DLine->SetWidth(7);
		hr = m_D3DLine->Begin();
		hr = m_D3DLine->Draw(&vectors[14], 2, 0xFFFFFFFF);
		hr = m_D3DLine->End();
	}
	if (m_HasZoom){ DrawZoom(); }
	// End the scene
	hr = m_D3DDevice->EndScene();
	hr = m_D3DDevice->Present(NULL, &m_WindowRect, NULL, NULL);
	if (D3DERR_DEVICELOST == hr ||
		D3DERR_DRIVERINTERNALERROR == hr){
		if (!m_DeviceLost){
			m_DeviceLost = true;
		}
		Render(true, false);
	}

}

void RendererDirectShow::RecreateSurface()
{
	int all = m_Height * m_Pitch;
	char *cpy = new char[all];
	byte *cpy1 = (byte*)cpy;
	byte *data1 = (byte*)m_FrameBuffer;
	memcpy(cpy1, data1, all);
	DrawTexture(cpy1);
	delete[] cpy;
}

bool RendererDirectShow::OpenFile(const wxString &fname, wxString *textsubs, bool vobsub, bool changeAudio)
{
	wxMutexLocker lock(m_MutexOpen);
	kainoteApp *Kaia = (kainoteApp*)wxTheApp;
	if (m_State == Playing){ videoControl->Stop(); }

	if (m_State != None){
		m_VideoResized = seek = videoControl->m_FullScreenProgressBar = false;
		m_State = None;
		Clear();
	}
	m_Time = 0;
	m_Frame = 0;


	if (!vplayer){ vplayer = new DShowPlayer(videoControl); }

	if (!vplayer->OpenFile(fname, vobsub)){
		return false;
	}
	wxSize videoSize = vplayer->GetVideoSize();
	m_Width = videoSize.x; m_Height = videoSize.y;
	if (m_Width % 2 != 0){ m_Width++; }

	m_Pitch = m_Width * vplayer->inf.bytes;
	videoControl->m_FPS = vplayer->inf.fps;
	m_Format = vplayer->inf.CT;
	videoControl->m_AspectRatioX = vplayer->inf.ARatioX;
	videoControl->m_AspectRatioY = vplayer->inf.ARatioY;
	m_D3DFormat = (m_Format == 5) ? D3DFORMAT('21VN') : (m_Format == 3) ? D3DFORMAT('21VY') :
		(m_Format == 2) ? D3DFMT_YUY2 : D3DFMT_X8R8G8B8;

	m_SwapFrame = (m_Format == 0 && !vplayer->HasVobsub());
	if (m_AudioPlayer){
		Kaia->Frame->OpenAudioInTab(tab, GLOBAL_CLOSE_AUDIO, L"");
	}

	diff = 0;
	m_FrameDuration = (1000.0f / videoControl->m_FPS);
	if (videoControl->m_AspectRatioY == 0 || videoControl->m_AspectRatioX == 0){ videoControl->m_AspectRatio = 0.0f; }
	else{ videoControl->m_AspectRatio = (float)videoControl->m_AspectRatioY / (float)videoControl->m_AspectRatioX; }

	m_MainStreamRect.bottom = m_Height;
	m_MainStreamRect.right = m_Width;
	m_MainStreamRect.left = 0;
	m_MainStreamRect.top = 0;
	if (m_FrameBuffer){ delete[] m_FrameBuffer; m_FrameBuffer = NULL; }
	m_FrameBuffer = new char[m_Height*m_Pitch];

	if (!InitDX()){ return false; }
	UpdateRects();

	if (!framee){ framee = new csri_frame; }
	if (!format){ format = new csri_fmt; }
	for (int i = 1; i < 4; i++){
		framee->planes[i] = NULL;
		framee->strides[i] = NULL;
	}

	framee->pixfmt = (m_Format == 5) ? CSRI_F_YV12A : (m_Format == 3) ? CSRI_F_YV12 :
		(m_Format == 2) ? CSRI_F_YUY2 : CSRI_F_BGR_;

	format->width = m_Width;
	format->height = m_Height;
	format->pixfmt = framee->pixfmt;

	if (!vobsub){
		OpenSubs(textsubs, false);
	}
	else{
		SAFE_DELETE(textsubs);
		OpenSubs(0, false);
	}
	m_State = Stopped;
	vplayer->GetChapters(&m_Chapters);

	if (m_Visual){
		m_Visual->SizeChanged(wxRect(m_BackBufferRect.left, m_BackBufferRect.top,
			m_BackBufferRect.right, m_BackBufferRect.bottom), m_D3DLine, m_D3DFont, m_D3DDevice);
	}
	return true;
}

bool RendererDirectShow::OpenSubs(wxString *textsubs, bool redraw, bool fromFile)
{
	wxCriticalSectionLocker lock(m_MutexRendering);
	if (instance) csri_close(instance);
	instance = NULL;

	if (!textsubs) {
		if (redraw && m_State != None && m_FrameBuffer){
			RecreateSurface();
		}
		m_HasDummySubs = true;
		return true;
	}

	if (m_HasVisualEdition && m_Visual->Visual == VECTORCLIP && m_Visual->dummytext){
		wxString toAppend = m_Visual->dummytext->Trim().AfterLast(L'\n') + L"\r\n";
		if (fromFile){
			OpenWrite ow(*textsubs, false);
			ow.PartFileWrite(toAppend);
			ow.CloseFile();
		}
		else{
			(*textsubs) << toAppend;
		}
	}

	m_HasDummySubs = !fromFile;

	wxScopedCharBuffer buffer = textsubs->mb_str(wxConvUTF8);
	int size = strlen(buffer);


	// Select renderer
	csri_rend *vobsub = Options.GetVSFilter();
	if (!vobsub){ KaiLog(_("CSRI odm�wi�o pos�usze�stwa.")); delete textsubs; return false; }

	instance = (fromFile) ? csri_open_file(vobsub, buffer, NULL) : csri_open_mem(vobsub, buffer, size, NULL);
	if (!instance){ KaiLog(_("B��d, instancja VobSuba nie zosta�a utworzona.")); delete textsubs; return false; }

	if (!format || csri_request_fmt(instance, format)){
		KaiLog(_("CSRI nie obs�uguje tego formatu."));
		csri_close(instance);
		instance = NULL;
		delete textsubs; return false;
	}

	if (redraw && m_State != None && m_FrameBuffer){
		RecreateSurface();
	}

	delete textsubs;
	return true;
}

bool RendererDirectShow::Play(int end)
{
	SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS);
	if (!(videoControl->IsShown() || (videoControl->m_FullScreenWindow && videoControl->m_FullScreenWindow->IsShown()))){ return false; }
	if (m_HasVisualEdition){
		wxString *txt = tab->Grid->SaveText();
		OpenSubs(txt, false, true);
		SAFE_DELETE(m_Visual->dummytext);
		m_HasVisualEdition = false;
	}
	else if (m_HasDummySubs && tab->editor){
		OpenSubs(tab->Grid->SaveText(), false, true);
	}

	if (end > 0){ m_PlayEndTime = end; }
	m_PlayEndTime = 0;
	if (m_Time < GetDuration() - m_FrameDuration) 
		vplayer->Play(); 

	m_State = Playing;
	return true;
}


bool RendererDirectShow::Pause()
{
	if (m_State == Playing){
		SetThreadExecutionState(ES_CONTINUOUS);
		m_State = Paused;
		vplayer->Pause();
		
	}
	else if (m_State != None){
		Play();
	}
	else{ return false; }
	return true;
}

bool RendererDirectShow::Stop()
{
	if (m_State == Playing){
		SetThreadExecutionState(ES_CONTINUOUS);
		m_State = Stopped;
		vplayer->Stop();
		m_PlayEndTime = 0;
		m_Time = 0;
		return true;
	}
	return false;
}

void RendererDirectShow::SetPosition(int _time, bool starttime/*=true*/, bool corect/*=true*/, bool async /*= true*/)
{

	bool playing = m_State == Playing;
	m_Time = MID(0, _time, GetDuration());
	if (corect){
		m_Time /= m_FrameDuration;
		if (starttime){ m_Time++; }
		m_Time *= m_FrameDuration;
	}
	m_PlayEndTime = 0;
	seek = true;
	vplayer->SetPosition(m_Time);
	if (m_HasVisualEdition){
		SAFE_DELETE(m_Visual->dummytext);
		if (m_Visual->Visual == VECTORCLIP){
			m_Visual->SetClip(m_Visual->GetVisual(), true, false, false);
		}
		else{
			OpenSubs((playing) ? tab->Grid->SaveText() : tab->Grid->GetVisible(), true, playing);
			if (m_State == Playing){ m_HasVisualEdition = false; }
		}
	}
	else if (m_HasDummySubs && tab->editor){
		OpenSubs((playing) ? tab->Grid->SaveText() : tab->Grid->GetVisible(), true, playing);
	}
	
}

int VideoRenderer::GetDuration()
{
	return vplayer->GetDuration();
}

int RendererDirectShow::GetFrameTime(bool start)
{
	int halfFrame = (start) ? -(m_FrameDuration / 2.0f) : (m_FrameDuration / 2.0f) + 1;
	return m_Time + halfFrame;
}

void RendererDirectShow::GetStartEndDelay(int startTime, int endTime, int *retStart, int *retEnd)
{
	if (!retStart || !retEnd){ return; }
	
	int frameStartTime = (((float)startTime / 1000.f) * videoControl->m_FPS);
	int frameEndTime = (((float)endTime / 1000.f) * videoControl->m_FPS);
	frameStartTime++;
	frameEndTime++;
	*retStart = (((frameStartTime * 1000) / videoControl->m_FPS) + 0.5f) - startTime;
	*retEnd = (((frameEndTime * 1000) / videoControl->m_FPS) + 0.5f) - endTime;

}

int RendererDirectShow::GetFrameTimeFromTime(int _time, bool start)
{
	int halfFrame = (start) ? -(m_FrameDuration / 2.0f) : (m_FrameDuration / 2.0f) + 1;
	return _time + halfFrame;
}

int RendererDirectShow::GetFrameTimeFromFrame(int frame, bool start)
{
	int halfFrame = (start) ? -(m_FrameDuration / 2.0f) : (m_FrameDuration / 2.0f) + 1;
	return (frame * (1000.f / videoControl->m_FPS)) + halfFrame;
}

int RendererDirectShow::GetPlayEndTime(int _time)
{
	int newTime = _time;
	newTime /= m_FrameDuration;
	newTime = (newTime * m_FrameDuration) + 1.f;
	if (_time == newTime && newTime % 10 == 0){ newTime -= 5; }
	return newTime;
}

void RendererDirectShow::OpenKeyframes(const wxString &filename)
{
	AudioBox * audio = tab->Edit->ABox;
	if (audio){
		// skip return when audio do not have own provider or file didn't have video for take timecodes.
		if (audio->OpenKeyframes(filename)){
			return;
		}
	}
	//if there is no FFMS2 or audiobox we store keyframes path;
	m_KeyframesFileName = filename;
}

void RendererDirectShow::GetFpsnRatio(float *fps, long *arx, long *ary)
{
	vplayer->GetFpsnRatio(fps, arx, ary);
}

void RendererDirectShow::GetVideoSize(int *width, int *height)
{
	wxSize sz = vplayer->GetVideoSize();
	*width = sz.x;
	*height = sz.y;
}

wxSize RendererDirectShow::GetVideoSize()
{
	wxSize sz;
	sz = vplayer->GetVideoSize(); 
	return sz; 
}

void RendererDirectShow::SetVolume(int vol)
{
	if (m_State == None){ return; }
	vplayer->SetVolume(vol);
}

int RendererDirectShow::GetVolume()
{
	if (m_State == None){ return 0; }
	return vplayer->GetVolume();
}

void RendererDirectShow::ChangePositionByFrame(int step)
{
	if (m_State == Playing || m_State == None){ return; }
	
	m_Time += (m_FrameDuration * step);
	SetPosition(m_Time, true, false);
	videoControl->RefreshTime();

}


wxArrayString RendererDirectShow::GetStreams()
{
	return vplayer->GetStreams();
}

void RendererDirectShow::EnableStream(long index)
{
	if (vplayer->stream){
		seek = true;
		auto hr = vplayer->stream->Enable(index, AMSTREAMSELECTENABLE_ENABLE);
		if (FAILED(hr)){
			KaiLog(L"Cannot change stream");
		}
	}
}



void RendererDirectShow::ChangeVobsub(bool vobsub)
{
	if (!vplayer){ return; }
	int tmptime = m_Time;
	OpenSubs((vobsub) ? NULL : tab->Grid->SaveText(), true, true);
	vplayer->OpenFile(tab->VideoPath, vobsub);
	m_Format = vplayer->inf.CT;
	D3DFORMAT tmpd3dformat = (m_Format == 5) ? D3DFORMAT('21VN') : (m_Format == 3) ? D3DFORMAT('21VY') :
		(m_Format == 2) ? D3DFMT_YUY2 : D3DFMT_X8R8G8B8;
	m_SwapFrame = (m_Format == 0 && !vplayer->HasVobsub());
	if (tmpd3dformat != m_D3DFormat){
		m_D3DFormat = tmpd3dformat;
		int tmppitch = m_Width * vplayer->inf.bytes;
		if (tmppitch != m_Pitch){
			m_Pitch = tmppitch;
			if (m_FrameBuffer){ delete[] m_FrameBuffer; m_FrameBuffer = NULL; }
			m_FrameBuffer = new char[m_Height * m_Pitch];
		}
		UpdateVideoWindow();
	}
	SetPosition(tmptime);
	if (m_State == Paused){ vplayer->Play(); vplayer->Pause(); }
	else if (m_State == Playing){ vplayer->Play(); }
	int pos = tab->Video->m_VolumeSlider->GetValue();
	SetVolume(-(pos * pos));
	tab->Video->ChangeStream();
}

bool RendererDirectShow::EnumFilters(Menu *menu)
{
	return vplayer->EnumFilters(menu); 
}

bool VideoRenderer::FilterConfig(wxString name, int idx, wxPoint pos)
{
	return vplayer->FilterConfig(name, idx, pos);
}

byte *VideoRenderer::GetFramewithSubs(bool subs, bool *del)
{
	bool dssubs = (IsDshow && subs && Notebook::GetTab()->editor);
	byte *cpy1;
	byte bytes = (vformat == RGB32) ? 4 : (vformat == YUY2) ? 2 : 1;
	int all = vheight*pitch;
	if (dssubs){
		*del = true;
		char *cpy = new char[all];
		cpy1 = (byte*)cpy;
	}
	else{ *del = false; }
	if (instance && dssubs){
		byte *data1 = (byte*)frameBuffer;
		memcpy(cpy1, data1, all);
		framee->strides[0] = vwidth * bytes;
		framee->planes[0] = cpy1;
		csri_render(instance, framee, (time / 1000.0));
	}
	return (dssubs) ? cpy1 : (byte*)frameBuffer;
}