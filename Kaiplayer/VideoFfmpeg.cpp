﻿//  Copyright (c) 2016 - 2020, Marcin Drob

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

#include <wx/dir.h>
#include "VideoFfmpeg.h"
#include "KainoteApp.h"
#include "Config.h"
#include "MKVWrap.h"
#include "KaiMessageBox.h"
#include "KeyframesLoader.h"
#include <objbase.h>
#include <algorithm>
#include <process.h>
#include "include\ffmscompat.h"
#include "Stylelistbox.h"
#include <wx/file.h>
#include <thread>



VideoFfmpeg::VideoFfmpeg(const wxString &filename, RendererVideo *renderer, wxWindow *progressSinkWindow, bool *_success)
	: rend(renderer)
	, eventStartPlayback(CreateEvent(0, FALSE, FALSE, 0))
	, eventRefresh(CreateEvent(0, FALSE, FALSE, 0))
	, eventSetPosition(CreateEvent(0, FALSE, FALSE, 0))
	, eventKillSelf(CreateEvent(0, FALSE, FALSE, 0))
	, eventComplete(CreateEvent(0, FALSE, FALSE, 0))
	, eventAudioComplete(CreateEvent(0, FALSE, FALSE, 0))
	, blnum(0)
	, Cache(0)
	, Delay(0)
	, audiosource(0)
	, videosource(0)
	, progress(0)
	, thread(0)
	, lastframe(-1)
	, width(-1)
	, index(NULL)
	, isBusy(false)
{
	if (!Options.AudioOpts && !Options.LoadAudioOpts()){ KaiMessageBox(_("Nie można wczytać opcji audio"), _("Błąd")); }
	disccache = !Options.GetBool(AUDIO_RAM_CACHE);

	success = false;
	fname = filename;
	progress = new ProgressSink(progressSinkWindow, _("Indeksowanie pliku wideo"));

	if (renderer){
		unsigned int threadid = 0;
		thread = (HANDLE)_beginthreadex(0, 0, FFMS2Proc, this, 0, &threadid);//CreateThread( NULL, 0,  (LPTHREAD_START_ROUTINE)FFMS2Proc, this, 0, 0);
		SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
		SetThreadName(threadid, "VideoThread");
		progress->ShowDialog();
		WaitForSingleObject(eventComplete, INFINITE);
		ResetEvent(eventComplete);
		*_success = success;
	}
	else{
		progress->SetAndRunTask([=](){return Init(); });
		progress->ShowDialog();
		*_success = ((int)progress->Wait() == 1);
	}
	SAFE_DELETE(progress);
	if (index){ FFMS_DestroyIndex(index); }

}

unsigned int __stdcall VideoFfmpeg::FFMS2Proc(void* cls)
{
	((VideoFfmpeg*)cls)->Processing();
	return 0;
}

void VideoFfmpeg::Processing()
{
	HANDLE events_to_wait[] = {
		eventStartPlayback,
		eventRefresh,
		eventSetPosition,
		eventKillSelf
	};

	success = (Init() == 1);

	progress->EndModal();

	fplane = height * width * 4;
	int tdiff = 0;

	SetEvent(eventComplete);
	if (width < 0){ return; }

	while (1){
		DWORD wait_result = WaitForMultipleObjects(sizeof(events_to_wait) / sizeof(HANDLE), events_to_wait, FALSE, INFINITE);

		if (wait_result == WAIT_OBJECT_0 + 0)
		{
			byte *buff = (byte*)rend->m_FrameBuffer;
			int acttime;
			//isBusy = false;
			while (1){

				if (rend->m_Frame != lastframe){
					rend->m_Time = Timecodes[rend->m_Frame];
					lastframe = rend->m_Frame;
				}
				GetFFMSFrame();

				if (!fframe){
					continue;
				}
				memcpy(&buff[0], fframe->Data[0], fplane);

				rend->DrawTexture(buff);
				rend->Render(false);

				if (rend->m_Time >= rend->m_PlayEndTime || rend->m_Frame >= NumFrames - 1){
					wxCommandEvent *evt = new wxCommandEvent(wxEVT_COMMAND_BUTTON_CLICKED, ID_END_OF_STREAM);
					wxQueueEvent(rend->videoControl, evt);
					break;
				}
				else if (rend->m_State != Playing){
					break;
				}
				acttime = timeGetTime() - rend->m_LastTime;
				
				rend->m_Frame++;
				rend->m_Time = Timecodes[rend->m_Frame];

				tdiff = rend->m_Time - acttime;

				if (tdiff > 0){ Sleep(tdiff); }
				else{
					while (1){
						int frameTime = Timecodes[rend->m_Frame];
						if (frameTime >= acttime || frameTime >= rend->m_PlayEndTime || rend->m_Frame >= NumFrames){
							if (rend->m_Frame >= NumFrames){
								rend->m_Frame = NumFrames - 1;
								rend->m_Time = rend->m_PlayEndTime;
							}
							break;
						}
						else{
							rend->m_Frame++;
						}
					}

				}

			}
		}
		else if (wait_result == WAIT_OBJECT_0 + 1){
			byte *buff = (byte*)rend->m_FrameBuffer;
			if (rend->m_Frame != lastframe){
				GetFFMSFrame();
				lastframe = rend->m_Frame;
			}
			if (!fframe){
				isBusy = false; 
				continue;
			}
			memcpy(&buff[0], fframe->Data[0], fplane);

			rend->DrawTexture(buff);
			rend->Render(false);

			isBusy = false;
		}
		else if (wait_result == WAIT_OBJECT_0 + 2){
			//entire seeking have to be in this thread or subtitles will out of sync
			rend->SetFFMS2Position(changedTime, isStartTime);
		}
		else{
			break;
		}

	}
}


int VideoFfmpeg::Init()
{

	FFMS_Init(0, 1);

	errinfo.Buffer = errmsg;
	errinfo.BufferSize = sizeof(errmsg);
	errinfo.ErrorType = FFMS_ERROR_SUCCESS;
	errinfo.SubType = FFMS_ERROR_SUCCESS;

	FFMS_Indexer *Indexer = FFMS_CreateIndexer(fname.utf8_str(), &errinfo);
	if (!Indexer){
		KaiLog(wxString::Format(_("Wystąpił błąd indeksowania: %s"), errinfo.Buffer)); return 0;
	}

	int NumTracks = FFMS_GetNumTracksI(Indexer);
	int audiotrack = -1;
	wxArrayInt audiotable;
	int videotrack = -1;

	for (int i = 0; i < NumTracks; i++) {
		if (FFMS_GetTrackTypeI(Indexer, i) == FFMS_TYPE_VIDEO && videotrack == -1) {
			videotrack = i;
		}
		else if (FFMS_GetTrackTypeI(Indexer, i) == FFMS_TYPE_AUDIO) {
			audiotable.Add(i);
		}
	}
	wxString ext = fname.AfterLast('.').Lower();
	bool ismkv = (ext == L"mkv");
	bool hasMoreAudioTracks = audiotable.size() > 1;

	if (hasMoreAudioTracks || ismkv){

		wxArrayString tracks;

		if (ismkv){
			MatroskaWrapper mw;
			if (mw.Open(fname, false)){
				Chapter *chap = NULL;
				UINT nchap = 0;
				mkv_GetChapters(mw.file, &chap, &nchap);

				if (chap && nchap && rend){
					for (int i = 0; i < (int)chap->nChildren; i++){
						chapter ch;
						ch.name = wxString(chap->Children[i].Display->String, wxConvUTF8);
						ch.time = (int)(chap->Children[i].Start / 1000000.0);
						chapters.push_back(ch);
					}
				}
				if (hasMoreAudioTracks){
					wxArrayString enabled;
					Options.GetTableFromString(ACCEPTED_AUDIO_STREAM, enabled, L";");
					int enabledSize = enabled.GetCount();
					int lowestIndex = enabledSize;
					for (size_t j = 0; j < audiotable.GetCount(); j++){
						TrackInfo* ti = mkv_GetTrackInfo(mw.file, audiotable[j]);
						if (!ti)
							continue;
						if (enabledSize){
							int index = enabled.Index(ti->Language, false);
							if (index > -1 && index < lowestIndex){
								lowestIndex = index;
								audiotrack = audiotable[j];
								continue;
							}
						}
						wxString all;
						char *description = (ti->Name) ? ti->Name : ti->Language;
						wxString codec = wxString(ti->CodecID, wxConvUTF8);
						if (codec.StartsWith(L"A_"))
							codec = codec.Mid(2);
						all << audiotable[j] << L": " << wxString(description, wxConvUTF8) << 
							L" (" << codec << L")";
						tracks.Add(all);
					}
					if (lowestIndex < enabledSize){
						tracks.Clear();
						hasMoreAudioTracks = false;
						mw.Close();
						goto done;
					}
				}
				mw.Close();
			}
			if (!hasMoreAudioTracks){ audiotrack = (audiotable.size()>0) ? audiotable[0] : -1; goto done; }
		}
		if (!tracks.size() && hasMoreAudioTracks){
			for (size_t j = 0; j < audiotable.size(); j++){
				wxString CodecName(FFMS_GetCodecNameI(Indexer, audiotable[j]), wxConvUTF8);
				wxString all;
				all << audiotable[j] << L": " << CodecName;
				tracks.Add(all);
			}
		}
		audiotrack = progress->ShowSecondaryDialog([=](){
			kainoteApp *Kaia = (kainoteApp*)wxTheApp;

			KaiListBox tracks(Kaia->Frame, tracks, _("Wybierz ścieżkę"), true);
			if (tracks.ShowModal() == wxID_OK){
				int result = wxAtoi(tracks.GetSelection().BeforeFirst(':'));
				return result;
			}
			return -1;
		});

		if (audiotrack == -1){
			FFMS_CancelIndexing(Indexer);
			return 0;
		}

	}
	else if (audiotable.size() > 0){
		audiotrack = audiotable[0];
	}
done:

	indexPath = Options.pathfull + L"\\Indices\\" + fname.AfterLast(L'\\').BeforeLast(L'.') + 
		wxString::Format(L"_%i.ffindex", audiotrack);

	if (wxFileExists(indexPath)){
		index = FFMS_ReadIndex(indexPath.utf8_str(), &errinfo);
		if (!index){/*do nothing to skip*/}
		else if (FFMS_IndexBelongsToFile(index, fname.utf8_str(), &errinfo))
		{
			FFMS_DestroyIndex(index);
			index = NULL;
		}
		else{
			FFMS_CancelIndexing(Indexer);
		}

	}
	bool newIndex = false;
	if (!index){
		FFMS_TrackIndexSettings(Indexer, audiotrack, 1, 0);
		FFMS_SetProgressCallback(Indexer, UpdateProgress, (void*)progress);
		index = FFMS_DoIndexing2(Indexer, FFMS_IEH_IGNORE, &errinfo);
		//in this moment indexer was released, there no need to release it
		if (index == NULL) {
			if (wxString(errinfo.Buffer).StartsWith(L"Cancelled")){
				//No need spam user that he clicked cancel button
				//KaiLog(_("Indeksowanie anulowane przez użytkownika"));
			}
			else{
				KaiLog(wxString::Format(_("Wystąpił błąd indeksowania: %s"), errinfo.Buffer));
			}
			//FFMS_CancelIndexing(Indexer);
			return 0;
		}
		if (!wxDir::Exists(indexPath.BeforeLast(L'\\')))
		{
			wxDir::Make(indexPath.BeforeLast(L'\\'));
		}
		if (FFMS_WriteIndex(indexPath.utf8_str(), index, &errinfo))
		{
			KaiLog(wxString::Format(_("Nie można zapisać indeksu, wystąpił błąd %s"), errinfo.Buffer));
			//FFMS_DestroyIndex(index);
			//FFMS_CancelIndexing(Indexer);
			//return 0;
		}
		newIndex = true;
	}


	if (videotrack != -1){
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		try{
			videosource = FFMS_CreateVideoSource(
				fname.utf8_str(),
				videotrack,
				index,
				sysinfo.dwNumberOfProcessors * 2,
				Options.GetInt(FFMS2_VIDEO_SEEKING),//FFMS_SEEK_NORMAL, // FFMS_SEEK_UNSAFE/*FFMS_SEEK_AGGRESSIVE*/
				&errinfo);
		}
		catch (...){}
		//Since the index is copied into the video source object upon its creation,
		//we can and should now destroy the index object. 

		if (videosource == NULL) {
			if (audiotrack == -1){
				KaiLog(_("Nie można utworzyć VideoSource."));
				return 0;
			}
			else
				goto audio;
		}

		const FFMS_VideoProperties *videoprops = FFMS_GetVideoProperties(videosource);

		NumFrames = videoprops->NumFrames;
		Duration = videoprops->LastTime;
		//Delay = videoprops->FirstTime + (Options.GetInt("Audio Delay")/1000);
		fps = (float)videoprops->FPSNumerator / (float)videoprops->FPSDenominator;

		const FFMS_Frame *propframe = FFMS_GetFrame(videosource, 0, &errinfo);

		width = propframe->EncodedWidth;
		height = propframe->EncodedHeight;
		arwidth = (videoprops->SARNum == 0) ? width : (float)width*((float)videoprops->SARNum / (float)videoprops->SARDen);
		arheight = height;
		CS = propframe->ColorSpace;
		CR = propframe->ColorRange;
		while (1){
			bool divided = false;
			for (int i = 10; i > 1; i--){
				if ((arwidth % i) == 0 && (arheight % i) == 0){
					arwidth /= i; arheight /= i;
					divided = true;
					break;
				}
			}
			if (!divided){ break; }
		}

		int pixfmt[2];
		pixfmt[0] = FFMS_GetPixFmt("bgra");//PIX_FMT_YUYV422; //PIX_FMT_NV12 == 25  PIX_FMT_YUVJ420P;//PIX_FMT_YUV411P;//PIX_FMT_YUV420P; //PIX_FMT_YUYV422;//PIX_FMT_NV12;//FFMS_GetPixFmt("bgra");PIX_FMT_YUYV422;//
		pixfmt[1] = -1;

		if (FFMS_SetOutputFormatV2(videosource, pixfmt, width, height, FFMS_RESIZER_BILINEAR, &errinfo)) {
			KaiLog(_("Nie można przekonwertować wideo na RGBA"));
			return 0;
		}

		if (rend){
			SubsGrid *grid = ((TabPanel*)rend->videoControl->GetParent())->Grid;
			const wxString &colormatrix = grid->GetSInfo(L"YCbCr Matrix");
			bool changeMatrix = false;
			if (CS == FFMS_CS_UNSPECIFIED){
				CS = width > 1024 || height >= 600 ? FFMS_CS_BT709 : FFMS_CS_BT470BG;
			}
			ColorSpace = RealColorSpace = ColorCatrixDescription(CS, CR);
			if (CS == FFMS_CS_BT709 && colormatrix == L"TV.709") {
				if (FFMS_SetInputFormatV(videosource, FFMS_CS_BT709, CR, FFMS_GetPixFmt(""), &errinfo)){
					KaiLog(_("Nie można zmienić macierzy YCbCr"));
				}
			}
			if (colormatrix == L"TV.601"){
				ColorSpace = ColorCatrixDescription(FFMS_CS_BT470BG, CR);
				if (FFMS_SetInputFormatV(videosource, FFMS_CS_BT470BG, CR, FFMS_GetPixFmt(""), &errinfo)) {
					KaiLog(_("Nie można zmienić macierzy YCbCr"));
				}
			}
			else if (colormatrix == L"TV.709"){
				ColorSpace = ColorCatrixDescription(FFMS_CS_BT709, CR);
			}
		}

		FFMS_Track *FrameData = FFMS_GetTrackFromVideo(videosource);
		if (FrameData == NULL){
			KaiLog(_("Nie można pobrać ścieżki wideo"));
			return 0;
		}
		const FFMS_TrackTimeBase *TimeBase = FFMS_GetTimeBase(FrameData);
		if (TimeBase == NULL){
			KaiLog(_("Nie można pobrać informacji o wideo"));
			return 0;
		}

		const FFMS_FrameInfo *CurFrameData;


		// build list of keyframes and timecodes
		for (int CurFrameNum = 0; CurFrameNum < videoprops->NumFrames; CurFrameNum++) {
			CurFrameData = FFMS_GetFrameInfo(FrameData, CurFrameNum);
			if (CurFrameData == NULL) {
				continue;
			}

			int Timestamp = ((CurFrameData->PTS * TimeBase->Num) / TimeBase->Den);
			// keyframe?
			if (CurFrameData->KeyFrame){ KeyFrames.Add(Timestamp); }
			Timecodes.push_back(Timestamp);

		}
		if (rend && !rend->videoControl->GetKeyFramesFileName().empty()){
			OpenKeyframes(rend->videoControl->GetKeyFramesFileName());
			rend->videoControl->SetKeyFramesFileName(L"");
		}
	}
audio:

	if (audiotrack != -1){
		audiosource = FFMS_CreateAudioSource(fname.utf8_str(), audiotrack, index, FFMS_DELAY_FIRST_VIDEO_TRACK, &errinfo);
		if (audiosource == NULL) {
			KaiLog(wxString::Format(_("Wystąpił błąd tworzenia źródła audio: %s"), errinfo.Buffer));
			return 0;
		}

		FFMS_ResampleOptions *resopts = FFMS_CreateResampleOptions(audiosource);
		resopts->ChannelLayout = FFMS_CH_FRONT_CENTER;
		resopts->SampleFormat = FFMS_FMT_S16;
		
		if (FFMS_SetOutputFormatA(audiosource, resopts, &errinfo)){
			KaiLog(wxString::Format(_("Wystąpił błąd konwertowania audio: %s"), errinfo.Buffer));
			return 1;
		}
		FFMS_DestroyResampleOptions(resopts);
		const FFMS_AudioProperties *audioprops = FFMS_GetAudioProperties(audiosource);

		SampleRate = audioprops->SampleRate;
		Delay = (Options.GetInt(AUDIO_DELAY) / 1000);
		NumSamples = audioprops->NumSamples;
		BytesPerSample = 2;
		Channels = 1;

		if (abs(Delay) >= (SampleRate * NumSamples * BytesPerSample)){
			KaiLog(_("Nie można ustawić opóźnienia, przekracza czas trwania audio"));
			Delay = 0;
		}
		audioLoadThread = new std::thread(AudioLoad, this, newIndex, audiotrack);
		audioLoadThread->detach();
	}
	return 1;
}



VideoFfmpeg::~VideoFfmpeg()
{
	if (thread){
		SetEvent(eventKillSelf);
		WaitForSingleObject(thread, 20000);
		CloseHandle(thread);
		CloseHandle(eventStartPlayback);
		CloseHandle(eventRefresh);
		CloseHandle(eventKillSelf);
	}

	if (audioLoadThread){
		stopLoadingAudio = true;
		WaitForSingleObject(eventAudioComplete, INFINITE);
		CloseHandle(eventAudioComplete);
	}
	KeyFrames.Clear();
	Timecodes.clear();

	if (videosource){
		FFMS_DestroyVideoSource(videosource); videosource = NULL;
	}

	if (disccache){ ClearDiskCache(); }
	else{ ClearRAMCache(); }
	if (!stopLoadingAudio && disccache && diskCacheFilename.EndsWith(L".part")){
		wxString discCacheNameWithGoodExt = diskCacheFilename;
		discCacheNameWithGoodExt.RemoveLast(5);
		_wrename(diskCacheFilename.wc_str(), discCacheNameWithGoodExt.wc_str());
	}
}



int FFMS_CC VideoFfmpeg::UpdateProgress(int64_t Current, int64_t Total, void *ICPrivate)
{
	ProgressSink *progress = (ProgressSink*)ICPrivate;
	progress->Progress(((double)Current / (double)Total) * 100);
	return progress->WasCancelled();
}

void VideoFfmpeg::AudioLoad(VideoFfmpeg *vf, bool newIndex, int audiotrack)
{
	if (vf->disccache){
		vf->diskCacheFilename = L"";
		vf->diskCacheFilename << Options.pathfull << L"\\AudioCache\\" << 
			vf->fname.AfterLast(L'\\').BeforeLast(L'.') << L"_track" << audiotrack << L".w64";
		if (!vf->DiskCache(newIndex)){ goto done; }
	}
	else{
		if (!vf->RAMCache()){ goto done; }
	}
	vf->audioNotInitialized = false;
done:
	if (vf->audiosource){ FFMS_DestroyAudioSource(vf->audiosource); vf->audiosource = NULL; }
	vf->lockGetFrame = false;
	SetEvent(vf->eventAudioComplete);
	if (vf->audioLoadThread){ delete vf->audioLoadThread; vf->audioLoadThread = NULL; }

}

void VideoFfmpeg::GetFrame(int ttime, byte *buff)
{
	byte* cpy = (byte *)fframe->Data[0];
	memcpy(&buff[0], cpy, height*width * 4);

}

void VideoFfmpeg::GetFFMSFrame()
{
	wxCriticalSectionLocker lock(blockframe);
	fframe = FFMS_GetFrame(videosource, rend->m_Frame, &errinfo);
	//memcpy(buff, fframe->Data[0], fplane);
}

void VideoFfmpeg::GetAudio(void *buf, int64_t start, int64_t count)
{

	if (count == 0 || !audiosource) return;
	if (start + count > NumSamples) {
		int64_t oldcount = count;
		count = NumSamples - start;
		if (count < 0) count = 0;

		// Fill beyond with zero

		short *temp = (short *)buf;
		for (int64_t i = count; i < oldcount; i++) {
			temp[i] = 0;
		}

	}
	wxCriticalSectionLocker lock(blockaudio);
	if (FFMS_GetAudio(audiosource, buf, start, count, &errinfo)){
		KaiLog(L"error audio" + wxString(errinfo.Buffer));
	}

}

void VideoFfmpeg::GetBuffer(void *buf, int64_t start, int64_t count, double volume)
{
	if (audioNotInitialized){ return; }

	if (start + count > NumSamples) {
		int64_t oldcount = count;
		count = NumSamples - start;
		if (count < 0) count = 0;


		short *temp = (short *)buf;
		for (int i = count; i < oldcount; i++) {
			temp[i] = 0;
		}
	}

	if (count) {
		if (disccache){
			if (fp){
				wxCriticalSectionLocker lock(blockaudio);
				_int64 pos = start* BytesPerSample;
				_fseeki64(fp, pos, SEEK_SET);
				fread(buf, 1, count* BytesPerSample, fp);
			}
		}
		else{
			if (!Cache){ return; }
			char *tmpbuf = (char *)buf;
			int i = (start* BytesPerSample) >> 22;
			int blsize = (1 << 22);
			int offset = (start* BytesPerSample) & (blsize - 1);
			int64_t remaining = count* BytesPerSample;
			int readsize = remaining;

			while (remaining){
				readsize = MIN(remaining, blsize - offset);

				memcpy(tmpbuf, (char *)(Cache[i++] + offset), readsize);
				tmpbuf += readsize;
				offset = 0;
				remaining -= readsize;
			}
		}
		if (volume == 1.0) return;


		// Read raw samples
		short *buffer = (short*)buf;
		int value;

		// Modify
		for (int64_t i = 0; i < count; i++) {
			value = (int)(buffer[i] * volume + 0.5);
			if (value < -0x8000) value = -0x8000;
			if (value > 0x7FFF) value = 0x7FFF;
			buffer[i] = value;
		}

	}
}


void VideoFfmpeg::GetWaveForm(int *min, int *peak, int64_t start, int w, int h, int samples, float scale)
{
	if (audioNotInitialized){ return; }
	//wxCriticalSectionLocker lock(blockaudio);
	int n = w * samples;
	for (int i = 0; i < w; i++) {
		peak[i] = 0;
		min[i] = h;
	}

	// Prepare waveform
	int cur;
	int curvalue;

	// Prepare buffers
	int needLen = n * BytesPerSample;

	void *raw;
	raw = new char[needLen];


	short *raw_short = (short*)raw;
	GetBuffer(raw, start, n);
	int half_h = h / 2;
	int half_amplitude = int(half_h * scale);
	// Calculate waveform
	for (int i = 0; i < n; i++) {
		cur = i / samples;
		curvalue = half_h - (int(raw_short[i])*half_amplitude) / 0x8000;
		if (curvalue > h) curvalue = h;
		if (curvalue < 0) curvalue = 0;
		if (curvalue < min[cur]) min[cur] = curvalue;
		if (curvalue > peak[cur]) peak[cur] = curvalue;
	}

	delete[] raw;

}

int VideoFfmpeg::GetSampleRate()
{
	return SampleRate;
}

int VideoFfmpeg::GetBytesPerSample()
{
	return BytesPerSample;
}

int VideoFfmpeg::GetChannels()
{
	return 1;
}

int64_t VideoFfmpeg::GetNumSamples()
{
	return NumSamples;
}

bool VideoFfmpeg::RAMCache()
{
	//progress->Title(_("Zapisywanie do pamięci RAM"));
	audioProgress = 0;
	int64_t end = NumSamples * BytesPerSample;

	int blsize = (1 << 22);
	blnum = ((float)end / (float)blsize) + 1;
	Cache = NULL;
	Cache = new char*[blnum];
	if (Cache == NULL){ KaiMessageBox(_("Za mało pamięci RAM")); return false; }

	int64_t pos = (Delay < 0) ? -(SampleRate * Delay * BytesPerSample) : 0;
	int halfsize = (blsize / BytesPerSample);


	for (int i = 0; i < blnum; i++)
	{
		if (i >= blnum - 1){ blsize = end - pos; halfsize = (blsize / BytesPerSample); }
		Cache[i] = new char[blsize];
		if (Delay > 0 && i == 0){
			int delaysize = SampleRate*Delay*BytesPerSample;
			if (delaysize % 2 == 1){ delaysize++; }
			int halfdiff = halfsize - (delaysize / BytesPerSample);
			memset(Cache[i], 0, delaysize);
			GetAudio(&Cache[i][delaysize], 0, halfdiff);
			pos += halfdiff;
		}
		else{
			GetAudio(Cache[i], pos, halfsize);
			pos += halfsize;
		}
		audioProgress = ((float)i / (float)(blnum - 1));
		if (stopLoadingAudio) {
			blnum = i + 1;
			break;
		}
	}
	if (Delay < 0){ NumSamples += (SampleRate * Delay * BytesPerSample); }
	audioProgress = 1.f;
	return true;
}



void VideoFfmpeg::ClearRAMCache()
{
	if (!Cache){ return; }
	for (int i = 0; i < blnum; i++)
	{
		delete[] Cache[i];
	}
	delete[] Cache;
	Cache = 0;
	blnum = 0;
}

int VideoFfmpeg::TimefromFrame(int nframe)
{
	if (nframe < 0){ nframe = 0; }
	if (nframe >= NumFrames){ nframe = NumFrames - 1; }
	return Timecodes[nframe];
}

int VideoFfmpeg::FramefromTime(int time)
{
	if (time <= 0){ return 0; }
	int start = lastframe;
	if (lasttime > time)
	{
		start = 0;
	}
	int wframe = NumFrames - 1;
	for (int i = start; i < NumFrames - 1; i++)
	{
		if (Timecodes[i] >= time && time < Timecodes[i + 1])
		{
			wframe = i;
			break;
		}
	}
	//if(lastframe==wframe){return-1;}
	lastframe = wframe;
	lasttime = time;
	return lastframe;
}

bool VideoFfmpeg::DiskCache(bool newIndex)
{
	audioProgress = 0;

	bool good = true;
	wxFileName discCacheFile;
	discCacheFile.Assign(diskCacheFilename);
	if (!discCacheFile.DirExists()){ wxMkdir(diskCacheFilename.BeforeLast(L'\\')); }
	bool fileExists = discCacheFile.FileExists();
	if (!newIndex && fileExists){
		fp = _wfopen(diskCacheFilename.wc_str(), L"rb");
		if (fp)
			return true;
		else
			return false;
	}
	else{
		if (fileExists){
			_wremove(diskCacheFilename.wc_str());
		}
		diskCacheFilename << L".part";
		fp = _wfopen(diskCacheFilename.wc_str(), L"w+b");
		if (!fp)
			return false;
	}
	int block = 332768;
	if (Delay > 0){

		int size = (SampleRate * Delay * BytesPerSample);
		if (size % 2 == 1){ size++; }
		char *silence = new char[size];
		memset(silence, 0, size);
		fwrite(silence, 1, size, fp);
		delete[] silence;
	}
	try {
		char *data = new char[block * BytesPerSample];
		int all = (NumSamples / block) + 1;
		//int64_t pos=0;
		int64_t pos = (Delay < 0) ? -(SampleRate * Delay * BytesPerSample) : 0;
		for (int i = 0; i < all; i++) {
			if (block + pos > NumSamples) block = NumSamples - pos;
			GetAudio(data, pos, block);
			fwrite(data, 1, block * BytesPerSample, fp);
			pos += block;
			audioProgress = ((float)pos / (float)(NumSamples));
			if (stopLoadingAudio) break;
		}
		delete[] data;

		rewind(fp);
		if (Delay < 0){ NumSamples += (SampleRate * Delay * BytesPerSample); }
	}
	catch (...) {
		good = false;
	}

	if (!good){ ClearDiskCache(); }
	else{ audioProgress = 1.f; }
	return good;
}

void VideoFfmpeg::ClearDiskCache()
{
	if (fp){ fclose(fp); fp = NULL; }
}

int VideoFfmpeg::GetMSfromFrame(int frame)
{
	if (frame >= NumFrames){ return frame * (1000.f / fps); }
	else if (frame < 0){ return 0; }
	return Timecodes[frame];
}

int VideoFfmpeg::GetFramefromMS(int MS, int seekfrom, bool safe)
{
	if (MS <= 0) return 0;
	int result = (safe) ? NumFrames - 1 : NumFrames;
	for (int i = seekfrom; i < NumFrames; i++)
	{
		if (Timecodes[i] >= MS)
		{
			result = i;
			break;
		}
	}
	return result;
}

void VideoFfmpeg::DeleteOldAudioCache()
{
	wxString path = Options.pathfull + L"\\AudioCache";
	size_t maxAudio = Options.GetInt(AUDIO_CACHE_FILES_LIMIT);
	if (maxAudio < 1)
		return;

	wxDir kat(path);
	wxArrayString audioCaches;
	if (kat.IsOpened()){
		kat.GetAllFiles(path, &audioCaches, L"", wxDIR_FILES);
	}
	if (audioCaches.size() <= maxAudio){ return; }
	FILETIME ft;
	SYSTEMTIME st;
	std::map<unsigned __int64, int> dates;
	unsigned __int64 datetime;
	for (size_t i = 0; i < audioCaches.size(); i++){
		HANDLE ffile = CreateFile(audioCaches[i].wc_str(), GENERIC_READ, FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
		if (ffile != INVALID_HANDLE_VALUE){
			GetFileTime(ffile, 0, &ft, 0);
			CloseHandle(ffile);
			FileTimeToSystemTime(&ft, &st);
			if (st.wYear > 3000){ st.wYear = 3000; }
			datetime = (st.wYear * 980294400000) + (st.wMonth * 2678400000) + (st.wDay * 86400000) + (st.wHour * 3600000) + (st.wMinute * 60000) + (st.wSecond * 1000) + st.wMilliseconds;
			dates[datetime] = i;
		}
		
	}
	int count = 0;
	int diff = audioCaches.size() - maxAudio;
	for (auto cur = dates.begin(); cur != dates.end(); cur++){
		if (count >= diff){ break; }
		int isgood = _wremove(audioCaches[cur->second].wchar_str());
		count++;
	}

}

void VideoFfmpeg::Render(bool wait){
	byte *buff = (byte*)rend->m_FrameBuffer;
	if (rend->m_Frame != lastframe){
		GetFFMSFrame();
		lastframe = rend->m_Frame;
	}
	if (!fframe){
		return;
	}
	memcpy(&buff[0], fframe->Data[0], fplane);
	rend->DrawTexture(fframe->Data[0], true);
	//it's not event thats why must be safe from start
	rend->Render(false);
}

void VideoFfmpeg::GetFrameBuffer(byte **buffer)
{
	if (rend->m_Frame != lastframe) {
		GetFFMSFrame();
		lastframe = rend->m_Frame;
	}
	if (!fframe) {
		return;
	}
	memcpy(*buffer, fframe->Data[0], fplane);
}

wxString VideoFfmpeg::ColorCatrixDescription(int cs, int cr) {
	// Assuming TV for unspecified
	wxString str = cr == FFMS_CR_JPEG ? L"PC" : L"TV";

	switch (cs) {
	case FFMS_CS_RGB:
		return _("Brak");
	case FFMS_CS_BT709:
		return str + L".709";
	case FFMS_CS_FCC:
		return str + L".FCC";
	case FFMS_CS_BT470BG:
	case FFMS_CS_SMPTE170M:
		return str + L".601";
	case FFMS_CS_SMPTE240M:
		return str + L".240M";
	default:
		return _("Brak");
	}
}

void VideoFfmpeg::SetColorSpace(const wxString& matrix)
{
	wxCriticalSectionLocker lock(blockframe);
	if (matrix == ColorSpace) return;
	//lockGetFrame = true;
	if (matrix == RealColorSpace || (matrix != L"TV.601" && matrix != L"TV.709"))
		FFMS_SetInputFormatV(videosource, CS, CR, FFMS_GetPixFmt(""), nullptr);
	else if (matrix == L"TV.601")
		FFMS_SetInputFormatV(videosource, FFMS_CS_BT470BG, CR, FFMS_GetPixFmt(""), nullptr);
	else{
		//lockGetFrame = false;
		return;
	}
	//lockGetFrame = false;
	ColorSpace = matrix;

}

void VideoFfmpeg::OpenKeyframes(const wxString & filename)
{
	wxArrayInt keyframes;
	KeyframeLoader kfl(filename, &keyframes, this);
	if (keyframes.size()){
		KeyFrames = keyframes;
		TabPanel *tab = (rend) ? (TabPanel*)rend->videoControl->GetParent() : Notebook::GetTab();
		if (tab->Edit->ABox){
			tab->Edit->ABox->SetKeyframes(keyframes);
		}
	}
	else{
		KaiMessageBox(_("Nieprawidłowy format klatek kluczowych"), _("Błąd"), 4L, Notebook::GetTab());
	}
}

void VideoFfmpeg::SetPosition(int _time, bool starttime)
{
	changedTime = _time;
	isStartTime = starttime;
	SetEvent(eventSetPosition);
}
//
//void VideoFfmpeg::ChangePositionByFrame(int step)
//{
//	numframe = MID(0, numframe + step, NumFrames - 1);
//	time = Timecodes[numframe];
//	/*if (rend){
//		rend->time = time;
//		rend->lastframe = numframe;
//	}*/
//	playingLastTime = timeGetTime() - time;
//}

void VideoFfmpeg::Play(){ 
	/*playingLastTime = timeGetTime() - time;
	time = Timecodes[numframe];
	if (rend)
		rend->time = time;*/

	SetEvent(eventStartPlayback); 
};