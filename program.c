// do not steel
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <float.h>

#include <sndfile.h>

#include <SDL2/SDL.h>

#define	BLOCK_SIZE 4096

void my_audio_callback(void *userdata, Uint8 *stream, int len) {
	
	if (audio_len ==0)
		return;
	
	len = ( len > audio_len ? audio_len : len );
	//SDL_memcpy (stream, audio_pos, len); 					// simply copy from one buffer into the other
	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);// mix from one buffer into another
	
	audio_pos += len;
	audio_len -= len;
}

int main (int argc, char * argv []){	
	// init
	char* infilename = "./audiodump.wav";
	SNDFILE* infile = NULL;
	SF_INFO	sfinfo;
	memset (&sfinfo, 0, sizeof (sfinfo));
	if ((infile = sf_open (infilename, SFM_READ, &sfinfo)) == NULL){
		printf ("Not able to open input file %s.\n", infilename);
		printf("%s\n",sf_strerror (NULL));
		return 1;
	}
	// info
	printf("# Converted from file %s.\n", infilename);
	printf("# Channels %d, Sample rate %d\n", sfinfo.channels, sfinfo.samplerate);


	float buf [BLOCK_SIZE];
	sf_count_t frames;
	int k, m, readcount;
	frames = BLOCK_SIZE / sfinfo.channels;
	while ((readcount = sf_readf_float (infile, buf, frames)) > 0)
	{	for (k = 0; k < readcount; k++) // read wave
		{	for (m = 0; m < sfinfo.channels; m++){
				//if (full_precision)
				//	
				//else
				//	fprintf (outfile, " % 12.10f", buf [k * channels + m]);
				//fprintf (outfile, "\n");
			}
		}
	}

	// close
	sf_close (infile);
	return 0;
}