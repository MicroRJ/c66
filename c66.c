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

#include "test_switch.c66"

typedef signed long long int     i64;
typedef unsigned long long int   u64;
typedef unsigned char            u8;
typedef int                      i32;
#define VARSIZE sizeof(i64)

/* generic dynamic string/array tool */
typedef struct{i32 max,min;}t_s;

#define sss(s) ((t_s*)(s))[-1]
#define slen(s) ((s)?sss(s).min:0)
#define sfree(s) ((s)?(free(&sss(s)),s=0),0:0)
#define sgrow(s,n) (salloc((void**)&(s),sizeof(*s),n,n))
#define sadd(s,x) do{int i=sgrow(s,1);s[i]=x;}while(0)

int salloc(void **v,int i,int r,int c){
	t_s*s=0;
	int max=0,min=0;
	if (*v!=0){
		s=&sss(*v);
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
	TOK_COL2,
	TOK_LAND,TOK_LOR,
	TOK_if,TOK_for,TOK_return,TOK_sizeof,
	TOK_void,TOK_unsigned,TOK_signed,TOK_long,TOK_int,TOK_float,TOK_double,
};


#define KWDEF(_) \
_(TOK_sizeof,"sizeof")\
_(TOK_if,"if")\
_(TOK_for,"for")\
_(TOK_return,"return")\
_(TOK_void,"void")\
_(TOK_unsigned,"unsigned")\
_(TOK_signed,"signed")\
_(TOK_long,"long")\
_(TOK_int,"int")\
_(TOK_float,"float")\
_(TOK_double,"double")

#define MCDEF(_) \
_(MACRO___LINE__,"__LINE__",(tok=TOK_INT,toki=fline))\
_(MACRO___FUNC__,"__func__",(tok=TOK_STR,toki=(i64)"no function"))\
_(MACRO___FILE__,"__FILE__",(tok=TOK_STR,toki=(i64)"no file"))\
_(MACRO___DATE__,"__DATE__",(tok=TOK_STR,toki=(i64)"no date"))

/*
	Jcc: No data, means the expression is a control
	condition, the control condition should jump
	when false, so the inverse of the original
	condition is used instead: !(a < b) == (a >= b).

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
enum specty{Nil=0,Jcc,Reg,Var,Str,Int,Ptr,Proc,Closure};
typedef struct{
	enum specty type;
	i64 info;
	/* todo: like whether the expression set
	zf flag, for instance sub or cmp */
	int flags;
	int *tj,*fj;
}spec;
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

/*simple macro system (todo: implement hashtable)*/
typedef struct{
	char*name;
	char*repl;
	char**args;
}macro;
static macro *macros;

/* executable memory */
static u8*xmem,*xcur;

#if defined(_WIN32)
HMODULE msvcrt;
HMODULE kernel32;
HMODULE user32;
#endif

static char toks[64];
static char*tokc;
static i64  toki;
static int  tok;/*tokty*/

static i32 regs=asm_regs_gpr;

static char gmem[4096];
static i32 guse;
static int fline=1;
static char *ffunc;
static char *ffile;
static char *fdate;

static entity entities[32];
static int nentities;

int offs(){return(int)(xcur-xmem);}

void gset(char x){gmem[guse++]=x;}
char *gget(int x){return gmem+x;}

void *procaddr(char *name){
#if defined(_WIN32)
	void *addr;
	addr=GetProcAddress(msvcrt,name);
	if(!addr)addr=GetProcAddress(kernel32,name);
	if(!addr)addr=GetProcAddress(user32,name);
	return addr;
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
/* expression/values */
int ty(spec*e){return e->type;}
i64 inf(spec*e){return e->info;}
void set(spec*e,i32 type,i64 info){
	e->type=type,e->info=info;
	e->flags=0;
	e->fj=e->tj=0;
}
/* entities */
void newentity(char*name,int type,i64 info){
	entities[nentities].name=name;
	entities[nentities].type=type;
	entities[nentities].info=info;
	nentities++;
}
entity getentity(char*name){int i;
	for(i=nentities-1;i>=0;i--){
		if(!strcmp(entities[i].name,name)){
			return entities[i];
		}
	}
	return(entity){0};
}
/* get a single character */
int inp(){
	if(*in=='\r'){
		in++;
		if(*in=='\n')in++;
		fline++;
		return '\n';
	}else if(*in=='\n'){
		fline++;
		return '\n';
	}
	return *in++;
}
int cmap(char a){
	if(a>='a'&&a<='z')return 10+a-'a';
	if(a>='A'&&a<='Z')return 10+a-'A';
	if(a=='_')return 36;
	if(a>='0'&&a<='9')return a-'0';
	return -1;
}
/* get a single basic token, no keywords or macro
expansions, only punctuator, ids, numbers and
the likes, toks and toki are updated accordingly,
strings are not added to globals. */
void scan(){retry:
	while((*in==' ')||(*in=='\t')||(*in=='\n')||(*in=='\r'))inp();
	tokc=in;
	switch(*in){
		case '0'...'9':{
			toki=0;
			do{
				toki*=10;
				toki+=cmap(inp());
			}while(*in>='0'&&*in<='9');
			tok=TOK_INT;
		}break;
		case '\'':{
			inp();
			tok=TOK_INT;
			toki=inp();
			if(inp()!='\'')err("expected '\''");
		}break;
		case'_':case 'A'...'Z':case 'a'...'z':{
			memset(toks,0,sizeof(toks));
			char*s=toks;
			do{*s++=inp();
			}while(cmap(*in)!=-1);
			tok=TOK_NAME;
			toki=(i64)toks;
		}break;
		case '"':{
			inp();
			memset(toks,0,sizeof(toks));
			char*s=toks;
			while((*in!='\0')&&(*in!='"')){char c;
				if((c=inp())=='\\'){
					if(*in=='n')c='\n';
					else err("invalid escape letter");
					inp();
				}
				*s++=c;
			}
			*s++=0;
			if(*in!='"')err("expected '\"'");
			inp();
			tok=TOK_STR;
			toki=(i64)toks;
		}break;
		case '/':{
			if(in[1]=='/'){
				while(*in!='\0'&&*in!='\r'&&*in!='\n'){
					inp();
				}
				goto retry;
			}else goto def;
		}break;
		case':':{
			tok=inp();
			if(*in==':')tok=TOK_COL2,inp();
		}break;
		case'&':{
			tok=inp();
			if(*in=='&')tok=TOK_LAND,inp();
		}break;
		case'|':{
			tok=inp();
			if(*in=='|')tok=TOK_LOR,inp();
		}break;
		default:{def:
			tok=inp();
		}break;
	}
	esc:;
}
char *pp(char **args,char *string){
	/* todo: to be implemented */
	return 0;
}
void lex(){retry:
	scan();
	switch(tok){
		case TOK_STR:{
			int len=strlen(toks);
			toki=guse;
			memcpy(gmem+guse,toks,len+1);
			guse+=len+1;
		}break;
		/* convert to macro or keyword */
		case TOK_NAME:{
			for(int i=0;i<slen(macros);i++){
				if(!strcmp(macros[i].name,toks)){
					// char *repl=pp(macros[i].args,macros[i].repl);
					// ins(repl);
					goto esc;
				}
			}
			//yes...
			#define MC(macro,name,def) if(!strcmp(toks,name))def;else
			MCDEF(MC)
			#undef MC
			#define KW(tk,name) if(!strcmp(toks,name))tok=tk;else
			KWDEF(KW) ;
			#undef KW
		}break;
		case'#':{
			char**args=0;
			char *repl=0;
			char *name=0;
			scan();
			if(tok!=TOK_NAME){
				err("invalid preprocessing directive");
			}
			if(strcmp(toks,"define")){
				err("invalid preprocessing directive");
			}
			scan();
			if(tok!=TOK_NAME){
				err("macro name must be an identifier");
			}
			if(strlen(toks)){
				for(int i=0;i<slen(macros);i++){
					if(!strcmp(macros[i].name,toks)){
						err("macro redefinition");
					}
				}
			}else err("macro name missing");

			name=malloc(strlen(toks)+1);
			strcpy(name,toks);

			scan();

			if(tok=='('){
				scan();
				while(tok!=')'){
					if(tok!=TOK_NAME){
						err("invalid token in macro parameter list");
					}

					char *arg=malloc(strlen(toks)+1);
					strcpy(arg,toks);
					sadd(args,arg);

					scan();

					if(tok!=',')break;
					scan();
				}
				if(tok!=')')err("expected ')'");
			}
			if(*in!=' '){
				err("whitespace required after macro name");
			}
			inp();

			while((*in!='\n')&&(*in!='\r')){
				sadd(repl,inp());
			}
			sadd(repl,'\0');

			macro m;
			m.name=name;
			m.args=args;
			m.repl=repl;
			sadd(macros,m);
			goto retry;
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
	if(ty(e)==Ptr){
		asm_loadq(r,inf(e),0);
	}else if(ty(e)==Str){
		asm_loadqi(r,(i64)gget(inf(e)));
	}else if(ty(e)==Var){
		asm_loadq(r,asm_rbp,-VARSIZE*(inf(e)+1));
	}else if(ty(e)==Int||ty(e)==Closure||ty(e)==Proc){
		asm_loadqi(r,inf(e));
	}
	set(e,Reg,r);
}
/* jump condition is unspecified */
void e2jcc(spec *e){
	if(ty(e)!=Jcc){
		d2reg(e,asm_rax);
		if(!reg(e)||inf(e)!=asm_rax)err("impossible");
		asm_cmpi(asm_rax,0);
		dismiss(e);
		set(e,Jcc,0);
	}
}
/* convert to false jump */
void e2fj(spec *e,cspec *c){
	e2jcc(e);
	if(inf(e)==0){
		asm_jz(0);
	}else{
		asm_jcc(inf(e),0);
	}
	sadd(c->fj,offs());
}
void c2d(spec*e,cspec c){
	if((ty(e)==Jcc)||(c.tj)||(c.fj)){
		e2fj(e,&c);
		usereg(asm_rax);
		pjs(c.tj);
		asm_loadqi(asm_rax,1);
		asm_jmp(0);
		u8 *j=xcur;
		pjs(c.fj);
		asm_loadqi(asm_rax,0);
		((i32*)j)[-1]=xcur-j;
		sfree(c.tj);
		sfree(c.fj);
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
		case'&':{
			lex();
			unary(e,js);
			if(!var(e))err("invalid l-value");
			usereg(asm_rax);
#if 1
			asm_leaq(asm_rax,asm_rbp,-VARSIZE*(inf(e)+1));
#else
			asm_moveq(asm_rax,asm_rbp);
			asm_subqi(asm_rax,VARSIZE*(inf(e)+1));
#endif
			set(e,Reg,asm_rax);
		}break;
		case'(':{
			lex();
			expr2(e,js,0,0);
			if(!eat(')'))err("expected ')'");
		}break;
		case'-':{
			lex();
			unary(e,js);
			if(ty(e)!=Int){
				d2reg(e,asm_rcx);
				asm_loadqi(asm_rax,0);
				asm_subq(asm_rax,asm_rcx);
				dismiss(e);
				set(e,Reg,asm_rax);
				usereg(asm_rax);
			}else{
				printf("converted: %lli to %lli\n",inf(e),-inf(e));
				set(e,Int,-inf(e));
			}
		}break;
		case TOK_sizeof:{
			expr(e,0,0);
			if(ty(e)==Str){
				set(e,Int,strlen((char*)inf(e))+1);
			}else{
				set(e,Int,VARSIZE);
			}
			dismiss(e);
		}break;
		case TOK_NAME:{
			entity en;
			set(e,Var,((char*)toki)[0]-'a');
			en=getentity(((char*)toki));
			if(en.type!=entity_nil){
				set(e,Closure,en.info);
			} else if(strlen(((char*)toki))>1){
				set(e,Proc,(i64)procaddr(((char*)toki)));
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
	/* rusty nail is in the area, Joy Ride */
	retry:
	if(eat('(')){
		// if(ty(e)!=Closure&&ty(e)!=Proc){
		// 	err("expected function");
		// }
		if(usingregs(asm_regs_arg)){
			err("invalid memory state, not all arg registers are available");
		}
		// asm_subqi(asm_rsp,40);
		int stat,i=0;
		stat=regs;
		//-32 first 4 arguments shadow space
		int stack=(26*VARSIZE+16&~15)-32;
		while((tok)&&(tok!=')')){spec y;
			expr(&y,0,0);
			if(nil(&y))goto esc;
			if(regs&asm_regs_arg){
				d2reg(&y,asm_arg_regs_list[i++]);
			}else{
				d2reg(&y,asm_rax);
				dismiss(&y);
				asm_storeq(asm_rbp,-stack,asm_rax);
				stack-=VARSIZE;
			}
			if(!eat(','))break;
		}
		regs=stat;
#if 0
		if(ty(e)==Closure){
			asm_jcall(0);
			((i32*)xcur)[-1]=(u8*)inf(e)-xcur;
		}else{
#endif
		d2reg(e,asm_rax);
		// asm_loadqi(asm_rax,inf(e));
		asm_call(asm_rax);
		dismiss(e);
#if 0
		}
#endif
		// asm_addqi(asm_rsp,40);

		usereg(asm_rax);
		set(e,Reg,asm_rax);
		if(!eat(')'))err("expected ')'");
		goto retry;
	}else if(eat('[')){
		spec y;
		expr(&y,0,0);
		if(!eat(']'))err("expected ']'");
#if 0
#else
		d2reg(&y,asm_rax);
		d2reg(e,asm_rcx);
		asm_addq(asm_rcx,asm_rax);
#endif
		set(e,Ptr,asm_rcx);
		dismiss(&y);
		goto retry;
	}
	esc:;
}
int prec(int t){
	if(t==TOK_LAND)return 1;
	if(t==TOK_LOR)return 2;
	if(t=='<')return 3;
	if(t=='+'||t=='-')return 4;
	if(t=='*'||t=='/'||t=='%')return 5;
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
		/* todo: avoid compares when expression
		sets the zf flag, '1-1' '1==1' */
		if(o==TOK_LOR){spec y;
			e2jcc(x)   ;
			asm_jnz(0) ; sadd(c->tj,offs());
			pjs(c->fj) ; sfree(c->fj);
			cspec cc={0};
			expr2(x,&cc,flags,prec(o));
			if(nil(x))goto esc;
			for(int i=0;i<slen(cc.fj);++i){
				sadd(c->fj,cc.fj[i]);
			}
			for(int i=0;i<slen(cc.tj);++i){
				sadd(c->tj,cc.tj[i]);
			}
		}else if(o==TOK_LAND){
			e2jcc(x)   ;
			asm_jz(0)  ; sadd(c->fj,offs());
			pjs(c->tj) ; sfree(c->tj);
			cspec cc={0};
			expr2(x,&cc,flags,prec(o));
			if(nil(x))goto esc;
			for(int i=0;i<slen(cc.fj);++i){
				sadd(c->fj,cc.fj[i]);
			}
			for(int i=0;i<slen(cc.tj);++i){
				sadd(c->tj,cc.tj[i]);
			}
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
			if(o=='<'){
				asm_cmpq(inf(x),inf(&y));
				dismiss(x);
				dismiss(&y);
				set(x,Jcc,asm_Jcc_jge);
			}else{
				if(o=='*'){
					asm_mulq(asm_rax,asm_rcx);
				}else if(o=='-'){
					asm_subq(asm_rax,asm_rcx);
				}else if(o=='+'){
					asm_addq(asm_rax,asm_rcx);
				}else err("impossible");
				freereg(asm_rcx);
				set(x,Reg,asm_rax);
			}
		}
	}

	/* right associative, and also least precedence
	of all expressions so far..., hence here */
	if(tok=='='){spec y;
		lex();
		if(!var(x)&&!ptr(x))err("invalid l-value");

		expr(&y,0,0);
		if(reg(&y)&&(inf(&y)!=asm_rax))err("expression too complex");

		d2reg(&y,asm_rax);
		if(var(x)){
			asm_storeq(asm_rbp,-VARSIZE*(inf(x)+1),inf(&y));
		}else{
			asm_storeq(inf(x),0,inf(&y));
		}
		*x=y;
	}
	esc:;
}
void endstat(){
	if(!eat(';'))err("expected ';'");
}
void stat(){int save;
	switch(tok){
		case';':{
			lex();
		}break;
		case'{':{
			lex();
			while(tok&&tok!='}')stat();
			if(!eat('}'))err("expected '}'");
		}break;
		case TOK_return:{spec e;
			lex();
			if(tok!=';'){
				expr(&e,0,0);
				d2reg(&e,asm_rax);
				dismiss(&e);
			}
			asm_leave();
			// asm_pop(asm_rbp);
			asm_ret();
			endstat();
		}break;
		case TOK_long:case TOK_int:
		case TOK_void:{
			int typesize=0;
			if(eat(TOK_void)){
				typesize=0;
			}else if(eat(TOK_long)){
				typesize=sizeof(long);
			}else if(eat(TOK_int)){
				typesize=sizeof(int);
			}

			if(tok!=TOK_NAME)err("expected 'id'");
			char *name=malloc(strlen((char*)toki)+1);
			memcpy(name,(char*)toki,strlen((char*)toki)+1);

			/* emit jump to skip function's code */
			asm_jmp(0);

			newentity(name,entity_fn,(i64)xcur);

			u8* fj=xcur;
			asm_push(asm_rbp);
			asm_moveq(asm_rbp,asm_rsp);
			asm_subqi(asm_rsp,(26*VARSIZE) + 16 & ~15);

			lex();
			if(!eat('('))err("expected '('");
			int arity=0;
			int scope=nentities;
			if(tok!=')')do{
				if(tok!=TOK_NAME)err("expected 'id'");
				name=(char*)toki;
				if(strlen(name)>1)err("variable name cannot be greater than 1");
				/* register where the param comes from */
				int r=asm_arg_regs_list[arity++];
				asm_storeq(asm_rbp,-VARSIZE*((name[0]-'a')+1),r);
				#if 0
				name=malloc(strlen((char*)toki)+1);
				memcpy(name,(char*)toki,strlen((char*)toki)+1);
				newentity(name,entity_param,arity++);
				#endif
				lex();
			}while(eat(','));
			if(!eat(')'))err("expected ')'");

			stat();

			asm_leave();
			// asm_pop(asm_rbp);
			asm_ret();

			((i32*)fj)[-1]=xcur-fj;
			nentities=scope;
		}break;
		/* since we can't store the iter expression
		for later, to emit at the end of the loop
		body, we have to do some rewiring... */
		case TOK_for:{spec e;
			/* l0: label to evaluate the condition,
			l1: label to jump over the iter expression
			l2: label to exit the loop,
			j0: jump over the iter expression to l1,
			l2: jump after the iter expression to l0 */
			u8*j0,*j1,*l0,*l1,*l2;
			lex();

			if(!eat('('))err("expected '('");
			expr(&e,0,0);
			dismiss(&e);
			if(!eat(';'))err("expected ';'");

			l0=xcur;
			expr(&e,0,0);
			d2reg(&e,asm_rax);

			asm_cmpi(inf(&e),0);
			asm_jz(0);
			j0=xcur;

			dismiss(&e);

			if(!eat(';'))err("expected ';'");

			asm_jmp(0);
			j1=l1=xcur;
			expr(&e,0,0);
			dismiss(&e);

			asm_jmp(0);
			((i32*)xcur)[-1]=l0-xcur;

			((i32*)j1)[-1]=xcur-j1;

			if(!eat(')'))err("expected ')'");
			stat();
			asm_jmp(0);
			((i32*)xcur)[-1]=l1-xcur;

			((i32*)j0)[-1]=xcur-j0;
		}break;
		case TOK_if:{spec e;
			cspec c={0};
			lex();
			if(!eat('('))err("expected '('");
			expr2(&e,&c,0,0);
			if(nil(&e))goto esc;
			if(!eat(')'))err("expected ')'");
			e2fj(&e,&c);
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
	asm_subqi(asm_rsp,(26*VARSIZE) + 16 & ~15);
	lex();
	while(tok)stat();
	asm_loadqi(asm_rax,0);
	asm_leave();
	asm_ret();
}
void foo(){
	long long int a=0,b=1;
	(&a)[-1]=7;
	// printf("b: %lli\n",b);
}
int main(int argc,char**args){
	test_switch();
#if defined(_WIN32)
	msvcrt=LoadLibraryA("msvcrt.dll");
	if(!msvcrt)err("error 'msvcrt.dll': could not load");
	kernel32=LoadLibraryA("Kernel32.dll");
	if(!kernel32)err("error 'Kernel32.dll': could not load");
	user32=LoadLibraryA("User32.dll");
	if(!user32)err("error 'User32.dll': could not load");
#endif
	xmem=xcur=execalloc(4096*4);
	// asm_storei(asm_rbp,256,777,0);
	// asm_storei(asm_r15,256,777,0);

	inb=in=ftext("tests.c66");
	if(in==0)err("tests.c66 not found...");
	comp();

	int result=0;
	if(xcur-xmem){
		FILE *io;
		io=fopen("tests.asm64","wb");
		fseek(io,0,SEEK_SET);
		fwrite(xmem,1,xcur-xmem,io);
		fclose(io);
		system("rm tests.objdump");
		system("objdump -D -Mintel,x86-64 -b binary -m i386 tests.asm64 >> tests.objdump");

		result=((int (*)())xmem)();
	}
	return result;
}