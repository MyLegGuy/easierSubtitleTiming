/*
	UIdeas - 
	Q - Rewind
	W - Forward
	S - stitch - Combine this and next one
	A - stitch this and previous one
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <float.h>
#include <pthread.h>
#include <unistd.h>
#include <fvad.h>
#include <samplerate.h>

#include <sndfile.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL_FontCache.h>

#include "goodLinkedList.h"
#include "stack.h"


// Valid options: 10, 20, 30
#define FVAD_TIME_INPUT 10
// 8000, 16000, 32000, 48000
// anything above 8000 is downscaled for you
#define FVAD_RATE 8000
// Calculate using FVAD_TIME_INPUT and FVAD_RATE
// (FVAD_RATE*FVAD_TIME_INPUT)/(double)1000
// For FVAD_TIME_INPUT 10 and FVAD_RATE 8000, values are: 80, 160, 240
#define FVAD_SAMPLE_INPUT 80
// 
#define FVAD_USE_GREATER_CHANNEL 0

// when looking for default font. Must end in a slash
#define DEFAULTFONTDIR "/usr/share/fonts/TTF/"
#define GETFONTCOMMAND "fc-match"
// Defined for the srt file format
#define SUBFORMATSTRING "%d\n%s --> %s\n%s\n\n"
#define SRTTIMEFORMAT "%02d:%02d:%02d,%03d"
#define READSRTFORMAT "%d\n"SRTTIMEFORMAT" --> "SRTTIMEFORMAT"\n" // For reading srt. does not read actual sub, does that manually

#define MAKERGBA(r,g,b,a) ((((a)&0xFF)<<24) | (((b)&0xFF)<<16) | (((g)&0xFF)<<8) | (((r)&0xFF)<<0))
#define PLAY_SAMPLES  4096
#define READBLOCKSIZE 8192
// in milliseconds
#define SENTENCE_SPACE_TIME 400
#define MIN_SENTENCE_TIME SENTENCE_SPACE_TIME
#define REDRAWTIME 40
#define NORMALSEEK 1000
#define MEGASEEK 3000
#define MINISEEK 500
#define REACTSEEK 400

#define BARHEIGHT 32
#define CASTDATA(a) ((struct sentence*)(a->data))
#define MODKEY SDLK_LSHIFT
#define INDICATORWIDTH 18
#define FONTSIZE 20
#define TIMEPERPIXEL 25
#define INDENTPIXELS 32
#define CROSSOUTHDENOM 10
#define EXPLICITCHOPW 3
#define BOTTOMINFOLINES 1
#define TIMEFORMAT "%02d:%02d,%03d"
#define TIMEARGS(a) (int)(a/60000) ,(int)((a%60000)/1000),(int)(a%1000)

// colors
#define BACKGROUNDCOLOR MAKERGBA(0,0,0,255)
#define MODBACKGROUNDCOLOR MAKERGBA(0,0,57,255)
#define MARKERCOLOR MAKERGBA(0,255,0,255)
#define PAUSEDMARKERCOLOR MAKERGBA(150,150,150,255);
#define COLORINACTIVESENTENCE MAKERGBA(255,255,255,255)
#define COLORCURRENTSENTENCE MAKERGBA(255,0,0,255)
#define COLORNOSENTENCE BACKGROUNDCOLOR
#define ACTIVESUBCOLOR FC_MakeColor(0,255,0,255)
#define CROSSOUTCOLOR MAKERGBA(255,0,0,255) // The line that goes though skipped subs
#define EXPLICITCHOPCOLOR MAKERGBA(195,110,0,255)
#define ACTIONHISTORYCOLOR FC_MakeColor(150,150,150,255)

typedef void(*keyFunc)(long _currentSample);
typedef void(*undoFunc)(long _currentSample, void* _data);
struct undo{
	char* message;
	undoFunc doThis;
	void* passData;
	long atSample;
	char freeMessage;
};
struct pointerHolder3{
	void* item1;
	void* item2;
	void* item3;
};
struct longHolder2{
	long item1;
	long item2;
};
struct sentence {
	long startSample;
	long endSample;
};
//
void undoStitch(long _currentSample, void* _passedData);
void loadRawsubs(const char* _filename);
//

SDL_Window* mainWindow;
SDL_Renderer* mainWindowRenderer;

float** pcmData=NULL;
long pcmPlayPos=0;
long totalSamples;
int sampleRate;
int totalChannels;

// Use these two together to find current sample
uint32_t audioTimeReference;
uint32_t audioTimeOther;

pthread_mutex_t audioPosLock;
FC_Font* goodFont;
int fontHeight;

struct nList* timings;
nStack* undoStack;

int numRawSubs=0;
char** rawSubs;
char* rawSkipped;
char* plainSubsFilename; // For reloading

int totalKeysBound=0;
SDL_Keycode* boundKeys;
char* boundKeyModStatus;
keyFunc* boundFuncs;
int sizeActionHistory=0;
int nextActionIndex=0; // Add the next one to this array index
char** actionHistory=NULL;
struct nList* lastActionMessages=NULL;
int addingSubIndex=-1;

void showMessageEasy(const char* _passedMessage){
	SDL_SetRenderDrawColor(mainWindowRenderer,0,0,0,255);
	SDL_RenderClear(mainWindowRenderer);
	FC_Draw(goodFont,mainWindowRenderer,0,0,_passedMessage);
	SDL_RenderPresent(mainWindowRenderer);
}
void seekPast(FILE* fp, unsigned char _target){
	while (1){
		int _lastRead = fgetc(fp);
		if (_lastRead==_target || _lastRead==EOF){
			break;
		}
	}
}
void seekNextLine(FILE* fp){
	seekPast(fp,0x0A);
}
int wrapNum(int _passed, int _min, int _max){
	if (_passed<_min){
		return _max-(_min-_passed-1);
	}else if (_passed>_max){
		return _min+(_passed-_max-1);
	}
	return _passed;
}
long roundMultiple(long _roundThis, int _multipleOf){
	int _remainder = _roundThis%_multipleOf;
	if (_remainder!=0){
		return _roundThis-_remainder;
	}else{
		return _roundThis;
	}
}
char fileExists(const char* _passedPath){
	return (access(_passedPath, R_OK)!=-1);
}
struct longHolder2* newLongHolder2(long item1, long item2){
	struct longHolder2* _ret = malloc(sizeof(struct longHolder2));
	_ret->item1=item1;
	_ret->item2=item2;
	return _ret;
}
struct pointerHolder3* newGenericHolder3(void* item1, void* item2, void* item3){
	struct pointerHolder3* _ret = malloc(sizeof(struct pointerHolder3));
	_ret->item1=item1;
	_ret->item2=item2;
	_ret->item3=item3;
	return _ret;
}
struct undo* makeUndoEntry(char* _message, undoFunc _action, void *_data, long _sampleTime, char _freeMessage){
	struct undo* _ret = malloc(sizeof(struct undo));
	_ret->message=_message;
	_ret->doThis=_action;
	_ret->passData=_data;
	_ret->atSample = _sampleTime;
	_ret->freeMessage=_freeMessage;
	return _ret;
}

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
long timeToSamples(long _numMilliseconds) {
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
	}else{
		SF_INFO* _passedInfo = userdata;
		int _possibleWriteSamples = (len/sizeof(float))/_passedInfo->channels;
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
	}

	pthread_mutex_unlock(&audioPosLock);
}
// Returns milliseconds
double samplesToTime(long _passedSamples) {
	return (_passedSamples/(double)sampleRate)*1000;
}
void lowSeekAudioSamplesExact(long _samplePosition, char _startLock){
	if (_startLock){
		pthread_mutex_lock(&audioPosLock);
	}
	
	pcmPlayPos=_samplePosition;
	if (pcmPlayPos<0) {
		pcmPlayPos=0;
	} else if (pcmPlayPos+PLAY_SAMPLES>totalSamples) {
		pcmPlayPos=totalSamples-PLAY_SAMPLES;
	}
	audioTimeOther=samplesToTime(pcmPlayPos);
	
	audioTimeReference=SDL_GetTicks();
	pthread_mutex_unlock(&audioPosLock);
}
void seekAudioSamplesExact(long _samplePosition){
	lowSeekAudioSamplesExact(_samplePosition,1);
}
void seekAudioSamples(int _numSamples) {
	pthread_mutex_lock(&audioPosLock);
	lowSeekAudioSamplesExact(pcmPlayPos+_numSamples,0);
}
void seekAudioMilli(int _numMilliseconds) {
	seekAudioSamples((_numMilliseconds/(double)1000)*sampleRate);
}
// approximation
long getCurrentSample() {
	long _currentTime;
	if (SDL_GetAudioStatus()==SDL_AUDIO_PLAYING){
		_currentTime = SDL_GetTicks();
	}else{
		_currentTime = audioTimeReference; // Don't account for current time when getting sample if we're paused
	}
	return (((_currentTime-audioTimeReference)+audioTimeOther)/(double)1000)*sampleRate;
}
double getAvgPcmVol(long _sampleNumber) {
	double _avg=0;
	int i;
	for (i=0; i<totalChannels; ++i) {
		_avg+=fabs(pcmData[i][_sampleNumber]);
	}
	return _avg/totalChannels;
}
struct nList* findSentences(long _startSample, int _passedMode) {
	showMessageEasy("Finding sentences...");
	struct nList* _ret=NULL;

	Fvad* _voiceState = fvad_new();
	fvad_set_mode(_voiceState,_passedMode);
	fvad_set_sample_rate(_voiceState,FVAD_RATE);

	int _lastError=0;
	SRC_STATE* _converter = src_new(SRC_SINC_BEST_QUALITY,1,&_lastError);
	if (_lastError!=0){
		printf("SRC_STATE new error\n");
		exit(1);
	}

	// Work with this, add it to the final list once it's good
	struct sentence _currentPart;
	char _sentenceActive=0;
	long _silenceStreak;
	long _silenceStreakStart;
	
	// Number of samples that go into the sample rate converter from the one channel original audio
	int _unscaledInLength = (sampleRate*FVAD_TIME_INPUT)/(double)1000;
	int _scaledOutLen = FVAD_SAMPLE_INPUT; // We need exactly this many for the voice detector

	float _scaledOutput[_scaledOutLen];
	float _singleChannelOriginal[_unscaledInLength];

	SRC_DATA _convertInfo;
	_convertInfo.data_in=_singleChannelOriginal;
	_convertInfo.input_frames=_unscaledInLength;
	_convertInfo.src_ratio = FVAD_RATE/(double)sampleRate;
	_convertInfo.end_of_input=0;
	
	// Main processing loop
	int _filledOriginalSamples=0; // Number of unprocessed samples currently inside the _singleChannelOriginal
	struct nList** _listAppender = initSpeedyAddnList(&_ret);
	long s;
	for (s=_startSample;s<totalSamples;){
		int _outputNeeded=FVAD_SAMPLE_INPUT;
		while(_outputNeeded!=0){
			_convertInfo.output_frames=_outputNeeded;
			_convertInfo.data_out = &(_scaledOutput[FVAD_SAMPLE_INPUT-_outputNeeded]);

			// If we don't have enough samples left, just discard the last bit. It's probably not important at all.
			if (s+(_unscaledInLength-_filledOriginalSamples)>=totalSamples){
				s=totalSamples+1;
				break;
			}
			// Fill up what's left in the _singleChannelOriginal
			int i;
			for (i=0;i<_unscaledInLength-_filledOriginalSamples;++i){
				// Convert to mono. Required for fvad.
				double _avg=0;
				int c;
				#if FVAD_USE_GREATER_CHANNEL==1
				// Use only the greatest channel
				for (c=0;c<totalChannels;++c){
					if (fabs(pcmData[c][s])>fabs(_avg)){
						_avg=pcmData[c][s];
					}
				}
				#else
				// Average all channels
				for (c=0;c<totalChannels;++c){
					_avg+=pcmData[c][s];
				}
				_avg=_avg/(double)totalChannels;
				#endif
				_singleChannelOriginal[i+_filledOriginalSamples]=_avg;
				++s;
			}
			// Do downscale
			_lastError = src_process(_converter,&_convertInfo);
			if (_lastError!=0){
				printf("%s\n",src_strerror(_lastError));
				exit(1);
			}
			// Shift over the unused input data
			_filledOriginalSamples = _unscaledInLength-_convertInfo.input_frames_used;
			if (_filledOriginalSamples!=0){
				memmove(_singleChannelOriginal,&(_singleChannelOriginal[_convertInfo.input_frames_used]),_filledOriginalSamples);
			}
			// Account for this
			_outputNeeded-=_convertInfo.output_frames_gen;
		}
		if (s==totalSamples+1){ // Early break
			break;
		}
		// From here we can assume that the output buffer is full
		// We need the data in short to use with the voice detector
		int16_t _shortVoiceIn[FVAD_SAMPLE_INPUT];
		src_float_to_short_array(_scaledOutput,_shortVoiceIn,FVAD_SAMPLE_INPUT);
		// Do
		int _voiceOn = fvad_process(_voiceState,_shortVoiceIn,FVAD_SAMPLE_INPUT);
		if (_sentenceActive){
			if (!_voiceOn){
				// Init silence streak if needed
				if (_silenceStreak==0){
					_silenceStreakStart=s;
				}
				//
				++_silenceStreak;
				// If the silence streak is too long, end the sentence
				if (_silenceStreak*FVAD_TIME_INPUT>=SENTENCE_SPACE_TIME){
					// Ensure that it isn't just an audio pop or something.
					// It has to be an actual sentence
					if (samplesToTime(_silenceStreakStart-_currentPart.startSample)>=MIN_SENTENCE_TIME){
						//printf("Sentence from %d to %d: %f\n",_currentPart.startSample,i,samplesToTime(i-_currentPart.startSample));
						// Put in list
						_currentPart.endSample=_silenceStreakStart;
						void* _destData = malloc(sizeof(struct sentence));
						memcpy(_destData,&_currentPart,sizeof(struct sentence));
						_listAppender=speedyAddnList(_listAppender,_destData);						
					}
					//
					_sentenceActive=0;
				}
			}else{
				_silenceStreak=0;
			}
		}else{
			if (_voiceOn){
				_sentenceActive=1;
				_silenceStreak=0;
				_currentPart.startSample=s-FVAD_SAMPLE_INPUT;
			}
		}
	}
	endSpeedyAddnList(_listAppender);
	src_delete(_converter);
	fvad_free(_voiceState);
	return _ret;
}

void setLastAction(char* _actionName) {
	free(actionHistory[nextActionIndex]);
	actionHistory[nextActionIndex] = strdup(_actionName);
	if ((++nextActionIndex)==sizeActionHistory){
		nextActionIndex=0;
	}
}

struct nList* getCurrentSentence(long _currentSample, int* _retIndex){
	struct nList* _currentEntry = timings;
	int i;
	for (i=0;_currentEntry->nextEntry!=NULL;++i){
		if (CASTDATA(_currentEntry->nextEntry)->startSample>_currentSample){ // If we're not at the next sample yet
			break;
		}
		_currentEntry=_currentEntry->nextEntry;
	}
	if (_retIndex!=NULL){
		*_retIndex=i;
	}
	return _currentEntry;
}
struct nList* getBeforeCurrentSentence(long _currentSample, int* _retIndex){
	struct nList* _currentEntry = timings;
	struct nList* _prevEntry=NULL;
	int i;
	for (i=-1;_currentEntry->nextEntry!=NULL;++i){
		if (CASTDATA(_currentEntry->nextEntry)->startSample>_currentSample){ // If we're not at the next sample yet
			break;
		}
		_prevEntry=_currentEntry;
		_currentEntry=_currentEntry->nextEntry;
	}
	if (_retIndex!=NULL){
		*_retIndex=i;
	}
	return _prevEntry;
}
int getCurrentSentenceIndex(long _currentSample){
	int _ret;
	getCurrentSentence(_currentSample,&_ret);
	return _ret;
}

// why is this in a method
void drawSentences(int _maxWidth, long _currentSample){
	int _currentX=0;
	long _highlightSample = _currentSample; // Holder
	// Center
	_currentSample-=timeToSamples((_maxWidth/2)*TIMEPERPIXEL);
	if (_currentSample<0){
		_currentX = _maxWidth/2 - samplesToTime(_highlightSample)/TIMEPERPIXEL;
		_currentSample=0;
	}
	struct nList* _currentEntry = getCurrentSentence(_currentSample,NULL);
	signed int _lastDrawXEnd=-20; // Where the last rectangle ended
	while(_currentX<_maxWidth) {
		struct sentence* _currentSentence = _currentEntry->data;
		if (_currentSample<_currentSentence->endSample) {
			int _lenSamples;
			uint32_t _drawColor;
			char _isDrawingSentence = !(_currentSample<_currentSentence->startSample);
			if (!_isDrawingSentence) { // Draw blank space
				_lenSamples = _currentSentence->startSample-_currentSample;
				_drawColor = COLORNOSENTENCE;
			} else { // Draw sentence
				_lenSamples = _currentSentence->endSample-_currentSample;
				if (_highlightSample>=_currentSentence->startSample && _highlightSample<_currentSentence->endSample){
					_drawColor = COLORCURRENTSENTENCE;
				}else{
					_drawColor = COLORINACTIVESENTENCE;
				}
			}
			int _drawWidth = ceil(samplesToTime(_lenSamples)/(double)TIMEPERPIXEL);
			drawRectangle(_currentX,0,_drawWidth,BARHEIGHT,_drawColor);


			if (_isDrawingSentence){
				if (_currentX<=_lastDrawXEnd+1){ // If we're pretty close to the end of the last actual sentence, show the division
					drawRectangle(_lastDrawXEnd+1,0,EXPLICITCHOPW,BARHEIGHT,EXPLICITCHOPCOLOR);
				}
				_lastDrawXEnd = _currentX+_drawWidth;
			}

			_currentSample+=timeToSamples(_drawWidth*TIMEPERPIXEL); // this will round samples to the nearest multiple of TIMEPERPIXEL
			_currentX+=_drawWidth;
		} else {
			_currentEntry=_currentEntry->nextEntry;
			if (_currentEntry==NULL){
				break;
			}
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

void removeNewline(char* _toRemove){
	int _cachedStrlen = strlen(_toRemove);
	if (_cachedStrlen==0){
		return;
	}
	if (_toRemove[_cachedStrlen-1]==0x0A){ // Last char is UNIX newline
		if (_cachedStrlen>=2 && _toRemove[_cachedStrlen-2]==0x0D){ // If it's a Windows newline
			_toRemove[_cachedStrlen-2]='\0';
		}else{ // Well, it's at very least a UNIX newline
			_toRemove[_cachedStrlen-1]='\0';
		}
	}
}

void lowStitchForwards(struct nList* _startHere, char* _message){
	// Undo data
	long _oldEnd = CASTDATA(_startHere)->endSample;
	long _oldStart = CASTDATA(_startHere->nextEntry)->startSample;

	CASTDATA(_startHere)->endSample=CASTDATA(_startHere->nextEntry)->endSample;
	struct nList* _tempHold = _startHere->nextEntry->nextEntry;
	free(_startHere->nextEntry->data);
	free(_startHere->nextEntry);
	_startHere->nextEntry=_tempHold;

	setLastAction(_message);
	addStack(&undoStack,makeUndoEntry(_message,undoStitch,newLongHolder2(_oldEnd,_oldStart),CASTDATA(_startHere)->startSample,0));
}

int correctSentenceIndex(int _passedIndex){
	if (_passedIndex>=numRawSubs){
		return numRawSubs-1;
	}
	int i;
	for (i=0;i<=_passedIndex;++i){
		if (rawSkipped[i]){
			++_passedIndex;
		}
	}
	if (_passedIndex>=numRawSubs){
		return numRawSubs-1;
	}
	return _passedIndex;
}

// realloc, but new memory is zeroed out
void* recalloc(void* _oldBuffer, int _newSize, int _oldSize){
	void* _newBuffer = realloc(_oldBuffer,_newSize);
	if (_newSize > _oldSize){
		void* _startOfNewData = ((char*)_newBuffer)+_oldSize;
		memset(_startOfNewData,0,_newSize-_oldSize);
	}
	return _newBuffer;
}

void resizeActionHistory(int _windowHeight){
	int _oldMax = sizeActionHistory;
	sizeActionHistory = _windowHeight/fontHeight;
	actionHistory = recalloc(actionHistory,sizeof(char*)*sizeActionHistory,sizeof(char*)*_oldMax);
	nextActionIndex = wrapNum(nextActionIndex,0,sizeActionHistory-1);
}

/////////////////////////////

// passed is longHolder2 with item1 being the end of the the first one and item2 being the start of the second one
void undoStitch(long _currentSample, void* _passedData){
	struct nList* _currentSentence = getCurrentSentence(_currentSample,NULL);
	struct nList* _undoneDeleted = malloc(sizeof(struct nList));
	_undoneDeleted->data = malloc(sizeof(struct sentence));
	CASTDATA(_undoneDeleted)->startSample = ((struct longHolder2*)(_passedData))->item2;
	CASTDATA(_undoneDeleted)->endSample = CASTDATA(_currentSentence)->endSample;
	CASTDATA(_currentSentence)->endSample = ((struct longHolder2*)(_passedData))->item1;
	_undoneDeleted->nextEntry = _currentSentence->nextEntry;
	_currentSentence->nextEntry = _undoneDeleted;
}
// Passed is malloc'd int with index
void undoSkip(long _currentSample, void* _passedData){
	rawSkipped[*((int*)_passedData)]=0;
}
// No data
void undoChop(long _currentSample, void* _passedData){
	struct nList* _currentEntry = getCurrentSentence(_currentSample,NULL);
	struct nList* _freeThis = _currentEntry->nextEntry;
	CASTDATA(_currentEntry)->endSample = CASTDATA(_freeThis)->endSample;
	_currentEntry->nextEntry = _freeThis->nextEntry;
	free(_freeThis->data);
	free(_freeThis);
}
// data is longHolder2 with start and end
void undoDeleteSentence(long _currentSample, void* _passedData){
	struct nList* _newEntry = malloc(sizeof(struct nList));
	_newEntry->data = malloc(sizeof(struct sentence));
	CASTDATA(_newEntry)->startSample = ((struct longHolder2*)_passedData)->item1;
	CASTDATA(_newEntry)->endSample = ((struct longHolder2*)_passedData)->item2;

	// insert sorted
	struct nList* _prevList=NULL;
	ITERATENLIST(timings,{
		//_curnList
		if (CASTDATA(_curnList)->startSample>CASTDATA(_newEntry)->endSample){
			_newEntry->nextEntry=_curnList;
			break;
		}
		_prevList = _curnList;
	});

	if (_prevList==NULL){
		timings = _newEntry;
	}else{
		_prevList->nextEntry = _newEntry;
	}
}

long timeToMilliseconds(int _hours, int _mins, int _secs, int _milliseconds){
	return (_hours*3600+_mins*60+_secs)*1000+_milliseconds;
}
int milliToMilli(long _milliseconds){
	return _milliseconds%1000; // Whatever can't fit into a second
}
int milliToSec(long _milliseconds){
	return (_milliseconds/1000)%60; // To seconds, whatever can't fit into a minute.
}
int milliToMin(long _milliseconds){
	return (_milliseconds/60000)%60; // To minutes, whatever can't fit in hours
}
int milliToHour(long _milliseconds){
	return _milliseconds/60000/60; // To minutes, then to hours
}
void makeTimestamp(char* _buff, long _milliseconds){
	sprintf(_buff,SRTTIMEFORMAT,milliToHour(_milliseconds),milliToMin(_milliseconds),milliToSec(_milliseconds),milliToMilli(_milliseconds));
}
void writeSingleSrt(int _index, long _startMilli, double _endMilli, char* _sub, FILE* fp){
	char strstampone[strlen(SRTTIMEFORMAT)];
	char strstamptwo[strlen(SRTTIMEFORMAT)];

	makeTimestamp(strstampone,_startMilli);
	makeTimestamp(strstamptwo,_endMilli);

	char complete[strlen(SUBFORMATSTRING)+strlen(strstampone)+strlen(_sub)+strlen(strstamptwo)+1];
	sprintf(complete,SUBFORMATSTRING,_index,strstampone,strstamptwo,_sub);

	fwrite(complete,strlen(complete),1,fp);
}

/////////////////////////////

void keyStitchForward(long _currentSample){
	struct nList* _currentSentence = getCurrentSentence(_currentSample,NULL);
	if (_currentSentence->nextEntry==NULL){
		return;
	}
	lowStitchForwards(_currentSentence,"stitch forwards");
}
void keyStitchBackward(long _currentSample){
	lowStitchForwards(getBeforeCurrentSentence(_currentSample,NULL),"stitch backwards");
}
void keyUndo(long _currentSample){
	if (stackEmpty(undoStack)){
		setLastAction("Nothing to undo.");
	}else{
		struct undo* _undoAction = popStack(&undoStack);
		_undoAction->doThis(_undoAction->atSample,_undoAction->passData);
	
		char* _stitchedMessage = malloc(strlen(_undoAction->message)+strlen("Undo: []")+1);
		strcpy(_stitchedMessage,"Undo: [");
		strcat(_stitchedMessage,_undoAction->message);
		strcat(_stitchedMessage,"]");
		setLastAction(_stitchedMessage);
		free(_stitchedMessage);
	
		if (_undoAction->freeMessage){
			free(_undoAction->message);
		}
		free(_undoAction->passData);
		free(_undoAction);
	}
}
void keyNormalSeekForward(long _currentSample){
	seekAudioMilli(NORMALSEEK);
}
void keyNormalSeekBack(long _currentSample){
	seekAudioMilli(NORMALSEEK*-1);
}
void keyMegaSeekForward(long _currentSample){
	seekAudioMilli(MEGASEEK);
}
void keyMegaSeekBack(long _currentSample){
	seekAudioMilli(MEGASEEK*-1);
}
void keySeekBackSentence(long _currentSample){
	struct nList* _possibleEntry = getBeforeCurrentSentence(_currentSample,NULL);
	if (_possibleEntry!=NULL){
		seekAudioSamplesExact(CASTDATA(_possibleEntry)->startSample);
	}
}
void keySeekForwardSentence(long _currentSample){
	struct nList* _possibleNext = getCurrentSentence(_currentSample,NULL)->nextEntry;
	if (_possibleNext!=NULL){
		seekAudioSamplesExact(CASTDATA(_possibleNext)->startSample);
	}
}
void keySeekSentenceStart(long _currentSample){
	seekAudioSamplesExact(CASTDATA(getCurrentSentence(_currentSample,NULL))->startSample);
}
void keySkip(long _currentSample){
	int _currentIndex = getCurrentSentenceIndex(_currentSample);
	_currentIndex = correctSentenceIndex(_currentIndex);
	rawSkipped[_currentIndex]=1;

	char* _messageBuff = malloc(strlen("Skip: \"\"")+strlen(rawSubs[_currentIndex])+1);
	strcpy(_messageBuff,"Skip: \"");
	strcat(_messageBuff,rawSubs[_currentIndex]);
	strcat(_messageBuff,"\"");
	setLastAction(_messageBuff);
	int* _dataPointer = malloc(sizeof(int));
	*_dataPointer=_currentIndex;
	addStack(&undoStack,makeUndoEntry(_messageBuff,undoSkip,_dataPointer,_currentSample,1));
}
void keyChop(long _currentSample){
	struct nList* _currentSentence = getCurrentSentence(_currentSample,NULL);
	if (_currentSample>=CASTDATA(_currentSentence)->endSample){
		setLastAction("Can't chop, too far");
		return;
	}
	struct nList* _newEntry = malloc(sizeof(struct nList));
	_newEntry->data = malloc(sizeof(struct sentence));
	CASTDATA(_newEntry)->startSample=_currentSample+1;
	CASTDATA(_newEntry)->endSample=CASTDATA(_currentSentence)->endSample;
	CASTDATA(_currentSentence)->endSample=_currentSample;
	struct nList* _temp = _currentSentence->nextEntry;
	_currentSentence->nextEntry = _newEntry;
	_newEntry->nextEntry = _temp;
	setLastAction("Chop");

	addStack(&undoStack,makeUndoEntry("Chop",undoChop,NULL,_currentSample,0));
}
void keyPause(long _currentSample){
	SDL_AudioStatus _gottonStatus = SDL_GetAudioStatus();
	if (_gottonStatus==SDL_AUDIO_PAUSED){
		unpauseMusic();
		setLastAction("Unpause");
	}else if (_gottonStatus==SDL_AUDIO_PLAYING){
		pauseMusic();
		setLastAction("Pause");
	}else{
		printf("Unknown SDL_GetAudioStatus\n");
	}
}
void keyPrintDivider(long _currentSample){
	long _numMilliseconds = samplesToTime(_currentSample);
	char _messageBuff[100];
	sprintf(_messageBuff,"---"TIMEFORMAT"---",TIMEARGS(_numMilliseconds));
	setLastAction(_messageBuff);
}
void keyDeleteSentence(long _currentSample){
	struct nList* _previousEntry = getBeforeCurrentSentence(_currentSample,NULL);
	struct nList* _currentEntry;
	if (_previousEntry==NULL){
		_currentEntry=timings;
	}else{
		_currentEntry=_previousEntry->nextEntry;
	}
	struct nList* _replacement = _currentEntry->nextEntry;
	long _oldStart = CASTDATA(_currentEntry)->startSample;
	long _oldEnd = CASTDATA(_currentEntry)->endSample;
	free(_currentEntry->data);
	free(_currentEntry);
	if (_previousEntry==NULL){
		timings = _replacement;
	}else{
		_previousEntry->nextEntry = _replacement;
	}
	addStack(&undoStack,makeUndoEntry("Del sentence",undoDeleteSentence,newLongHolder2(_oldStart,_oldEnd),_currentSample,0));
	setLastAction("Del sentence");
}
void keyRecalculateSentences(long _currentSample){
	struct nList* _currentSentence = getBeforeCurrentSentence(_currentSample,NULL);
	if (_currentSentence!=NULL && _currentSentence->nextEntry!=NULL){
		pauseMusic();
		int _inputResult=-1;
		while(_inputResult==-1){
			SDL_Event e;
			while(SDL_PollEvent(&e)!=0){
				if (e.type==SDL_KEYDOWN){
					switch(e.key.keysym.sym){
					case SDLK_0:
						_inputResult=0;
						break;
					case SDLK_1:
						_inputResult=1;
						break;
					case SDLK_2:
						_inputResult=2;
						break;
					case SDLK_3:
						_inputResult=3;
						break;
					case SDLK_ESCAPE:
						_inputResult=-2;
						break;
					}
				}
			}
			showMessageEasy("vad mode:\n0 (\"quality\"),\n1 (\"low bitrate\"),\n2 (\"aggressive\"),\n3 (\"very aggressive\")\n<esc> (\"cancel\")");
		}
		if (_inputResult!=-2){
			freenList(_currentSentence->nextEntry, 1);
			_currentSentence->nextEntry = findSentences(CASTDATA(_currentSentence)->endSample,_inputResult);
			setLastAction("Recalculate sentences");
		}
		unpauseMusic();
	}
}
void keyAddSub(long _currentSample){
	struct nList* _newEntry = malloc(sizeof(struct nList));
	_newEntry->data = malloc(sizeof(struct sentence));
	CASTDATA(_newEntry)->startSample=_currentSample;
	CASTDATA(_newEntry)->endSample=_currentSample+1;

	struct nList* _prevList=NULL;
	int i=0;
	ITERATENLIST(timings,{
		if (_currentSample<CASTDATA(_curnList)->endSample){
			break;
		}
		_prevList=_curnList;
		++i;
	});
	struct nList* _tempHold = _prevList->nextEntry;
	_prevList->nextEntry = _newEntry;
	_newEntry->nextEntry = _tempHold;

	addingSubIndex = i;
	setLastAction("Adding sub");

}
void keyReactAddSub(long _currentSample){
	keyAddSub(_currentSample-timeToSamples(REACTSEEK));
}
void keyEndSub(long _currentSample){
	addingSubIndex=-1;
}
void keyReloadPlain(long _currentSample){
	int i;
	for (i=0;i<numRawSubs;++i){
		free(rawSubs[i]);
	}
	free(rawSubs);
	rawSubs=NULL;
	loadRawsubs(plainSubsFilename);
}

/////////////////////////////

char* getFontFilename(){
	FILE* _outStream = popen(GETFONTCOMMAND,"r");
	if (_outStream==NULL){
		printf("popen %s failed\n",GETFONTCOMMAND);
		return NULL;
	}
	char _commandOut[256];
	_commandOut[fread(_commandOut,1,sizeof(_commandOut)-1,_outStream)]='\0';
	fclose(_outStream);
	char* _endHere = strchr(_commandOut,':');
	if (_endHere==NULL){
		printf("bad output from %s: %s\n",GETFONTCOMMAND,_commandOut);
		return NULL;
	}
	_endHere[0]='\0';
	char* _fullFilename = malloc(strlen(DEFAULTFONTDIR)+strlen(_commandOut)+1);
	strcpy(_fullFilename,DEFAULTFONTDIR);
	strcat(_fullFilename,_commandOut);
	if (!fileExists(_fullFilename)){
		printf("font does not exist: %s\n",_fullFilename);
		free(_fullFilename);
		return NULL;
	}else{
		return _fullFilename;
	}
}

void loadRawsubs(const char* _filename){
	FILE* fp = fopen(_filename,"rb");
	size_t _lineSize=0;
	char* _lastLine=NULL;
	int _oldNumRaw = numRawSubs;
	numRawSubs=0;
	while (getline(&_lastLine,&_lineSize,fp)!=-1){
		removeNewline(_lastLine);
		if (strlen(_lastLine)<=1){
			free(_lastLine);
		}else{
			numRawSubs++;
			rawSubs = realloc(rawSubs,sizeof(char*)*(numRawSubs));
			rawSubs[numRawSubs-1]=_lastLine;
		}
		_lineSize=0;
		_lastLine=NULL;
	}
	free(_lastLine); // Need to free the pointer even after getline fails
	fclose(fp);
	rawSkipped = recalloc(rawSkipped,sizeof(char)*numRawSubs,sizeof(char)*_oldNumRaw);
}
void loadSrt(const char* _filename){
	struct nList* _subList=NULL;
	FILE* fp = fopen(_filename,"rb");
	numRawSubs=0;
	while(!feof(fp)){
		int _currentIndex;
		int _numHours[2];
		int _numMinutes[2];
		int _numSeconds[2];
		int _numMilliseconds[2];
		if (fscanf(fp,READSRTFORMAT,&_currentIndex,&_numHours[0],&_numMinutes[0],&_numSeconds[0],&_numMilliseconds[0],&_numHours[1],&_numMinutes[1],&_numSeconds[1],&_numMilliseconds[1])==EOF){
			break;
		}
		size_t _lineSize=0;
		char* _lastLine=NULL;
		getline(&_lastLine,&_lineSize,fp);
		seekNextLine(fp);

		if (_numMilliseconds[0]!=-1){ // Subs with milliseconds of -1 indicate subs that didn;t have a sentence when saving
			// Make sentence
			struct nList* _currentEntry = addnList(&timings);
			_currentEntry->data = malloc(sizeof(struct sentence));
			CASTDATA(_currentEntry)->startSample = timeToSamples(timeToMilliseconds(_numHours[0],_numMinutes[0],_numSeconds[0],_numMilliseconds[0]));
			CASTDATA(_currentEntry)->endSample = timeToSamples(timeToMilliseconds(_numHours[1],_numMinutes[1],_numSeconds[1],_numMilliseconds[1]));
		}
		// put sub in list
		addnList(&_subList)->data = _lastLine;

		numRawSubs++;
	}
	// Put subs in an array instead
	rawSubs = malloc(sizeof(char*)*numRawSubs);
	int i=0;
	ITERATENLIST(_subList,{
		rawSubs[i++] = _curnList->data;
	});
	freenList(_subList,0);

	fclose(fp);
}

char init(int argc, char** argv, const char* _manualFontFilename) {
	if (pthread_mutex_init(&audioPosLock,NULL)!=0) {
		printf("mutex init failed");
		return 1;
	}

	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) < 0) {
		printf("fail open sdl");
		return 1;
	}
	mainWindow = SDL_CreateWindow( "easierTiming", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN); //SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
	mainWindowRenderer = SDL_CreateRenderer( mainWindow, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

	goodFont = FC_CreateFont();
	if (_manualFontFilename==NULL){
		char* _fontFilename = getFontFilename();
		if (_fontFilename==NULL){
			printf("Could not find font filename. Pass one with arguments.\n");
			return 1;
		}
		FC_LoadFont(goodFont, mainWindowRenderer, _fontFilename, FONTSIZE, FC_MakeColor(255,255,255,255), TTF_STYLE_NORMAL);
		free(_fontFilename);
	}else{
		FC_LoadFont(goodFont, mainWindowRenderer, _manualFontFilename, FONTSIZE, FC_MakeColor(255,255,255,255), TTF_STYLE_NORMAL);
	}
	fontHeight = FC_GetLineHeight(goodFont);

	// Normally, the SDL window resize will be called on start, but do this explicitly just in case.
	int _maxHeight;
	SDL_GetWindowSize(mainWindow,NULL,&_maxHeight);
	resizeActionHistory(_maxHeight);

	//https://wiki.libsdl.org/SDL_Keycode
	bindKey(SDLK_s,keyStitchForward,0);
	bindKey(SDLK_a,keyStitchBackward,0);
	bindKey(SDLK_e,keyUndo,0);
	bindKey(SDLK_1,keyNormalSeekBack,0);
	bindKey(SDLK_2,keyNormalSeekForward,0);
	bindKey(SDLK_1,keyMegaSeekBack,1);
	bindKey(SDLK_2,keyMegaSeekForward,1);
	bindKey(SDLK_q,keySeekBackSentence,0);
	bindKey(SDLK_w,keySeekForwardSentence,0);
	bindKey(SDLK_BACKQUOTE,keySeekSentenceStart,0); // `
	bindKey(SDLK_d,keySkip,0);
	bindKey(SDLK_c,keyChop,0);
	bindKey(SDLK_SPACE,keyPause,0);
	bindKey(SDLK_ESCAPE,keyPrintDivider,0);
	bindKey(SDLK_x,keyDeleteSentence,0);
	bindKey(SDLK_r,keyRecalculateSentences,0);
	bindKey(SDLK_F5,keyReloadPlain,1);

	bindKey(SDLK_a,keyAddSub,1);
	bindKey(SDLK_s,keyEndSub,1);
	bindKey(SDLK_d,keyReactAddSub,1);

	setLastAction("Welcome");
	return 0;
}

int main (int argc, char** argv) {
	if (argc<3){
		printf("Usage:\n");
		printf("%s <sound in> <srt out>\n",argv[0]);
		printf("Stuff you can append to the end:\n");
		printf("--font\t<ttf filename>\n--continue\tAllows to continue from srt\n--plain\t<plain sub file>\n--readonly\n\nNote that you must pass plain subs or continue from srt.\n");
		return 1;
	}
	// Need to parse args here so we can use local variables
	char _doWriteSrt=1;
	char* _overrideFont=NULL;
	char _isContinue=0;
	char* _srtOut=NULL;
	char* _soundIn=NULL;
	_soundIn=argv[1];
	_srtOut=argv[2];
	int i;
	for (i=3;i<argc;++i){
		if (strcmp(argv[i],"--font")==0){
			_overrideFont = argv[++i];
		}else if (strcmp(argv[i],"--continue")==0){
			_isContinue=1;
		}else if (strcmp(argv[i],"--plain")==0){
			plainSubsFilename=argv[++i];
		}else if (strcmp(argv[i],"--readonly")==0){
			_doWriteSrt=0;
			printf("Will not write srt\n");
		}
	}
	if (!(plainSubsFilename!=NULL || _isContinue)){
		printf("Need plain subs or continue from srt\n");
		return 1;
	}
	if (init(argc,argv,_overrideFont)) {
		return 1;
	}

	showMessageEasy("Loading audio...");
	
	// init audio
	SNDFILE* infile = NULL;
	SF_INFO	_audioInfo;
	memset (&_audioInfo, 0, sizeof (_audioInfo));
	if ((infile = sf_open (_soundIn, SFM_READ, &_audioInfo)) == NULL) {
		printf ("Not able to open input file %s.\n", _soundIn);
		printf("%s\n",sf_strerror (NULL));
		return 1;
	}
	totalSamples=_audioInfo.frames;
	sampleRate=_audioInfo.samplerate;
	totalChannels=_audioInfo.channels;
	// info
	printf("# Converted from file %s.\n", _soundIn);
	printf("# Channels %d, Sample rate %d\n", _audioInfo.channels, _audioInfo.samplerate);

	// Read entire file as pcm info big boy buffer
	pcmData = malloc(sizeof(float*)*_audioInfo.channels);
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

	if (_isContinue){
		printf("Continue from srt\n");
		// init sentences and raw subs from srt
		loadSrt(_srtOut);
		rawSkipped = calloc(1,sizeof(char)*numRawSubs);
	}else{
		loadRawsubs(plainSubsFilename);
		timings = findSentences(0,3);
		if (timings==NULL){
			printf("Failed to find sentences.\n");
			exit(1);
		}
	}

	unpauseMusic();

	/////////////////////////////

	char _modDown=0;
	char _running=1;
	while(_running) {
		long _currentSample = getCurrentSample();
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
						if (e.key.keysym.sym==boundKeys[i] && boundKeyModStatus[i]==_modDown){
							boundFuncs[i](_currentSample);
							break;
						}
					}
					if (i==totalKeysBound){
						printf("invalid key %s, mod:%d\n",SDL_GetKeyName(e.key.keysym.sym),_modDown);
					}
				}
			}else if (e.type==SDL_KEYUP){
				if (e.key.keysym.sym==MODKEY){
					_modDown=0;
				}
			}else if (e.window.event==SDL_WINDOWEVENT_RESIZED){
				if (sizeActionHistory!=0 && _currentSample>100){
					setLastAction("Window resize, messed up history");
				}
				int _newHeight;
				SDL_GetWindowSize(mainWindow,NULL,&_newHeight);
				resizeActionHistory(_newHeight);
			}
		}

		// Explicitly placed after key events so it can account for new sentences being made
		int _currentIndex;
		struct nList* _currentEntry = getCurrentSentence(_currentSample,&_currentIndex);
		struct sentence* _currentSentence = _currentEntry->data;

		if (addingSubIndex!=-1){
			if (addingSubIndex==_currentIndex){
				_currentSentence->endSample=_currentSample;
			}else{ // If we've hit the next sentence
				CASTDATA(getBeforeCurrentSentence(_currentSample,NULL))->endSample=_currentSentence->startSample;
				addingSubIndex=-1;
			}
		}

		if (_modDown){
			setDrawColor(MODBACKGROUNDCOLOR);
		}else{
			setDrawColor(BACKGROUNDCOLOR);
		}

		int _maxWidth;
		int _maxHeight;
		SDL_GetWindowSize(mainWindow,&_maxWidth,&_maxHeight);
		SDL_RenderClear(mainWindowRenderer);

		
		drawSentences(_maxWidth,_currentSample);

		// Draw indicator traingle
		int _triangleWidth;
		uint32_t _triangleColor = SDL_GetAudioStatus()==SDL_AUDIO_PLAYING ? MARKERCOLOR : PAUSEDMARKERCOLOR;
		for (_triangleWidth=1;_triangleWidth<=INDICATORWIDTH;_triangleWidth+=2){
			drawRectangle(_maxWidth/2-_triangleWidth/2,BARHEIGHT+(_triangleWidth/2),_triangleWidth,1,_triangleColor);
		}

		// Draw subs
		int _currentY = BARHEIGHT+(INDICATORWIDTH/2);
		_currentIndex = correctSentenceIndex(_currentIndex);
		int i=_currentIndex;
		// Center
		i-=(_maxHeight-_currentY-BOTTOMINFOLINES*fontHeight)/2/fontHeight;
		if (i<0){
			i=0;
			_currentY=roundMultiple((_maxHeight-_currentY)/2,fontHeight)+_currentY-_currentIndex*fontHeight;
		}
		for (;_currentY<_maxHeight-fontHeight*(BOTTOMINFOLINES+1) && i<numRawSubs;++i){
			if (i==_currentIndex){
				FC_DrawColor(goodFont, mainWindowRenderer, (_currentSample<=_currentSentence->endSample ? INDENTPIXELS : 0), _currentY, ACTIVESUBCOLOR, rawSubs[i]);
			}else{
				FC_Draw(goodFont, mainWindowRenderer, 0, _currentY, rawSubs[i]);
			}
			if (rawSkipped[i]){
				drawRectangle(0,_currentY+(fontHeight-(fontHeight/(double)CROSSOUTHDENOM))/2,FC_GetWidth(goodFont,rawSubs[i]),fontHeight/CROSSOUTHDENOM,CROSSOUTCOLOR);
			}
			_currentY+=fontHeight;
		}

		int _actionIndex=nextActionIndex-1;
		for (_currentY=_maxHeight-fontHeight;_currentY>BARHEIGHT+INDICATORWIDTH/2;_currentY-=fontHeight){
			_actionIndex = wrapNum(_actionIndex,0,sizeActionHistory-1);
			FC_DrawColor(goodFont, mainWindowRenderer, _maxWidth-FC_GetWidth(goodFont,actionHistory[_actionIndex]), _currentY, ACTIONHISTORYCOLOR, actionHistory[_actionIndex]);
			--_actionIndex;
		}

		long _numMilliseconds = samplesToTime(_currentSample);
		FC_Draw(goodFont, mainWindowRenderer, 0, _maxHeight-fontHeight,TIMEFORMAT,TIMEARGS(_numMilliseconds));
		SDL_RenderPresent(mainWindowRenderer);
	}

	if (_doWriteSrt){
		printf("Writing srt...\n");
		FILE* _outfp = fopen(_srtOut,"wb");
		struct nList* _current = timings;
		int _currentIndex=1;
		for (i=0;i<numRawSubs;++i){
			if (!rawSkipped[i]){
				double _startTime;
				double _endTime;
				if (_current==NULL){ // If we're out of sentences, write -1 as an indication of that
					//printf("Unexpected end of sentences. Was on raw sub number %d\n",i);
					_startTime=-1;
					_endTime=-1;
				}else{
					_startTime = samplesToTime(CASTDATA(_current)->startSample);
					_endTime = samplesToTime(CASTDATA(_current)->endSample);
					_current = _current->nextEntry;
				}
				writeSingleSrt(_currentIndex++,_startTime,_endTime,rawSubs[i],_outfp);
			}
		}
		fclose(_outfp);
	}


	// whatever the opposite of init is
	FC_FreeFont(goodFont);
	pauseMusic();
	pthread_mutex_destroy(&audioPosLock);

	return 0;
}
