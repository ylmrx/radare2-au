
enum aucpu_opcodes {
	AUCPU_OP_NOP,
	AUCPU_OP_MOV,
	AUCPU_OP_MKW,
	AUCPU_OP_PLAY,
	AUCPU_OP_PLAYREG,
};

typedef struct {
	int regs[32];
} AuCpuState;

