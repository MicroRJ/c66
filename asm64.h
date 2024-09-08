/*
aurora
saxon
gus
galleon
malta
"Cometh The Hour Cometh The Man", Robinhood
See:
https://wiki.osdev.org/X86-64_Instruction_Encoding */


#if defined(_MSC_VER)
typedef __int64 asm_i64;
typedef __int32 asm_i32;
#endif


typedef int asm_reg;


/* registers are in order, to be used with
the instructions directly... */
#define GPRDEF(_) \
_(rax)_(rcx)_(RDX)_(RBX)\
_(rsp)_(rbp)_(RSI)_(RDI)\
_(r8) _(R9) _(R10)_(R11)\
_(R12)_(R13)_(R14)_(r15)


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
	asm_rcx,GPR_RDX,asm_r8,GPR_R9
};
enum{
	asm_regs_nil=0,
	asm_regs_gpr=0xffff,
	asm_regs_arg=(1<<asm_rcx)|(1<<GPR_RDX)|(1<<asm_r8)|(1<<GPR_R9),
};


#define REX 0x40
#define REX_W (REX|0x08)
#define REX_R (REX|0x04)
#define REX_B (REX|0x01)


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




void asm_o(int y);
void asm_i(asm_i64 x,int y);

void asm_err(char *msg){
	fputs(msg,stderr);
}
void asm_push(int reg){int flags;
	flags=reg>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x50+(reg&7));
}
/* todo: replace with intrinsic */
void asm_pushall(){int i;
	for(i=0;i<asm_num_gpr_regs;i++){
		asm_push(i);
	}
}
void asm_pop(int reg){
	asm_o(0x58+reg);
}
void asm_ret(){
	asm_o(0xC3);
}
void asm_leave(){
	asm_o(0xC9);
}
void asm_jcall(asm_i32 dsp){
	asm_o(0xE8);
	asm_i(dsp,4);
}
void asm_call(asm_reg reg){
	asm_o(0xFF);
	asm_o(0xC0|0x02<<3|reg&7);
}
#define asm_store32(dst,off,src) (asm_o(0x89),asm_o(mode8(src,dst)),asm_o(off))
#define asm_store64(dst,off,src) (o(REX_W),asm_store32(dst,off,src))
#define asm_store32_i(dst,off,src) (o(0xC7),o(mode8(0,dst)),o(off),asm_i(src,4))
#define asm_store64_i(dst,off,src) (o(REX_W),asm_store32_i(dst,off,src))


void asm_move(asm_reg dst,asm_reg src,int flags);
void asm_moveq(asm_reg dst,asm_reg src);
void asm_test(asm_reg dst,asm_reg src,int flags);
void asm_testq(asm_reg dst,asm_reg src);


void asm_test(asm_reg dst,asm_reg src,int flags){
	flags|=src>>1&4|dst>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x85);
	asm_o(0xC0|(src&7)<<3|dst&7);
}
void asm_move(asm_reg dst,asm_reg src,int flags){
	flags|=src>>1&4|dst>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x89);
	asm_o(0xC0|(src&7)<<3|dst&7);
}
/* todo: support 16 bit offsets */
void asm_load(asm_reg dst,asm_reg src,asm_i32 off,int flags){
	flags|=dst>>1&4|src>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x8B);
	asm_o(0x40|(dst&7)<<3|src&7);
	asm_o(off);
}
void asm_loadd(asm_reg dst,asm_reg src,asm_reg off){
	asm_load(dst,src,off,0);
}
void asm_loadq(asm_reg dst,asm_reg src,asm_reg off){
	asm_load(dst,src,off,REX_W);
}
void asm_testq(asm_reg dst,asm_reg src){
	asm_test(dst,src,REX_W);
}
void asm_moveq(asm_reg dst,asm_reg src){
	asm_move(dst,src,REX_W);
}
void asm_loadi(asm_reg dst,asm_i64 src){int flags;
	flags=(((asm_i32)src!=src)<<3)|(dst>>3&1);
	if(flags)asm_o(REX|flags);
	asm_o(0xB8+(dst&7));
	asm_i(src,4<<((asm_i32)src!=src));
}
void asm_subi(asm_reg dst,asm_i64 src,int flags){
	flags|=(((asm_i32)src!=src)<<3)|(dst>>3&1);
	if(flags)asm_o(REX|flags);
	asm_o(0x81);
	asm_o(0xC0|5<<3|dst);
	asm_i(src,4<<((asm_i32)src!=src));
}
void asm_subdi(asm_reg dst,asm_reg src){
	return asm_subi(dst,src,0);
}
void asm_subqi(asm_reg dst,asm_reg src){
	return asm_subi(dst,src,REX_W);
}


#define asm_add64_i(x,y) (o(REX_W),o(0x81),o(moder(0x00,x)),asm_i(y,4))

#define asm_sub32(x,y) (asm_o(0x2B),asm_o(moder(x,y)))
#define asm_sub64(x,y) (o(REX_W),asm_sub32(x,y))

#define asm_add32(x,y) (asm_o(0x03),asm_o(moder(x,y)))
#define asm_add64(x,y) (o(REX_W),asm_add32(x,y))

void asm_cmpi(asm_reg reg,asm_i32 src){
	asm_o(0x81);
	asm_o(0xC0|0x07<<3|reg&7);
	asm_i(src,4);
}
//(* jumps *)
void asm_jmp(asm_i32 rel){
	asm_o(0xE9);
	asm_i(rel,4);
}
void asm_jnz(asm_i32 rel){
	asm_o(0x0F);
	asm_o(0x85);
	asm_i(rel,4);
}
void asm_jz(asm_i32 rel){
	asm_o(0x0F);
	asm_o(0x84);
	asm_i(rel,4);
}




/* variable layout (byte terms) of an x64 instruction:
[REX-PREFIX][ESC][OPCODE][MOD-R/M][SIB][DISP][IMM]
*/

/* 0F: used for 2 byte opcodes
   other escape codes which are not mentioned...
*/

/*
   Mod(R/M) Bit Layout:
   [[00][000][000]]
     76  543  210
   Bits   Usage
   7..6   MOD: Addressing mode
   5..3   reg: Reg or Opcode extension
   2..0   r/m: Reg or Memory
*/

/* the rex byte can extend both targets
of the modrm byte,
'r' bit flag extends 'reg',
'b' bit flag extends 'r/m',
use this macro when both targets are registers so
that the appropriate bit is set in the rex byte
should either register be part of the extended
register set. */

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
