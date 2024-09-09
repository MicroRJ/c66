// gcc       | gcc -g c66.c -oc66.exe
// msvc      | cl /nologo -Zi c66.c -Fec66.exe
// clang-cl  | clang-cl /nologo -Zi c66.c -Fec66.exe
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <malloc.h>
#include "asm64.h"
#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#endif


typedef signed long long int     i64;
typedef unsigned long long int   u64;
typedef unsigned char            u8;
typedef int                      i32;


/* generic dynamic string/array tool */
typedef struct{i32 max,min;}t_s;

#define p2s(s) ((t_s*)(s))[-1]
#define slen(s) ((s)?p2s(s).min:0)
#define sfree(s) ((s)?(free(&p2s(s)),s=0),0:0)
#define sgrow(s,n) (salloc((void**)&(s),sizeof(*s),n,n))
#define sadd(s,x) do{int i=sgrow(s,1);s[i]=x;}while(0)

int salloc(void **v,int i,int r,int c){
	t_s*s=0;
	int max=0,min=0;
	if (*v!=0){
		s=&p2s(*v);
		max=s->max;
		min=s->min;
	}
	if (max+r<c){
		r+=(c-(max+r));
	}
	if (min+r>max){
		if(min+r>(max<<=1)){
			max=min+r;
		}
		s=realloc(s,sizeof(t_s)+i*max);
	}
	if (s!=0){
		s->max=max;
		s->min=min+c;
	}
	*v=s+1;
	return min;
}


/* all other tokens are ascii */
enum tokty {
	TOK_NONE=-1,
	TOK_NAME=0x80,TOK_INT,TOK_STR,
	TOK_LAND,TOK_LOR,
	TOK_VOID,TOK_IF,TOK_FOR,TOK_RETURN
};


#define KWDEF(_) \
_(TOK_VOID,"void")\
_(TOK_IF,"if")\
_(TOK_FOR,"for")\
_(TOK_RETURN,"return")

/*
	In a logical sense, an expression is ultimately
	made up of two main parts, data and control.

	'spec' refers stores data part, and 'cspec'
	the control flow part.

	Notes:
	- A call to to expr will always return
	data, use expr2 to get control and data.
	- For any control expression, for instance
	'a && b', the caller has to generate the
	final jump, can be jz, jnz, use d2c before
	hand.

	- It is useful to keep boolean expressions
	in a mixed state within the two, this allows
	us to chain multiple control expressions
	together efficiently, and use them effecively
	contextually.
	For instance, within a control structure, we
	are primarly interested in the control flow
	part of the expression.
	Take 'if (a && b)...'

	Here we don't want 'a && b' to end up evaluating
	some data value in some register, to then have
	to have the if stamement test once again and emit
	additional jumps. (e.i convert data to control)

	Instead we want the control of the expression to
	route directly to the approiate clauses.

	So in some cases we want control, in others data,
	and sometimes we just have to convert between
	the two.

	But in principle, we want control structures to
	take control, and data structures, to take data.


	Spec types:
	Jump: No data, means is part of the
	control flow.
	Reg: is a register, data is already within
	a register, cannot be assignment.
	Ptr: is also a register, however the register
	holds a memory address, which can be assigned.
	Var: is a stack based address, can
	read and write to it.
	Int: An actual integer
	Proc: Any function pointer
	Closure: A function pointer from within
	this program.
*/
enum specty{Nil=0,Jump,Reg,Var,Str,Int,Ptr,Proc,Closure};
typedef struct{
	enum specty type;
	i64 info;
	/* todo: like whether the expression set
	zf flag, for instance sub or cmp */
	int flags;
}spec;

/* control flow info for an expression,
used for things like boolean expressions. */
typedef struct{
	int *tj,*fj;
}cspec;

enum{entity_nil=0,entity_fn,entity_param};
typedef struct{
	char*name;
	int type;
	i64 info;
}entity;


//(* globals  *)

/* input stream */
static char *inb,*in;

/* executable memory */
static u8*xmem,*xcur;

static entity entities[32];
static int nentities;

#if defined(_WIN32)
HMODULE win32_;
#endif

static char toks[64];
static char*tokc;
static i64  toki;
static enum tokty tok;

static i32 regs=asm_regs_gpr;
static char gmem[1024];
static i32 guse;


int offs(){return(int)(xcur-xmem);}

void gset(char x){gmem[guse++]=x;}
char *gget(int x){return gmem+x;}

void *procaddr(char *name){
#if defined(_WIN32)
	return GetProcAddress(win32_,name);
#else
	return dlsym(0,name);
#endif
}
//(* bit scan forward *)
int bsf(int b){int i;
	if(b)for(i=0;i<32;i++)if(b&1<<i)return i;
	return-1;
}
char *ftext(char *name){char *data;
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
void i(i64 x,int n){while(n--)o(x),x>>=8;}
void asm_i(asm_i64 x,int n){i(x,n);}
void asm_o(asm_i32 x){o(x);}

#if defined(_WIN32)
u8 *execalloc(int size) {
	return VirtualAlloc(NULL,size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
}
#endif

#define err(note) err_(note,__LINE__)
void err_(char const *note, int line){
#if 1
	printf("%.*s\n",(int)(tokc-inb),inb);
#endif
	fprintf(stderr,"error[%i]: %s\n",line,note);
#if defined(_WIN32)
	DebugBreak();
#endif
	exit(line);
}

int inp(){
	return *in++;
}

/* expression/values */
int ty(spec*e){return e->type;}
i64 inf(spec*e){return e->info;}
void set(spec*e,i32 type,i64 info){
	e->type=type,e->info=info;
	e->flags=0;
}

/* entities */
void newentity(char*name,int type,i64 info){
	// printf("\nnew entity: %s\n",name);
	entities[nentities].name=name;
	entities[nentities].type=type;
	entities[nentities].info=info;
	nentities++;
}
entity getentity(char*name){int i;
	for(i=0;i<nentities;i++){
		if(!strcmp(entities[i].name,name)){
			return entities[i];
		}
	}
	return(entity){0};
}
//(* lexing *)
void lex(){retry:
	while((*in==' ')||(*in=='\t')||(*in=='\n')||(*in)=='\r')inp();
	tokc=in;
	switch(*in){
		case '0'...'9':{
			toki=0;
			do{
				toki=toki*10+(inp()-'0');
			}while(*in>='0'&&*in<='9');
			tok=TOK_INT;
		}break;
		case '_':
		case 'A'...'Z':
		case 'a'...'z':{
			memset(toks,0,sizeof(toks));
			char*s=toks;
			do{*s++=inp();
			}while((*in=='_')||(*in>='a'&&*in<='z')||(*in>='0'&&*in<='9'));
			tok=TOK_NAME;
			toki=(i64)toks;
			//yes...
			#define KW(tk,name) if(!strcmp(toks,name))tok=tk;else
			KWDEF(KW) ;
			#undef KW
		}break;
		case '"':{int c;
			toki=guse;
			tok=TOK_STR;
			inp();
			while((*in!='\0')&&(*in!='"')){
				if((c=inp())=='\\'){
					if(*in=='n')c='\n';
					else err("invalid escape letter");
					inp();
				}
				gset(c);
			}
			if(*in=='"')inp();
			else err("expected '\"'");
			gset(0);
		}break;
		case '/':{
			if(in[1]=='/'){
				while(*in!='\0'&&*in!='\r'&&*in!='\n'){
					inp();
				}
				goto retry;
			}else goto def;
		}break;
		case'&':{
			tok=TOK_NONE,inp();
			if(*in=='&')tok=TOK_LAND,inp();
		}break;
		case'|':{
			tok=TOK_NONE,inp();
			if(*in=='|')tok=TOK_LOR,inp();
		}break;
		default:{def:
			tok=inp();
		}break;
	}
	esc:;
}
int eat(int tk){return(tok==tk)?(lex(),1):0;}

/* register management */
int reg2regs(int r){return 1<<r;}
int usingregs(int r){return~regs&r;}
int usingreg(asm_reg r){return~regs&reg2regs(r);}
void freereg(asm_reg r){regs|=reg2regs(r);}
void usereg(asm_reg r){
	if(usingreg(r))err("already using register");
	regs&=~reg2regs(r);
}
int nil(spec*e){return(e->type==Nil);}
int var(spec*e){return(e->type==Var);}
int ptr(spec*e){return(e->type==Ptr);}
int reg(spec*e){return(e->type==Reg);}
void dismiss(spec*e){
	if(reg(e))freereg(inf(e));
	// set(e,0,0);
}
/* patch a jump string */
void pjs(int *j){int i;
	for(i=0;i<slen(j);i++){
		((i32*)(xmem+j[i]))[-1]=(xcur-xmem)-j[i];
	}
}
/* data to register */
void d2reg(spec*e,asm_reg r){
	if(reg(e))return;
	if(r<0||r>=asm_num_gpr_regs)err("invalid argument");
	if(usingreg(r))err("register already in use");
	usereg(r);
	if(ty(e)==Str){
		asm_loadi(r,(i64)gget(inf(e)));
	}else if(ty(e)==Var){
		asm_loadd(r,asm_rbp,-sizeof(int)*(inf(e)+1));
	}else if(ty(e)==Int){
		/* todo: when come back, make support reg2rex */
		asm_loadi(r,inf(e));
	}
	set(e,Reg,r);
}
/* data to control */
void d2c(spec *e){
	if(ty(e)!=Jump){
		d2reg(e,asm_rax);
		if(!reg(e)||inf(e)!=asm_rax)err("impossible");
		asm_cmpi(asm_rax,0);
		dismiss(e);
		set(e,Jump,0);
	}
}
/* control to data */
void c2d(spec*e,cspec c){
	if((c.tj)||(c.fj)){
		if(reg(e)&&inf(e)!=asm_rax)err("impossible");
		d2reg(e,asm_rax);
		if(!reg(e)||inf(e)!=asm_rax)err("impossible");
		asm_cmpi(asm_rax,0);
		dismiss(e);
		asm_jz(0);
		sadd(c.fj,offs());

		pjs(c.tj);
		asm_loadi(asm_rax,1);
		asm_jmp(0);
		u8 *j=xcur;
		pjs(c.fj);
		asm_loadi(asm_rax,0);
		((i32*)j)[-1]=xcur-j;
		sfree(c.tj);
		sfree(c.fj);
		usereg(asm_rax);
		set(e,Reg,asm_rax);
	}
}
void unary(spec*e,cspec*js);
void expr2(spec*x,cspec*js,int flags,int u);
void expr(spec*e,int flags,int u);
/* "Do I Look Like A Carry A Pencil", Blitz, Jason Statham */
void unary(spec*e,cspec*js){
	set(e,Nil,0);
	if(!tok)goto esc;
	switch(tok){
		case'(':{lex();
			expr2(e,js,0,0);
			if(tok==')')lex();
			else err("expected ')'");
		}break;
		case'-':{
			lex();
			unary(e,js);
			d2reg(e,asm_rcx);
			asm_loadi(asm_rax,0);
			asm_sub32(asm_rax,asm_rcx);
			freereg(asm_rcx);
			set(e,Reg,asm_rax);
			usereg(asm_rax);
		}break;
		case TOK_NAME:{
			entity en;
			set(e,Var,((char*)toki)[0]-'a');
			en=getentity((char*)toki);
			if(en.type!=entity_nil){
				set(e,Closure,en.info);
			} else if(strlen(toks)>1){
				set(e,Proc,(i64)procaddr((char*)toki));
			}
			lex();
		}break;
		case TOK_INT:{
			set(e,Int,toki);
			lex();
		}break;
		case TOK_STR:{
			set(e,Str,toki);
			lex();
		}break;
		default:;
	}

	retry:
	if(eat('(')){
		if(ty(e)!=Closure&&ty(e)!=Proc){
			err("expected function");
		}
		if(usingregs(asm_regs_arg)){
			err("invalid memory state, not all arg registers are available");
		}

		asm_subqi(asm_rsp,40);

		int stat,i=0;
		stat=regs;
		spec y;
		while(tok&&tok!=')'){
			if(!(regs&asm_regs_arg)){
				err("too many arguments, max of 4");
			}
			/* todo: handle this properly */
			cspec _js={0};
			unary(&y,&_js);
			if(nil(&y))goto esc;

			d2reg(&y,asm_arg_regs_list[i++]);

			if(tok==',')lex();
			else break;
		}
		regs=stat;
#if 0
		if(ty(e)==Closure){
			asm_jcall(0);
			((i32*)xcur)[-1]=(u8*)inf(e)-xcur;
		}else{
#endif
		asm_loadi(asm_rax,inf(e));
		asm_call(asm_rax);
#if 0
		}
#endif
		asm_add64_i(asm_rsp,40);

		usereg(asm_rax);
		set(e,Reg,asm_rax);
		if(!eat(')'))err("expected ')'");
		goto retry;
	}else if(eat('[')){spec y;
		expr(&y,0,0);
		if(!eat(']'))err("expected ']'");
		d2reg(&y,asm_rax);
		d2reg(e,asm_rcx);
		asm_addq(asm_rcx,asm_rax);
		set(e,Ptr,asm_rcx);
		dismiss(&y);
		goto retry;
	}
	esc:;
}
int prec(int t){
	if(t==TOK_LAND)return 1;
	if(t==TOK_LOR)return 2;
	if(t=='+'||t=='-')return 3;
	if(t=='*'||t=='/'||t=='%')return 4;
	return-1;
}
void expr(spec*e,int flags,int u){
	cspec c={0};
	expr2(e,&c,flags,u);
	c2d(e,c);
}
void expr2(spec*x,cspec*c,int flags,int u){
	int o;

	unary(x,c);
	if(nil(x))goto esc;

	/* > left associative, >= right associative */
	while(prec(o=tok)>u){
		lex();
		/*
		todo: this system does not work
		well with precedence parsing...
		todo: avoid compares when expression
		sets the zf flag, '1-1' '1==1' */
		if(o==TOK_LOR){spec y;
			d2c(x)     ;
			asm_jnz(0) ; sadd(c->tj,offs());
			pjs(c->fj) ; sfree(c->fj);
			expr2(x,c,flags,prec(o));
			if(nil(x))goto esc;
		}else if(o==TOK_LAND){
			d2c(x)     ;
			asm_jz(0)  ; sadd(c->fj,offs());
			pjs(c->tj) ; sfree(c->tj);
			expr2(x,c,flags,prec(o));
			if(nil(x))goto esc;
		} else {spec y;
			expr(&y,flags,prec(o));
			if(nil(&y))goto esc;

			if(reg(x)&&reg(&y)){
				err("impossible");
			}
			if(!reg(x)){
				if(!usingreg(asm_rax))d2reg(x,asm_rax);
				else if(!usingreg(asm_rcx))d2reg(x,asm_rcx);
				else err("expression too complex");
			}
			if(!reg(&y)){
				if(!usingreg(asm_rax))d2reg(&y,asm_rax);
				else if(!usingreg(asm_rcx))d2reg(&y,asm_rcx);
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

	/* right associative, and also least precedence
	of all expressions, hence here */
	if(tok=='='){spec y;
		lex();
		if(!var(x)&&!ptr(x))err("invalid l-value");

		expr(&y,0,0);
		if(reg(&y)&&(inf(&y)!=asm_rax))err("expression too complex");

		d2reg(&y,asm_rax);
		if(var(x)){
			asm_stored(asm_rbp,-sizeof(int)*(inf(x)+1),inf(&y));
		}else{
			asm_stored(inf(x),0,inf(&y));
		}
		*x=y;
	}
	esc:;
}
void endstat(){
	if(tok!=';')err("expected ';'");
	else lex();
}
void stat(){int save;
	switch(tok){
		case';':{
			lex();
		}break;
		case'{':{
			lex();
			while(tok&&tok!='}')stat();
			if(tok=='}')lex();else err("expected '}'");
		}break;
		case TOK_RETURN:{spec e;
			lex();
			if(tok!=';'){
				expr(&e,0,0);
				d2reg(&e,asm_rax);
				dismiss(&e);
			}
			asm_pop(asm_rbp);
			asm_ret();
			endstat();
		}break;
		case TOK_VOID:{
			lex();
			if(tok!=TOK_NAME)err("expected 'id'");
			char *name=malloc(strlen((char*)toki)+1);
			memcpy(name,(char*)toki,strlen((char*)toki)+1);

			/* emit jump to skip function's code */
			asm_jmp(0);

			newentity(name,entity_fn,(i64)xcur);

			u8* fj=xcur;
			asm_push(asm_rbp);
			asm_moveq(asm_rbp,asm_rsp);

			lex();
			if(tok!='(')err("expected '('");else lex();
			int arity=0;
			int scope=nentities;
			if(tok!=')')do{
				if(tok!=TOK_NAME)err("expected 'id'");
				name=(char*)toki;
				if(strlen(name)>1)err("variable name cannot be greater than 1");
				/* register where the param comes from */
				int r=asm_arg_regs_list[arity++];
				asm_stored(asm_rbp,-sizeof(int)*((name[0]-'a')+1),r);
				#if 0
				name=malloc(strlen((char*)toki)+1);
				memcpy(name,(char*)toki,strlen((char*)toki)+1);
				newentity(name,entity_param,arity++);
				#endif
				lex();
			}while(eat(','));
			if(tok!=')')err("expected ')'");else lex();

			stat();

			asm_pop(asm_rbp);
			asm_ret();

			((i32*)fj)[-1]=xcur-fj;
			nentities=scope;
		}break;
		case TOK_FOR:{spec e;
			// save=regs;
			lex();
			if(tok=='(')lex();else err("expected '('");

			expr(&e,0,0);
			dismiss(&e);

			if(tok==';')lex();else err("expected ';'");

			u8*j0,*j1,*l0,*l1,*l2;

			l0=xcur;
			expr(&e,0,0);
			d2reg(&e,asm_rax);

			asm_cmpi(inf(&e),0);
			asm_jz(0);
			j0=xcur;

			dismiss(&e);

			if(tok==';')lex();else err("expected ';'");

			asm_jmp(0);
			j1=l1=xcur;
			expr(&e,0,0);
			dismiss(&e);

			asm_jmp(0);
			((i32*)xcur)[-1]=l0-xcur;

			((i32*)j1)[-1]=xcur-j1;

			if(tok==')')lex();else err("expected ')'");
			stat();
			asm_jmp(0);
			((i32*)xcur)[-1]=l1-xcur;

			((i32*)j0)[-1]=xcur-j0;
			// regs=save;
		}break;
		case TOK_IF:{spec e;
			cspec c={0};
			lex();
			if(tok!='(')err("expected '('");else lex();
			expr2(&e,&c,0,0);
			if(nil(&e))goto esc;
			if(tok!=')')err("expected ')'");else lex();
			d2c(&e);
			asm_jz(0);
			sadd(c.fj,offs());
			pjs(c.tj);
			stat();
			pjs(c.fj);
		}break;
		default:{spec e;
			save=regs;
			expr(&e,0,0);
			regs=save;
			endstat();
		}break;
	}
	esc:;

}
void comp(){
	asm_push(asm_rbp);
	asm_moveq(asm_rbp,asm_rsp);
	lex();
	while(tok)stat();
	asm_pop(asm_rbp);
	asm_ret();
	printf("\n");
}
// asm_subqi(asm_rsp,40);
// asm_add64_i(asm_rsp,40);
void lollygags();
int main(int nargs,char**args){
#if defined(_MSC_VER)
	win32_=LoadLibraryA("msvcrt.dll");
	if(!win32_)err("error 'msvcrt.dll': could not load");
#endif
	xmem=xcur=execalloc(1024);
	// lollygags();
	#if 1
	inb=in=ftext("tests.c66");
	if(in==0)err("tests.c66 not found...");
	comp();
	#endif

	if(xcur-xmem){
		FILE *io;
		io=fopen("tests.asm64","wb");
		fseek(io,0,SEEK_SET);
		fwrite(xmem,1,xcur-xmem,io);
		fclose(io);
		system("rm tests.objdump");
		system("objdump -D -Mintel,x86-64 -b binary -m i386 tests.asm64 >> tests.objdump");

		((int (*)())xmem)();
	}
}