#include <r_util.h>

#define OUT

enum aoc_opcodes {
	AOC_OP_MKW,
	AOC_OP_PS,
};

typedef struct {
	int regs[32];
} AocState;

int aoc_op_mkw(AocState *st, int type) {
	return 0;
}
