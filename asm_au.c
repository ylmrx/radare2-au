/* radare2-au - MIT - Copyright 2018 - pancake */

#include <r_asm.h>
#include <r_lib.h>
#include "cpu.h"

static int assemble(RAsm *a, RAsmOp *op, const char *buf) {
	char *arg = strdup (buf);
	r_str_replace_char (arg, ',', ' ');
	RList *args = r_str_split_list (arg, " ");
	const char *mnemonic = r_list_get_n (args, 0);
	eprintf ("MNEMO %s\n", mnemonic);
	r_list_free (args);
	op->size = 4;
	return 4;
}

static const char *waveType(const ut8 t) {
	const char *types[] = {
		"sin", "cos", "saw", "rsaw", "pulse",
		"noise", "triangle", "silence", "inc", "dec",
		NULL
	};
	int i = 0;
	for (i=0;types[i] && i<t;i++) {

	}
	return types[i];
}

static void invalid (RAsmOp *op, const ut8 *buf) {
	st16 *dword = (st16*)buf;
	snprintf (op->buf_asm, sizeof (op->buf_asm), ".short %d", *dword);
	op->size = 2;
}

static int disassemble(RAsm *a, RAsmOp *op, const ut8 *buf, int len) {
	if (len < 4) {
		return -1;
	}
	op->size = 4;
	switch (buf[0]) {
	case AUCPU_OP_NOP:
		strcpy (op->buf_asm, "nop");
		break;
	case AUCPU_OP_MOV:
		{
			int r = buf[1];
			int v = (buf[2] << 8) | buf[3];
			snprintf (op->buf_asm, sizeof (op->buf_asm), "mov r%d, %d", r, v);
		}
		break;
	case AUCPU_OP_MKW:
		{
			int t = buf[1];
			st16 *dword = (st16*)(buf + 2);
			const char *type = waveType(buf[1]);
			if (type) {
				snprintf (op->buf_asm, sizeof (op->buf_asm), "mkw %s, %d", type, *dword);
			} else {
				invalid (op, buf);
			}
		}
		break;
	case AUCPU_OP_PLAY:
		strcpy (op->buf_asm, "play");
		break;
	case AUCPU_OP_PLAYREG:
		break;
	default:
		invalid (op, buf);
		break;
	}
	// unaligned check?
	return op->size;
}

RAsmPlugin r_asm_plugin_au = {
	.name = "au",
	.desc = "virtual audio chip",
	.arch = "au",
	.bits = 8|16|32,
	.endian = R_SYS_ENDIAN_LITTLE | R_SYS_ENDIAN_BIG,
	.license = "MIT",
	.assemble = &assemble,
	.disassemble = &disassemble,
};

#ifndef CORELIB
RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_ASM,
	.data = &r_asm_plugin_au,
	.version = R2_VERSION
};
#endif

