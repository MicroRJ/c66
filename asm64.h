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

#if 1
#define ASM_FPF printf
#else
#define ASM_FPF(...)
#endif

/* registers are in order, to be used with instructions
directly... number of register must be power of 2 */
#define GPRDEF(_) \
_(rax, eax)_(rcx, ecx)_(rdx, edx)_(rbx, ebx)\
_(rsp, esp)_(rbp, ebp)_(rsi, esi)_(rdi, edi)\
_( r8, r8d)_( r9, r9d)_(r10,r10d)_(r11,r11d)\
_(r12,r12d)_(r13,r13d)_(r14,r14d)_(r15,r15d)


enum{
#define GPR(QNAME,_) asm_##QNAME,
	GPRDEF(GPR)
#undef GPR
#define GPR(_,DNAME) asm_##DNAME,
	GPRDEF(GPR)
#undef GPR
};


const char* asm_reg2s[]={
#define GPR(QNAME,_) #QNAME,
	GPRDEF(GPR)
#undef GPR
#define GPR(_,DNAME) #DNAME,
	GPRDEF(GPR)
#undef GPR
};


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
	asm_regs_args=(1<<asm_rcx)|(1<<asm_rdx)|(1<<asm_r8)|(1<<asm_r9),
};


#define REX 0x40
#define REX_W (REX|0x08)


void asm_o(int y);
void asm_i(asm_i64 x,int y);
void asm_err(char *msg){
	fputs(msg,stderr);
}
void asm_push(asm_reg reg){int flags;
	ASM_FPF("push %s\n",asm_reg2s[reg]);
	flags=reg>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x50+(reg&7));
}
void asm_pop(asm_reg reg){
	ASM_FPF("pop %s\n",asm_reg2s[reg]);
	asm_o(0x58+reg);
}
void asm_ret(){
	ASM_FPF("ret\n");
	asm_o(0xC3);
}
void asm_leave(){
	ASM_FPF("leave\n");
	asm_o(0xC9);
}
void asm_jcall(asm_i32 dsp){
	ASM_FPF("jcall %i\n",dsp);
	asm_o(0xE8);
	asm_i(dsp,4);
}
void asm_call(asm_reg reg){
	ASM_FPF("call %s\n",asm_reg2s[reg]);
	asm_o(0xFF);
	asm_o(0xC0|0x02<<3|reg&7);
}
void asm_test(asm_reg dst,asm_reg src,int flags){
	ASM_FPF("test %s, %s\n",asm_reg2s[dst],asm_reg2s[src]);
	flags|=src>>1&4|dst>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x85);
	asm_o(0xC0|(src&7)<<3|dst&7);
}
void asm_testq(asm_reg dst,asm_reg src){
	asm_test(dst,src,REX_W);
}
void asm_cmp(asm_reg dst,asm_i32 src,int flags){
	ASM_FPF("cmp%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src]);
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
// void asm_movsxd(asm_reg dst,asm_reg src){
// }
void asm_mov(asm_reg dst,asm_reg src,int flags){
	flags|=(~dst|~src)&16>>1|src>>1&4|dst>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x89);
	asm_o(0xC0|(src&7)<<3|dst&7);
	ASM_FPF("mov %s, %s\n",asm_reg2s[dst],asm_reg2s[src]);
}
void asm_moveq(asm_reg dst,asm_reg src){
	asm_mov(dst,src,REX_W);
}
void asm_load(asm_reg dst,asm_reg src,asm_i32 off,int flags){
	ASM_FPF("mov%c %s, [%s%+i]\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src],off);
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
	ASM_FPF("mov%c [%s%+i], %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],off,asm_reg2s[src]);
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
	if(flags)asm_o(REX|flags);
	asm_o(0xC7);
	asm_o(0x80>>((asm_i8)off==off)|dst&7);
	asm_i(off,4>>((asm_i8)off==off)*2);
	asm_i(src,8>>((asm_i32)src==src));
	ASM_FPF("mov%c [%s%+i], #%lli\n",flags&REX_W?'q':'d',asm_reg2s[dst],off,src);
}
void asm_storedi(asm_reg dst,asm_i32 off,asm_i64 src){
	asm_storei(dst,off,src,0);
}
void asm_storeqi(asm_reg dst,asm_i32 off,asm_i64 src){
	asm_storei(dst,off,src,REX_W);
}
void asm_loadi(asm_reg dst,asm_i64 src,int flags){
	flags|=((asm_i32)src!=src)<<3|(dst>>3&1);
	ASM_FPF("mov%c %s, %llx\n",flags&REX_W?'q':'d',asm_reg2s[dst],src);
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
	ASM_FPF("mul%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src]);
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
	ASM_FPF("sub%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src]);
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
	ASM_FPF("add%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src]);
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
void asm_shr(asm_reg dst,int flags){
	ASM_FPF("shr%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[asm_rcx]);
	flags|=dst>>1&4;
	if(flags)asm_o(REX|flags);
	asm_o(0xD3);
	asm_o(0xC0|5<<3|dst&7);
}
void asm_shrd(asm_reg dst){
	asm_shr(dst,0);
}
void asm_shrq(asm_reg dst){
	asm_shr(dst,REX_W);
}
void asm_shl(asm_reg dst,int flags){
	ASM_FPF("shl%c %s, %s\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[asm_rcx]);
	flags|=dst>>1&4;
	if(flags)asm_o(REX|flags);
	asm_o(0xD3);
	asm_o(0xC0|4<<3|dst&7);
}
void asm_shld(asm_reg dst){
	asm_shl(dst,0);
}
void asm_shlq(asm_reg dst){
	asm_shl(dst,REX_W);
}
void asm_shli(asm_reg dst,asm_i32 src,int flags){
	ASM_FPF("shl%c %s, %i\n",flags&REX_W?'q':'d',asm_reg2s[dst],src);
	flags|=dst>>1&4;
	if(flags)asm_o(REX|flags);
	asm_o(0xC1);
	asm_o(0xC0|4<<3|dst&7);
	asm_o(src);
}
void asm_shldi(asm_reg dst,asm_i32 src){
	asm_shli(dst,src,0);
}
void asm_shlqi(asm_reg dst,asm_i32 src){
	asm_shli(dst,src,REX_W);
}
void asm_cmpi(asm_reg reg,asm_i32 src){
	asm_o(0x81);
	asm_o(0xC0|0x07<<3|reg&7);
	asm_i(src,4);
}
void asm_lea(asm_reg dst,asm_reg src,asm_i32 off,int flags){
	ASM_FPF("lea%c %s, [%s%+i]\n",flags&REX_W?'q':'d',asm_reg2s[dst],asm_reg2s[src],off);
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
void asm_jmp(asm_i32 off){
	asm_o(0xE9);
	asm_i(off,4);
	ASM_FPF("jmp %i\n",off);
}
typedef enum{
	asm_cc_z =0x04,
	asm_cc_nz=0x05,
	asm_cc_l =0x0C,
	asm_cc_ge=0x0D,
	asm_cc_le=0x0E,
	asm_cc_g =0x0F,
} asm_cc;
static char *asm_cc2s[]={
	"","","","","z","nz","","","","","","","l","ge","le","g"
};

// !(cc)
asm_cc asm_icc(asm_cc c){
#if 0
	return cc ^ 1;
#else
	if(c==asm_cc_z) return asm_cc_nz;
	if(c==asm_cc_l) return asm_cc_ge;
	if(c==asm_cc_g) return asm_cc_le;
	if(c==asm_cc_ge)return asm_cc_l;
	if(c==asm_cc_le)return asm_cc_g;
	return c;
#endif
}
void asm_scc(asm_reg dst,asm_cc cc){int flags;
	flags=dst>>3&1;
	if(flags)asm_o(REX|flags);
	asm_o(0x0F);
	asm_o(0x90|cc);
	asm_o(0xC0|dst&7);
	ASM_FPF("set%s %s\n",asm_cc2s[cc],asm_reg2s[dst]);
}
void asm_jcc(asm_cc cc,asm_i32 off){
	ASM_FPF("j%-3s %i\n",asm_cc2s[cc],off);

	asm_o(0x0F);
	asm_o(0x80|cc);
	asm_i(off,4);
}
void asm_jz(asm_i32 off){
	asm_jcc(asm_cc_z,off);
}
void asm_jnz(asm_i32 off){
	asm_jcc(asm_cc_nz,off);
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

