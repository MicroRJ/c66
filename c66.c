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
typedef struct{i32 max,min;}S;

#define VARSIZE sizeof(i64)


/* set of all the keywords we support */
#define KWSET(_) _(sizeof)_(if)_(for)_(return)_(void)_(unsigned)_(signed)_(long)_(int)_(float)_(double)

/* all other single char punctuator
are their ascii values */
enum tokty{
	TOK_EOF=0,TOK_NAME=0x80,TOK_INT,TOK_STR,
	TOK_COL2,TOK_SHL,TOK_SHR,TOK_LAND,TOK_LOR,
#define KW(NAME) TOK_##NAME,
	KWSET(KW)
#undef KW
};


/* Cond: No data, means the expression is a
	comparison of sorts e.i a < b, see asm_cc.
	Reg: registers holds data.
	Ptr: register holds memory address.
	Var: actual memory offset, relative to
	rsp pointer.
	Int: An actual integer.
	tj and fj are true and false jump strings,
	used for bypassing or short-circuiting
	some arbitrary expression. e.i
	'1 && 2', '2' has one false gate, because
	where 1 to be false, the entire expression
	would be false. this jump could be patched
	to the false branch of an if statement.
	'1 || 2', '2' has one true gate, ditto.
*/
enum specty{Nil=0,Cond,Reg,Var,Str,Int,Ptr,Proc,Closure};
typedef struct{
	enum specty type;
	i64 info;
	int flags;
	int *tj,*fj;
}spec;


enum{entity_nil=0,entity_fn,entity_param};
typedef struct{
	char*name;
	int type;
	i64 info;
}entity;


typedef struct{
	char*name;
	char*repl;
	char**args;
}macro;

static macro *macros;
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
/* current line, function, file & date */
static int fline=1;
static char *ffunc;
static char *ffile;
static char *fdate;
/* entity storage */
static entity entities[32];

/* current function state, tracked
locally, see 'function' */
static i32 regs=asm_regs_gpr;
static int nentities;
static int xstack;

/* platform stuff */
#if defined(_WIN32)
HMODULE msvcrt;
HMODULE kernel32;
HMODULE user32;
#endif



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
int xget(){
	return(int)(xcur-xmem);
}
void xput(u8 x){
	*xcur++=(u8)x;
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
int getvoff(char letter){
	return -VARSIZE*(letter-'a'+1);
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
int ty(spec*e){return e->type;}
i64 inf(spec*e){return e->info;}
void set(spec*e,i32 type,i64 info){
	e->type=type,e->info=info;
	e->flags=0;
}
int noj(spec*e){return(!e->fj&&!e->tj);}
int nil(spec*e){return(e->type==Nil);}
int var(spec*e){return(e->type==Var);}
int ptr(spec*e){return(e->type==Ptr);}
int reg(spec*e){return(e->type==Reg);}
void dismiss(spec*e){
	if(reg(e)||ptr(e))freereg(inf(e));
}
/* entities */
void decl(char*name,int type,i64 info){
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
int cget(){
	if(*in=='\r'){
		in++;
		if(*in=='\n')in++;
		fline++;
		return '\n';
	}else if(*in=='\n'){
		fline++;
	}
	return *in++;
}
/* map alphanumeric ascii chars to a continuous
range 0..36 */
//0..9 => 0..9
//A..Z => 10..35
//a..z => 10..35
//_ => 36
int cmap(char a){
	if(a>='0'&&a<='9')return a-'0';
	if(a>='a'&&a<='z')return 10+a-'a';
	if(a>='A'&&a<='Z')return 10+a-'A';
	if(a=='_')return 36;
	return -1;
}
/* get a single basic token, no keywords or macro
expansions, only punctuator, ids, numbers and
the likes, strings and names are in temporary storage*/
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
			ntok=TOK_INT;
		}break;
		case '\'':{
			cget();
			ntok=TOK_INT;
			ntoki=cget();
			// while(*in!='\''){
			// 	ntoki<<=8;
			// 	ntoki|=cget();
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
			ntok=TOK_NAME;
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
			ntok=TOK_STR;
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
			if(*in=='<')ntok=TOK_SHL,cget();
		}break;
		case'>':{
			ntok=cget();
			if(*in=='>')ntok=TOK_SHR,cget();
		}break;
		case':':{
			ntok=cget();
			if(*in==':')ntok=TOK_COL2,cget();
		}break;
		case'&':{
			ntok=cget();
			if(*in=='&')ntok=TOK_LAND,cget();
		}break;
		case'|':{
			ntok=cget();
			if(*in=='|')ntok=TOK_LOR,cget();
		}break;
		default:{def:
			ntok=cget();
		}break;
	}
	esc:;
	ntoke=in;
}
char *pp(char **args,char *string){
	/* todo: to be implemented */
	return 0;
}
void lex(){retry:
	scan();
	switch(ntok){
		case TOK_STR:{
			int len=strlen(ntoks);
			ntoki=guse;
			memcpy(gmem+guse,ntoks,len+1);
			guse+=len+1;
		}break;
		/* convert to macro or keyword */
		case TOK_NAME:{
			for(int i=0;i<slen(macros);i++){
				if(!strcmp(macros[i].name,ntoks)){
					// char *repl=pp(macros[i].args,macros[i].repl);
					// ins(repl);
					goto esc;
				}
			}
			/* few builtin macros */
			if(!strcmp(ntoks,"__LINE__"))(ntok=TOK_INT,ntoki=fline);
			else if(!strcmp(ntoks,"__FUNC__"))(ntok=TOK_STR,ntoki=(i64)(ntoks="no function"));
			else if(!strcmp(ntoks,"__FILE__"))(ntok=TOK_STR,ntoki=(i64)(ntoks="no file"));
			else if(!strcmp(ntoks,"__DATE__"))(ntok=TOK_STR,ntoki=(i64)(ntoks="no date"));
			else{
				#define KW(NAME) if(!strcmp(ntoks,#NAME))ntok=TOK_##NAME;else
				KWSET(KW) ;
				#undef KW
			}
		}break;
		case'#':{
			enum{
				d_invalid=-1,
				d_define,
				d_if,
				d_endif
			};
			int type=d_invalid;
			char**args=0;
			char *repl=0;
			char *name=0;
			scan();
			if(ntok!=TOK_NAME){
				err("invalid preprocessing directive");
			}
			if(!strcmp(ntoks,"define")){
				type=d_define;

				scan();
				if(ntok!=TOK_NAME){
					err("macro name must be an identifier");
				}
				if(*ntoke!='('&&*ntoke!=' '){
					err("whitespace required after macro name");
				}
				if(strlen(ntoks)){
					for(int i=0;i<slen(macros);i++){
						if(!strcmp(macros[i].name,ntoks)){
							err("macro redefinition");
						}
					}
				}else err("macro name missing");
				name=gs(ntoks);

				/* handle parameter list */
				scan();
				if(ntok=='('){
					scan();
					while(ntok!=')'){
						if(ntok!=TOK_NAME){
							err("invalid token in macro parameter list");
						}

						char *arg=malloc(strlen(ntoks)+1);
						strcpy(arg,ntoks);
						sadd(args,arg);

						scan();

						if(ntok!=',')break;
						scan();
					}
					if(ntok!=')')err("expected ')'");
				}
				in=ntoke;
				cget();
				while(*in && (*in!='\n')&&(*in!='\r')){
					sadd(repl,cget());
				}
				sadd(repl,'\0');

				macro m;
				m.name=name;
				m.args=args;
				m.repl=repl;
				sadd(macros,m);
			}else if(!strcmp(ntoks,"if")){
				type=d_if;
				scan();
				if(ntok!=TOK_INT && toki!=0){
					err("not supported");
				}
				/* todo: implement this properly */
				while(*in){
					cget();
				}
				tok=ntok=TOK_EOF;
				goto esc;
			}else if(!strcmp(ntoks,"endif")){
				type=d_endif;
				scan();
			}
			/* finish rest of the line */
			in=ntoke;
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
/* patch a jump string */
void pjs(int *j){int i;
	for(i=0;i<slen(j);i++){
		((i32*)(xmem+j[i]))[-1]=(xcur-xmem)-j[i];
	}
}
void place(spec*e,asm_reg r);
void e2cc(spec*e){
	if(ty(e)!=Cond){
		place(e,asm_rax);
		if(!reg(e)||inf(e)!=asm_rax)err("impossible");
		asm_cmpi(asm_rax,0);
		dismiss(e);
		set(e,Cond,asm_cc_z);
	}
}
void ejz(spec*e){
	e2cc(e);
	asm_jcc(inf(e),0);
	sadd(e->fj,xget());
}
typedef u8 *hole;
typedef u8 *label;
hole jump(){
	asm_jmp(0);
	return xcur;
}
void patch(hole j){
	((i32*)j)[-1]=xcur-j;
}
void go2(label l){
	asm_jmp(0);
	((i32*)xcur)[-1]=l-xcur;
}
void prune(spec*e){
	/* use setcc when dst branch is cmp */
	// ((1+!e->tj-!e->fj)>>1);
	if(ty(e)==Cond&&e->fj&&!e->tj){
		place(e,asm_rax);
		dismiss(e);
		hole j=jump();
		pjs(e->fj);
		usereg(asm_rax);
		asm_loadqi(asm_rax,0);
		patch(j);
		set(e,Reg,asm_rax);
	}else if(ty(e)==Cond&&!e->fj&&e->tj){
		place(e,asm_rax);
		dismiss(e);
		hole j=jump();
		pjs(e->tj);
		usereg(asm_rax);
		asm_loadqi(asm_rax,1);
		patch(j);
		set(e,Reg,asm_rax);
	}else if(e->tj||e->fj){else_:
		ejz(e);
		pjs(e->tj);
		usereg(asm_rax);
		asm_loadqi(asm_rax,1);
		hole j=jump();
		pjs(e->fj);
		asm_loadqi(asm_rax,0);
		patch(j);
		set(e,Reg,asm_rax);
	}
	if(e->tj)sfree(e->tj);
	if(e->fj)sfree(e->fj);
}
void place(spec*e,asm_reg r){
	if(r<0||r>=asm_num_gpr_regs)err("invalid argument");
	if(reg(e)||ptr(e))return;
	usereg(r);
	if(ty(e)==Cond){
		asm_scc(r,asm_icc(inf(e)));
	}else if(ty(e)==Ptr){
		asm_loadq(r,inf(e),0);
	}else if(ty(e)==Str){
		asm_loadqi(r,(i64)gget(inf(e)));
	}else if(ty(e)==Var){
		asm_loadq(r,asm_rbp,inf(e));
	}else if(ty(e)==Int||ty(e)==Closure||ty(e)==Proc){
		asm_loadqi(r,inf(e));
	}
	set(e,Reg,r);
}
void merge(spec*x,spec y){
	int i;
	for(i=0;i<slen(y.fj);i++)sadd(x->fj,y.fj[i]);
	for(i=0;i<slen(y.tj);i++)sadd(x->tj,y.tj[i]);
	set(x,y.type,y.info);
}
void unary(spec*e);
void asisexpr(spec*x,int flags,int u);
void expr(spec*e,int flags,int u);
/* "Do I Look Like A Carry A Pencil", Blitz, Jason Statham */
void unary(spec*e){
	set(e,Nil,0);
	if(!tok)goto esc;
	switch(tok){
		case'&':{
			lex();
			unary(e);
			if(!var(e))err("invalid l-value");
			usereg(asm_rax);
			asm_leaq(asm_rax,asm_rbp,inf(e));
			set(e,Reg,asm_rax);
		}break;
		case'(':{
			lex();
			expr(e,0,1);
			if(!eat(')'))err("expected ')'");
		}break;
		case'-':{
			lex();
			unary(e);
			if(ty(e)!=Int){
				place(e,asm_rcx);
				asm_loadqi(asm_rax,0);
				asm_subq(asm_rax,asm_rcx);
				dismiss(e);
				set(e,Reg,asm_rax);
				usereg(asm_rax);
			}else{
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
			set(e,Var,getvoff(toks[0]));
			entity en=getentity(toks);
			if(en.type!=entity_nil){
				set(e,Closure,en.info);
			} else if(strlen((toks))>1){
				set(e,Proc,(i64)getproc(toks));
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
		if(usingregs(asm_regs_args)){
			err("invalid memory state, not all argument registers are available");
		}
		int stat,i=0;
		stat=regs;
		//-32 first 4 arguments shadow space
		int stack=(26*VARSIZE+16&~15)-32;
		while((tok)&&(tok!=')')){
			spec y={};
			expr(&y,0,1);
			if(nil(&y))goto esc;
			if(regs&asm_regs_args){
				place(&y,asm_arg_regs_list[i++]);
			}else{
				place(&y,asm_rax);
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
		place(e,asm_rax);
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
		spec y={};
		expr(&y,0,0);
		if(!eat(']'))err("expected ']'");
		place(&y,asm_rax);
		place(e,asm_rcx);
		asm_addq(asm_rcx,asm_rax);
		set(e,Ptr,asm_rcx);
		dismiss(&y);
		goto retry;
	}
	esc:;
}
/* token precedence map */
int tpmap(int t){
	if(t=='*'||t=='/'||t=='%')return 7;
	if(t=='+'||t=='-')return 6;
	if(t==TOK_SHL||t==TOK_SHR)return 5;
	if(t=='<'||t=='>')return 4;
	if(t==TOK_LAND)return 3;
	if(t==TOK_LOR)return 2;
	if(t==',')return 1;
	return-1;
}
void expr(spec*e,int flags,int u){
	asisexpr(e,flags,u);
	prune(e);
}
void asisexpr(spec*x,int flags,int u){

	unary(x);
	if(nil(x))goto esc;

	/* > left associative, >= right associative */
	int o;
	while(tpmap(o=tok)>u){
		lex();
		if(o==','){
			place(x,asm_rax);
			dismiss(x);
			expr(x,flags,tpmap(o));
		}else if((o==TOK_LOR)||(o==TOK_LAND)){
			e2cc(x);
			if(o==TOK_LOR){
				asm_jnz(0);
				sadd(x->tj,xget());
				pjs(x->fj);
				sfree(x->fj);
			}else{
				asm_jz(0);
				sadd(x->fj,xget());
				pjs(x->tj);
				sfree(x->tj);
			}
			spec y={};
			asisexpr(&y,flags,tpmap(o));
			if(nil(&y))goto esc;
			merge(x,y);
		}else{
			spec y={};
			expr(&y,flags,tpmap(o));
			if(nil(&y))goto esc;

			if(reg(x)&&reg(&y)){
				err("impossible");
			}
			if(!reg(x)){
				if(!usingreg(asm_rax))place(x,asm_rax);
				else if(!usingreg(asm_rcx))place(x,asm_rcx);
				else err("expression too complex");
			}
			if(!reg(&y)){
				if(!usingreg(asm_rax))place(&y,asm_rax);
				else if(!usingreg(asm_rcx))place(&y,asm_rcx);
				else err("expression too complex");
			}
			if(!reg(x))err("impossible");
			if(!reg(&y))err("impossible");
			// if(inf(x)!=asm_rax)err("impossible");
			// if(inf(&y)!=asm_rcx)err("impossible");
			if(o=='<'||o=='>'){
				asm_cmpq(inf(x),inf(&y));
				dismiss(x);
				dismiss(&y);
				set(x,Cond,o=='<'?asm_cc_ge:asm_cc_le);
			}else{
				if(o==TOK_SHR){
					asm_shrq(asm_rax);
				}else if(o==TOK_SHL){
					asm_shlq(asm_rax);
				}else if(o=='*'){
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
	/* right associative, hence here */
	if(tok=='='){
		lex();
		if(!var(x)&&!ptr(x))err("invalid l-value");
		spec y={};
		asisexpr(&y,0,0);
		if(var(x)&&noj(&y)&&ty(&y)==Int){
			asm_storeqi(asm_rbp,inf(x),inf(&y));
		}else{
			prune(&y);
			place(&y,asm_rax);
			if(!reg(&y)&&!ptr(&y))err("impossible");
			if(var(x)){
				asm_storeq(asm_rbp,inf(x),inf(&y));
			}else{
				asm_storeq(inf(x),0,inf(&y));
			}
		}
		if(!noj(&y))err("impossible");
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
		case TOK_return:{
			spec e={};
			lex();
			if(tok!=';'){
				expr(&e,0,0);
				place(&e,asm_rax);
				dismiss(&e);
			}
			asm_leave();
			// asm_pop(asm_rbp);
			asm_ret();
			endstat();
		}break;
		case TOK_long:case TOK_int:case TOK_void:{
			int typesize=0;
			if(eat(TOK_void)){
				typesize=0;
			}else if(eat(TOK_long)){
				typesize=sizeof(long);
			}else if(eat(TOK_int)){
				typesize=sizeof(int);
			}
			if(tok!=TOK_NAME)err("expected 'id'");
			lex();

			/* emit jump to skip function's code */
			hole fj=jump();

			char *name=gs(toks);
			decl(name,entity_fn,(i64)xcur);

			asm_push(asm_rbp);
			asm_moveq(asm_rbp,asm_rsp);
			asm_subqi(asm_rsp,(26*VARSIZE) + 16 & ~15);

			int scope=nentities;
			int arity=0;

			if(!eat('('))err("expected '('");
			if(tok!=')')do{
				if(tok!=TOK_NAME)err("expected 'id'");
				name=toks;
				if(strlen(name)>1)err("one letter variable names only");

				if(arity<asm_num_gpr_arg_regs){
					int r=asm_arg_regs_list[arity];
					asm_storeq(asm_rbp,getvoff(name[0]),r);
				}
				arity++;

				#if 0
				name=malloc(strlen(toks)+1);
				memcpy(name,toks,strlen(toks)+1);
				decl(name,entity_param,arity++);
				#endif
				lex();
			}while(eat(','));
			if(!eat(')'))err("expected ')'");
			stat();
			asm_leave();
			asm_ret();
			patch(fj);
			/* restore previous state */
			nentities=scope;
		}break;
		/* since we can't (but could) append the iter
		expr at the end of the loop's body, rewire
		it from:
		cond -> (body or exit)
		body -> iter -> cond
		to:
		cond -> (body or exit)
		body -> iter
		iter -> cond */
		case TOK_for:{
			spec e={};
			lex();
			if(!eat('('))err("expected '('");
			expr(&e,0,0);
			dismiss(&e);
			if(!eat(';'))err("expected ';'");
			label c=xcur;
			asisexpr(&e,0,0);
			ejz(&e);
			dismiss(&e);
			hole c2e=xcur;
			if(!eat(';'))err("expected ';'");
			hole i=jump();
			expr(&e,0,0);
			dismiss(&e);
			go2(c);
			patch(i);
			if(!eat(')'))err("expected ')'");
			stat();
			go2(i);
			patch(c2e);
		}break;
		case TOK_if:{
			spec e={};
			lex();
			if(!eat('('))err("expected '('");
			asisexpr(&e,0,0);
			if(nil(&e))goto esc;
			if(!eat(')'))err("expected ')'");
			ejz(&e);
			pjs(e.tj);
			stat();
			pjs(e.fj);
		}break;
		default:{
			spec e={};
			expr(&e,0,0);
			dismiss(&e);
			if(regs!=asm_regs_gpr)err("impossible!");
			endstat();
		}break;
		case TOK_EOF:;
	}
	esc:;
}
void comp(){
	asm_push(asm_rbp);
	asm_moveq(asm_rbp,asm_rsp);
	asm_subqi(asm_rsp,(26*VARSIZE) + 16 & ~15);

	lex();
	lex();
	while(tok)stat();

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
int main(int argc,char**args){
	// fib(40);
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