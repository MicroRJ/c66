/*
aurora
saxon
gus
galleon
malta
seaslug
cslug
"Cometh The Hour Cometh The Man", Robinhood
See:
https://wiki.osdev.org/X86-64_Instruction_Encoding */


#if defined(_MSC_VER)
typedef __int64 asm_i64;
typedef __int32 asm_i32;
typedef __int8  asm_i8;
#else
typedef long long int  asm_i64;
typedef int  			  asm_i32;
typedef char  			  asm_i8;
#endif

typedef int asm_reg;

#if 0
#define ASM_DBG printf
#else
#define ASM_DBG(...)
#endif

/* registers are in order, to be used with
the instructions directly... */
#define GPRDEF(_) \
_(rax)_(rcx)_(rdx)_(rbx)\
_(rsp)_(rbp)_(rsi)_(rdi)\
_(r8) _(r9) _(r10)_(r11)\
_(r12)_(r13)_(r14)_(r15)

#define GPR(NAME) asm_##NAME,
enum{
	GPRDEF(GPR)
};
#undef GPR


#define GPR(NAME) #NAME,
const char* asm_reg2s[]={
	GPRDEF(GPR)
};
#undef GPR

enum{
	asm_num_gpr_regs=16,
	asm_num_gpr_arg_regs=4
};
static char asm_arg_regs_list[]={
	asm_rcx,asm_rdx,asm_r8,asm_r9
};
enum{
	asm_regs_nil=0,
	asm_regs_gpr=0xffff,
	asm_regs_arg=(1<<asm_rcx)|(1<<asm_rdx)|(1<<asm_r8)|(1<<asm_r9),
};


#define REX 0x40
#define REX_W (REX|0x08)


void asm_o(int y);
void asm_i(asm_i64 x,int y);
void asm_err(char *msg){
	fputs(msg,stderr);
}
void asm_push(asm_reg reg){int flags;
	ASM_DBG("push %s\n",asm_reg2s[reg]);
	flags=reg>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x50+(reg&7));
}
void asm_pop(asm_reg reg){
	ASM_DBG("pop %s\n",asm_reg2s[reg]);
	asm_o(0x58+reg);
}
void asm_ret(){
	ASM_DBG("ret\n");
	asm_o(0xC3);
}
void asm_leave(){
	ASM_DBG("leave\n");
	asm_o(0xC9);
}
void asm_jcall(asm_i32 dsp){
	ASM_DBG("jcall %i\n",dsp);
	asm_o(0xE8);
	asm_i(dsp,4);
}
void asm_call(asm_reg reg){
	ASM_DBG("call %s\n",asm_reg2s[reg]);
	asm_o(0xFF);
	asm_o(0xC0|0x02<<3|reg&7);
}
void asm_test(asm_reg dst,asm_reg src,int flags){
	ASM_DBG("test %s, %s\n",asm_reg2s[dst],asm_reg2s[src]);
	flags|=src>>1&4|dst>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x85);
	asm_o(0xC0|(src&7)<<3|dst&7);
}
void asm_testq(asm_reg dst,asm_reg src){
	asm_test(dst,src,REX_W);
}
void asm_cmp(asm_reg dst,asm_i32 src,int flags){
	ASM_DBG("cmp%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src]);
	flags|=src>>1&4|dst>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x39);
	asm_o(0xC0|(src&7)<<3|dst&7);
}
void asm_cmpq(asm_reg reg,asm_i32 src){
	asm_cmp(reg,src,REX_W);
}
void asm_cmpd(asm_reg reg,asm_i32 src){
	asm_cmp(reg,src,0);
}
void asm_move(asm_reg dst,asm_reg src,int flags){
	ASM_DBG("mov%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src]);
	flags|=src>>1&4|dst>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x89);
	asm_o(0xC0|(src&7)<<3|dst&7);
}
void asm_moveq(asm_reg dst,asm_reg src){
	asm_move(dst,src,REX_W);
}
void asm_load(asm_reg dst,asm_reg src,asm_i32 off,int flags){
	ASM_DBG("mov%c %s, [%s%+i]\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src],off);
	flags|=dst>>1&4|src>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x8B);
	asm_o(0x80>>((asm_i8)off==off)|(dst&7)<<3|src&7);
	asm_i(off,4>>((asm_i8)off==off)*2);
}
void asm_loadd(asm_reg dst,asm_reg src,asm_reg off){
	asm_load(dst,src,off,0);
}
void asm_loadq(asm_reg dst,asm_reg src,asm_reg off){
	asm_load(dst,src,off,REX_W);
}
void asm_store(asm_reg dst,asm_i32 off,asm_reg src,int flags){
	ASM_DBG("mov%c [%s%+i], %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],off,asm_reg2s[src]);
	flags|=src>>1&4|dst>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x89);
	asm_o(0x80>>((asm_i8)off==off)|(src&7)<<3|dst&7);
	asm_i(off,4>>((asm_i8)off==off)*2);
}
void asm_stored(asm_reg dst,asm_i32 off,asm_reg src){
	asm_store(dst,off,src,0);
}
void asm_storeq(asm_reg dst,asm_i32 off,asm_reg src){
	asm_store(dst,off,src,REX_W);
}
void asm_storei(asm_reg dst,asm_i32 off,asm_i64 src,int flags){
	flags|=((asm_i32)src!=src)<<3|dst>>3&1;
	ASM_DBG("mov%c [%s%+i], #%lli\n",flags&REX_W?'q':'d',asm_reg2s[dst],off,src);
	if(flags)asm_o(REX|flags);
	asm_o(0xC7);
	asm_o(0x80>>((asm_i8)off==off)|dst&7);
	asm_i(off,4>>((asm_i8)off==off)*2);
	asm_i(src,8>>((asm_i32)src==src));
}
void asm_storedi(asm_reg dst,asm_i32 off,asm_i64 src){
	asm_storei(dst,off,src,0);
}
void asm_storeqi(asm_reg dst,asm_i32 off,asm_i64 src){
	asm_storei(dst,off,src,REX_W);
}
void asm_loadi(asm_reg dst,asm_i64 src,int flags){
	flags|=((asm_i32)src!=src)<<3|(dst>>3&1);
	ASM_DBG("mov%c %s, %llx\n",flags&REX_W?'q':'d',asm_reg2s[dst],src);
	if(flags)asm_o(REX|flags);
	if((asm_i32)src==src){
		asm_o(0xC7);
		asm_o(0xC0|(dst&7));
	}else{
		asm_o(0xB8|(dst&7));
	}
	asm_i(src,8>>((asm_i32)src==src));
}
void asm_loadqi(asm_reg dst,asm_i64 src){
	asm_loadi(dst,src,REX_W);
}
void asm_mul(asm_reg dst,asm_reg src,int flags){
	ASM_DBG("mul%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src]);
	flags|=dst>>1&4|src>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x0F);
	asm_o(0xAF);
	asm_o(0xC0|(dst&7)<<3|src&7);
}
void asm_muld(asm_reg dst,asm_reg src){
	asm_mul(dst,src,0);
}
void asm_mulq(asm_reg dst,asm_reg src){
	asm_mul(dst,src,REX_W);
}
void asm_subi(asm_reg dst,asm_i64 src,int flags){
	flags|=(((asm_i32)src!=src)<<3)|(dst>>3&1);
	if(flags)asm_o(REX|flags);
	asm_o(0x81);
	asm_o(0xC0|0x05<<3|dst);
	asm_i(src,4<<((asm_i32)src!=src));
}
void asm_subdi(asm_reg dst,asm_i64 src){
	return asm_subi(dst,src,0);
}
void asm_subqi(asm_reg dst,asm_i64 src){
	return asm_subi(dst,src,REX_W);
}
void asm_sub(asm_reg dst,asm_reg src,int flags){
	ASM_DBG("sub%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src]);
	flags|=dst>>1&4|src>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x2B);
	asm_o(0xC0|(dst&7)<<3|src&7);
}
void asm_subd(asm_reg dst,asm_reg src){
	asm_sub(dst,src,0);
}
void asm_subq(asm_reg dst,asm_reg src){
	asm_sub(dst,src,REX_W);
}
void asm_addi(asm_reg dst,asm_i64 src,int flags){
	flags|=(((asm_i32)src!=src)<<3)|(dst>>3&1);
	if(flags)asm_o(REX|flags);
	asm_o(0x81);
	asm_o(0xC0|0x00<<3|dst);
	asm_i(src,4<<((asm_i32)src!=src));
}
void asm_addqi(asm_reg dst,asm_i64 src){
	asm_addi(dst,src,REX_W);
}
void asm_adddi(asm_reg dst,asm_i64 src){
	asm_addi(dst,src,0);
}
void asm_add(asm_reg dst,asm_reg src,int flags){
	ASM_DBG("add%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src]);
	flags|=dst>>1&4|src>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x03);
	asm_o(0xC0|(dst&7)<<3|src&7);
}
void asm_addd(asm_reg dst,asm_reg src){
	asm_add(dst,src,0);
}
void asm_addq(asm_reg dst,asm_reg src){
	asm_add(dst,src,REX_W);
}
void asm_cmpi(asm_reg reg,asm_i32 src){
	asm_o(0x81);
	asm_o(0xC0|0x07<<3|reg&7);
	asm_i(src,4);
}
void asm_lea(asm_reg dst,asm_reg src,asm_i32 off,int flags){
	ASM_DBG("lea%c %s, [%s%+i]\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src],off);
	flags|=dst>>1&4|src>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x8D);
	asm_o(0x80>>((asm_i8)off==off)|(dst&7)<<3|src&7);
	asm_i(off,4>>((asm_i8)off==off)*2);
}
void asm_lead(asm_reg dst,asm_reg src,asm_i32 off){
	asm_lea(dst,src,off,0);
}
void asm_leaq(asm_reg dst,asm_reg src,asm_i32 off){
	asm_lea(dst,src,off,REX_W);
}
void asm_jmp(asm_i32 rel){
	asm_o(0xE9);
	asm_i(rel,4);
}
typedef enum{
	asm_Jcc_jz  =0x84,
	asm_Jcc_jnz =0x85,
	asm_Jcc_jl  =0x8C,
	asm_Jcc_jge =0x8D,
	asm_Jcc_jg  =0x8F,
}asm_Jcc;
void asm_jcc(asm_Jcc jcc,asm_i32 rel){
	asm_o(0x0F);
	asm_o(jcc);
	asm_i(rel,4);
}
void asm_jz(asm_i32 rel){
	asm_jcc(asm_Jcc_jz,rel);
}
void asm_jnz(asm_i32 rel){
	asm_jcc(asm_Jcc_jnz,rel);
}
void asm_jl(asm_i32 rel){
	asm_jcc(asm_Jcc_jl,rel);
}




/* Some quick notes for encoding:

Variable layout of an x64 instruction, each
term is a byte, () means required:
[REX-PREFIX][ESC](OPCODE)[MOD-R/M][SIB][DISP][IMM]

2.1.3:
The rex byte can extend both register targets of the
MODRM byte, to access registers r8-r15.
The default value of a REX byte is 0x40, to use 64-bit
operands bit or with 0x08.
'r' bit (3) flag extends register in 'reg' field of MODRM,
'b' bit (1) flag extends register in 'r/m' field of MODRM,

0F escape code is used for 2 byte opcodes, other
escape codes will be mentioned if used.

The ModR/M byte is used for specifying the addressing
mode. Layout is as follows:
   Mod(R/M) Bit Layout:
   [[00][000][000]]
     76  543  210
   Bits   Usage
   7..6   MOD: Addressing mode
   5..3   reg: Reg or Opcode extension
   2..0   r/m: Reg or Memory
*/

