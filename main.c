// gcc       | gcc -g main.c -omain.exe && a.exe
// msvc      | cl /nologo -Zi main.c -Femain.exe && a.exe
// clang-cl  | clang-cl /nologo -Zi main.c -Femain.exe && a.exe
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <malloc.h>
#include "x86.h"
#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#endif


typedef signed long long int     i64;
typedef unsigned long long int   u64;
typedef unsigned char            u8;
typedef int                      i32;


enum{tok_id=0x80,tok_if,tok_int,tok_str};

enum specty{Nil=0,Reg,Var,Str,Int,Fn};
struct spec{enum specty type;i64 info;};

typedef struct spec spec;

static char *in;

/* executable memory */
static u8 *xmem, *xcur;

/* string memory */
static u8 smem[1024];
static i32 suse;
static i32 regs=asm_regs_gpr;
static int xstk;

char *load_text_file(char *name){char *data;
	FILE *file;
	int size;
	data=0;
	file=fopen(name,"rb");
	if(file==0)goto esc;
	fseek(file,0,SEEK_END);
	size=ftell(file);
	fseek(file,0,SEEK_SET);
	data=malloc(size+1);
	fread(data,1,size,file);
	fclose(file);
	data[size]=0;
	esc:
	return data;
}
void o(u8 x){*xcur++=(u8)x;}
void on(i64 x,int n) {while(n--)o(x),x>>=8;}
void asm_on(asm_i64 x,int n){on(x,n);}
void asm_o(int byte){o(byte);}
/* allocates executable memory, this is considered
a security risk, instead mark executable *after* all the
code has been generated. */
u8 *asm_alloc(int size);
#if defined(_WIN32)
u8 *asm_alloc(int size) {
	return VirtualAlloc(NULL,size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
}
#endif
#define err(note) err_(note,__LINE__)
void err_(char const *note, int line){
	fprintf(stderr,"\nerror[%i]: %s\n",line,note);
#if defined(_WIN32)
	DebugBreak();
#endif
	exit(line);
}
int ty(spec*e){return e->type;}
i64 inf(spec*e){return e->info;}
void set(spec *e,i32 type,i64 info){
	e->type=type,e->info=info;
}
void sset(int x) {smem[suse++]=x;}
u8 *sget(int x) {return smem+x;}
static char temp[64];
static i64 tinfo;
static int thistok;
int tok(){return thistok;};
int feed(){
#if 1
	printf("%c",*in);
#endif
	return *in++;
}
int lex(){int t;
	retry:
	while((*in==' ')||(*in=='\t')||(*in=='\n')||(*in)=='\r')feed();
	t=-1;
	switch(*in){
		case '0'...'9':{i64 a,b;
			a=0,b=1;
			do{
				a=a*b+(feed()-'0');
				b*=10;
			}while(*in>='0'&&*in<='9');
			t=tok_int;
			tinfo=a;
		}break;
		case 'a'...'z':{
			memset(temp,0,sizeof(temp));
			char*s=temp;
			do{*s++=feed();
			}while((*in=='_')||(*in>='a'&&*in<='z')||(*in>='0'&&*in<='9'));
			//yes...
			if(!strcmp(temp,"if"))t=tok_if;
			else t=tok_id;
			tinfo=(i64)temp;
		}break;
		case '"':{int c;
			tinfo=suse;
			t=tok_str;
			feed();
			while((*in!='\0')&&(*in!='"')){
				if((c=feed())=='\\'){
					if(*in=='n')c='\n';
					else err("invalid escape letter");
					feed();
				}
				sset(c);
			}
			if(*in=='"')feed();
			else err("expected '\"'");
			sset(0);
		}break;
		case '/':{
			if(in[1]=='/'){
				while(*in!='\0'&&*in!='\r'&&*in!='\n'){
					feed();
				}
				goto retry;
			}else goto def;
		}break;
		default:{def:
			t=feed();
		}break;
	}
	esc:
	thistok=t;
	return t;
}
int line(){
	while((*in!='\0')&&(*in!='\r')&&(*in!='\n'))feed();
	if(*in=='\r')feed();
	if(*in!='\n')return 0;
	feed();
	return 1;
}
int nil(spec *e){return(e->type==Nil);}
int var(spec *e){return(e->type==Var);}
int dig(spec *e){return(e->type==Int);}
int reg(spec *e){return(e->type==Reg);}
int reg2regs(int r){return 1<<r;}
int usingregs(int r){return~regs&r;}
int usingreg(int r){return~regs&reg2regs(r);}
void freereg(int r){regs|=reg2regs(r);}
void usereg(int r){
	if(usingreg(r))err("already using register");
	regs&=~reg2regs(r);
}
void e2r2(spec *e,int r){
	if(reg(e))return;
	if(r<0||r>=asm_num_gpr_regs)err("invalid argument");
	if(usingreg(r))err("register already in use");
	usereg(r);
	if(ty(e)==Str){
		asm_move64_i(r,(i64)sget(inf(e)));
	}else if(ty(e)==Var){
		asm_load32(r,GPR_RBP,-sizeof(int)*(inf(e)+1));
	}else if(ty(e)==Int){
		/* todo: when come back, make support reg2rex */
		asm_move32_i(r,inf(e));
	}
	set(e,Reg,r);
}
void myprintf(char *fmt,int rcx,int rdx,int r8,int r9){
	printf(fmt,rcx,rdx,r8,r9);
}
void unary(spec*e);
void expr(spec*e,int u);
/* "Do I Look Like A Carry A Pencil", Blitz, Jason Statham */
void unary(spec*e){
	set(e,Nil,0);
	if(!tok())goto esc;
	switch(tok()){
		case'(':{lex();
			expr(e,0);
			if(tok()==')')lex();
			else err("expected ')'");
		}break;
		case'-':{lex();
			unary(e);
			e2r2(e,asm_rcx);
			asm_move32_i(asm_rax,0);
			asm_sub32(asm_rax,asm_rcx);
			set(e,Reg,asm_rax);
			usereg(asm_rax);
			freereg(asm_rcx);
		}break;
		case tok_id:{
			set(e,Var,((char*)tinfo)[0]-'a');
			if(!strcmp((char*)tinfo,"printf")){
				set(e,Fn,(i64)myprintf);
			}
			lex();
		}break;
		case tok_int:{
			set(e,Int,tinfo);
			lex();
		}break;
		case tok_str:{
			set(e,Str,tinfo);
			lex();
		}break;
		default:;
	}
	if(tok()=='('){lex();
		if(ty(e)!=Fn){
			err("expected function");
		}
		asm_sub64_i(GPR_RSP,32);
		if(usingregs(asm_regs_arg)){
			err("invalid memory state, not all arg registers are available");
		}
		int stat,i;
		stat=regs;
		for(i=0;i<asm_num_gpr_arg_regs;++i){spec y;
			unary(&y);
			if(nil(&y))goto esc;
			e2r2(&y,asm_arg_regs_list[i]);
			if(tok()==',')lex();
			else break;
		}
		regs=stat;
		asm_move64_i(asm_rax,inf(e));
		asm_call64_r(asm_rax);
		asm_add64_i(GPR_RSP,32);

		usereg(asm_rax);
		set(e,Reg,asm_rax);
		if(tok()==')')lex();
		else err("expected ')'");
	}
	esc:;
}
int prec(int t){
	if(t=='=')return 1;
	if(t=='+'||t=='-')return 2;
	if(t=='*'||t=='/'||t=='%')return 3;
	return-1;
}
// checkreg(asm_rax);
// checkreg(asm_rcx);
void expr(spec*x,int u){int o;
	unary(x);
	if(nil(x))goto esc;

	while(prec(o=tok())>u){spec y;
		lex();
		expr(&y,prec(o));
		if(nil(&y))goto esc;

		if(o=='='){
			if(!var(x))err("expected variable (a...z)");
			if(reg(&y)&&(inf(&y)!=asm_rax))err("impossible");
			if(!reg(&y))e2r2(&y,asm_rax);
			asm_store32(GPR_RBP,-sizeof(int)*(inf(x)+1),inf(&y));
			*x=y;
		}else{
			if(reg(x)&&reg(&y)){
				err("impossible");
			}
			if(!reg(x)){
				if(!usingreg(asm_rax))e2r2(x,asm_rax);
				else if(!usingreg(asm_rcx))e2r2(x,asm_rcx);
				else err("expression too complex");
			}
			if(!reg(&y)){
				if(!usingreg(asm_rax))e2r2(&y,asm_rax);
				else if(!usingreg(asm_rcx))e2r2(&y,asm_rcx);
				else err("expression too complex");
			}
			if(!reg(x))err("impossible");
			if(!reg(&y))err("impossible");
			if(o=='-'){
				asm_sub32(asm_rax,asm_rcx);
			}else if(o=='+'){
				asm_add32(asm_rax,asm_rcx);
			}else err("impossible");
			freereg(asm_rcx);
			set(x,Reg,asm_rax);
		}
	}
	esc:;
}
void finishstat(){
	if(tok()==';')lex();
	else err("expected ';'");
}
void stat(){int save;
	switch(tok()){
		case '{':{
			lex();
			while(tok()&&tok()!='}')stat();
			if(tok()=='}')lex();else err("expected '}'");
		}break;
		case tok_if:{spec e;
			lex();
			if(tok()=='(')lex();
			else err("expected '('");
			save=regs;
			expr(&e,0);
			regs=save;
			if(nil(&e))goto esc;
			if(tok()==')')lex();
			else err("expected ')'");
			if(tok()=='{')stat();
			else expr(&e,0);
		}break;
		default:{spec e;
			save=regs;
			expr(&e,0);
			regs=save;
			finishstat();
		}break;
	}
	esc:;

}
void comp(){
	asm_push_rbp();
	asm_move64(GPR_RBP,GPR_RSP);
	// lex();
	lex();
	while(tok()){
		stat();
	}
	// do stat(-1); while(line());
	asm_pop_rbp();
	asm_ret();
	printf("\n");
}
void lollygags();
int main(int nargs,char**args){
	xmem=xcur=asm_alloc(1024);
	in=load_text_file("main.gel");
	if(in==0)err("main.gel not found...");
	comp();
	#if 0
	// asm_sub64_i(GPR_RSP,40);
	// asm_add64_i(GPR_RSP,40);
	#endif

	if(xcur-xmem){
		FILE *io;
		io=fopen("main.x86","wb");
		fseek(io,0,SEEK_SET);
		fwrite(xmem,1,xcur-xmem,io);
		fclose(io);
		system("rem main.objdump");
		system("objdump -D -Mintel,x86-64 -b binary -m i386 main.x86 >> main.objdump");

		((int (*)())xmem)();
	}
}
void foo(int rcx, int rdx) {
	printf("foo, rcx: %i, rdx: %i\n", rcx, rdx);
	exit(0);
}
#define TEST(name) test_##name
#define DEFTEST(name) void (test_##name)()
DEFTEST(loads){
	asm_load(asm_rax,GPR_RBP,-4,32);
	asm_load(GPR_R8,GPR_R8,-4,32);
	asm_load(asm_rax,GPR_R8,-4,64);
}
DEFTEST(registers){int i;
	for(i=0;i<8;++i){
		asm_move64(GPR_R8+i,asm_rax+i);
		asm_move64(asm_rax+i,GPR_R8+i);
	}
}
DEFTEST(call_foo){
	asm_move64_i(asm_rcx,3);
	asm_move64_i(GPR_RDX,7);
	asm_move64_i(asm_rax,(i64)foo);
	asm_call64_r(asm_rax);
}
void lollygags() {
	asm_push_rbp();
	asm_move_rbp_rsp();
	TEST(loads)();
	// TEST(registers)();
	// TEST(call_foo)();
	asm_pop_rbp();
	asm_ret();
	#if 0
	asm_push_rbp();
	asm_move64(GPR_RBP,GPR_RSP);
	if(0){
		/* allocate from stack */
		asm_sub64_i(GPR_RSP,16);
	}
	if(0){
		/* store immediate */
		asm_move64_i(asm_rcx,3);
		asm_move64_i(GPR_RDX,7);
	}
	if(0){
		/* store and load */
		asm_store32_i(GPR_RBP,-8,3);
		asm_store32_i(GPR_RBP,-4,7);
		asm_load32(asm_rcx,GPR_RBP,-8);
		asm_load32(GPR_RDX,GPR_RBP,-4);
	}
	if(0){
		/* call foo */
		asm_move64_i(asm_rax,(i64)foo);
		asm_call64_r(asm_rax);
	}
	if(0){
		/* return */
		asm_ret();
	}
	#if 0
	asm_leave();
	asm_pop_rbp();
	#endif

   #if 0
	asm_move32_i(asm_rax,0xffffff);
	asm_move64_i(asm_rax,0xffffffffff);
	asm_move64(GPR_RBP,GPR_RSP);
	asm_move32(GPR_RBP,GPR_RSP);
	asm_store32_i(GPR_RBP,+4,0xffffff);
	asm_store64_i(GPR_RBP,+4,0xffffff);
	asm_load64(asm_rax,GPR_RBP,-4);
	asm_store32(GPR_RBP,-4,asm_rax);
	asm_store64(GPR_RBP,-4,asm_rax);
	asm_sub32(GPR_RBP,asm_rax);
	asm_add32(GPR_RBP,asm_rax);
	asm_sub64(GPR_RBP,asm_rax);
	asm_add64(GPR_RBP,asm_rax);
	asm_sub32_i(GPR_RBP,32);
	asm_sub64_i(GPR_RBP,64);
	#endif

	asm_push_rbp();
	asm_move64(GPR_RBP,GPR_RSP);
	asm_move64(GPR_RDI,asm_rcx);
	asm_store32_i(GPR_RBP,+4,1);
	asm_store32(GPR_RBP,+4,asm_rcx);
	asm_load32(asm_rcx,GPR_RBP,-4);
	asm_move64_i(asm_rax,1);
	asm_cmp32_i(asm_rax,2);
	asm_jl(/*self*/5+1);
	asm_sub32_i(asm_rax,777);
	asm_sub32(asm_rax,asm_rcx);
	asm_add32(asm_rax,asm_rcx);
	asm_move64_i(asm_rax,(i64)&foo);
	asm_call64_r(asm_rax);
	asm_move32(asm_rax,GPR_RDI);
	asm_pop_rbp();
	asm_ret();
	#endif
}
