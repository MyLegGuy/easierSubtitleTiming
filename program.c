// do not steel
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <float.h>

#include <sndfile.h>

#include <SDL2/SDL.h>

#define	BLOCK_SIZE 4096

#define READBLOCKSIZE 8192

//https://gist.github.com/armornick/3447121
//float buf [BLOCK_SIZE];
float** pcmData=NULL;
long pcmPlayPos=0;

void my_audio_callback(void *userdata, Uint8 *stream, int len) {
	SF_INFO* _passedInfo = userdata;
	//while ((readcount = sf_readf_float (infile, buf, frames)) > 0)
	//{
	//	for (k = 0; k < readcount; k++) // read wave
	//	{	for (m = 0; m < _audioInfo.channels; m++){
	//			//if (full_precision)
	//			//	
	//			//else
	//			//	fprintf (outfile, " % 12.10f", buf [k * channels + m]);
	//			//fprintf (outfile, "\n");
	//		}
	//	}
	//}
	
		// HEY, LIUSTEN
	//readcount = sf_readf_float (userdata, buf, frames);
	int i;
	for (i=0;i<(len/sizeof(float))/2;++i){
		int j;
		for (j=0;j<_passedInfo->channels;++j){
			SDL_memcpy (&(stream[(i*_passedInfo->channels+j)*sizeof(float)]), &(pcmData[j][pcmPlayPos]), sizeof(float));
		}
		//stream[i*2*sizeof(float)] = pcmData[0][pcmPlayPos];
		//stream[(i*2+1)*sizeof(float)] = pcmData[1][pcmPlayPos];
		++pcmPlayPos;
	}
	//SDL_memcpy (stream, pcmData[pcmPlayPos], BLOCK_SIZE*sizeof(float));
	//pcmPlayPos+=BLOCK_SIZE;
	

	//SDL_MixAudio(stream, buf, readcount*sizeof(float), SDL_MIX_MAXVOLUME);

	//if (audio_len ==0)
	//	return;
	//
	//len = ( len > audio_len ? audio_len : len );
	////SDL_memcpy (stream, audio_pos, len); 					// simply copy from one buffer into the other
	//SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);// mix from one buffer into another
	//
	//audio_pos += len;
	//audio_len -= len;
}

int main (int argc, char * argv []){
	// init
	char* infilename = "./audiodump2.wav";
	SNDFILE* infile = NULL;
	SF_INFO	_audioInfo;
	memset (&_audioInfo, 0, sizeof (_audioInfo));
	if ((infile = sf_open (infilename, SFM_READ, &_audioInfo)) == NULL){
		printf ("Not able to open input file %s.\n", infilename);
		printf("%s\n",sf_strerror (NULL));
		return 1;
	}
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
			
				pcmData[j][_wroteSamples] = _readBuf[k*_audioInfo.channels+j];
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
	wav_spec.samples = BLOCK_SIZE;
	wav_spec.callback = my_audio_callback;
	wav_spec.userdata = &_audioInfo;
	if ( SDL_OpenAudio(&wav_spec, NULL) < 0 ){
		printf("Couldn't open audio: %s\n", SDL_GetError());
		return 1;
	}
	SDL_PauseAudio(0);
	SDL_Delay(5000);
	SDL_CloseAudio();

	//float buf [BLOCK_SIZE];
	//sf_count_t frames;
	//int k, m, readcount;
	//frames = BLOCK_SIZE / _audioInfo.channels;
	//while ((readcount = sf_readf_float (infile, buf, frames)) > 0)
	//{	for (k = 0; k < readcount; k++) // read wave
	//	{	for (m = 0; m < _audioInfo.channels; m++){
	//			//if (full_precision)
	//			//	
	//			//else
	//			//	fprintf (outfile, " % 12.10f", buf [k * channels + m]);
	//			//fprintf (outfile, "\n");
	//		}
	//	}
	//}

	
	return 0;
}