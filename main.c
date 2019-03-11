// do not steel
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <float.h>
#include <pthread.h>

#include <sndfile.h>
#include <SDL2/SDL.h>

#include "goodLinkedList.h"

#define PLAY_SAMPLES  4096

#define READBLOCKSIZE 8192

struct sentence{
	long startSample;
	long endSample;
}

float** pcmData=NULL;
long pcmPlayPos=0;
long totalSamples;
int sampleRate;

uint32_t audioTimeReference;
uint32_t audioTimeOther;

pthread_mutex_t audioPosLock;

nList* timings;

void unpauseMusic(){
    	audioTimeReference=SDL_GetTicks();
	SDL_PauseAudio(0);
}
void pauseMusic(){
	SDL_PauseAudio(1);
	audioTimeOther+=SDL_GetTicks()-audioTimeReference;
}
void my_audio_callback(void *userdata, Uint8 *stream, int len) {
	pthread_mutex_lock(&audioPosLock);

	if (pcmPlayPos>=totalSamples){
		pauseMusic();
		return;
	}
    
	SF_INFO* _passedInfo = userdata;
	int _possibleWriteSamples = (len/sizeof(float))/2;
	int _shouldWriteSamples;
	if (pcmPlayPos+_possibleWriteSamples>totalSamples){
		_shouldWriteSamples=totalSamples-pcmPlayPos;
	}else{
    		_shouldWriteSamples = _possibleWriteSamples;
	}
	int i;
	for (i=0;i<_shouldWriteSamples;++i){
		int j;
		for (j=0;j<_passedInfo->channels;++j){
			SDL_memcpy (&(stream[(i*_passedInfo->channels+j)*sizeof(float)]), &(pcmData[j][pcmPlayPos]), sizeof(float));
		}
		//stream[i*2*sizeof(float)] = pcmData[0][pcmPlayPos];
		//stream[(i*2+1)*sizeof(float)] = pcmData[1][pcmPlayPos];
		++pcmPlayPos;
	}
	if (_shouldWriteSamples!=_possibleWriteSamples){
		memset(&(stream[_shouldWriteSamples]),0,_possibleWriteSamples-_shouldWriteSamples);
	}
	//SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);// mix from one buffer into another

	pthread_mutex_unlock(&audioPosLock);
}

void seekAudioSamples(int _numSamples){
	pthread_mutex_lock(&audioPosLock);
	pcmPlayPos+=_numSamples;
	if (pcmPlayPos<0){
		pcmPlayPos=0;
	}else if (pcmPlayPos+PLAY_SAMPLES>totalSamples){
		pcmPlayPos=totalSamples-PLAY_SAMPLES;
	}
	audioTimeOther=(pcmPlayPos/(double)sampleRate)*1000;
	audioTimeReference=SDL_GetTicks();
	pthread_mutex_unlock(&audioPosLock);
}
void seekAudioMilli(int _numMilliseconds){
    seekAudioSamples((_numMilliseconds/(double)1000)*sampleRate);
}
long getCurrentSample(){
    return ((audioTimeOther+(SDL_GetTicks()-audioTimeReference))/(double)1000)*sampleRate;
}

nList* findSentences(){
	nList* _ret;
	_ret = newnList();
}

int main (int argc, char * argv []){
	if (pthread_mutex_init(&audioPosLock,NULL)!=0){
		printf("mutex init failed");
		return 1;
	}
    
	// init audio
	char* infilename = "./audiodump2.wav";
	SNDFILE* infile = NULL;
	SF_INFO	_audioInfo;
	memset (&_audioInfo, 0, sizeof (_audioInfo));
	if ((infile = sf_open (infilename, SFM_READ, &_audioInfo)) == NULL){
		printf ("Not able to open input file %s.\n", infilename);
		printf("%s\n",sf_strerror (NULL));
		return 1;
	}
	totalSamples=_audioInfo.frames;
	sampleRate=_audioInfo.samplerate;
	// info
	printf("# Converted from file %s.\n", infilename);
	printf("# Channels %d, Sample rate %d\n", _audioInfo.channels, _audioInfo.samplerate);


	// Read entire file as pcm info big boy buffer
	pcmData = malloc(sizeof(float*)*_audioInfo.channels);
	int i;
	for (i=0;i<_audioInfo.channels;++i){
		pcmData[i] = malloc(sizeof(float)*_audioInfo.frames);
	}
	// 
	int _singleReadSamples = READBLOCKSIZE/_audioInfo.channels;
	float _readBuf[_singleReadSamples*2];
	int _wroteSamples=0;
	int _lastReadCount;
	while ((_lastReadCount = sf_readf_float (infile, _readBuf, _singleReadSamples)) > 0){
		for (i = 0; i < _lastReadCount; i++){
			int j;
			for (j = 0; j < _audioInfo.channels; j++){
			
				pcmData[j][_wroteSamples] = _readBuf[i*_audioInfo.channels+j];
				//if (full_precision)
				//	
				//else
				//	fprintf (outfile, " % 12.10f", buf [k * channels + m]);
				//fprintf (outfile, "\n");
			}
			++_wroteSamples;
		}
	}
	sf_close (infile);


	// init
	if (SDL_Init(SDL_INIT_AUDIO) < 0){
		printf("fail open audio");
		return 1;
	}
	SDL_AudioSpec wav_spec; // the specs of our piece of music
	wav_spec.freq = _audioInfo.samplerate;
	wav_spec.format = AUDIO_F32LSB;
	wav_spec.channels = _audioInfo.channels;
	wav_spec.samples = PLAY_SAMPLES;
	wav_spec.callback = my_audio_callback;
	wav_spec.userdata = &_audioInfo;
	int _gottenAudioId = SDL_OpenAudio(&wav_spec, NULL);
	if ( _gottenAudioId < 0 ){
		printf("Couldn't open audio: %s\n", SDL_GetError());
		return 1;
	}

	/*

	unpauseMusic();

	/////////////////////////////


	long _myRef = SDL_GetTicks();

	//SDL_Delay(3000);
	//seekAudioSamples(-44100);
	//SDL_Delay(1000);
	//SDL_CloseAudio();
	printf("\n");
	for (int i=0;i<20000;++i){

    		printf("\033[A\r",27);
    		int _sampleIndex=getCurrentSample();
    		int j;
    		for (j=0;j<20*fabs(pcmData[0][_sampleIndex]);++j){
			printf("=");
    		}
    		for (;j<20;++j){
			printf(" ");
    		}
		printf("\n");
    		//printf("%f\n",pcmData[0][getCurrentSample()]);
		SDL_Delay(1);
	}
	*/

	// whatever the opposite of init is
	pthread_mutex_destroy(&audioPosLock);
	
	return 0;
}
