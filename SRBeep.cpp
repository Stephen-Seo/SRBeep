/***********************************
A Docile Sloth adocilesloth@gmail.com
************************************/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <thread>
#include <atomic>
#include <sstream>
#include <mutex>
#include <cstring>
#include <array>

extern "C"
{
	#include "libavcodec/avcodec.h"
	#include "libavformat/avformat.h"
	#include "libswresample/swresample.h"
	#include "SDL.h"
	#include "SDL_thread.h"
};

#include "RingBuffer.h"

std::mutex audioMutex;
std::thread st_stt_Thread, st_sto_Thread, rc_stt_Thread, rc_sto_Thread, bf_stt_Thread, bf_sto_Thread, bf_sav_Thread, ps_stt_Thread, ps_sto_Thread;

#define	MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

template <unsigned int CAPACITY>
struct AudioData {
	std::array<unsigned char, CAPACITY> audio_chunk;
	unsigned int audio_capacity() {
		return CAPACITY;
	}
	unsigned int audio_len;
	unsigned long long audio_offset;
};

using RingBufferT = RingBuffer<AudioData<1024>, 32, true>;

OBS_DECLARE_MODULE()

#ifdef _WIN32
	#include <windows.h>

	void psleep(unsigned milliseconds)
	{
		Sleep(milliseconds);
	}
#else
	#include <unistd.h>

	void psleep(unsigned milliseconds)
	{
		usleep(milliseconds * 1000); // takes microseconds
	}
#endif

void obs_module_unload(void)
{
	if(st_stt_Thread.joinable())
	{
		st_stt_Thread.join();
	}
	if(st_sto_Thread.joinable())
	{
		st_sto_Thread.join();
	}
	if(rc_stt_Thread.joinable())
	{
		rc_stt_Thread.join();
	}
	if(rc_sto_Thread.joinable())
	{
		rc_sto_Thread.join();
	}
	if(bf_stt_Thread.joinable())
	{
		bf_stt_Thread.join();
	}
	if(bf_sto_Thread.joinable())
	{
		bf_sto_Thread.join();
	}
	if(bf_sav_Thread.joinable())
	{
		bf_sav_Thread.join();
	}
	if(ps_stt_Thread.joinable())
	{
		ps_stt_Thread.join();
	}
	if(ps_sto_Thread.joinable())
	{
		ps_sto_Thread.join();
	}
	return;
}

const char *obs_module_author(void)
{
	return "A Docile Sloth";
}

const char *obs_module_name(void)
{
	return "Stream/Recording Start/Stop Beeps";
}

const char  *obs_module_description(void)
{
	return "Adds audio sound when streaming/recording/buffer starts/stops or when recording is paused/unpaused.";
}

void fill_audio(void *udata, Uint8 *stream, int len)
{
//	/*****************************************************************
//	From simplest_ffmpeg_audio_player by leixiaohua1020
//	Download at https://sourceforge.net/projects/simplestffmpegplayer/
//	*****************************************************************/
//	//SDL 2.0
//	SDL_memset(stream, 0, len);
//	if(audio_len == 0)		/*  Only  play  if  we  have  data  left  */
//		return;
//	len = ((unsigned int)len > audio_len ? audio_len : len);	/*  Mix  as  much  data  as  possible  */
//
//	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
//	audio_pos += len;
//	audio_len -= len;

	SDL_memset(stream, 0, len);

	RingBufferT *buffers = (RingBufferT*)udata;
	int stream_offset = 0;
	while (len > 0) {
		bool emptied = false;
		{
			auto locked_top = buffers->locked_top();
			AudioData<1024> *adata = std::get<0>(locked_top);
			if (adata == nullptr) {
				break;
			}
			int contained_size = adata->audio_len - adata->audio_offset;
			int mix_size = len > contained_size ? contained_size : len;
			SDL_MixAudio(stream + stream_offset, adata->audio_chunk.data() + adata->audio_offset, mix_size, SDL_MIX_MAXVOLUME);
			stream_offset += mix_size;
			adata->audio_offset += mix_size;
			len -= mix_size;
			if (adata->audio_offset == adata->audio_len) {
				emptied = true;
			}
		}

		if (emptied) {
			buffers->pop(nullptr, nullptr);
		}

		if (buffers->is_empty()) {
			break;
		}
	}
}

void play_clip(const char *filepath)
{
	//fix problems with audio_len being assigned a value
	//static  Uint32  fixer;
	//audio_len = fixer;
	//carry on
	/*****************************************************************
	Adapted from simplest_ffmpeg_audio_player by leixiaohua1020
	Download at https://sourceforge.net/projects/simplestffmpegplayer/
	*****************************************************************/
	AVFormatContext *stream_start = NULL;
	const AVCodec *cdc = nullptr;
	int audioStreamIndex = -1;

	if(avformat_open_input(&stream_start, filepath, NULL, NULL) != 0)
	{
		blog(LOG_WARNING, "SRBeep: play_clip: Failed to open file \"%s\"", filepath);
		return;
	}

	if(avformat_find_stream_info(stream_start, NULL) < 0)
	{
		avformat_close_input(&stream_start);
		blog(LOG_WARNING, "SRBeep: play_clip: Failed to find audio file's stream info");
		return;
	}

	audioStreamIndex = av_find_best_stream(stream_start, AVMEDIA_TYPE_AUDIO, -1, -1, &cdc, 0);
	if(audioStreamIndex < 0)
	{
		avformat_close_input(&stream_start);
		blog(LOG_WARNING, "SRBeep: play_clip: Failed to find audio stream in audio file");
		return;
	}

	AVCodecContext *cdx = avcodec_alloc_context3(cdc);
	if (!cdx) {
		avformat_close_input(&stream_start);
		blog(LOG_WARNING, "SRBeep: play_clip: Failed to avcodec_alloc_context3(cdc)");
	}

	avcodec_parameters_to_context(cdx, stream_start->streams[audioStreamIndex]->codecpar);
	if (avcodec_open2(cdx, cdc, nullptr) != 0) {
		avcodec_free_context(&cdx);
		avformat_close_input(&stream_start);
		blog(LOG_WARNING, "SRBeep: play_clip: Failed to avcodec_open2(...)");
		return;
	}

	//audio packet
	AVPacket *packet = av_packet_alloc();

	//Audio Parameters
	AVChannelLayout out_channel_layout;
	av_channel_layout_default(&out_channel_layout, 2);
	int out_nb_samples = cdx->frame_size;
	AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
	int out_sample_rate = 44100;
	int out_buffer_size = 0;
	const int out_buffer_capacity = MAX_AUDIO_FRAME_SIZE * 2;
	uint8_t *out_buffer = (uint8_t*)av_malloc(out_buffer_capacity);
	AVFrame *frame = av_frame_alloc();

	audioMutex.lock();

	//init SDL
	if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		av_packet_free(&packet);
		av_freep(&out_buffer);
		av_frame_free(&frame);
		avformat_close_input(&stream_start);
		avcodec_free_context(&cdx);
		blog(LOG_WARNING, "SRBeep: play_clip: SDL init failed");
		audioMutex.unlock();
		return;
	}

	RingBufferT rb_data;

	SDL_AudioSpec wanted_spec;
	wanted_spec.freq = cdx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = cdx->ch_layout.nb_channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = out_nb_samples;
	wanted_spec.callback = fill_audio;
	wanted_spec.userdata = &rb_data;

	if(SDL_OpenAudio(&wanted_spec, NULL) < 0)
	{
		SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
		SDL_Quit();
		av_packet_free(&packet);
		av_freep(&out_buffer);
		av_frame_free(&frame);
		avformat_close_input(&stream_start);
		avcodec_free_context(&cdx);
		blog(LOG_WARNING, "SRBeep: play_clip: SDL_OpenAudio failed");
		audioMutex.unlock();
		return;
	}

	int ret;

	//Swr
	struct SwrContext *au_convert_ctx = nullptr;
	ret = swr_alloc_set_opts2(
						&au_convert_ctx,
						&out_channel_layout,
						out_sample_fmt,
						out_sample_rate,
						&cdx->ch_layout,
						cdx->sample_fmt,
						cdx->sample_rate,
						0,
						nullptr);
	swr_init(au_convert_ctx);

	if (ret != 0) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
		SDL_Quit();
		av_packet_free(&packet);
		av_freep(&out_buffer);
		av_frame_free(&frame);
		avformat_close_input(&stream_start);
		avcodec_free_context(&cdx);
		if (au_convert_ctx) {
			swr_free(&au_convert_ctx);
		}
		blog(LOG_WARNING, "SRBeep: play_clip: swr_alloc_set_opts2 failed");
		audioMutex.unlock();
		return;
	}

	while(av_read_frame(stream_start, packet) >= 0)
	{
		if(packet->stream_index == audioStreamIndex)
		{
			ret = avcodec_send_packet(cdx, packet);
			if(ret == AVERROR(EAGAIN))
				ret = 0;
			if(ret == 0)
			{
				ret = avcodec_receive_frame(cdx, frame);
				if (ret == AVERROR(EAGAIN)) {
					continue;
				}
			}

			if(ret < 0)
			{
				SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
				SDL_Quit();
				av_packet_free(&packet);
				av_freep(&out_buffer);
				av_frame_free(&frame);
				avformat_close_input(&stream_start);
				avcodec_free_context(&cdx);
				blog(LOG_WARNING, "SRBeep: play_clip: Decoding audio frame error");
				audioMutex.unlock();
				return;
			}
			else
			{
				ret = swr_get_out_samples(au_convert_ctx, frame->nb_samples * 2);
				if (ret > out_buffer_capacity) {
					blog(
							LOG_WARNING,
							"SRBeep: play_clip: out samples greater than buffer (buffer size is %d, out size is %d)",
							out_buffer_capacity,
							ret);
				}
				out_buffer_size = swr_convert(
							au_convert_ctx,
							&out_buffer,
							out_buffer_capacity / 2,
							(const uint8_t**)frame->data,
							frame->nb_samples
						);
				if (out_buffer_size > 0) {
					out_buffer_size *= 2 + 2; // two channels, two bytes per sample (16-bit)
				} else {
					blog(LOG_WARNING, "SRBeep: play_clip: got negative value from swr_convert(...)");
				}
			}

			int out_buffer_offset = 0;
			while (out_buffer_size > 0) {
				AudioData<1024> data;
				data.audio_offset = 0;
				data.audio_len = (unsigned int)out_buffer_size > data.audio_capacity() ? data.audio_capacity() : out_buffer_size;
				std::memcpy(data.audio_chunk.data(), out_buffer + out_buffer_offset, data.audio_len);
				out_buffer_offset += data.audio_len;
				out_buffer_size -= data.audio_len;
				while(!rb_data.push(data)) {
					SDL_PauseAudio(0);
					SDL_Delay(30);
				}
			}

			//Play
			if (rb_data.get_remaining_space() < rb_data.get_capacity() / 2) {
				SDL_PauseAudio(0);
			}
		}
	}

	while(!rb_data.is_empty()) {
		SDL_PauseAudio(0);
		SDL_Delay(400);
	}

	av_packet_free(&packet);
	swr_free(&au_convert_ctx);
	//Close SDL
	SDL_CloseAudio();
	SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
	SDL_Quit();
	//clean up
	av_freep(&out_buffer);
	av_frame_free(&frame);
	avformat_close_input(&stream_start);
	avcodec_free_context(&cdx);
	audioMutex.unlock();
	return;
}

std::string clean_path(std::string audio_path)
{
	std::string cleaned_path;
	//If relative path then the first 2 chars should be ".."
	if(audio_path.find("..") != std::string::npos)
	{
		size_t pos = audio_path.find("..");
		cleaned_path = audio_path.substr(pos);
	}
	//If absolute path, Windows will start with a capital, Linux/Mac will start with "/"
	else
	{
		#ifdef _WIN32
			while(islower(audio_path[0]) && audio_path.length() > 0)
			{
				audio_path = audio_path.substr(1);
			}
		#else
			while(audio_path.substr(0, 1) != "/" && audio_path.length() > 0)
			{
				audio_path = audio_path.substr(1);
			}
		#endif
		cleaned_path = audio_path;
	}
	return cleaned_path;
}

void play_sound(std::string file_name)
{
	const char *obs_data_path = obs_get_module_data_path(obs_current_module());
	std::stringstream audio_path;
	std::string true_path;

	audio_path << obs_data_path;
	audio_path << file_name;
	true_path = clean_path(audio_path.str());
	play_clip(true_path.c_str());
	audio_path.str("");

	return;
}

void obsstudio_srbeep_frontend_event_callback(enum obs_frontend_event event, void *private_data)
{
	if(event == OBS_FRONTEND_EVENT_STREAMING_STARTED)
	{
		if(st_stt_Thread.joinable())
		{
			st_stt_Thread.join();
		}
		st_stt_Thread = std::thread(play_sound, "/stream_start_sound.mp3");
	}
	else if(event == OBS_FRONTEND_EVENT_RECORDING_STARTED)
	{
		if (rc_stt_Thread.joinable())
		{
			rc_stt_Thread.join();
		}
		rc_stt_Thread = std::thread(play_sound, "/record_start_sound.mp3");
	}
	else if(event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED)
	{
		if(bf_stt_Thread.joinable())
		{
			bf_stt_Thread.join();
		}
		bf_stt_Thread = std::thread(play_sound, "/buffer_start_sound.mp3");
	}
	else if(event == OBS_FRONTEND_EVENT_RECORDING_PAUSED)
	{
		if(ps_stt_Thread.joinable())
		{
			ps_stt_Thread.join();
		}
		ps_stt_Thread = std::thread(play_sound, "/pause_start_sound.mp3");
	}
	else if(event == OBS_FRONTEND_EVENT_STREAMING_STOPPED)
	{
		if(st_sto_Thread.joinable())
		{
			st_sto_Thread.join();
		}
		st_sto_Thread = std::thread(play_sound, "/stream_stop_sound.mp3");
	}
	else if(event == OBS_FRONTEND_EVENT_RECORDING_STOPPED)
	{
		if(rc_sto_Thread.joinable())
		{
			rc_sto_Thread.join();
		}
		rc_sto_Thread = std::thread(play_sound, "/record_stop_sound.mp3");
	}
	else if(event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED)
	{
		if(bf_sto_Thread.joinable())
		{
			bf_sto_Thread.join();
		}
		bf_sto_Thread = std::thread(play_sound, "/buffer_stop_sound.mp3");
	}
	else if(event == OBS_FRONTEND_EVENT_RECORDING_UNPAUSED)
	{
		if(ps_stt_Thread.joinable())
		{
			ps_stt_Thread.join();
		}
		ps_stt_Thread = std::thread(play_sound, "/pause_stop_sound.mp3");
	}
	else if (event== OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED)
	{
		if(bf_sav_Thread.joinable()) {
			bf_sav_Thread.join();
		}
		bf_sav_Thread = std::thread(play_sound, "/buffer_save_sound.mp3");
	}
}

bool obs_module_load(void)
{
	obs_frontend_add_event_callback(obsstudio_srbeep_frontend_event_callback, 0);
	return true;
}
