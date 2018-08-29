all:
	r2 -nqc 'aui 44100 16 1;;aub $$s;au.' test.wav

wav:
	ffmpeg -i test.m4a test.wav

demo:
	r2 -nqc 'aui 44100 16 2;aub 700K;auo) 200K;au.' dubmi.wav
