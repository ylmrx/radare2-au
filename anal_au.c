#include <r_types.h>
#include <r_lib.h>
#include <r_asm.h>
#include <r_anal.h>
#include "cpu.h"

static int typeChar(const ut8 t) {
	const char *str = "sczZpntsid";
	if (t < strlen (str)) {
		return str[t];
	}
	return '?';
}

static int aucpu_esil_mkw (RAnalOp *op, const ut8 *data) {
	int type = typeChar(data[1]);
	int freq = ((data[2]<< 8) | data[3]) << 2;
	r_strbuf_setf (&op->esil, "#!auw%c %d@ r0!r1", type, freq);
}

static int _au_op(RAnal *anal, RAnalOp *op, ut64 addr, const ut8 *data, int len) {
	if (len < 4) {
		return -1;
	}
	op->size = 4;
	op->cycles = 1;
	switch (data[0]) {
	case AUCPU_OP_NOP:
		op->type = R_ANAL_OP_TYPE_NOP;
		break;
	case AUCPU_OP_PLAY:
		op->type = R_ANAL_OP_TYPE_SWI;
		r_strbuf_setf (&op->esil, "#!au.@ r0!r1");
		break;
	case AUCPU_OP_MOV:
		op->type = R_ANAL_OP_TYPE_MOV;
		{
			int v = (data[2] << 8) | data[3];
			int r = data[1];
			r_strbuf_setf (&op->esil, "%d,r%d,=", v, r);
		}
		break;
	case AUCPU_OP_MKW:
		op->type = R_ANAL_OP_TYPE_STORE;
		aucpu_esil_mkw (op, data);
		break;
	defaultr:
		r_strbuf_setf (&op->esil, "#!?E hello world");
		break;
	}
	return 4;
}

static int set_reg_profile(RAnal *anal) {
	char *p =
		"=PC	pc\n"
		"=SP	sp\n"
		"gpr	pc	.32	0	0\n"
		"gpr	sp	.32	4	0\n"
		"gpr	r0	.32	8	0\n"
		"gpr	r1	.32	12	0\n"
		"gpr	r2	.32	16	0\n"
		"gpr	r3	.32	20	0\n"

		"gpr	flags	.8	.192	0\n"
		"gpr	C	.1	.192	0\n"
		"gpr	Z	.1	.193	0\n"
		"gpr	I	.1	.194	0\n"
		"gpr	D	.1	.195	0\n";
	return r_reg_set_profile_string (anal->reg, p);
}

RAnalPlugin r_anal_plugin_au = {
	.name = "au",
	.desc = "virtual audio chip analysis",
	.license = "MIT",
	.arch = "au",
	.bits = 16 | 32,
	.op = &_au_op,
	.set_reg_profile = &set_reg_profile,
	.esil = true,
};

#ifndef CORELIB
RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_ANAL,
	.data = &r_anal_plugin_au,
	.version = R2_VERSION
};
#endif

