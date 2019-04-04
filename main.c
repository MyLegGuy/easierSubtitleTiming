// do not steel
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

#include <sndfile.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL_FontCache.h>

#include "goodLinkedList.h"
#include "stack.h"

// when looking for default font. Must end in a slash
#define DEFAULTFONTDIR "/usr/share/fonts/TTF/"
#define GETFONTCOMMAND "fc-match"

#define MAKERGBA(r,g,b,a) ((((a)&0xFF)<<24) | (((b)&0xFF)<<16) | (((g)&0xFF)<<8) | (((r)&0xFF)<<0))
#define PLAY_SAMPLES  4096
#define READBLOCKSIZE 8192
// in PCM float
#define SILENCE_THRESHOLD .01
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

nList* timings;
nStack* undoStack;

int numRawSubs=0;
char** rawSubs;
char* rawSkipped;

int totalKeysBound=0;
SDL_Keycode* boundKeys;
char* boundKeyModStatus;
keyFunc* boundFuncs;
char* lastAction=NULL;

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
	}else{
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
long getCurrentSample() {
	//long _fake = ((audioTimeOther+(SDL_GetTicks()-audioTimeReference))/(double)1000)*sampleRate;
	//printf("oh  apparent:%ld actual:%ld actual-fake:%ld\n",_fake,pcmPlayPos,pcmPlayPos-_fake);
	return (((SDL_GetTicks()-audioTimeReference)+audioTimeOther)/(double)1000)*sampleRate;
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
	free(lastAction);
	lastAction = strdup(_actionName);
	printf("%s\n",lastAction);
}

nList* getCurrentSentence(long _currentSample, int* _retIndex){
	nList* _currentEntry = timings;
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
	nList* _currentEntry = getCurrentSentence(_currentSample,NULL);
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

void lowStitchForwards(nList* _startHere, char* _message){
	// Undo data
	long _oldEnd = CASTDATA(_startHere)->endSample;
	long _oldStart = CASTDATA(_startHere->nextEntry)->startSample;

	CASTDATA(_startHere)->endSample=CASTDATA(_startHere->nextEntry)->endSample;
	nList* _tempHold = _startHere->nextEntry->nextEntry;
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

/////////////////////////////

// passed is longHolder2 with item1 being the end of the the first one and item2 being the start of the second one
void undoStitch(long _currentSample, void* _passedData){
	nList* _currentSentence = getCurrentSentence(_currentSample,NULL);
	nList* _undoneDeleted = malloc(sizeof(nList));
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
	nList* _currentEntry = getCurrentSentence(_currentSample,NULL);
	nList* _freeThis = _currentEntry->nextEntry;
	CASTDATA(_currentEntry)->endSample = CASTDATA(_freeThis)->endSample;
	_currentEntry->nextEntry = _freeThis->nextEntry;
	free(_freeThis->data);
	free(_freeThis);
}

/////////////////////////////

void keyStitchForward(long _currentSample){
	nList* _currentSentence = getCurrentSentence(_currentSample,NULL);
	if (_currentSentence->nextEntry==NULL){
		return;
	}
	lowStitchForwards(_currentSentence,"stitch forwards");
}
void keyStitchBackward(long _currentSample){
	int _currentIndex = getCurrentSentenceIndex(_currentSample);
	if (_currentIndex==0){
		return;
	}
	lowStitchForwards(getnList(timings,_currentIndex-1),"stitch backwards");
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
	int _currentIndex = getCurrentSentenceIndex(_currentSample);
	if (_currentIndex==0){
		return;
	}
	seekAudioSamplesExact(CASTDATA(getnList(timings, _currentIndex-1))->startSample);
}
void keySeekForwardSentence(long _currentSample){
	nList* _possibleNext = getCurrentSentence(_currentSample,NULL)->nextEntry;
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

	char* _messageBuff = malloc(strlen("Chopped: \"\"")+strlen(rawSubs[_currentIndex])+1);
	strcpy(_messageBuff,"Chopped: \"");
	strcat(_messageBuff,rawSubs[_currentIndex]);
	strcat(_messageBuff,"\"");
	setLastAction(_messageBuff);
	int* _dataPointer = malloc(sizeof(int));
	*_dataPointer=_currentIndex;
	addStack(&undoStack,makeUndoEntry(_messageBuff,undoSkip,_dataPointer,_currentSample,1));
}
void keyChop(long _currentSample){
	nList* _currentSentence = getCurrentSentence(_currentSample,NULL);
	if (_currentSample>=CASTDATA(_currentSentence)->endSample){
		setLastAction("Can't chop here, we've gone too far");
		return;
	}
	nList* _newEntry = malloc(sizeof(nList));
	_newEntry->data = malloc(sizeof(struct sentence));
	CASTDATA(_newEntry)->startSample=_currentSample+1;
	CASTDATA(_newEntry)->endSample=CASTDATA(_currentSentence)->endSample;
	CASTDATA(_currentSentence)->endSample=_currentSample;
	nList* _temp = _currentSentence->nextEntry;
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
		setLastAction("Unknown SDL_GetAudioStatus");
	}
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

void loadRawsubs(char* filename){
	FILE* fp = fopen(filename,"rb");
	size_t _lineSize=0;
	char* _lastLine=NULL;
	numRawSubs=0;
	while (getline(&_lastLine,&_lineSize,fp)!=-1){
		_lineSize=0;
		numRawSubs++;
		rawSubs = realloc(rawSubs,sizeof(char*)*(numRawSubs));
		removeNewline(_lastLine);
		rawSubs[numRawSubs-1]=_lastLine;
		_lastLine=NULL;
	}
	fclose(fp);
	rawSkipped = calloc(1,sizeof(char)*numRawSubs);
}

char init(int argc, char** argv) {
	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) < 0) {
		printf("fail open sdl");
		return 1;
	}
	mainWindow = SDL_CreateWindow( "easierTiming", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN); //SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
	mainWindowRenderer = SDL_CreateRenderer( mainWindow, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

	goodFont = FC_CreateFont();
	char* _fontFilename = getFontFilename();
	if (_fontFilename==NULL){
		printf("Could not find font filename. Please pass one using the --font option. See --help for more info.\n");
		return 1;
	}
	FC_LoadFont(goodFont, mainWindowRenderer, _fontFilename, FONTSIZE, FC_MakeColor(255,255,255,255), TTF_STYLE_NORMAL);
	free(_fontFilename);
	fontHeight = FC_GetLineHeight(goodFont);

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

	setLastAction("Welcome");
	return 0;
}

int main (int argc, char** argv) {
	// init
	if (init(argc,argv)) {
		return 1;
	}
	if (pthread_mutex_init(&audioPosLock,NULL)!=0) {
		printf("mutex init failed");
		return 1;
	}

	loadRawsubs("./1.fakesubs");
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
		int _currentIndex;
		struct sentence* _currentSentence = getCurrentSentence(_currentSample,&_currentIndex)->data;
		_currentIndex = correctSentenceIndex(_currentIndex);
		int i=_currentIndex;
		// Center
		i-=(_maxHeight-_currentY)/2/fontHeight;
		if (i<0){
			i=0;
			_currentY=roundMultiple((_maxHeight-_currentY)/2,fontHeight)+_currentY-_currentIndex*fontHeight;
		}
		for (;_currentY<_maxHeight-fontHeight && i<numRawSubs;++i){
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

		SDL_RenderPresent(mainWindowRenderer);
	}

	// whatever the opposite of init is
	FC_FreeFont(goodFont);
	pauseMusic();
	pthread_mutex_destroy(&audioPosLock);

	return 0;
}
