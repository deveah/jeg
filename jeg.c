
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include <sndfile.h>
#include <portaudio.h>

#define OUTPUT_FILE "wub.wav"

#define SAMPLE_RATE 44100
#define BUFFER_SIZE 512
#define CHANNELS 1
#define DEFAULT_BPM 200
#define BEATS_PER_MEASURE 8

#define A4_FREQ 440.0f
#define BASE_NOTE 28

#define RANDOM_GLITCH_CHANCE 3
#define RANDOM_GLITCH_RETURN_CHANCE 3
#define RANDOM_NOTE_CHANGE_CHANCE 20
#define RANDOM_SNARE_CHANCE 4
#define RANDOM_MODULATION 5
#define RANDOM_SNARE_SILENCE_CHANCE 3
#define RANDOM_OCTAVE_JUMP_CHANCE 3

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

float midi_to_hz( int note )
{
	return ( A4_FREQ * powf( 2.0f, (float)(note-69) / 12.0f ) );
}

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
		dest[i] = _amp * sin( 2.0 * M_PI *_freq * (double)i / (double)SAMPLE_RATE );
		f = (noisefilter) * f + (1.0f-noisefilter) * _noise * ( (float)( rand()%100 ) / 100.0f - 0.5f );
		dest[i] += f;

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

		if( beat == 0 )
		{
			if( beat_count % 4 == 0 )
				bass_lfofreq = ((float)DEFAULT_BPM/60.0f) * flt_freq[ rand()%4 ];

			if( rand()%RANDOM_NOTE_CHANGE_CHANCE == 0 )
			{
				bass_freq = midi_to_hz( BASE_NOTE + minor_scale[ rand()%7 ] + ((rand()%RANDOM_OCTAVE_JUMP_CHANCE==0)?12:0) );
			}

			if( ( beat_count % 4 == 0 ) && ( rand()%RANDOM_GLITCH_CHANCE == 0 ) )
			{
				bass_fmindex = (float)( rand()%999 + 1 );
			}

			if( ( beat_count % 4 == 0 ) && ( rand()%RANDOM_GLITCH_RETURN_CHANCE == 0 ) )
			{
				bass_fmindex = 0.0f;
			}
		
			if( rand()%RANDOM_MODULATION == 0 )
			{
				bass_fmmod = (float)( rand()%3 + 1 );
			}

			if( beat_count % 16 == 0 )
				bd_time = 0;

			if( beat_count % 16 == 8 )
			{
				if( rand()%RANDOM_SNARE_SILENCE_CHANCE == 0 )
					bass_vol = 0.0f;
				sd_time = 0;
			}

			if( ( beat_count % 16 == 10 ) && ( rand()%RANDOM_SNARE_CHANCE == 0 ) )
				sd_time = 0;

			if( beat_count % 16 == 12 )
				bass_vol = 1.0f;

			if( beat_count % 16 == 6 )
				bd_time = 0;

			if( beat_count % 2 == 0 )
				hh_time = 0;

			beat_count++;
		}

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

		bass_lfoval = sin( 2.0 * M_PI * bass_lfofreq * (float)global_frame / (float)SAMPLE_RATE );
		bass_lfoval = bass_lfoval/100.0f + 0.99f;

		mod = sin( 2.0 * M_PI * bass_fmmod * bass_freq * (float)global_frame / (float)SAMPLE_RATE );
		b = sgn( sin( 2.0 * M_PI * ( bass_freq * (float)global_frame / (float)SAMPLE_RATE ) + mod * bass_fmindex ) );

		bass_z = bass_lfoval * bass_z + ( 1.0f - bass_lfoval ) * b;

		v = 0.3f;

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

int main( void )
{
	PaStreamParameters outputParameters;
	PaError pa_err;
	SF_INFO sfinfo;

	gen_default_drums();

	pa_err = Pa_Initialize();
	outputParameters.device = Pa_GetDefaultOutputDevice();
	outputParameters.channelCount = CHANNELS;
	outputParameters.sampleFormat = paFloat32;
	outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
	outputParameters.hostApiSpecificStreamInfo = NULL;
	pa_err = Pa_OpenStream( &stream, NULL, &outputParameters, SAMPLE_RATE, BUFFER_SIZE, paClipOff, audio_callback, NULL );

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


