// do not steel
/*
	UIdeas - 
	Q - Rewind
	W - Forward
	S - stitch - Combine this and next one
	A - stitch this and previous one


	TODO - Need undo
		Do this using a stack with two pieces of data on it. An undo function and an undo data variable to be passed to the function. Also, maybe we can make it pass the currentSample (at the time you used the command) by default.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <float.h>
#include <pthread.h>

#include <sndfile.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "goodLinkedList.h"

#define PLAY_SAMPLES  4096
#define READBLOCKSIZE 8192
// in PCM float
#define SILENCE_THRESHOLD .01
// in milliseconds
#define SENTENCE_SPACE_TIME 400
#define MIN_SENTENCE_TIME SENTENCE_SPACE_TIME
#define REDRAWTIME 40
#define TIMEPERPIXEL 25

#define MAKERGB(r,g,b,a) ((((a)&0xFF)<<24) | (((b)&0xFF)<<16) | (((g)&0xFF)<<8) | (((r)&0xFF)<<0))
#define BARHEIGHT 20
#define CASTDATA(a) ((struct sentence*)(a->data))
#define MODKEY SDLK_LSHIFT

// colors
#define BACKGROUNDCOLOR MAKERGB(0,0,0,255)
#define MODBACKGROUNDCOLOR MAKERGB(0,0,57,255)

struct sentence {
	long startSample;
	long endSample;
};
typedef void(*keyFunc)();
typedef void(*undoFunc)(long _currentSample, void* _data);

SDL_Window* mainWindow;
SDL_Renderer* mainWindowRenderer;

float** pcmData=NULL;
long pcmPlayPos=0;
long totalSamples;
int sampleRate;
int totalChannels;

uint32_t audioTimeReference;
uint32_t audioTimeOther;

pthread_mutex_t audioPosLock;

nList* timings;

int totalKeysBound=0;
SDL_Keycode* boundKeys;
char* boundKeyModStatus;
keyFunc* boundFuncs;

void setDrawColor(uint32_t _passedColor) {
	SDL_SetRenderDrawColor(mainWindowRenderer,(_passedColor) & 0xFF, (_passedColor>>8) & 0xFF, (_passedColor>>16) & 0xFF, (_passedColor>>24) & 0xFF);
}
void drawRectangle(int x, int y, int w, int h, uint32_t _color) {
	setDrawColor(_color);
	SDL_Rect tempRect;
	tempRect.x=x;
	tempRect.y=y;
	tempRect.w=w;
	tempRect.h=h;
	SDL_RenderFillRect(mainWindowRenderer,&tempRect);
}
long timeToSamples(int _numMilliseconds) {
	return ((_numMilliseconds*sampleRate)/1000);
}
void unpauseMusic() {
	audioTimeReference=SDL_GetTicks();
	SDL_PauseAudio(0);
}
void pauseMusic() {
	SDL_PauseAudio(1);
	audioTimeOther+=SDL_GetTicks()-audioTimeReference;
}
void my_audio_callback(void *userdata, Uint8 *stream, int len) {
	pthread_mutex_lock(&audioPosLock);

	if (pcmPlayPos>=totalSamples) {
		pauseMusic();
		return;
	}

	SF_INFO* _passedInfo = userdata;
	int _possibleWriteSamples = (len/sizeof(float))/2;
	int _shouldWriteSamples;
	if (pcmPlayPos+_possibleWriteSamples>totalSamples) {
		_shouldWriteSamples=totalSamples-pcmPlayPos;
	} else {
		_shouldWriteSamples = _possibleWriteSamples;
	}
	int i;
	for (i=0; i<_shouldWriteSamples; ++i) {
		int j;
		for (j=0; j<_passedInfo->channels; ++j) {
			SDL_memcpy (&(stream[(i*_passedInfo->channels+j)*sizeof(float)]), &(pcmData[j][pcmPlayPos]), sizeof(float));
		}
		//stream[i*2*sizeof(float)] = pcmData[0][pcmPlayPos];
		//stream[(i*2+1)*sizeof(float)] = pcmData[1][pcmPlayPos];
		++pcmPlayPos;
	}
	if (_shouldWriteSamples!=_possibleWriteSamples) {
		memset(&(stream[_shouldWriteSamples]),0,_possibleWriteSamples-_shouldWriteSamples);
	}
	//SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);// mix from one buffer into another

	pthread_mutex_unlock(&audioPosLock);
}
// Returns milliseconds
double samplesToTime(long _passedSamples) {
	return (_passedSamples/(double)sampleRate)*1000;
}
void seekAudioSamples(int _numSamples) {
	pthread_mutex_lock(&audioPosLock);
	pcmPlayPos+=_numSamples;
	if (pcmPlayPos<0) {
		pcmPlayPos=0;
	} else if (pcmPlayPos+PLAY_SAMPLES>totalSamples) {
		pcmPlayPos=totalSamples-PLAY_SAMPLES;
	}
	audioTimeOther=samplesToTime(pcmPlayPos);
	audioTimeReference=SDL_GetTicks();
	pthread_mutex_unlock(&audioPosLock);
}
void seekAudioMilli(int _numMilliseconds) {
	seekAudioSamples((_numMilliseconds/(double)1000)*sampleRate);
}
long getCurrentSample() {
	return ((audioTimeOther+(SDL_GetTicks()-audioTimeReference))/(double)1000)*sampleRate;
}
double getAvgPcm(long _sampleNumber) {
	double _avg=0;
	int i;
	for (i=0; i<totalChannels; ++i) {
		_avg+=fabs(pcmData[i][_sampleNumber]);
	}
	return _avg/totalChannels;
}
nList* findSentences() {
	nList* _ret=NULL;

	// Work with this, add it to the final list once it's good
	struct sentence _currentPart;

	char _sentenceActive=0;
	long _silenceStreak=0;
	long _silenceStreakStart;

	long i;
	for (i=0; i<totalSamples; ++i) {
		if (_sentenceActive) {
			if (getAvgPcm(i)<SILENCE_THRESHOLD) {
				if (_silenceStreak==0) {
					_silenceStreakStart=i;
				}
				++_silenceStreak;
				// Sentence end
				if (samplesToTime(_silenceStreak)>=SENTENCE_SPACE_TIME) {
					// Ensure that it isn't just an audio pop or something.
					// It has to be an actual sentence
					if (samplesToTime(i-_currentPart.startSample)>=MIN_SENTENCE_TIME) {
						//printf("Sentence from %d to %d: %f\n",_currentPart.startSample,i,samplesToTime(i-_currentPart.startSample));
						// Put in list
						_currentPart.endSample=_silenceStreakStart;
						void* _destData = malloc(sizeof(struct sentence));
						memcpy(_destData,&_currentPart,sizeof(struct sentence));
						addnList(&_ret)->data = _destData;
						//
						_sentenceActive=0;
					}
				}
			} else {
				_silenceStreak=0;
			}
		} else {
			if (getAvgPcm(i)>=SILENCE_THRESHOLD) {
				_sentenceActive=1;
				_silenceStreak=0;
				_currentPart.startSample=i;
			}
		}
	}

	return _ret;
}

void setLastAction(char* _actionName) {
	//
}

nList* getCurrentSentence(long _currentSample){
	nList* _currentEntry = timings;
	for (;_currentEntry->nextEntry!=NULL;){
		if (CASTDATA(_currentEntry->nextEntry)->startSample>_currentSample){ // If we're not at the next sample yet
			break;
		}
		_currentEntry=_currentEntry->nextEntry;
	}
	return _currentEntry;
}

void drawSentences() {
	long _currentSample = getCurrentSample();
	nList* _currentEntry = getCurrentSentence(_currentSample);
	int _currentX=0;
	int _maxWidth;
	SDL_GetWindowSize(mainWindow,&_maxWidth,NULL);
	while(_currentX<_maxWidth) {
		struct sentence* _currentSentence = _currentEntry->data;
		if (_currentSample<_currentSentence->endSample) {
			int _lenSamples;
			uint32_t _drawColor;
			if (_currentSample<_currentSentence->startSample) { // Draw blank space
				_lenSamples = _currentSentence->startSample-_currentSample;
				_drawColor = MAKERGB(0,0,0,255);
			} else { // Draw sentence
				_lenSamples = _currentSentence->endSample-_currentSample;
				_drawColor = MAKERGB(255,255,255,255);
			}
			int _drawWidth = samplesToTime(_lenSamples)/TIMEPERPIXEL;
			drawRectangle(_currentX,0,_drawWidth,BARHEIGHT,_drawColor);
			_currentSample+=_lenSamples;
			_currentX+=_drawWidth;
		} else {
			_currentEntry=_currentEntry->nextEntry;
		}
	}
}


void bindKey(SDL_Keycode _bindThis, keyFunc _bindFunc, char _modStatus){
	totalKeysBound++;
	boundFuncs = realloc(boundFuncs, sizeof(keyFunc)*totalKeysBound);
	boundKeys = realloc(boundKeys, sizeof(SDL_Keycode)*totalKeysBound);
	boundKeyModStatus = realloc(boundKeyModStatus, sizeof(char)*totalKeysBound);
	boundKeys[totalKeysBound-1] = _bindThis;
	boundFuncs[totalKeysBound-1] = _bindFunc;
	boundKeyModStatus[totalKeysBound-1] = _modStatus;
}

/////////////////////////////
void keyStitchForward(){
	nList* _currentSentence = getCurrentSentence(getCurrentSample());
	CASTDATA(_currentSentence)->endSample=CASTDATA(_currentSentence->nextEntry)->endSample;
	nList* _tempHold = _currentSentence->nextEntry->nextEntry;
	free(_currentSentence->nextEntry->data);
	free(_currentSentence->nextEntry);
	_currentSentence->nextEntry=_tempHold;

	setLastAction("Stitch forward");
}
/////////////////////////////

char init() {
	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) < 0) {
		printf("fail open sdl");
		return 1;
	}
	mainWindow = SDL_CreateWindow( "easierTiming", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN); //SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
	mainWindowRenderer = SDL_CreateRenderer( mainWindow, -1, SDL_RENDERER_PRESENTVSYNC);

	//https://wiki.libsdl.org/SDL_Keycode
	bindKey(SDLK_s,keyStitchForward,0);

	setLastAction("Welcome");
	return 0;
}


int main (int argc, char * argv []) {
	// init
	if (init()) {
		return 1;
	}
	if (pthread_mutex_init(&audioPosLock,NULL)!=0) {
		printf("mutex init failed");
		return 1;
	}

	// init audio
	char* infilename = "./1.ogg";
	SNDFILE* infile = NULL;
	SF_INFO	_audioInfo;
	memset (&_audioInfo, 0, sizeof (_audioInfo));
	if ((infile = sf_open (infilename, SFM_READ, &_audioInfo)) == NULL) {
		printf ("Not able to open input file %s.\n", infilename);
		printf("%s\n",sf_strerror (NULL));
		return 1;
	}
	totalSamples=_audioInfo.frames;
	sampleRate=_audioInfo.samplerate;
	totalChannels=_audioInfo.channels;
	// info
	printf("# Converted from file %s.\n", infilename);
	printf("# Channels %d, Sample rate %d\n", _audioInfo.channels, _audioInfo.samplerate);


	// Read entire file as pcm info big boy buffer
	pcmData = malloc(sizeof(float*)*_audioInfo.channels);
	int i;
	for (i=0; i<_audioInfo.channels; ++i) {
		pcmData[i] = malloc(sizeof(float)*_audioInfo.frames);
	}
	//
	int _singleReadSamples = READBLOCKSIZE/_audioInfo.channels;
	float _readBuf[_singleReadSamples*2];
	int _wroteSamples=0;
	int _lastReadCount;
	while ((_lastReadCount = sf_readf_float (infile, _readBuf, _singleReadSamples)) > 0) {
		for (i = 0; i < _lastReadCount; i++) {
			int j;
			for (j = 0; j < _audioInfo.channels; j++) {

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
	SDL_AudioSpec wav_spec; // the specs of our piece of music
	wav_spec.freq = _audioInfo.samplerate;
	wav_spec.format = AUDIO_F32LSB;
	wav_spec.channels = _audioInfo.channels;
	wav_spec.samples = PLAY_SAMPLES;
	wav_spec.callback = my_audio_callback;
	wav_spec.userdata = &_audioInfo;
	int _gottenAudioId = SDL_OpenAudio(&wav_spec, NULL);
	if ( _gottenAudioId < 0 ) {
		printf("Couldn't open audio: %s\n", SDL_GetError());
		return 1;
	}

	timings = findSentences();

	unpauseMusic();

	/////////////////////////////

	/*
	printf("%p\n",getnList(timings,0)->data);

	int _lastIndex=0;
	char _isStart=0;
	while(1){
		int _sampleIndex = getCurrentSample();
		if (!_isStart && ((struct sentence*)(getnList(timings,_lastIndex)->data))->startSample<=_sampleIndex){
			printf("start %d\n",_lastIndex);
			_isStart=1;
		}else if (_isStart && ((struct sentence*)(getnList(timings,_lastIndex)->data))->endSample<=_sampleIndex){
			printf("end %d\n",_lastIndex++);
			_isStart=0;
		}

	}*/

	char _modDown=0;
	char _running=1;
	while(_running) {

		SDL_Event e;
		while( SDL_PollEvent( &e ) != 0 ) {
			if( e.type == SDL_QUIT ) {
				_running=0;
			} else if( e.type == SDL_KEYDOWN) {
				if (e.key.keysym.sym==MODKEY){
					_modDown=1;
				}else{
					int i;
					for (i=0;i<totalKeysBound;++i){
						if (e.key.keysym.sym==boundKeys[i]){
							boundFuncs[i]();
							break;
						}
					}
					if (i==totalKeysBound){
						printf("invalid key %s\n",SDL_GetKeyName(e.key.keysym.sym));
					}
				}
			}else if (e.type==SDL_KEYUP){
				if (e.key.keysym.sym==MODKEY){
					_modDown=0;
				}
			}
		}

		if (_modDown){
			setDrawColor(MODBACKGROUNDCOLOR);
		}else{
			setDrawColor(BACKGROUNDCOLOR);
		}
		SDL_RenderClear(mainWindowRenderer);
		drawSentences();
		SDL_RenderPresent(mainWindowRenderer);


		drawSentences();
	}

	/*
	long _myRef = SDL_GetTicks();

	//SDL_Delay(3000);
	//seekAudioSamples(-44100);
	//SDL_Delay(1000);
	//SDL_CloseAudio();
	printf("\n");
	for (int i=0;i<20000;++i){

			printf("\033[A\r",27);
			int _sampleIndex=getCurrentSample();
			printf("%f:",getAvgPcm(_sampleIndex));
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
	pauseMusic();
	pthread_mutex_destroy(&audioPosLock);

	return 0;
}
