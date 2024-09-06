/* (x86_64)

aurora
saxon

"Cometh The Hour Cometh The Man", Robinhood


See:
https://wiki.osdev.org/X86-64_Instruction_Encoding */


typedef unsigned long long int asm_i64;


/* registers are in order, to be used with
the instructions directly... */
#define GPRDEF(_) \
_(rax)_(rcx)_(RDX)_(RBX)\
_(RSP)_(RBP)_(RSI)_(RDI)\
_(R8) _(R9) _(R10)_(R11)\
_(R12)_(R13)_(R14)_(R15)


#define GPR(NAME) GPR_##NAME,
enum{
	GPR_NONE=-1,GPRDEF(GPR)
};
#undef GPR
#define GPR(NAME) asm_##NAME,
enum{
	asm_reg_non=-1,GPRDEF(GPR)
};
#undef GPR


#define GPR(NAME) #NAME,
const char* asm_reg2s[]={
	"NONE",GPRDEF(GPR)
};
#undef GPR

enum{
	asm_num_gpr_regs=16,
	asm_num_gpr_arg_regs=4
};
static char asm_arg_regs_list[]={
	asm_rcx,GPR_RDX,GPR_R8,GPR_R9
};
enum{
	asm_regs_nil=0,
	asm_regs_gpr=0xffff,
	asm_regs_arg=(1<<asm_rcx)|(1<<GPR_RDX)|(1<<GPR_R8)|(1<<GPR_R9),
};


/* variable layout of an x86_64 instruction,
constituded by the following terms (bytes):
[REX-PREFIX][ESC][OPCODE][MOD-R/M][SIB][DISP][IMM]
*/


/* ESC: one or more escape opcode prefixes
come before opcode and after the REX prefix. */

/* 0F: used for 2 byte opcodes
   other escape codes which are not mentioned...
*/
#define ESC2 0x0F


#define REX 0x40
#define REX_W (REX|0x08)
#define REX_R (REX|0x04)
#define REX_B (REX|0x01)

/*
   Mod(R/M) Bit Layout:
   [[00][000][000]]
     76  543  210
   Bits   Usage
   7..6   MOD: Addressing mode
   5..3   reg: Reg or Opcode extension
   2..0   r/m: Reg or Memory
*/


/* To build a ModRM byte, use one of the following
addressing modes:


01: Uses 8 bit displacement addressing, added to the base
register specified in the r/m field. (*)

10: Uses a 32 bit displacement instead. (*)

* The displacement is encoded in the following one or
four bytes after the ModRM byte accordingly.
If r/m specifies a register greater than RBP = b101 = 5,
then more complex addressing can be done with an additional
SIB byte.

00: For optional indirect addressing,
if r/m > 101 the displacement is the memory
address, otherwise no displacement is used.
*/


/* z is one of the addressing mode bytes, already
offset, x is reg field, y is r/m field */
#define MODRM(z,x,y) ((z)<<0|(x)<<3|(y)<<0)

/* use moder to build a mod r/m byte for direct register
addressing, where both the reg, and r/m fields are
registers. */
#define moder(x,y) MODRM(0xC0,x,y)

#define mode8(x,y) MODRM(0x40,x,y)
#define mode16(x,y) MODRM(0x80,x,y)


/*
** SIB.Scale 2bits
** SIB.Index 3bits (register)
** SIB.Base  3bits (register)
*/
#define SIB(x,y,z) (((x) << 6)|((y) << 3)|((z) << 0))


#define OP_JMP  0xE9


/* both push and pop are single byte instructions,
starting at a base offset plus the register to
create actual instruction */
/* store instructions move data from (src) register
to to target memory, at address specified by base
register (dst) and an offset (off). */
/* store 32 or 64 bit value (src) at target address in
base register (dst) plus one byte offset (off) */
/* opposite of store */
/* note that the lower 3 bits of opcode are used to
encode the target register, and the payload is the
same size as the register. */


/* the rex byte can extend both targets
of the modrm byte,
'r' bit flag extends 'reg',
'b' bit flag extends 'r/m',
use this macro when both targets are registers so
that the appropriate bit is set in the rex byte
should either register be part of the extended
register set. */
#define asm_reg2rex64(r,b) o(REX_W|(((r)&8)>>1)|(((b)&8)>>3))
#define asm_reg2rex32(r,b) o((((r)&8)>>1)|(((b)&8)>>3))


void asm_o(int y);
void asm_on(asm_i64 x,int y);

void asm_err(char *msg){
	fputs(msg,stderr);
}
void asm_push(int reg){
	if(reg>7)asm_err("invalid reg");
	asm_o(0x50+(reg&7));
}
void asm_pop(int reg){
	asm_o(0x58+reg);
}
void asm_push_rbp(){
	asm_push(GPR_RBP);
}
void asm_pop_rbp(){
	asm_pop(GPR_RBP);
}
void asm_ret(){
	asm_o(0xC3);
}
void asm_leave(){
	asm_o(0xC9);
}
void asm_call32_rel(int rel){
	asm_o(0xE8);
	asm_on(rel,4);
}
#define asm_call32(rel) (o(0xE8),on(rel,4))
#define asm_call64_r(reg) (o(0xFF),o(moder(0x02,reg)))
#define asm_store32(dst,off,src) (asm_o(0x89),asm_o(mode8(src,dst)),asm_o(off))
#define asm_store64(dst,off,src) (o(REX_W),asm_store32(dst,off,src))
#define asm_store32_i(dst,off,src) (o(0xC7),o(mode8(0,dst)),o(off),on(src,4))
#define asm_store64_i(dst,off,src) (o(REX_W),asm_store32_i(dst,off,src))


void asm_load(int dst,int src,int off,int sze){int wrb;
	wrb=(sze>>3&8)|(dst>>1&4)|(src>>3&1);
	if(wrb)asm_o(REX|wrb);
	asm_o(0x8B);
	asm_o(mode8(dst&7,src&7));
	asm_o(off);
}

#define asm_load32(dst,src,off) asm_load(dst,src,off,32)
#define asm_load64(dst,src,off) asm_load(dst,src,off,64)


#define asm_move32(dst,src) (o(0x89),o(moder(src,dst)))
#define asm_move64(dst,src) (asm_reg2rex64(src,dst),asm_move32(dst&7,src&7))

#define asm_move_rbp_rsp()	asm_move64(GPR_RBP,GPR_RSP)

#define asm_move32_i(dst,src) (o(0xB8+(dst)),on(src,4))
#define asm_move64_i(dst,src) (o(REX_W),o(0xB8+(dst)),on(src,8))


#define asm_sub32_i(x,y) (o(0x81),o(moder(0x05,x)),on(y,4))
#define asm_sub32(x,y) (asm_o(0x2B),asm_o(moder(x,y)))
#define asm_add64_i(x,y) (o(REX_W),o(0x81),o(moder(0x00,x)),on(y,4))
#define asm_sub64_i(x,y) (o(REX_W),asm_sub32_i(x,y))
#define asm_sub64(x,y) (o(REX_W),asm_sub32(x,y))
#define asm_cmp32_i(x,y) (o(0x81),o(moder(0x07,x)),on(y,4))
#define asm_add32(x,y) (asm_o(0x03),asm_o(moder(x,y)))
#define asm_add64(x,y) (o(REX_W),asm_add32(x,y))
#define asm_jne(x) (o(ESC2,0x85),on(x,4))
#define asm_jl(x) (o(ESC2),o(0x8C),on(x,4))

