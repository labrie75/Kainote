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

#include "RendererFFMS2.h"
#include "kainoteApp.h"
#include "CsriMod.h"
#include "OpennWrite.h"

RendererFFMS2::RendererFFMS2(VideoCtrl *control)
	: RendererVideo(control)
	, VFF(NULL)
{
	
}
RendererFFMS2::~RendererFFMS2()
{
	SAFE_DELETE(VFF);
}

void RendererFFMS2::Render(bool redrawSubsOnFrame, bool wait)
{
	if (redrawSubsOnFrame && !m_DeviceLost){
		VFF->Render(wait);
		m_VideoResized = false;
		return;
	}
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

	
	hr = m_D3DDevice->StretchRect(m_MainSurface, &m_MainStreamRect, m_BlackBarsSurface, &m_BackBufferRect, D3DTEXF_LINEAR);
	if (FAILED(hr)){ KaiLog(_("Nie mo�na na�o�y� powierzchni na siebie")); }


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

bool RendererFFMS2::OpenFile(const wxString &fname, wxString *textsubs, bool vobsub, bool changeAudio)
{
	wxMutexLocker lock(m_MutexOpen);
	kainoteApp *Kaia = (kainoteApp*)wxTheApp;
	VideoFfmpeg *tmpvff = NULL;
	if (m_State == Playing){ videoControl->Stop(); }

	bool success;
	tmpvff = new VideoFfmpeg(fname, this, (videoControl->m_IsFullscreen) ? videoControl->m_FullScreenWindow : (wxWindow *)Kaia->Frame, &success);
	//this is safe mode, when new video not load, 
	//the last opened will not be released
	if (!success || !tmpvff){
		SAFE_DELETE(tmpvff);
		return false;
	}
	//when loading only audio do not remove video
	if (tmpvff->width < 0 && tmpvff->GetSampleRate() > 0){
		VideoFfmpeg *tmp = VFF;
		VFF = tmpvff;
		Kaia->Frame->OpenAudioInTab(tab, 40000, fname);
		m_AudioPlayer = tab->Edit->ABox->audioDisplay;
		VFF = tmp;
		return false;
	}

	SAFE_DELETE(VFF);

	if (m_State != None){
		m_VideoResized = seek = videoControl->m_FullScreenProgressBar = false;
		m_State = None;
		Clear();
	}

	m_Time = 0;
	m_Frame = 0;

	VFF = tmpvff;
	m_D3DFormat = D3DFMT_X8R8G8B8;
	m_Format = RGB32;
	m_Width = VFF->width;
	m_Height = VFF->height;
	videoControl->m_FPS = VFF->fps;
	videoControl->m_AspectRatioX = VFF->arwidth;
	videoControl->m_AspectRatioY = VFF->arheight;
	if (m_Width % 2 != 0){ m_Width++; }
	m_Pitch = m_Width * 4;
	if (changeAudio){
		if (VFF->GetSampleRate() > 0){
			Kaia->Frame->OpenAudioInTab(tab, 40000, fname);
			m_AudioPlayer = tab->Edit->ABox->audioDisplay;
		}
		else if (m_AudioPlayer){ Kaia->Frame->OpenAudioInTab(tab, GLOBAL_CLOSE_AUDIO, L""); }
	}
	if (!VFF || VFF->width < 0){
		return false;
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
	VFF->GetChapters(&m_Chapters);

	if (m_Visual){
		m_Visual->SizeChanged(wxRect(m_BackBufferRect.left, m_BackBufferRect.top,
			m_BackBufferRect.right, m_BackBufferRect.bottom), m_D3DLine, m_D3DFont, m_D3DDevice);
	}
	return true;
}

bool RendererFFMS2::OpenSubs(wxString *textsubs, bool redraw, bool fromFile)
{
	wxCriticalSectionLocker lock(m_MutexRendering);
	if (instance) csri_close(instance);
	instance = NULL;

	if (!textsubs) {
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

	delete textsubs;
	return true;
}

bool RendererFFMS2::Play(int end)
{
	if (m_Time >= GetDuration()){ return false; }
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
	m_PlayEndTime = GetDuration();

	m_State = Playing;

	m_Time = VFF->Timecodes[m_Frame];
	m_LastTime = timeGetTime() - m_Time;
	if (m_AudioPlayer){ m_AudioPlayer->Play(m_Time, -1, false); }
	VFF->Play();

	return true;
}


bool RendererFFMS2::Pause()
{
	if (m_State == Playing){
		SetThreadExecutionState(ES_CONTINUOUS);
		m_State = Paused;
		if (m_AudioPlayer){ m_AudioPlayer->Stop(false); }
	}
	else if (m_State != None){
		Play();
	}
	else{ return false; }
	return true;
}

bool RendererFFMS2::Stop()
{
	if (m_State == Playing){
		SetThreadExecutionState(ES_CONTINUOUS);
		m_State = Stopped;
		if (m_AudioPlayer){
			m_AudioPlayer->Stop();
			m_PlayEndTime = GetDuration();
		}
		m_Time = 0;
		return true;
	}
	return false;
}

void RendererFFMS2::SetPosition(int _time, bool starttime/*=true*/, bool corect/*=true*/, bool async /*= true*/)
{
	if (m_State == Playing || !async)
		SetFFMS2Position(_time, starttime);
	else
		VFF->SetPosition(_time, starttime);
}

//is from video thread make safe any deletion
void RendererFFMS2::SetFFMS2Position(int _time, bool starttime){
	bool playing = m_State == Playing;
	m_Frame = VFF->GetFramefromMS(_time, (m_Time > _time) ? 0 : m_Frame);
	if (!starttime){
		m_Frame--;
		if (VFF->Timecodes[m_Frame] >= _time){ m_Frame--; }
	}
	m_Time = VFF->Timecodes[m_Frame];
	m_LastTime = timeGetTime() - m_Time;
	m_PlayEndTime = GetDuration();

	if (m_HasVisualEdition){
		//block removing or changing visual from main thread
		wxMutexLocker lock(m_MutexVisualChange);
		SAFE_DELETE(m_Visual->dummytext);
		if (m_Visual->Visual == VECTORCLIP){
			m_Visual->SetClip(m_Visual->GetVisual(), true, false, false);
		}
		else{
			OpenSubs((playing) ? tab->Grid->SaveText() : tab->Grid->GetVisible(), true, playing);
			if (playing){ m_HasVisualEdition = false; }
		}
	}
	else if (m_HasDummySubs){
		OpenSubs((playing) ? tab->Grid->SaveText() : tab->Grid->GetVisible(), true, playing);
	}
	if (playing){
		if (m_AudioPlayer){
			m_AudioPlayer->player->SetCurrentPosition(m_AudioPlayer->GetSampleAtMS(m_Time));
		}
	}
	else{
		//rebuild spectrum cause position can be changed
		//and it causes random bugs
		if (m_AudioPlayer){ m_AudioPlayer->UpdateImage(false, true); }
		VFF->Render();
		videoControl->RefreshTime();
	}
}

int RendererFFMS2::GetDuration()
{
	return VFF->Duration * 1000.0;
}

int RendererFFMS2::GetFrameTime(bool start)
{
	if (start){
		int prevFrameTime = VFF->GetMSfromFrame(m_Frame - 1);
		return m_Time + ((prevFrameTime - m_Time) / 2);
	}
	else{
		if (m_Frame + 1 >= VFF->NumFrames){
			int prevFrameTime = VFF->GetMSfromFrame(m_Frame - 1);
			return m_Time + ((m_Time - prevFrameTime) / 2);
		}
		else{
			int nextFrameTime = VFF->GetMSfromFrame(m_Frame + 1);
			return m_Time + ((nextFrameTime - m_Time) / 2);
		}
	}
}

void RendererFFMS2::GetStartEndDelay(int startTime, int endTime, int *retStart, int *retEnd)
{
	if (!retStart || !retEnd){ return; }
	
	int frameStartTime = VFF->GetFramefromMS(startTime);
	int frameEndTime = VFF->GetFramefromMS(endTime, frameStartTime);
	*retStart = VFF->GetMSfromFrame(frameStartTime) - startTime;
	*retEnd = VFF->GetMSfromFrame(frameEndTime) - endTime;
}

int RendererFFMS2::GetFrameTimeFromTime(int _time, bool start)
{
	if (start){
		int frameFromTime = VFF->GetFramefromMS(_time);
		int prevFrameTime = VFF->GetMSfromFrame(frameFromTime - 1);
		int frameTime = VFF->GetMSfromFrame(frameFromTime);
		return frameTime + ((prevFrameTime - frameTime) / 2);
	}
	else{
		int frameFromTime = VFF->GetFramefromMS(_time);
		int nextFrameTime = VFF->GetMSfromFrame(frameFromTime + 1);
		int frameTime = VFF->GetMSfromFrame(frameFromTime);
		return frameTime + ((nextFrameTime - frameTime) / 2);
	}
}

int RendererFFMS2::GetFrameTimeFromFrame(int frame, bool start)
{
	if (start){
		int prevFrameTime = VFF->GetMSfromFrame(frame - 1);
		int frameTime = VFF->GetMSfromFrame(frame);
		return frameTime + ((prevFrameTime - frameTime) / 2);
	}
	else{
		int nextFrameTime = VFF->GetMSfromFrame(frame + 1);
		int frameTime = VFF->GetMSfromFrame(frame);
		return frameTime + ((nextFrameTime - frameTime) / 2);
	}
}

int RendererFFMS2::GetPlayEndTime(int _time)
{
	int frameFromTime = VFF->GetFramefromMS(_time);
	int prevFrameTime = VFF->GetMSfromFrame(frameFromTime - 1);
	return prevFrameTime;
}

void RendererFFMS2::OpenKeyframes(const wxString &filename)
{
	VFF->OpenKeyframes(filename);
}

void RendererFFMS2::GetFpsnRatio(float *fps, long *arx, long *ary)
{
	*fps = VFF->fps;
	*arx = VFF->arwidth;
	*ary = VFF->arheight;
}

void RendererFFMS2::GetVideoSize(int *width, int *height)
{
	*width = VFF->width;
	*height = VFF->height;
}

void RendererFFMS2::SetVolume(int vol)
{
	if (m_State == None || !m_AudioPlayer){ return; }
	
	vol = 7600 + vol;
	double dvol = vol / 7600.0;
	int sliderValue = (dvol * 99) + 1;
	if (tab->Edit->ABox){
		tab->Edit->ABox->SetVolume(sliderValue);
	}
}

int RendererFFMS2::GetVolume()
{
	if (m_State == None || !m_AudioPlayer){ return 0; }
	double dvol = m_AudioPlayer->player->GetVolume();
	dvol = sqrt(dvol);
	dvol *= 8100.0;
	dvol -= 8100.0;
	return dvol;
}

void RendererFFMS2::ChangePositionByFrame(int step)
{
	if (m_State == Playing || m_State == None){ return; }
	
		m_Frame = MID(0, m_Frame + step, VFF->NumFrames - 1);
		m_Time = VFF->Timecodes[m_Frame];
		if (m_HasVisualEdition || m_HasDummySubs){
			OpenSubs(tab->Grid->SaveText(), false, true);
			m_HasVisualEdition = false;
		}
		if (m_AudioPlayer){ m_AudioPlayer->UpdateImage(true, true); }
		Render(true, false);
	
	
	videoControl->RefreshTime();

}

//bool VideoRenderer::EnumFilters(Menu *menu)
//{
//	if (vplayer){ return vplayer->EnumFilters(menu); }
//	return false;
//}
//
//bool VideoRenderer::FilterConfig(wxString name, int idx, wxPoint pos)
//{
//	if (vplayer){ return vplayer->FilterConfig(name, idx, pos); }
//	return false;
//}

byte *RendererFFMS2::GetFramewithSubs(bool subs, bool *del)
{
	bool ffnsubs = (!subs);
	byte *cpy1;
	byte bytes = (m_Format == RGB32) ? 4 : (m_Format == YUY2) ? 2 : 1;
	int all = m_Height * m_Pitch;
	if (ffnsubs){
		*del = true;
		char *cpy = new char[all];
		cpy1 = (byte*)cpy;
		VFF->GetFrame(m_Time, cpy1);
	}
	else{ *del = false; }
	return (ffnsubs) ? cpy1 : (byte*)m_FrameBuffer;
}

void RendererFFMS2::GoToNextKeyframe()
{
	for (size_t i = 0; i < VFF->KeyFrames.size(); i++){
		if (VFF->KeyFrames[i] > m_Time){
			SetPosition(VFF->KeyFrames[i]);
			return;
		}
	}
	SetPosition(VFF->KeyFrames[0]);
}
void RendererFFMS2::GoToPrevKeyframe()
{
	for (int i = VFF->KeyFrames.size() - 1; i >= 0; i--){
		if (VFF->KeyFrames[i] < m_Time){
			SetPosition(VFF->KeyFrames[i]);
			return;
		}
	}
	SetPosition(VFF->KeyFrames[VFF->KeyFrames.size() - 1]);
}

bool RendererFFMS2::HasFFMS2()
{
	return VFF != NULL;
}

VideoFfmpeg * RendererFFMS2::GetFFMS2()
{
	return VFF;
}