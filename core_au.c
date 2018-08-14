/* radare - MIT - Copyright 2018 - pancake */

#if 0
gcc -o core_au.so -fPIC `pkg-config --cflags --libs r_core` core_test.c -shared
mkdir -p ~/.config/radare2/plugins
mv core_au.so ~/.config/radare2/plugins
#endif

#include <r_types.h>
#include <r_lib.h>
#include <r_cmd.h>
#include <r_core.h>
#include <string.h>
#include "ao.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include "notes.c"

#undef R_API
#define R_API static
#undef R_IPI
#define R_IPI static

#define WAVETYPES 10
#define PRINT_MODES 6
#define WAVECMD "sctpPn-idzZ"

#define WAVERATE 22050
// #define WAVERATE 44100
// SID is 16bit, 8bit sounds too much like PDP
#define WAVEBITS 16

static int waveType = 0;
static int waveFreq = 500;
static int cycleSize = 220;
static int toneSize = 4096; // 0x1000
static int printMode = 0;
static bool zoomMode = false;
static int zoomLevel = 1;
static bool cursorMode = false;
static int cursorPos = 0;
static int animateMode = 0;
static int aBlocksize = 1024*8;
static int keyboard_offset = 0;

#define captureBlocksize() int obs = core->blocksize; r_core_block_size(core, aBlocksize)
#define restoreBlocksize() r_core_block_size (core, obs)

enum {
	FORM_SIN,      // .''.''.
	FORM_COS,      // '..'..'
	FORM_SAW,      // /|/|/|/
	FORM_ANTISAW,  // |\|\|\|
	FORM_PULSE,    // '_'_'_'
	FORM_VPULSE,   // '__'__'
	FORM_NOISE,    // \:./|.:
	FORM_TRIANGLE, // /\/\/\/
	FORM_SILENCE,  // ______
	FORM_INC,      // _..--''
	FORM_DEC,      // ''--.._
};

enum {
	FILTER_INVERT,    // 1 -> 0
	FILTER_ATTACK,    // ____.'
	FILTER_DECAY,     // -----.
	FILTER_VOLUME,    // _..oo#
	FILTER_INC,       // ++++++
	FILTER_DEC,       // ------
	FILTER_INTERLACE, //  A + B
	FILTER_SHIFT,     // >> >>
	FILTER_ROTATE,    // >>> >>>
	FILTER_MOD,       // %
	FILTER_XOR,       // ^
	FILTER_SIGN,      // + -> -
	FILTER_SCALE,     // *=
};

static short sample;
static ao_device *device = NULL;
static ao_sample_format format = {0};

void sample_filter(char *buf, int size, int filter, int value) {
	int i, j;
	int isize = size / 2;
	short *ibuf = (short*)buf;
	switch (filter) {
	case FILTER_ATTACK:
		value = isize / 100 * value;
		for (i = 0; i < isize; i++) {
			if (i < value) {
				int total_pending = value;
				int pending = value - i;
				float mul = pending / total_pending;
				ibuf[i] *= mul;
			}
		}
		break;
	case FILTER_DECAY:
		value = isize / 100 * value;
		for (i = 0; i < isize; i++) {
			if (i >= value) {
				float total_pending = isize - value;
				float pending = isize - i;
				float mul = pending / total_pending;
				ibuf[i] *= mul;
			}
		}
		break;
	case FILTER_XOR:
		for (i = 0; i + value< isize; i++) {
			// ibuf[i] ^= value; //ibuf[i + 1];
			ibuf[i] ^= ibuf[i + value];
		}
		break;
	case FILTER_INVERT:
		for (i = 0; i < isize; i++) {
			ibuf[i] = -ibuf[i];
		}
		break;
	case FILTER_DEC:
	case FILTER_INC:
		for (i = 0; i < isize; i++) {
			float pc = (float)i / (float)format.rate * 100;
			if (FILTER_INC == filter) {
				pc = 100 - pc;
			}
			pc /= value;
			pc += 1;
			if (!((int)i % (int)pc)) {
				ibuf[i] = 0xffff / 2;
			}
			else {
				//	sample = -max_sample;
			}
		}
		break;
	case FILTER_SHIFT:
		value = (isize / 100 * value);
		if (value > 0) {
			const int max = isize - value;
			for (i = 0; i < max; i++) {
				ibuf[i] = ibuf[i + value];
			}
			for (i = max; i < value; i++) {
				ibuf[i] = 0;
			}
		} else {
			/* TODO */
			const int max = isize - value;
			for (i = isize; i > value; i--) {
				ibuf[i] = ibuf[i - value];
			}
			for (i = 0; i < value; i++) {
				ibuf[i] = 0;
			}
		}
		break;
	case FILTER_SIGN:
		if (value > 0) {
			for (i = 0; i < isize; i++) {
				if (ibuf[i] > 0) {
					ibuf[i] = 0;
				}
			}
		} else {
			for (i = 0; i < isize; i++) {
				if (ibuf[i] < 0) {
					ibuf[i] = 0;
				}
			}
		}
		break;
	case FILTER_ROTATE:
		if (value > 0) {
			short *tmp = calloc (sizeof (short), value);
			if (tmp) {
				for (i = 0; i < value; i++) {
					tmp[i] = ibuf[i];
				}
				const int max = isize - value;
				for (i = 0; i < max; i++) {
					ibuf[i] = ibuf[i + value];
				}
				for (i = max; i < value; i++) {
					ibuf[i] = tmp[i - max];
				}
				free(tmp);
			}
		}
		else {
			/* TODO */
		}
		break;
	case FILTER_INTERLACE:
		if (value < 2) {
			value = 2;
		}
		for (i = 0; i < size / 2; i++) {
			if (!((i / value) % value)) {
				for (j = 0; j< value; j++) {
					ibuf[i] = 0;
				}
			}
		}
		break;
	case FILTER_SCALE:
		if (value < 100) {
			int j = value;
			for (i = 0; i + j < isize; i++) {
				ibuf[i] = ibuf[i + j];
				j++;
			}
			int base;
			for (base = i; i< isize; i++) {
				ibuf[i] = ibuf[i - base];
			}
		}
		else {
			// TODO
		}
		break;
	case FILTER_MOD:
		for (i = 0; i < isize; i++) {
			ibuf[i] = ibuf[i] / value * value;
		}
		break;
	case FILTER_VOLUME:
		for (i = 0; i < isize; i++) {
			ibuf[i] *= ((float)value / 100);
		}
		break;
	}
}

char *sample_new(float freq, int form, int *size) {
	int i;
	short sample; // float ?
	float wax_sample = format.bits == 16 ? 0xffff / 3 : 0xff / 3;
	float max_sample = format.bits == 16 ? 0xffff / 3 : 0xff / 3;
	float volume = 1; // 0.8;
	float pc;
	// int buf_size = format.bits / 8 * format.channels * format.rate;
	int buf_size = 16 / 8 * format.channels * format.rate;
	char *buffer = calloc (buf_size, sizeof (char));
	if (!buffer) {
		return NULL;
	}
// eprintf ("bufsz %d\n", buf_size);
	if (size) {
		*size = buf_size; // 22050 // huh
		// eprintf ("sz = %d\n", *size);
	}
	short *word = (short*)(buffer);
	int words = buf_size / sizeof (short);
	for (i = 0; i < words; i++) {
		// sample = (char)(max_sample * sin(2 * M_PI * freq * ((float)i / format.rate * 2)));
//		sample = (char)(max_sample * sin(i * (freq / format.rate))); // 2 * M_PI * freq * ((float)i / format.rate * 2)));
		// sample = (short) max_sample * sin (freq * (2 * M_PI) * i / format.rate);
// eprintf ("%d ", sample);

  // buffer[i] = sin(1000 * (2 * pi) * i / 44100);

		switch (form) {
		case FORM_SILENCE:
			sample = 0;
			break;
		case FORM_DEC:
		case FORM_INC:
			pc = (float)i / (float)format.rate * 100;
			if (form == FORM_INC) {
				pc = 100 - pc;
			}
			pc /= 11 ; // step -- should be parametrizable
			pc += 1;
			if (!((int)i % (int)pc)) {
				sample = max_sample;
			} else {
				sample = -max_sample;
			}
			break;
		case FORM_COS:
			sample = (int)(max_sample * cos (2 * M_PI * freq * ((float)i / format.rate * 2)));
			break;
		case FORM_SIN:
			sample = (short) max_sample * sin (freq * (2 * M_PI) * i / format.rate);
			break;
		case FORM_SAW:
			{
				int rate = 14000 / freq;
				sample = ((i % rate) * (max_sample * 2) / rate) - max_sample;
				sample = -sample;
				// printf ("%f\n", (float)sample);
			}
			break;
		case FORM_ANTISAW:
			{
				int rate = 14000 / freq;
				sample = ((i % rate) * (max_sample * 2) / rate) - max_sample;
				//sample = -sample;
				// printf ("%f\n", (float)sample);
			}
			break;
		case FORM_TRIANGLE:
			{
				if (freq < 1) {
					freq = 1;
				}
				int rate = (14000 / freq) * 2;
				sample = ((i % rate) * (max_sample * 2) / rate) - max_sample;
				if (sample > max_sample / 8) {
					sample = -sample;
				}
				sample *= 1.5;
				ut64 n = sample;
				n *= 2;
				sample = n - ST16_MAX;
				sample += 200;
				// sample *= 2; // interesting ascii art wave
				// XXX: half wave :?
			}
			break;
		case FORM_PULSE:
			sample = sample > 0? max_sample : -max_sample;
			break;
		case FORM_VPULSE:
			sample = sample > 0x7000? -max_sample : max_sample;
			break;
		case FORM_NOISE:
			sample = (rand() % (int)(max_sample * 2)) - max_sample;
			int s = (int)sample * (freq / 1000);
			if (s > 32766) {
				s = 32700;
			}
			if (s < -32766) {
				s = -32700;
			}
			sample = s;
			break;
		}
		//sample *= volume;
// printf ("SAMP %d\n", sample);
		/* left channel */
		word[i] = sample;
		// buffer[2 * i] = sample & 0xf;
		// buffer[2 * i + 1] = (sample >> 4) & 0xff;
		// buffer[(2 * i) + 1] = ((unsigned short)sample >> 8) & 0xff;
		// i++;
	}
	// sample_filter (buffer, buf_size, FILTER_SIGN, 1);
	return buffer;
}

static bool au_init(int rate, int bits, int channels, int endian) {
	ao_initialize ();

	int default_driver = ao_default_driver_id ();
	memset (&format, 0, sizeof (format));
	format.byte_format = endian? AO_FMT_BIG: AO_FMT_LITTLE;
	format.rate = rate;
	format.bits = bits;
	format.channels = channels;
	// format.rate = 11025;

	device = ao_open_live (default_driver, &format, NULL);
	if (!device) {
		fprintf (stderr, "core_au: Error opening audio device.\n");
		return false;
	}
	// seems like we need to feed it once to make it work
	if (0) {
		int len = 4096;
		char *silence = calloc (sizeof (short), len);
		ao_play (device, silence, len);
		free (silence);
	}
	return true;
}

static bool au_fini() {
	ao_close (device);
	device = NULL;
	ao_shutdown ();
	return true;
}

static void au_help(RCore *core) {
	eprintf ("Usage: auw[type] [args]\n");
	eprintf (" fill current block with wave\n");
	eprintf (" args: frequence\n");
	eprintf (" types:\n"
		" (s)in    .''.''.\n"
		" (c)os    '..'..'\n"
		" (z)aw    /|/|/|/\n"
		" (Z)aw    \\|\\|\\|\\\n"
		" (p)ulse  |_|'|_|\n"
		" (n)oise  /:./|.:\n"
		" (t)ri..  /\\/\\/\\/\n"
		" (-)silen _______\n"
		" (i)nc    _..--''\n"
		" (d)ec    ''--.._\n"
	);
}

static bool au_mix(RCore *core, const char *args) {
	ut64 narg = *args? r_num_math (core->num, args + 1): 0;
	float arg = narg;
	if (arg == 0) {
		eprintf("Usage: aum \n");
		return true;
	}
	const int bs = core->blocksize;
	eprintf ("[au] Mixing from 0x%"PFMT64x" to 0x%"PFMT64x"\n", narg, core->offset);
	short *dst = calloc (bs, 1);
	short *src = calloc (bs, 1);
	if (!src || !dst) {
		return false;
	}
	r_io_read_at (core->io, core->offset, dst, bs);
	r_io_read_at (core->io, narg, src, bs);
	for (int i = 0; i< core->blocksize / 2; i++) {
		dst[i] += src[i];
	}
	r_io_write_at (core->io, core->offset, dst, bs);
	return true;
}

static bool au_operate(RCore *core, const char *args) {
	ut64 narg = *args? r_num_math (core->num, args + 1): 0;
	float arg = narg;
	const int bs = core->blocksize;
	if (arg == 0) {
		au_help (core);
		return true;
	}
	short *dst = calloc (bs, 1);
	if (!dst) {
		return false;
	}
	r_io_read_at (core->io, core->offset, dst, bs);
	switch (*args) {
	case '=':
		for (int i = 0; i< core->blocksize / 2; i++) {
			dst[i] = arg; //src[i];
		}
		break;
	case '+':
		for (int i = 0; i< core->blocksize / 2; i++) {
			dst[i] += arg;
		}
		break;
	case '-':
		for (int i = 0; i< core->blocksize / 2; i++) {
			dst[i] -= arg;
		}
		break;
	case '/':
		for (int i = 0; i< core->blocksize / 2; i++) {
			dst[i] /= arg;
		}
		break;
	case '*':
		for (int i = 0; i< core->blocksize / 2; i++) {
			dst[i] *= arg;
		}
		break;
	}
	r_io_write_at (core->io, core->offset, dst, bs);
	return true;
}

static bool au_write(RCore *core, const char *args) {
	int size = 0;
	char *sample = NULL;
	ut64 narg = *args? r_num_math (core->num, args + 1): 0;
	float arg = narg;
	if (arg == 0) {
		au_help (core);
		return true;
	}
	switch (*args) {
	case '?':
		au_help (core);
		break;
	case 's':
		sample = sample_new (arg, FORM_SIN, &size);
		break;
	case 't':
		sample = sample_new (arg, FORM_TRIANGLE, &size);
		break;
	case 'i':
		sample = sample_new (arg, FORM_INC, &size);
		break;
	case 'd':
		sample = sample_new (arg, FORM_DEC, &size);
		break;
	case 'c':
		sample = sample_new (arg, FORM_COS, &size);
		break;
	case 'p':
		sample = sample_new (arg, FORM_PULSE, &size);
		break;
	case 'P':
		sample = sample_new (arg, FORM_VPULSE, &size);
		break;
	case 'n':
		sample = sample_new (arg, FORM_NOISE, &size);
		break;
	case 'z':
		sample = sample_new (arg, FORM_SAW, &size);
		break;
	case 'Z':
		sample = sample_new (arg, FORM_ANTISAW, &size);
		break;
	case '-':
		sample = sample_new (arg, FORM_SILENCE, &size);
		break;
	}
	if (size > 0) {
		int i;
		for (i = 0; i < core->blocksize ; i+= size) {
			int left = R_MIN (size, core->blocksize -i);
			r_io_write_at (core->io, core->offset + i, (const ut8*)sample, left);
		}
	}
	r_core_block_read (core);
	free (sample);
	return true;
}

const char *asciiWaveSin[4] = {
	".''.''.'",
	"''.''.''",
	"'.''.''.",
	".''.''.'",
};

const char *asciiWaveCos[4] = {
	"..'..'..",
	".'..'..'",
	"'..'..'.",
	"..'..'..",
};

const char *asciiWaveTriangle[4] = {
	"/\\/\\/\\/\\",
	"\\/\\/\\/\\/",
	"/\\/\\/\\/\\",
	"\\/\\/\\/\\/",
};

const char *asciiWavePulse[4] = {
	"_|'|_|'|",
	"|'|_|'|_",
	"'|_|'|_|",
	"|_|'|_|'",
};

const char *asciiWaveVPulse[4] = {
	"__'___'_",
	"_'___'__",
	"'___'___",
	"___'___'",
};

const char *asciiWaveNoise[4] = {
	"/:./|.:/",
	":./|.:/:",
	"./|.:/:.",
	"/|.:/:./"
};

const char *asciiWaveSilence[4] = {
	"________",
	"________",
	"________",
	"________",
};

const char *asciiWaveIncrement[4] = {
	"_..---''",
	"_..---''",
	"_..---''",
	"_..---''",
};

const char *asciiWaveDecrement[4] = {
	"''---.._",
	"''---.._",
	"''---.._",
	"''---.._",
};

const char *asciiWaveSaw[4] = {
	"/|/|/|/|",
	"|/|/|/|/",
	"/|/|/|/|",
	"|/|/|/|/",
};

const char *asciiWaveAntiSaw[4] = {
	"\\|\\|\\|\\|",
	"|\\|\\|\\|\\",
	"\\|\\|\\|\\|",
	"|\\|\\|\\|\\",
};

extern int print_piano (int off, int nth, int pressed);
static int lastKey = -1;

static bool printPiano(RCore *core) {
	int w = r_cons_get_size (NULL);
	print_piano (keyboard_offset, w / 3, lastKey);
}

static bool printWave(RCore *core) {
	short sample = 0;
	short *words = (short*)core->block;
	// TODO: shift with 'h' and 'l'
	int x , y, h;
	int i, nwords = core->blocksize / 2;
	int w = r_cons_get_size (&h) - 10;
	
#if 0
	h = 20;

	for (y = 0; y<h; y++) {
		for (x = 0; x<w; x++) {
			r_cons_printf ("#");
		}
		r_cons_printf ("\n");
	}
#endif
#if 1
	if (w < 1) w = 1;
	int j, min = 32768; //4200;
	int step = zoomMode? 2: 1;
	if (cursorMode) {
		for (i = 0; i<h; i++) {
			r_cons_gotoxy (cursorPos + 2, i);
			r_cons_printf ("|");
		}
	}
	int oy = 0;
	step *= zoomLevel;
	for (i = j = 0; i < nwords; i += step, j++) {
		int x = j + 2;
		int y = ((words[i]) + min) / 4096;
		if (y < 1) {
			y = 1;
		}
		if (x + 1 >= w) {
			break;
		}
		if (cursorMode && x == cursorPos + 2) {
			r_cons_gotoxy (x - 1, y + 3);
			r_cons_printf ("[#]");
			oy = y;
		} else if (cursorMode && x == cursorPos + 3 && y == oy) {
			// do nothing
		} else {
			r_cons_gotoxy (x, y + 3);
			r_cons_printf (Color_MAGENTA"*"Color_RESET);
		}
		// r_cons_printf ("%d %d - ", x, y);
	}
	r_cons_gotoxy (0, h - 4);
#endif
	return true;
}

static const char *asciin(int waveType) {
	int mod = waveType % WAVETYPES;
	switch (mod) {
	case 0: return "sinus";
	case 1: return "cos..";
	case 2: return "tri..";
	case 3: return "pulse";
	case 4: return "vpuls";
	case 5: return "noise";
	case 6: return "silen";
	case 7: return "incrm";
	case 8: return "decrm";
	case 9: return "saw..";
	case 10: return "ansaw";
	}
	return NULL;
}

static const char *asciis(int i) {
	int mod = waveType % WAVETYPES;
	i %= 4;
	switch (mod) {
	case 0: return asciiWaveSin[i];
	case 1: return asciiWaveCos[i];
	case 2: return asciiWaveTriangle[i];
	case 3: return asciiWavePulse[i];
	case 4: return asciiWaveVPulse[i];
	case 5: return asciiWaveNoise[i];
	case 6: return asciiWaveSilence[i];
	case 7: return asciiWaveIncrement[i];
	case 8: return asciiWaveDecrement[i];
	case 9: return asciiWaveSaw[i];
	case 10: return asciiWaveAntiSaw[i];
	}
	return NULL;
}

const char **aiis[WAVETYPES] = {
	asciiWaveSin,
	asciiWaveCos,
	asciiWaveTriangle,
	asciiWavePulse,
	asciiWaveNoise,
	asciiWaveSilence,
	asciiWaveIncrement,
	asciiWaveDecrement,
	asciiWaveSaw,
	asciiWaveAntiSaw,
};

typedef struct note_t {
	int type;
	int freq;
	int bsize; // TODO
	// TODO: add array of filters like volume, attack, decay, ...
} AUNote;

static AUNote notes[10];

static void au_note_playtone(RCore *core, int note) {
	int idx = keyboard_offset + note;
	// waveType = notes[note].type;
	float toneFreq = notes_freq (idx);
	char waveTypeChar = WAVECMD[waveType % WAVETYPES];
	r_core_cmdf (core, "auw%c %d", waveTypeChar, (int)toneFreq);
	r_core_cmd0 (core, "au.");
	// r_core_cmd0 (core, "au.&");
}

static void au_note_play(RCore *core, int note, bool keyboard_visible) {
	if (keyboard_visible) {
		au_note_playtone (core, note);
		return;
	}
	waveType = notes[note].type;
	waveFreq = notes[note].freq;
	
	char waveTypeChar = WAVECMD[waveType % WAVETYPES];
	r_core_cmdf (core, "auw%c %d", waveTypeChar, waveFreq);
	r_core_cmd0 (core, "au.");
}

static void au_note_set(RCore *core, int note) {
	notes[note].type = waveType;
	notes[note].freq = waveFreq;
}

static bool au_visual_help(RCore *core) {
	r_cons_clear00 ();
	r_cons_printf ("Usage: auv - visual mode for audio processing\n\n");
	r_cons_printf (" jk -> change wave type (sin, saw, ..)\n");
	r_cons_printf (" hl -> seek around the buffer\n");
	r_cons_printf (" HL -> seek faster around the buffer\n");
	r_cons_printf (" R  -> randomize color theme\n");
	r_cons_printf (" n  -> assign current freq+type into [0-9] key\n");
	r_cons_printf (" 0-9-> play and write the recorded note\n");
	r_cons_printf (" +- -> increment/decrement the frequency\n");
	r_cons_printf (" pP -> rotate print modes\n");
	r_cons_printf (" .  -> play current block\n");
	r_cons_printf (" i  -> insert current note in current offset\n");
	r_cons_printf (" :  -> type r2 command\n");

	r_cons_flush (); // use less here
	r_cons_readchar ();
	return true;
}

static void editCycle (RCore *core, int step) {
	// adjust wave (use [] to specify the width to be affected)
	short data = 0;
	r_io_read_at (core->io, core->offset + (cursorPos*2), &data, 2);
	data += step;
	r_io_write_at (core->io, core->offset + (cursorPos*2), &data, 2);

	char *cycle = malloc (cycleSize);
	if (!cycle) {
		return;
	}
	r_io_read_at (core->io, core->offset, cycle, cycleSize);
	int i;
	for (i = cycleSize; i<core->blocksize; i+= cycleSize) {
		r_io_write_at (core->io, core->offset + i, cycle, cycleSize);
	}
	free (cycle);
	r_core_block_read (core);
	r_core_cmd0 (core, "au.");
}

static bool au_visual(RCore *core) {
	r_cons_flush ();
	r_cons_print_clear ();
	r_cons_clear00 ();

	ut64 now, base = r_sys_now () / 1000 / 500;
	int otdiff = 0;
	bool keyboard_visible = false;
	while (true) {
		now = r_sys_now () / 1000 / 500;
		int tdiff = now - base;
		const char *wave = asciis (tdiff);
		const char *waveName = asciin (waveType);
		r_cons_clear00 ();
		if (tdiff + 1 > otdiff) {
		//	r_core_cmd (core, "au.", 0);
			if (animateMode) {
				r_core_cmd0 (core, "s+2");
			}
		}
		r_cons_printf ("[r2:auv] [0x%08"PFMT64x"] [%04x] %s %s freq %d block %d cursor %d cycle %d zoom %d\n",
			core->offset, tdiff, wave, waveName, waveFreq, toneSize, cursorPos, cycleSize, zoomLevel);
		int minus = 64;
		if (keyboard_visible) {
			int w = r_cons_get_size (NULL);
			print_piano (keyboard_offset, w / 3, lastKey);
			minus = 128;
		}
		switch (printMode % PRINT_MODES) {
		case 0:
			printWave (core);
			break;
		case 1:
			r_core_cmdf (core, "pze ($r*16)-(%d * 5)", minus);
			printWave (core);
			break;
		case 2:
		//	r_cons_gotoxy (0, 2);
			r_core_cmdf (core, "pze ($r*16)-(%d*3)", minus);
			break;
		case 3:
		//	r_cons_gotoxy (0, 2);
			r_core_cmdf (core, "pxd2 ($r*16)-(%d*3)", minus);
			printWave (core);
			break;
		case 4:
		//	r_cons_gotoxy (0, 2);
			r_core_cmdf (core, "pxd2 ($r*16)-(%d*3)", minus);
			break;
		case 5:
			{
			int zoom = 2;
			r_core_cmdf (core, "p=2 %d @!160", zoom);
			}
			break;
		}
		r_cons_flush ();
	//	r_cons_visual_flush ();
		int ch = r_cons_readchar_timeout (500);
		char waveTypeChar = WAVECMD[waveType % WAVETYPES];
		switch (ch) {
		case 'a':
			animateMode = !animateMode;
			break;
		case 'p':
			printMode++;
			printMode %= PRINT_MODES;
			break;
		case 'P':
			printMode--;
			printMode %= PRINT_MODES;
			break;
		case 'c':
			cursorMode = !cursorMode;
			break;
		case '0':
			au_note_play (core, 0, keyboard_visible);
			lastKey = 10;
			break;
		case '1':
			lastKey = 1;
			au_note_play (core, 1, keyboard_visible);
			break;
		case '2':
			lastKey = 2;
			au_note_play (core, 2, keyboard_visible);
			break;
		case '3':
			lastKey = 3;
			au_note_play (core, 3, keyboard_visible);
			break;
		case '4':
			lastKey = 4;
			au_note_play (core, 4, keyboard_visible);
			break;
		case '5':
			lastKey = 5;
			au_note_play (core, 5, keyboard_visible);
			break;
		case '6':
			lastKey = 6;
			au_note_play (core, 6, keyboard_visible);
			break;
		case '7':
			lastKey = 7;
			au_note_play (core, 7, keyboard_visible);
			break;
		case '8':
			lastKey = 8;
			au_note_play (core, 8, keyboard_visible);
			break;
		case '9':
			lastKey = 9;
			au_note_play (core, 9, keyboard_visible);
			break;
		case '=':
			if (keyboard_visible) {
				keyboard_visible = false;
			} else {
				keyboard_visible = true;
			}
			break;
		case 'n':
			r_cons_printf ("\nWhich note? (1 2 3 4 5 6 7 8 9 0) \n");
r_cons_flush();
			int ch = r_cons_readchar ();
			if (ch >= '0' && ch <= '9') {
				au_note_set (core, ch - '0');
			} else if (ch == 'q') {
				// foo
			} else {
				eprintf ("Invalid char\n");
				sleep(1);
			}
			break;
		case 'R':
			// honor real random themes: r_core_cmdf (core, "ecr");
			r_core_cmdf (core, "ecn");
			break;
		case 'f':
			{
				RCons *I = r_cons_singleton ();
				r_line_set_prompt ("(freq)> ");
				I->line->contents = r_str_newf ("%d", toneSize);
				const char *buf = r_line_readline ();
				waveFreq = r_num_math (core->num, buf);
				I->line->contents = NULL;
				r_core_cmdf (core, "auw%c %d", waveTypeChar, waveFreq);
				r_core_cmd0 (core, "au.");
			}
			break;
		case 'b':
			{
				RCons *I = r_cons_singleton ();
				r_line_set_prompt ("audio block size> ");
				I->line->contents = r_str_newf ("%d", toneSize);
				const char *buf = r_line_readline ();
				toneSize = r_num_math (core->num, buf);
				I->line->contents = NULL;
			}
			break;
		case 'K':
			break;
		case 'J':
			break;
		case '*':
			if (cursorMode) {
				editCycle (core, -0x2000);
			} else {
				waveFreq += 100;
				r_core_cmdf (core, "auw%c %d", waveTypeChar, waveFreq);
				r_core_cmd0 (core, "au.");
			}
			break;
		case '/':
			if (cursorMode) {
				editCycle (core, 0x2000);
			} else {
				waveFreq -= 100;
				if (waveFreq < 10) {
					waveFreq = 10;
				}
				r_core_cmdf (core, "auw%c %d", waveTypeChar, waveFreq);
				r_core_cmd0 (core, "au.");
			}
			break;
		case '+':
			if (cursorMode) {
				editCycle (core, -0x1000);
			} else {
				waveFreq += 10;
				r_core_cmdf (core, "auw%c %d", waveTypeChar, waveFreq);
				r_core_cmd0 (core, "au.");
			}
			break;
		case '-':
			if (cursorMode) {
				editCycle (core, 0x1000);
			} else {
				waveFreq -= 10;
				if (waveFreq < 10) {
					waveFreq = 10;
				}
				r_core_cmdf (core, "auw%c %d", waveTypeChar, waveFreq);
				r_core_cmd0 (core, "au.");
			}
			break;
		case ':':
			r_core_visual_prompt_input (core);
			break;
		case 'h':
			if (keyboard_visible) {
				if (keyboard_offset > 0) {
					keyboard_offset --;
					au_note_play (core, 1, keyboard_visible);
				}
				waveFreq = notes_freq (keyboard_offset);
			} else {
				if (cursorMode) {
					if (cursorPos > 0) {
						cursorPos--;
					}
				} else {
					r_core_seek_delta (core, -2);
					r_core_cmd0 (core, "au.");
				}
			}
			break;
		case 'l':
			if (keyboard_visible) {
				keyboard_offset ++;
				waveFreq = notes_freq (keyboard_offset);
				if (waveFreq) {
					au_note_play (core, 1, keyboard_visible);
				} else {
					keyboard_offset --;
				}
			} else {
				if (cursorMode) {
					cursorPos++;
				} else {
					r_core_seek_delta (core, 2);
					r_core_cmd0 (core, "au.");
				}
			}
			break;
		case 'H':
			if (keyboard_visible) {
				if (keyboard_offset > 0) {
					keyboard_offset -= 6;
					if (keyboard_offset < 0) {
						keyboard_offset = 0;
					}
				}
				waveFreq = notes_freq (keyboard_offset);
				au_note_play (core, 1, keyboard_visible);
			} else {
				r_core_seek_delta (core, -toneSize); // zoomMode? -512: -128);
				r_core_cmd0 (core, "au.");
			}
			break;
		case 'L':
			if (keyboard_visible) {
				keyboard_offset += 6;
				while (!notes_freq (keyboard_offset)) {
					keyboard_offset --;
					if (keyboard_offset < 0) {
						break;
					}
				}
				waveFreq = notes_freq (keyboard_offset);
				au_note_play (core, 1, keyboard_visible);
			} else {
				r_core_seek_delta (core, toneSize); // zoomMode? 512: 128);
				r_core_cmd0 (core, "au.");
			}
			break;
		case 'z':
			zoomMode = !zoomMode;
			break;
		case 'j':
			if (cursorMode) {
				editCycle(core, 0x1000);
			} else {
				waveType++;
				waveTypeChar = WAVECMD[waveType % WAVETYPES];
				r_core_cmdf (core, "auw%c %d", waveTypeChar, waveFreq);
				r_core_cmd0 (core, "au.");
			}
			break;
		case '?':
			au_visual_help (core);
			break;
		case 'k':
			if (cursorMode) {
				editCycle(core, -0x1000);
			} else {
				waveType--;
				if (waveType < 0) {
					waveType = 0;
				}
				waveTypeChar = WAVECMD[waveType % WAVETYPES];
				r_core_cmdf (core, "auw%c %d", waveTypeChar, waveFreq);
				r_core_cmd0 (core, "au.");
			}
			break;
		case 'i':
			r_core_cmdf (core, "auws %d", waveFreq);
			break;
		case '[':
			if (cursorMode) {
				cycleSize -= 2;
				if (cycleSize < 0) {				
					cycleSize = 0;
				}
				editCycle (core, 0);
			} else {
				zoomLevel--;
				if (zoomLevel < 1) {
					zoomLevel = 1;
				}
			}
			break;
		case ']':
			if (cursorMode) {
				cycleSize += 2;
				if (cycleSize < 0) {				
					cycleSize = 0;
				}
				editCycle (core, 0);
			} else {
				zoomLevel++;
			}
			break;
		case '.':
			// TODO : run in a thread?
			r_core_cmd0 (core, "au.");
			break;
		case 'q':
			if (keyboard_visible) {
				keyboard_visible = false;
			} else {
				return false;
			}
			break;
		}
	}
	
	return false;
}

static bool au_play(RCore *core) {
	ao_play (device, (char *)core->block, core->blocksize);
	// eprintf ("Played %d bytes\n", core->blocksize);
	return false;
}

static int _cmd_au (RCore *core, const char *args) {
	switch (*args) {
	case 'i': // "aui"
		// setup arguments here
		{
			char *arg_freq = strchr (args, ' ');
			int rate = WAVERATE;
			int bits = WAVEBITS;
			int chan = 1;
			if (arg_freq) {
				char *arg_bits = strchr (arg_freq + 1, ' ');
				*arg_freq++ = 0;
				rate = r_num_math (core->num, arg_freq);
				if (arg_bits) {
					*arg_bits++ = 0;
					char *arg_chans = strchr (arg_bits, ' ');
					bits = r_num_math (core->num, arg_bits);
					if (arg_chans) {
						*arg_chans++ = 0;
						chan = r_num_math (core->num, arg_chans);
					}
				}
			}
			int be = r_config_get_i (core->config, "cfg.bigendian");
			// TODO: register 'e au.rate' 'au.bits'... ?
			eprintf ("[au] %d Hz %d bits %d channels\n", rate, bits, chan);
			au_init (rate, bits, chan, be);
			// ao_play (device, (char *)core->block, core->blocksize);
		}
		break;
	case 'm': // "aum"
		// write pattern here
		{
			captureBlocksize();
			au_mix (core, args + 1);
			r_core_block_read (core);
			restoreBlocksize();
		}
		break;
	case 'w': // "auw"
		// write pattern here
		{
		captureBlocksize();
		au_write (core, args + 1);
		r_core_block_read (core);
		restoreBlocksize();
		}
		break;
	case 'b': // "aub"
		if (args[1] == ' ') {
			aBlocksize = r_num_math (core->num, args + 2);
		} else {
			r_cons_printf ("0x%"PFMT64x"\n", aBlocksize);
		}
		break;
	case 'o': // "auo"
		if (args[1]) {
			captureBlocksize();
			au_operate (core, args + 1);
			r_core_block_read (core);
			restoreBlocksize();
		} else {
			eprintf ("Usage: auo[+-*/] [val]\n");
		}
		break;
	case 'p': // "aup"
		switch (args[1]) {
		case 'p':
		case 'i':
			printPiano (core);
			break;
		case '?':
			eprintf ("Usage: aup[p] arg\n");
			break;
		default:
			printWave (core);
			break;
		}
		break;
	case '.': // "au."
		if (args[1] == '&') {
			eprintf ("Temporal magic\n");
		} else {
			captureBlocksize();
			au_play (core);
			restoreBlocksize();
		}
		break;
	case 'f': // "auf"
		for (int i = 0; i <TONES; i++) {
			char *note[32], *dolar;
			strcpy (note, tones[i].note);
			dolar = strchr (note, '$');
			if (dolar) {
				*dolar = '_';
			}
			printf ("f tone.%s = %d\n", note, (int)tones[i].freq);
		}
		break;
	case 'v': // "auv"
		au_visual (core);
		break;
	default:
	case '?':
		eprintf ("Usage: au[imopPwv] [args]\n");
		eprintf (" aui - init audio\n");
		eprintf (" au. - play current block (au.& in bg)\n");
		eprintf (" aub - audio blocksize\n");
		eprintf (" auf - flags per freqs associated with keys\n");
		eprintf (" aum - mix from given address into current with bsize\n");
		eprintf (" auo - apply operation with immediate\n");
		eprintf (" aup - print wave (aupi print piano)\n");
		eprintf (" auw - write wave (see auw?)\n");
		eprintf (" auv - visual wave mode\n");
		break;
	}
	return false;
}

static int r_cmd_au_call(void *user, const char *input) {
	RCore *core = (RCore *) user;
	if (!strncmp (input, "au", 2)) {
		_cmd_au (core, input + 2);
		return true;
	}
	return false;
}

RCorePlugin r_core_plugin_au = {
	.name = "audio",
	.desc = "play mustic with radare2",
	.license = "MIT",
	.call = r_cmd_au_call,
};

#ifndef CORELIB
RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_CORE,
	.data = &r_core_plugin_au,
	.version = R2_VERSION
};
#endif

