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


typedef   signed long long int   i64;
typedef unsigned long long int   u64;
typedef int                      i32;
typedef unsigned char            u8;
typedef unsigned char            i8;
typedef short                    i16;
typedef struct{i32 max,min;}S;


#define KWSET(_) \
_(void)_(unsigned)_(signed)_(char)_(long)_(int)_(float)_(double)\
_(i64)_(i32)_(i16)_(i8)\
_(u64)_(u32)_(u16)_(u8)\
_(sizeof)\
_(if)_(else)_(for)_(return)_(switch)_(break)_(continue)


/* all other single char punctuator
are their ascii values */
enum tokty{
	tok_EOF=0,tok_NAME=0x80,tok_INT,tok_STR,
	tok_COL2,tok_GE,tok_LE,tok_SHL,tok_SHR,tok_LAND,tok_LOR,
#define KW(NAME) tok_##NAME,
	KWSET(KW)
#undef KW
};
typedef enum{DT_VOID,DT_INT,DT_PTR,DT_PROC,DT_STR}dataty;
static char *dataty2s[]={"void","integer","pointer","function","string"};
/* Cond: No data, means the expression is a
   comparison of sorts e.i a < b, see asm_cc.
   Reg: data is in a register (info).
   Var: is a local variable, offset from rbp (info).
   Const: data itself (info).
   tj and fj are true and false jumps.
*/
enum specki{Nil=0,Cond,Reg,Var,Glob,Const};
typedef struct{
	enum specki ki;
	dataty ty;
	i64 info;
	int flags;
	int *tj,*fj;
}spec;
typedef struct{
	dataty retty;
	int arity;
	char **paramids;
	dataty *paramtys;
	int pos;
	void *ptr;
}proto;
enum{entity_nil=0,entity_fn,entity_local};
typedef struct{
	char*name;
	int  type;
	int flags;
	int level;
	union{
		struct{
			dataty datatype;
			int    stackpos;
		}local;
		proto proto;
		i64   info;
	};
}entity;
#define VARSIZE sizeof(i64)
#define B_ENDED  1
#define B_RETURN 2
typedef struct{
	int flags;
	int xscope;
	int xstack;
}block;
typedef struct{
	proto *pro;
	int ystack;
	int zstack;
}funcstate;
typedef struct{
	int tar;
	int*fjs;
}loopstate;
#if defined(_WIN32)
HMODULE msvcrt;
HMODULE kernel32;
HMODULE user32;
#endif

/* executable memory */
static u8*xmem,*xcur;
/* temporary storage & permanent/global storage */
static char tmem[4096];
static char gmem[4096];
static i32 tuse,guse;
/* input base and input cursor */
static char *inb,*in;
/* token starting and ending position in
stream, string value, integer value and
type, for this and the next token */
static char*tokc,*ntokc;
static char*toke,*ntoke;
static char*toks,*ntoks;
static i64  toki,ntoki;
static int  tok,ntok;
static int line=1;
/* per expression really */
static i32 regs=asm_regs_gpr;

static funcstate fs;
static block     bl;
static loopstate ls;

static int level;
static int nloops;
/* entity storage */
static entity entities[1024];
static int scope;


/* platform functions */
i64 getclockfreq() {
#if defined(_WIN32)
	LARGE_INTEGER x;
	QueryPerformanceFrequency(&x);
	return x.QuadPart;
#endif
}
i64 getclocktime() {
#if defined(_WIN32)
	LARGE_INTEGER i;
	QueryPerformanceCounter(&i);
	return i.QuadPart;
#endif
}
void *getproc(char *name){
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
u8 *execalloc(int size) {
#if defined(_WIN32)
	return VirtualAlloc(NULL,size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
#endif
}
char *gettextf(char *name){char *data;
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
/* basic error handling */
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
/* basic memory routines */
int pos(){
	return(int)(xcur-xmem);
}
void xput(u8 x){
	ASM_FPF("%x ",x);
	*xcur++=x;
}
void xputs(i64 x,int n){
	while(n--)xput(x),x>>=8;
}
/* these functions are used by the assembler */
void asm_i(asm_i64 x,int n){
	xputs(x,n);
}
void asm_o(asm_i32 x){
	xput(x);
}
void tput(char x){
	if(tuse>=sizeof(tmem)){
		err("not implemented, fixup allocations");
	}
   // tuse&=sizeof(tmem)-1;
	tmem[tuse++]=x;
}
char *tget(){
	return tmem+tuse;
}
void gset(char x){
	gmem[guse++]=x;
}
char *gget(int x){
	return gmem+x;
}
/* convert to permanent/global string */
char *gs(char*s){
	char *g=gmem+guse;
	memcpy(g,s,strlen(s)+1);
	guse+=strlen(s)+1;
	return g;
}


/* basic dynamic string tool, may or may not
be too portable */
#define slen(s) ((s)?((S*)(s))[-1].min:0)
#define sfree(s) ((s)?(free(&((S*)(s))[-1]),s=0),0:0)
#define sgrow(s,n) (sget((void**)&(s),sizeof(*s),n,n))
#define sadd(s,x) do{int i=sgrow(s,1);s[i]=x;}while(0)

int sget(void **v,int i,int r,int c){
	S*s=0;
	int max=0;
	int min=0;
	if (*v!=0){
		s=&((S*)*v)[-1];
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
		s=realloc(s,sizeof(S)+i*max);
	}
	if (s!=0){
		s->max=max;
		s->min=min+c;
	}
	*v=s+1;
	return min;
}
//(* bit scan forward tool *)
int bsf(int b){int i;
	if(b)for(i=0;i<32;i++)if(b&1<<i)return i;
	return-1;
}
int stackalloc(int size){
	int stack=fs.ystack;
	fs.ystack+=size;
	if(fs.ystack>fs.zstack){
		fs.zstack=fs.ystack;
	}
	return stack;
}
/* register management */
int reg2regs(int r){return 1<<r;}
int usingregs(int r){return~regs&r;}
int usingreg(asm_reg r){return~regs&reg2regs(r);}
void freereg(asm_reg r){regs|=reg2regs(r);}
void usereg(asm_reg r){
	if(usingreg(r))err("register already in use");
	regs&=~reg2regs(r);
}
/* expression/values */
int eki(spec*e){return e->ki;}
int ety(spec*e){return e->ty;}
i64 inf(spec*e){return e->info;}
void set(spec*e,enum specki ki,dataty ty,i64 info){
	e->ki=ki,e->ty=ty,e->info=info;
	e->flags=0;
}
int isnoj(spec*e){return(!e->fj&&!e->tj);}
int isnil(spec*e){return(e->ki==Nil);}
int isvar(spec*e){return(e->ki==Var);}
int isreg(spec*e){return(e->ki==Reg);}
int isptrty(spec*e){return(e->ty==DT_PTR);}
int iskstr(spec*e){return(e->ki==Const&&e->ty==DT_STR);}
int iskint(spec*e){return(e->ki==Const&&e->ty==DT_INT);}

void unuse(spec*e){
	if(isreg(e))freereg(inf(e));
}
/* entities */
int decl(char*name,int type,i64 info){
	entities[scope].name=name;
	entities[scope].type=type;
	entities[scope].info=info;
	return scope++;
}
entity getentity(char*name){int i;
	for(i=scope-1;i>=0;i--){
		if(!strcmp(entities[i].name,name)){
			return entities[i];
		}
	}
	return(entity){0};
}
/* get a single character */
int cget(){
	if(*in=='\r'){
		in++;
		if(*in=='\n')in++;
		line++;
		return '\n';
	}else if(*in=='\n'){
		line++;
	}
	return *in++;
}
/* map alphanumeric ascii chars to a continuous range 0...36 */
int cmap(char a){
	if(a>='0'&&a<='9')return a-'0';
	if(a>='a'&&a<='z')return 10+a-'a';
	if(a>='A'&&a<='Z')return 10+a-'A';
	if(a=='_')return 36;
	return -1;
}
void scan(){retry:
	while((*in==' ')||(*in=='\t')||(*in=='\n')||(*in=='\r'))cget();

	tokc=ntokc;
	toke=ntoke;
	toks=ntoks;
	toki=ntoki;
	tok =ntok;

	ntokc=in;
	switch(*in){
		case '0'...'9':{
			ntoki=0;
			do{
				ntoki*=10;
				ntoki+=cmap(cget());
			}while(*in>='0'&&*in<='9');
			ntok=tok_INT;
		}break;
		case '\'':{
			cget();
			ntok=tok_INT;
			ntoki=cget();
         // while(*in!='\''){
         //    ntoki<<=8;
         //    ntoki|=cget();
         // }
			if(cget()!='\'')err("expected '\''");
		}break;
		case'_':case 'A'...'Z':case 'a'...'z':{
			if((sizeof(tmem)-tuse)<128){
				tuse=0;
			}
			ntoks=tget();
			do tput(cget());
			while(cmap(*in)!=-1);
			tput(0);
			ntok=tok_NAME;
			ntoki=(i64)ntoks;
		}break;
		case '"':{
			if((sizeof(tmem)-tuse)<128){
				tuse=0;
			}
			cget();
			ntoks=tget();
			while((*in!='\0')&&(*in!='"')){
				if(*in=='\\'){
					cget();
					if(*in=='x'||*in=='0'){
						int i=0;
						cget();
						int b=16>>(*in=='0');
						if(b==16&&cmap(*in)==-1){
							err("\\x used with no following hex digits");
						}
						while((cmap(*in)>=0)&&(cmap(*in)<b)){
							i*=b;
							i+=cmap(cget());
						}
						tput(i);
					}else if(*in=='r'){
						cget();
						tput('\r');
					}else if(*in=='n'){
						cget();
						tput('\n');
					}else{
						err("unknown escape sequence");
					}

				}else{
					tput(cget());
				}
			}
			tput(0);
			if(*in!='"')err("expected '\"'");
			cget();
			ntok=tok_STR;
			ntoki=(i64)ntoks;
		}break;
		case '/':{
			if(in[1]=='/'){
				while(*in!='\0'&&*in!='\r'&&*in!='\n'){
					cget();
				}
				goto retry;
			}else goto def;
		}break;
		case'<':{
			ntok=cget();
			if(*in=='=')ntok=tok_LE ,cget();else
			if(*in=='<')ntok=tok_SHL,cget();
		}break;
		case'>':{
			ntok=cget();
			if(*in=='=')ntok=tok_GE ,cget();else
			if(*in=='>')ntok=tok_SHR,cget();
		}break;
		case':':{
			ntok=cget();
			if(*in==':')ntok=tok_COL2,cget();
		}break;
		case'&':{
			ntok=cget();
			if(*in=='&')ntok=tok_LAND,cget();
		}break;
		case'|':{
			ntok=cget();
			if(*in=='|')ntok=tok_LOR,cget();
		}break;
		default:{def:
			ntok=cget();
		}break;
	}
	esc:;
	ntoke=in;
}
void lex(){retry:
	scan();
	switch(ntok){
		case tok_STR:{
			int len=strlen(ntoks);
			ntoki=guse;
			memcpy(gmem+guse,ntoks,len+1);
			guse+=len+1;
		}break;
		case tok_NAME:{
         /* few builtin macros */
			if(!strcmp(ntoks,"__LINE__"))(ntok=tok_INT,ntoki=line);
			else if(!strcmp(ntoks,"__FUNC__"))(ntok=tok_STR,ntoki=(i64)(ntoks="no function"));
			else if(!strcmp(ntoks,"__FILE__"))(ntok=tok_STR,ntoki=(i64)(ntoks="no file"));
			else if(!strcmp(ntoks,"__DATE__"))(ntok=tok_STR,ntoki=(i64)(ntoks="no date"));
			else{
            #define KW(NAME) if(!strcmp(ntoks,#NAME))ntok=tok_##NAME;else
				KWSET(KW) ;
            #undef KW
			}
		}break;
		case'#':{
			while(*in && (*in!='\n')&&(*in!='\r')){
				cget();
			}
         /* macros are treated like comments */
			goto retry;
		}break;
	}
	esc:;
}
int eat(int tk){return(tok==tk)?(lex(),1):0;}

int typesize(dataty type){
	switch(type){
		case DT_VOID:return 0;
		case DT_PTR:case DT_STR:
		case DT_PROC:
		case DT_INT:return sizeof(i64);
	}
	err("impossible");
	return-1;
}
int getvoff(int sp,dataty ty){
	return -sp-typesize(ty);
}
/* ~ code generation */
/* patch a jump string */
void pjs(int *j){int i;
	for(i=0;i<slen(j);i++){
		((i32*)(xmem+j[i]))[-1]=(xcur-xmem)-j[i];
	}
}
void regeval(spec*e,asm_reg r);
void e2cc(spec*e){
	if(eki(e)!=Cond){
		regeval(e,asm_rax);
		if(!isreg(e)||inf(e)!=asm_rax)err("impossible");
		asm_cmpi(asm_rax,0);
		unuse(e);
		set(e,Cond,DT_INT,asm_cc_z);
	}
}
void ejz(spec*e){
	e2cc(e);
	asm_jcc(inf(e),0);
	sadd(e->fj,pos());
}
int jump(){
	asm_jmp(0);
	return pos();
}
void patch(int j){
	((i32*)&xmem[j])[-1]=pos()-j;
}
void go2(int l){
	asm_jmp(0);
	((i32*)xcur)[-1]=l-pos();
}
void prune(spec*e){
   /* use setcc when dst branch is cmp */
	if(eki(e)==Cond&&e->fj&&!e->tj){
		regeval(e,asm_rax);
		unuse(e);
		int j=jump();
		pjs(e->fj);
		usereg(asm_rax);
		asm_loadqi(asm_rax,0);
		patch(j);
		set(e,Reg,DT_INT,asm_rax);
	}else if(eki(e)==Cond&&!e->fj&&e->tj){
		regeval(e,asm_rax);
		unuse(e);
		int j=jump();
		pjs(e->tj);
		usereg(asm_rax);
		asm_loadqi(asm_rax,1);
		patch(j);
		set(e,Reg,DT_INT,asm_rax);
	}else if(e->tj||e->fj){
		ejz(e);
		pjs(e->tj);
		usereg(asm_rax);
		asm_loadqi(asm_rax,1);
		int j=jump();
		pjs(e->fj);
		asm_loadqi(asm_rax,0);
		patch(j);
		set(e,Reg,DT_INT,asm_rax);
	}
	if(e->tj)sfree(e->tj);
	if(e->fj)sfree(e->fj);
}
/* evaluates to rvalue */
void regeval(spec*e,asm_reg r){
	if(r<0||r>=asm_num_gpr_regs)err("invalid argument");
	if(isreg(e))return;
	usereg(r);
	if(eki(e)==Cond){
		asm_scc(r,asm_icc(inf(e)));
		set(e,Reg,e->ty,r);
	}
   #if 0
   else if(eki(e)==Ptr){
      asm_loadq(r,inf(e),0);
   /* todo: rename Str to Glob */
   }
   #endif
	else if(eki(e)==Glob){
		err("impossible");
		if(ety(e)==DT_STR){
			asm_loadqi(r,(i64)gget(inf(e)));
		}else err("impossible");
		set(e,Reg,e->ty,r);
	}else if(eki(e)==Var){
		asm_loadq(r,asm_rbp,getvoff(inf(e),ety(e)));
		set(e,Reg,ety(e),r);
	}else if(eki(e)==Const){
      /* 64-bits or less */
		asm_loadqi(r,inf(e));
		set(e,Reg,e->ty,r);
	}else err("impossible");
}
void merge(spec*x,spec y){
	int i;
	for(i=0;i<slen(y.fj);i++)sadd(x->fj,y.fj[i]);
	for(i=0;i<slen(y.tj);i++)sadd(x->tj,y.tj[i]);
	set(x,y.ki,y.ty,y.info);
}
void unary(spec*e);
void asisexpr(spec*x,int flags,int u);
void expr(spec*e,int flags,int u);
/* "Do I Look Like A Carry A Pencil", Blitz, Jason Statham */
void unary(spec*e){
	set(e,Nil,DT_VOID,0);
	if(!tok)goto esc;
	switch(tok){
		case'&':{
			lex();
			unary(e);
			if(!isvar(e))err("invalid l-value");
			usereg(asm_rax);
			asm_leaq(asm_rax,asm_rbp,getvoff(inf(e),ety(e)));
			set(e,Reg,DT_PTR,asm_rax);
		}break;
		case'(':{
			lex();
			expr(e,0,1);
			if(!eat(')'))err("expected ')'");
		}break;
		case'-':{
			lex();
			unary(e);
			if(iskint(e)){
				set(e,Const,DT_INT,-inf(e));
			}else{
				regeval(e,asm_rcx);
				asm_loadqi(asm_rax,0);
				asm_subq(asm_rax,asm_rcx);
				unuse(e);
				usereg(asm_rax);
				set(e,Reg,e->ty,asm_rax);
			}
		}break;
		case tok_sizeof:{
			expr(e,0,0);
         /* todo: global strings */
			if(iskstr(e)){
				set(e,Const,DT_INT,strlen((char*)inf(e))+1);
			}else{
				set(e,Const,DT_INT,VARSIZE);
			}
			unuse(e);
		}break;
		case tok_NAME:{
			entity en=getentity(toks);
			if(en.type==entity_fn){
				set(e,Const,DT_PROC,en.info);
			}else if(en.type==entity_local){
				set(e,Var,en.local.datatype,en.local.stackpos);
			}else if(strlen(toks)>1){
				printf("warning '%s': implicit function declaration\n",toks);
				int sym=decl(gs(toks),entity_fn,0);
				entities[sym].proto.ptr=getproc(toks);
				set(e,Const,DT_PROC,sym);
			}else{
				err("undeclared identifier");
            // set(e,Var,DT_PTR,getvoff(toks[0]));
			}
			lex();
		}break;
		case tok_INT:{
			set(e,Const,DT_INT,toki);
			lex();
		}break;
		case tok_STR:{
			set(e,Const,DT_STR,(i64)toks);
			lex();
		}break;
		default:;
	}
   /* "rusty nail is in the area", Joy Ride */
	retry:
	// -- function call
	if(eat('(')){
		if(ety(e)!=DT_PROC){
			err("not a function");
		}
		if(usingregs(asm_regs_args)){
			err("invalid memory state, not all argument registers are available");
		}
		int status=regs;
		int nargs=0;
		int argstack=0;
		while((tok)&&(tok!=')')){
			spec y={};
			expr(&y,0,1);
			if(isnil(&y))goto esc;
			if(regs&asm_regs_args){
				regeval(&y,asm_arg_regs_list[nargs++]);
			}else{
				if(iskint(&y)){
					asm_storeqi(asm_rsp,argstack,inf(&y));
				}else{
					regeval(&y,asm_rax);
					unuse(&y);
					asm_storeq(asm_rsp,argstack,asm_rax);
				}
			}
			argstack+=VARSIZE;
			if(!eat(','))break;
		}
		stackalloc(argstack);
		regs=status;

		proto p=entities[inf(e)].proto;
		usereg(asm_rax);
		if(p.ptr){
			asm_loadqi(asm_rax,(i64)(p.ptr));
		}else{
         /* todo: add to list of proto-calls
         to be patched instead... */
			asm_loadqi(asm_rax,(i64)(xmem+p.pos));
		}
		asm_call(asm_rax);
		set(e,Reg,DT_INT,asm_rax);
		if(!eat(')'))err("expected ')'");
		goto retry;
	}else if(eat('[')){
		spec y={};
		expr(&y,0,0);
		if(!eat(']'))err("expected ']'");
		regeval(&y,asm_rax);
		regeval(e,asm_rcx);
		asm_addq(asm_rcx,asm_rax);
		set(e,Reg,DT_PTR,asm_rcx);
		unuse(&y);
		goto retry;
	}
	esc:;
}
/* token precedence map */
int tpmap(int t){
	if(t=='*'||t=='/'||t=='%')return 7;
	if(t=='+'||t=='-')return 6;
	if(t==tok_SHL||t==tok_SHR)return 5;
	if(t==tok_GE||t==tok_LE||t=='<'||t=='>')return 4;
	if(t==tok_LAND)return 3;
	if(t==tok_LOR)return 2;
	if(t==',')return 1;
	return-1;
}
void expr(spec*e,int flags,int u){
	asisexpr(e,flags,u);
	prune(e);
}
/* 'x' is lvalue */
void assign(spec*x,int flags){
	if(tok=='='){
		lex();
		if(!isptrty(x))err("invalid l-value");
		spec y={};
		asisexpr(&y,0,0);
		if(isvar(x)&&isnoj(&y)&&iskint(&y)){
			asm_storeqi(asm_rbp,getvoff(inf(x),ety(x)),inf(&y));
		}else{
			prune(&y);
			regeval(&y,asm_rax);
			if(!isreg(&y))err("impossible");
			if(isvar(x)){
				asm_storeq(asm_rbp,getvoff(inf(x),ety(x)),inf(&y));
			}else{
				asm_storeq(inf(x),0,inf(&y));
			}
		}
		if(!isnoj(&y))err("impossible");
		*x=y;
	}
}
void asisexpr(spec*x,int flags,int u){
	unary(x);
	if(isnil(x))goto esc;
   /* > left associative, >= right associative */
	int o;
	while(tpmap(o=tok)>u){
		lex();
		if(o==','){
			regeval(x,asm_rax);
			unuse(x);
			expr(x,flags,tpmap(o));
		}else if((o==tok_LOR)||(o==tok_LAND)){
			e2cc(x);
			if(o==tok_LOR){
				asm_jnz(0);
				sadd(x->tj,pos());
				pjs(x->fj);
				sfree(x->fj);
			}else{
				asm_jz(0);
				sadd(x->fj,pos());
				pjs(x->tj);
				sfree(x->tj);
			}
			spec y={};
			asisexpr(&y,flags,tpmap(o));
			if(isnil(&y))goto esc;
			merge(x,y);
		}else{
			spec y={};
			expr(&y,flags,tpmap(o));
			if(isnil(&y))goto esc;
			if(isreg(x)&&isreg(&y))err("impossible");

			// regeval(x,getreg(1<<asm_rax|1<<asm_rcx));
			// regeval(&y,getreg(1<<asm_rax|1<<asm_rcx));
			#if 1
			if(!isreg(x)){
				if(!usingreg(asm_rax))regeval(x,asm_rax);
				else if(!usingreg(asm_rcx))regeval(x,asm_rcx);
				else err("expression too complex");
			}
			if(!isreg(&y)){
				if(!usingreg(asm_rax))regeval(&y,asm_rax);
				else if(!usingreg(asm_rcx))regeval(&y,asm_rcx);
				else err("expression too complex");
			}
			#endif
			if(!isreg(x))err("impossible");
			if(!isreg(&y))err("impossible");
         // if(inf(x)!=asm_rax)err("impossible");
         // if(inf(&y)!=asm_rcx)err("impossible");
			if(o=='<'||o=='>'||o==tok_GE||o==tok_LE){
				asm_cmpq(inf(x),inf(&y));
				unuse(x);
				unuse(&y);
				set(x,Cond,DT_INT,
				o=='<'?asm_cc_ge:o==tok_LE?asm_cc_g:
				o=='>'?asm_cc_le:o==tok_GE?asm_cc_l:-1);
			}else{
				if(o==tok_SHR){
					asm_shrq(asm_rax);
				}else if(o==tok_SHL){
					asm_shlq(asm_rax);
				}else if(o=='*'){
					asm_mulq(asm_rax,asm_rcx);
				}else if(o=='-'){
					asm_subq(asm_rax,asm_rcx);
				}else if(o=='+'){
					asm_addq(asm_rax,asm_rcx);
				}else err("impossible");
				freereg(asm_rcx);
				set(x,Reg,x->ty,asm_rax);
			}
		}
	}
	esc:;
	return assign(x,flags);
}
void stat();
void poked(int pos,int d){
	((i32*)&xmem[pos])[-1]=d;
}
void body(){
	block bl_=bl;
	bl.xstack=0;
	bl.flags=0;
   /* skip function's code */
	int fj=jump();
   /* todo: code alignment? */
	proto *p=fs.pro;
	p->pos=pos();
	asm_push(asm_rbp);
	asm_movq(asm_rbp,asm_rsp);
	asm_subqi(asm_rsp,0);
	int sub_rsp_pos=pos();
	int i;
	for(i=0;i<p->arity;i++){
		int ofs=40-8*i;
		decl(p->paramids[i],entity_local,ofs);
		if(i<asm_num_gpr_arg_regs){
			asm_storeq(asm_rbp,ofs,asm_arg_regs_list[i]);
		}
	}
	stat();
	fs.zstack=(fs.zstack+15&~15);
	asm_leave();
	poked(sub_rsp_pos,fs.zstack);
	asm_ret();
	patch(fj);
	bl=bl_;
	fs.ystack=bl.xstack;
}
dataty typename(int *size_){
	dataty type=-1;
	int size=-1;
	switch(tok){
		case tok_void:{
			lex();
			type=DT_VOID;
			size=0;
		}break;
		case tok_char:case tok_i8:case tok_u8:{
			lex();
			type=DT_INT;
			size=sizeof(i8);
		}break;
		case tok_i16:case tok_u16:{
			lex();
			type=DT_INT;
			size=sizeof(i16);
		}break;
		case tok_i64:case tok_u64:{
			lex();
			type=DT_INT;
			size=sizeof(i64);
		}break;
		case tok_long:{
			lex();
			type=DT_INT;
			size=sizeof(int);
		}break;
		case tok_int:case tok_i32:case tok_u32:{
			lex();
			type=DT_INT;
			size=sizeof(i32);
		}break;
		default:;
	}
	*size_=size;
	return type;
}
void stat(){
	switch(tok){
		case';':{
			lex();
		}break;
		case'{':{
			block bl_=bl;
			level++;
			lex();
			while(tok&&tok!='}')stat();
			if(!eat('}'))err("expected '}'");
			level--;
			bl=bl_;
			fs.ystack=bl.xstack;
		}break;
		case tok_break:{
			if(!nloops)err("not in a loop");
			lex();
			bl.flags|=B_ENDED;
			asm_jmp(0);
			sadd(ls.fjs,pos());
		}break;
		case tok_continue:{
			if(!nloops)err("not in a loop");
			lex();
			bl.flags|=B_ENDED;
			go2(ls.tar);
		}break;
		case tok_return:{
			bl.flags|=B_ENDED;

			dataty retty=fs.pro->retty;

			lex();
			if(tok!=';'){
				if(retty==DT_VOID){
					err("void function may not return a value");
				}
				spec e={};
				expr(&e,0,0);
				regeval(&e,asm_rax);
				if(ety(&e)!=retty){
					fprintf(stderr,"error: type mismatch %s, %s\n", dataty2s[retty],dataty2s[ety(&e)]);
					err("type mismatch");
				}
				unuse(&e);
			}else if(retty!=DT_VOID){
				err("function does not return a value");
			}
			asm_leave();
			asm_ret();
			if(!eat(';'))err("expected ';'");
		}break;
		case tok_void:
		case tok_long:case tok_int:case tok_char:
		case tok_i16:case tok_u16:
		case tok_i64:case tok_u64:
		case tok_i32:case tok_u32:
		case tok_u8: case tok_i8:{
			int size;
			dataty type=typename(&size);

			if(tok!=tok_NAME)err("expected identifier or '('");
			char *name=gs(toks);
			int sym=decl(name,-1,-1);
			lex();

         /* function prototype & definition */
			if(eat('(')){
				entities[sym].type=entity_fn;
				proto *p=&entities[sym].proto;
				p->retty=type;
				p->pos=-1;
				if(tok!=')')do{
					type=typename(&size);
					if(type==-1)err("expected typename");
					sadd(p->paramtys,type);
					if(tok!=')'&&tok!=','){
						if(tok!=tok_NAME)err("expected 'id'");
						sadd(p->paramids,gs(toks));
					}else{
						sadd(p->paramids,".");
					}
					p->arity++;
					lex();
				}while(eat(','));
				if(!eat(')'))err("expected ')'");
				if(tok=='{'){
					funcstate fs_=fs;
					fs.pro=p;
					fs.zstack=0;
					fs.ystack=0;
					body();
					fs=fs_;
				}
				// if(bl.proto>=sizeof(protos))err("too many nested functions");
				// protos[bl.proto++]=p;
			}else{
				/* variable */
            // x_stack=x_stack+16&~15;
				entities[sym].type=entity_local;
				entities[sym].local.stackpos=fs.ystack;
				entities[sym].local.datatype=type;

				spec e={};
				set(&e,Var,DT_PTR,fs.ystack);
				stackalloc(size);

				assign(&e,0);

				unuse(&e);
				if(!eat(';'))err("expected ';'");
			}
		}break;
      /* rewire loop:
      cond -> (body | exit)
      body -> (iter + cond)
      to:
      cond -> (body | exit)
      body -> iter
      iter -> cond */
		case tok_for:{
			loopstate pls=ls;
			nloops++;
			spec e={};
			lex();
			if(!eat('('))err("expected '('");
			expr(&e,0,0);
			unuse(&e);
			if(!eat(';'))err("expected ';'");
			int c=pos();
			asisexpr(&e,0,0);
			ejz(&e);
			int c2e=pos();
			unuse(&e);
			if(!eat(';'))err("expected ';'");
			ls.tar=jump();
			expr(&e,0,0);
			unuse(&e);
			go2(c);
			patch(ls.tar);
			if(!eat(')'))err("expected ')'");
			stat();
			go2(ls.tar);
			patch(c2e);
			pjs(ls.fjs);
			nloops--;
			ls=pls;
		}break;
		case tok_switch:{
			lex();
			spec e={};
			expr(&e,0,0);
			stat();
			#if 0
			if(!eat('{'))err("expected '{'");
			while(tok&&tok!='}'){
			}break;
			if(!eat('}'))err("expected '}'");
			#endif
		}break;
		case tok_if:{
			lex();
			if(!eat('('))err("expected '('");
			spec e={};
			asisexpr(&e,0,0);
			if(isnil(&e))goto esc;
			if(!eat(')'))err("expected ')'");
			ejz(&e);
			pjs(e.tj);
			stat();
			if(eat(tok_else)){
				int j=jump();
				pjs(e.fj);
				stat();
				patch(j);
			}else{
				pjs(e.fj);
			}
		}break;
		default:{
			spec e={};
			expr(&e,0,0);
			unuse(&e);
			if(regs!=asm_regs_gpr)err("impossible!");
			if(!eat(';'))err("expected ';'");
		}break;
		case tok_EOF:;
	}
	esc:;
}
/* pretty much identical to 'body' */
void comp(){
	lex();
	lex();
	asm_push(asm_rbp);
	asm_movq(asm_rbp,asm_rsp);
	asm_subqi(asm_rsp,0);
	int sub_rsp_pos=pos();
	while(tok)stat();
   fs.zstack=(fs.zstack+15&~15);/*+8*/
	poked(sub_rsp_pos,fs.zstack);
	asm_loadqi(asm_rax,0);
	asm_leave();
	asm_ret();
}
int fib(int a){
	if(a<2){
		return a;
	}
	return fib(a-2)+fib(a-1);
}
void foo(int rcx,int rdx,int r8,int r9,int ss0,int ss1){
}
void foocaller(){
	foo(1,2,3,4,5,6);
}
int main(int argc,char**args){
	foocaller();
#if defined(_WIN32)
	msvcrt=LoadLibraryA("msvcrt.dll");
	if(!msvcrt)err("error 'msvcrt.dll': could not load");
	kernel32=LoadLibraryA("Kernel32.dll");
	if(!kernel32)err("error 'Kernel32.dll': could not load");
	user32=LoadLibraryA("User32.dll");
	if(!user32)err("error 'User32.dll': could not load");
#endif
	xmem=xcur=execalloc(4096*4);

	inb=in=gettextf("tests.c66");
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