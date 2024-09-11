/* tests some things */

#define TEST(name) test_##name
#define DEFTEST(name) void (test_##name)()
void tests();


void foo(int rcx, int rdx) {
	printf("foo, rcx: %i, rdx: %i\n", rcx, rdx);
	exit(0);
}
DEFTEST(subs){
	asm_subdi(asm_rax,100);
	asm_subqi(asm_rax,100);
	asm_subdi(asm_r15,100);
}
DEFTEST(compares){
	asm_test(asm_rax,asm_r15,0);
	asm_testq(asm_rax,asm_r15);
	asm_test(asm_r15,asm_rax,0);
	asm_testq(asm_r15,asm_rax);
}
DEFTEST(moves){
	asm_moveq(asm_rax,asm_rcx);
	asm_moveq(asm_rax,asm_r15);
	asm_moveq(asm_r15,asm_rax);
}
DEFTEST(loads){
	asm_loadqi(asm_rax,32);
	asm_loadqi(asm_r8,32);
	asm_loadqi(asm_r8,1llu<<32);
	asm_loadq(asm_rax,asm_rbp,-4);
	asm_loadq(asm_r15,asm_rbp,-128);
	// asm_load(asm_rax,asm_rbp,-4,32);
	// asm_load(asm_r8,asm_r8,-4,32);
	// asm_load(asm_rax,asm_r8,-4,64);
}
DEFTEST(registers){int i;
	for(i=0;i<8;++i){
		asm_moveq(asm_r8+i,asm_rax+i);
		asm_moveq(asm_rax+i,asm_r8+i);
	}
}
DEFTEST(call_foo){
	asm_loadqi(asm_rcx,3);
	asm_loadqi(asm_rdx,7);
	asm_loadqi(asm_rax,(i64)foo);
	asm_call(asm_rax);
}
void tests() {
	asm_push_rbp();
	asm_move_rbp_rsp();
	TEST(loads)();
	// TEST(registers)();
	// TEST(call_foo)();
	asm_pop_rbp();
	asm_ret();
	#if 0
	asm_push_rbp();
	asm_moveq(asm_rbp,asm_rsp);
	if(0){
		/* allocate from stack */
		asm_subqi(asm_rsp,16);
	}
	if(0){
		/* store immediate */
		asm_loadqi(asm_rcx,3);
		asm_loadqi(asm_rdx,7);
	}
	if(0){
		/* store and load */
		asm_storedi(asm_rbp,-8,3);
		asm_storedi(asm_rbp,-4,7);
		asm_loadd(asm_rcx,asm_rbp,-8);
		asm_loadd(asm_rdx,asm_rbp,-4);
	}
	if(0){
		/* call foo */
		asm_loadqi(asm_rax,(i64)foo);
		asm_call(asm_rax);
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
	asm_loadqi(asm_rax,0xffffff);
	asm_loadqi(asm_rax,0xffffffffff);
	asm_moveq(asm_rbp,asm_rsp);
	asm_move32(asm_rbp,asm_rsp);
	asm_storedi(asm_rbp,+4,0xffffff);
	asm_storeqi(asm_rbp,+4,0xffffff);
	asm_loadq(asm_rax,asm_rbp,-4);
	asm_stored(asm_rbp,-4,asm_rax);
	asm_storeq(asm_rbp,-4,asm_rax);
	asm_subd(asm_rbp,asm_rax);
	asm_addd(asm_rbp,asm_rax);
	asm_subq(asm_rbp,asm_rax);
	asm_addq(asm_rbp,asm_rax);
	asm_subdi(asm_rbp,32);
	asm_subqi(asm_rbp,64);
	#endif

	asm_push_rbp();
	asm_moveq(asm_rbp,asm_rsp);
	asm_moveq(GPR_RDI,asm_rcx);
	asm_storedi(asm_rbp,+4,1);
	asm_stored(asm_rbp,+4,asm_rcx);
	asm_loadd(asm_rcx,asm_rbp,-4);
	asm_loadqi(asm_rax,1);
	asm_cmpi(asm_rax,2);
	asm_jl(/*self*/5+1);
	asm_subdi(asm_rax,777);
	asm_subd(asm_rax,asm_rcx);
	asm_addd(asm_rax,asm_rcx);
	asm_loadqi(asm_rax,(i64)&foo);
	asm_call(asm_rax);
	asm_move32(asm_rax,GPR_RDI);
	asm_pop_rbp();
	asm_ret();
	#endif
}
