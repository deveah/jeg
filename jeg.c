
/*
 *	jeg.c
 *	Procedural Dubstep Generator
 *	(c) 2012-2014 Vlad Dumitru, Marius Petcu, Claudiu Tașcă
 *	Released under Public Domain.
 *
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <signal.h>

#include <sndfile.h>
#include <portaudio.h>

#define OUTPUT_FILE "wub.wav"

/*	stream parameters */
#define SAMPLE_RATE						44100
#define BUFFER_SIZE						512
#define CHANNELS						1

/*	basic song parameters */
#define DEFAULT_BPM						140
#define BEATS_PER_MEASURE				8
#define A4_FREQ							440.0f
#define BASE_NOTE						28

/*	song randomness properties */
#define RANDOM_GLITCH_CHANCE			3
#define RANDOM_GLITCH_RETURN_CHANCE		3
#define RANDOM_NOTE_CHANGE_CHANCE		20
#define RANDOM_SNARE_CHANCE				4
#define RANDOM_MODULATION				5
#define RANDOM_SNARE_SILENCE_CHANCE		3
#define RANDOM_OCTAVE_JUMP_CHANCE		3

#define sgn(x) (((x)<0)?-1.0f:1.0f)

PaStream *stream;
SNDFILE *wave_output;

unsigned int global_frame = 0;
unsigned int beat_count = 0;

int minor_scale[] = {
	0, 2, 3, 5, 7, 9, 10, 12
};

/* drum parameters */
int bd_time = -1, sd_time = -1, hh_time = -1;
float	bd[SAMPLE_RATE],
		sd[SAMPLE_RATE],
		hh[SAMPLE_RATE];

/* bass parameters */
float	bass_vol = 0.0f;
float	bass_freq, bass_lfoval, bass_lfofreq,
		bass_fmmod = 2.0, bass_fmindex;
float	bass_z; /* filter internal */

/* filter frequencies */
float flt_freq[] = {
	2.0f,
	4.0f,
	1.0f,
	6.0f
};

/*	this function converts a MIDI note number into its corresponding frequency,
	represented in Hertz */
float midi_to_hz( int note )
{
	return ( A4_FREQ * powf( 2.0f, (float)(note-69) / 12.0f ) );
}

/*	this function generates a drum sound by overlapping a variable amplitude
	and frequency sine wave with lowpass-filtered white noise */
int gen_drum(	float *dest, int nframes,
				float amp, float decay,
				float freq, float freqdecay,
				float noise, float noisedecay, float noisefilter )
{
	int i;
	float _noise = noise, _amp = amp, _decay = decay, _freq = freq;
	float f = 0;

	for( i = 0; i < nframes; i++ )
	{
		/*	the tonal component */
		dest[i] = _amp * sin( 2.0 * M_PI *_freq * (double)i /
			(double)SAMPLE_RATE );
		/*	the noise component */
		f = (noisefilter) * f + (1.0f-noisefilter) * _noise * (
			(float)( rand()%100 ) / 100.0f - 0.5f );
		dest[i] += f;

		/*	advancing the envelopes */
		_noise *= noisedecay;
		_amp *= decay;
		_freq -= freqdecay;

		if( _noise < .0 ) noise = .0;
		if( _amp < .0 ) amp = .0;
		if( _freq < .0 ) freq = .0;
		if( dest[i] < -1.0 ) dest[i] = -1.0;
		if( dest[i] >  1.0 ) dest[i] =  1.0;
	}

	return 0;
}

/*	main function that makes the dubstep sound */
static int audio_callback(	const void *inputBuffer, void *outputBuffer,
							unsigned long framesPerBuffer,
							const PaStreamCallbackTimeInfo *timeInfo,
							PaStreamCallbackFlags statusFlags,
							void *userData )
{
	int i;
	float *out = (float*)outputBuffer;
	float b, d, f, mod;
	int beat;

	float v;

	for( i = 0; i < framesPerBuffer; i++ )
	{
		d = 0;

		beat = global_frame % ( SAMPLE_RATE / ( DEFAULT_BPM / 60 ) / 4 );

		/*	if the stream has hit a beat */
		if( beat == 0 )
		{
			/*	4 beats is half a measure; alter the filter's LFO */
			if( beat_count % 4 == 0 )
				bass_lfofreq = ((float)DEFAULT_BPM/60.0f) *
					flt_freq[ rand()%4 ];

			/*	change the note by choosing another from the provided scale */
			if( rand()%RANDOM_NOTE_CHANGE_CHANCE == 0 )
			{
				bass_freq = midi_to_hz( BASE_NOTE + minor_scale[ rand()%7 ] +
					((rand()%RANDOM_OCTAVE_JUMP_CHANCE==0)?12:0) );
			}

			/*	alter the FM index (modulator amplitude), to add overtones to
				the bassline, thus creating the 'glitched' sound */
			if( ( beat_count % 4 == 0 ) && ( rand()%RANDOM_GLITCH_CHANCE == 0 ) )
			{
				bass_fmindex = (float)( rand()%999 + 1 );
			}

			/*	reset the FM index to return to the normal square bass sound */
			if( ( beat_count % 4 == 0 ) && ( rand()%RANDOM_GLITCH_RETURN_CHANCE == 0 ) )
			{
				bass_fmindex = 0.0f;
			}
		
			/*	alter the FM mod (modulator frequency multiplier) */
			if( rand()%RANDOM_MODULATION == 0 )
			{
				bass_fmmod = (float)( rand()%3 + 1 );
			}

			/*	a bassdrum hits every two measures */
			if( beat_count % 16 == 0 )
				bd_time = 0;

			/*	sometimes, the snare won't trigger */
			if( beat_count % 16 == 8 )
			{
				if( rand()%RANDOM_SNARE_SILENCE_CHANCE == 0 )
					bass_vol = 0.0f;
				sd_time = 0;
			}

			/*	trigger a snare drum on the 10th beat of two measures combined */
			if( ( beat_count % 16 == 10 ) && ( rand()%RANDOM_SNARE_CHANCE == 0 ) )
				sd_time = 0;

			/*	reset the volume of the bassline, if it has been previously
				diminished, to create the impression of a compressor */
			if( beat_count % 16 == 12 )
				bass_vol = 1.0f;

			/*	trigger a bass drum */
			if( beat_count % 16 == 6 )
				bd_time = 0;

			/*	trigger a hihat */
			if( beat_count % 2 == 0 )
				hh_time = 0;

			beat_count++;
		}

		/*	advance sample counters and mix the drum channel */
		if( bd_time >= 0 )
		{
			d = bd[bd_time++];
			if( bd_time > SAMPLE_RATE )
				bd_time = -1;
		}

		if( sd_time >= 0 )
		{
			d = d * 0.8f + sd[sd_time++];
			if( sd_time > SAMPLE_RATE )
				sd_time = -1;
		}

		if( hh_time >= 0 )
		{
			d = d * 0.8f + hh[hh_time++] * 0.05f;
			if( hh_time > SAMPLE_RATE )
				hh_time = -1;
		}

		/*	compute the LFO value */
		bass_lfoval = sin( 2.0 * M_PI * bass_lfofreq * (float)global_frame /
			(float)SAMPLE_RATE );
		/*	the LFO value is in range 0.00 .. 0.01 */
		bass_lfoval = bass_lfoval/100.0f + 0.99f;

		/*	compute the modulator */
		mod = sin( 2.0 * M_PI * bass_fmmod * bass_freq * (float)global_frame /
			(float)SAMPLE_RATE );
		/*	compute the bassline */
		b = sgn( sin( 2.0 * M_PI * ( bass_freq * (float)global_frame /
			(float)SAMPLE_RATE ) + mod * bass_fmindex ) );

		/*	process the bassline through a lowpass filter */
		bass_z = bass_lfoval * bass_z + ( 1.0f - bass_lfoval ) * b;

		v = 0.3f;

		/*	final mix of the two tracks */
		out[i] = bass_vol * bass_z * v + d * ( 1.0f-v );


		if( out[i] > 1.0f ) out[i] = 1.0f;
		if( out[i] < -1.0f ) out[i] = -1.0f;

		global_frame++;
	}

	sf_write_float( wave_output, out, framesPerBuffer );

	return paContinue;
}

void gen_default_drums( void )
{
	gen_drum( bd, SAMPLE_RATE, 2.0f, 0.9995f, 35.0f, 0.001f, 5.0f, 0.99f, 0.93f );
	gen_drum( sd, SAMPLE_RATE, 2.0f, 0.9995f, 70.0f, 0.002f, 1.0f, 0.9998f, 0.5f );
	gen_drum( hh, SAMPLE_RATE, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.9995f, 0.0f );
}

void interrupt( int sig )
{
	(void) sig;
	PaError pa_err;

	sf_close( wave_output );
	pa_err = Pa_StopStream( stream );
	pa_err = Pa_CloseStream( stream );
	Pa_Terminate();

	printf( "Successfully terminated.\n" );
	exit( 0 );
}

int main( void )
{
	PaStreamParameters outputParameters;
	PaError pa_err;
	SF_INFO sfinfo;

	signal( SIGINT, interrupt );

	gen_default_drums();

	pa_err = Pa_Initialize();
	outputParameters.device = Pa_GetDefaultOutputDevice();
	outputParameters.channelCount = CHANNELS;
	outputParameters.sampleFormat = paFloat32;
	outputParameters.suggestedLatency =
		Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
	outputParameters.hostApiSpecificStreamInfo = NULL;
	pa_err = Pa_OpenStream( &stream, NULL, &outputParameters, SAMPLE_RATE,
		BUFFER_SIZE, paClipOff, audio_callback, NULL );

	sfinfo.samplerate = SAMPLE_RATE;
	sfinfo.channels = CHANNELS;
	sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	wave_output = sf_open( OUTPUT_FILE, SFM_WRITE, &sfinfo );

	srand( time( 0 ) );

	bass_z = 0.0f;
	bass_freq = midi_to_hz( BASE_NOTE );

	pa_err = Pa_StartStream( stream );
	
	while( 1 )
		Pa_Sleep( 1000 );

	sf_close( wave_output );
	pa_err = Pa_StopStream( stream );
	pa_err = Pa_CloseStream( stream );
	Pa_Terminate();

	return 0;
}

