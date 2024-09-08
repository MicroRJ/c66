// gcc       | gcc -g main.c -omain.exe && a.exe
// msvc      | cl /nologo -Zi main.c -Femain.exe && a.exe
// clang-cl  | clang-cl /nologo -Zi main.c -Femain.exe && a.exe
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


/* simple dynamic array tool */
typedef struct{
	i32 max,min;
} t_array;


#define ARRAY(D) ((t_array*)(D))[-1]
#define ARRAY_MAX(D) ((D)?ARRAY(D).max:0)
#define ARRAY_MIN(D) ((D)?ARRAY(D).min:0)

#define ARRAY_LENGTH ARRAY_MIN
#define ARRAY_FREE(D) ((D)?(free(&ARRAY(D)),D=0),0:0)
#define ARRAY_POP(D) ((D)?ARRAY(D).min-=1:0)
#define ARRAY_GROW(D,N) (array_alloc((void**)&(D),sizeof(*D),N,N))
#define ARRAY_ADD(D,T) do { i32 X = ARRAY_GROW(D,1); D[X] = T; } while(0)

int array_alloc(void **var,int per,int res,int com) {
	t_array*arr=0;
	int max=0,min=0;
	if (*var!=0){
		arr=&ARRAY(*var);
		max=arr->max;
		min=arr->min;
	}
	if (max+res<com){
		res+=(com-(max+res));
	}
	if (min+res>max){
		if(min+res>(max<<=1)){
			max=min+res;
		}
		arr=realloc(arr,sizeof(t_array)+per*max);
	}
	if (arr!=0){
		arr->max=max;
		arr->min=min+com;
	}
	*var=arr+1;
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

/* expression/value descriptor
	Reg: is a register, expression has
	already been evaluated.
	Var: is a stack based address, can
	read and write to it.
	Jump: is a boolean like expression represented
	by a set of true and false jumps
	Int: An actual integer
	Proc: Any function pointer
	Closure: A function pointer within another one
*/
enum specty{Nil=0,Reg,Var,Jump,Str,Int,Proc,Closure};
/* complementary jump list for the expression,
(offsets) */
typedef struct{
	int *tj,*fj;
}jset;
typedef struct{
	enum specty type;
	i64 info;
	//like whether the expression set
	//zf flag, for instance sub or cmp
	//or test or any arithmetic op...
	int flags;
}spec;
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

//(* register management *)
int reg2regs(int r){return 1<<r;}
int usingregs(int r){return~regs&r;}
int usingreg(asm_reg r){return~regs&reg2regs(r);}
void freereg(asm_reg r){regs|=reg2regs(r);}
void usereg(asm_reg r){
	if(usingreg(r))err("already using register");
	regs&=~reg2regs(r);
}
//(* spec *)
int nil(spec*e){return(e->type==Nil);}
int var(spec*e){return(e->type==Var);}
int dig(spec*e){return(e->type==Int);}
int reg(spec*e){return(e->type==Reg);}
void dismiss(spec*e){
	if(reg(e))freereg(inf(e));
	// set(e,0,0);
}
void spec2reg(spec*e,asm_reg r){
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
/* assuming all jumps are of
the same kind */
void patch(int *j){int i;
	for(i=0;i<ARRAY_LENGTH(j);i++){
		((i32*)(xmem+j[i]))[-1]=(xcur-xmem)-j[i];
	}
}
void unary(spec*e,jset*js);
void expr2(spec*x,jset*js,int flags,int u);
/* "Do I Look Like A Carry A Pencil", Blitz, Jason Statham */
void unary(spec*e,jset*js){
	set(e,Nil,0);
	if(!tok)goto esc;
	switch(tok){
		case'(':{lex();
			expr2(e,js,0,0);
			if(tok==')')lex();
			else err("expected ')'");
		}break;
		case'-':{lex();
			unary(e,js);
			spec2reg(e,asm_rcx);
			asm_loadi(asm_rax,0);
			asm_sub32(asm_rax,asm_rcx);
			set(e,Reg,asm_rax);
			usereg(asm_rax);
			freereg(asm_rcx);
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
	if(tok=='('){lex();
		if(ty(e)!=Closure&&ty(e)!=Proc){
			err("expected function");
		}
		asm_subqi(asm_rsp,40);
		if(usingregs(asm_regs_arg)){
			err("invalid memory state, not all arg registers are available");
		}
		int stat,i=0;
		stat=regs;
		spec y;
		while(tok&&tok!=')'){
			if(!(regs&asm_regs_arg)){
				err("too many arguments, max of 4");
			}
			/* todo: handle this properly */
			jset _js={0};
			unary(&y,&_js);
			if(nil(&y))goto esc;

			spec2reg(&y,asm_arg_regs_list[i++]);

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
		if(tok==')')lex();
		else err("expected ')'");
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
int offs(){return(int)(xcur-xmem);}
/* convert some expression
to a jump expression */
void spec2jump(spec*e){
	if(ty(e)!=Jump){
		spec2reg(e,asm_rax);
		if(!reg(e)||inf(e)!=asm_rax)err("impossible");
		asm_cmpi(asm_rax,0);
		dismiss(e);
		set(e,Jump,0);
	}
}
void expr(spec*e,int flags,int u){
	jset js={0};
	expr2(e,&js,flags,u);

	if((js.tj)||(js.fj)){
		if(reg(e)&&inf(e)!=asm_rax)err("impossible");
		spec2reg(e,asm_rax);
		if(!reg(e)||inf(e)!=asm_rax)err("impossible");
		asm_cmpi(asm_rax,0);
		dismiss(e);
		asm_jz(0);
		ARRAY_ADD(js.fj,offs());

		patch(js.tj);
		asm_loadi(asm_rax,1);
		asm_jmp(0);
		u8 *j=xcur;
		patch(js.fj);
		asm_loadi(asm_rax,0);
		((i32*)j)[-1]=xcur-j;
		ARRAY_FREE(js.tj);
		ARRAY_FREE(js.fj);
		usereg(asm_rax);
		set(e,Reg,asm_rax);
	}
}
void expr2(spec*x,jset*js,int flags,int u){
	int o;

	unary(x,js);
	if(nil(x))goto esc;

	/* > left associative, >= right associative */
	while(prec(o=tok)>u){
		lex();
		/* todo: avoid compares when expression
		sets the zf flag, '1-1' '1==1' */
		if(o==TOK_LOR){
			spec2jump(x)  ;
			asm_jnz(0)    ; ARRAY_ADD(js->tj,offs());
			patch(js->fj) ; ARRAY_FREE(js->fj);
			expr2(x,js,flags,prec(o));
			if(nil(x))goto esc;
		}else if(o==TOK_LAND){
			spec2jump(x)  ;
			asm_jz(0)     ; ARRAY_ADD(js->fj,offs());
			patch(js->tj) ; ARRAY_FREE(js->tj);
			expr2(x,js,flags,prec(o));
			if(nil(x))goto esc;
		} else {spec y;

			expr(&y,flags,prec(o));
			if(nil(&y))goto esc;

			if(reg(x)&&reg(&y)){
				err("impossible");
			}
			if(!reg(x)){
				if(!usingreg(asm_rax))spec2reg(x,asm_rax);
				else if(!usingreg(asm_rcx))spec2reg(x,asm_rcx);
				else err("expression too complex");
			}
			if(!reg(&y)){
				if(!usingreg(asm_rax))spec2reg(&y,asm_rax);
				else if(!usingreg(asm_rcx))spec2reg(&y,asm_rcx);
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
		if(!var(x)){
			err("expected variable (a...z)");
		}
		expr(&y,0,0);
		if(reg(&y)&&(inf(&y)!=asm_rax)){
			err("impossible");
		}
		if(!reg(&y)){
			spec2reg(&y,asm_rax);
		}
		asm_store32(asm_rbp,-sizeof(int)*(inf(x)+1),inf(&y));
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
				spec2reg(&e,asm_rax);
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
				asm_store32(asm_rbp,-sizeof(int)*((name[0]-'a')+1),r);
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
			spec2reg(&e,asm_rax);

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
			lex();
			if(tok!='(')err("expected '('");else lex();
			save=regs;
			expr(&e,0,0);
			regs=save;
			spec2reg(&e,asm_rax);
			asm_cmpi(asm_rax,0);
			asm_jz(0);
			u8 *temp=xcur;
			freereg(asm_rax);
			if(nil(&e))goto esc;
			if(tok!=')')err("expected ')'");else lex();
			stat();
			((i32*)temp)[-1]=xcur-temp;
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