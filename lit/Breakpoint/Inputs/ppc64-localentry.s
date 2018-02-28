	.text
	.abiversion 2
	.globl	lfunc
	.p2align	4
	.type	lfunc,@function
lfunc:                                  # @lfunc
.Lfunc_begin0:
.Lfunc_gep0:
	addis 2, 12, .TOC.-.Lfunc_gep0@ha
	addi 2, 2, .TOC.-.Lfunc_gep0@l
.Lfunc_lep0:
	.localentry	lfunc, .Lfunc_lep0-.Lfunc_gep0
# BB#0:
	mr 4, 3
	addis 3, 2, .LC0@toc@ha
	ld 3, .LC0@toc@l(3)
	stw 4, -12(1)
	lwz 4, 0(3)
	lwz 5, -12(1)
	mullw 4, 4, 5
	extsw 3, 4
	blr
	.long	0
	.quad	0
.Lfunc_end0:
	.size	lfunc, .Lfunc_end0-.Lfunc_begin0

	.globl	main
	.p2align	4
	.type	main,@function
main:                                   # @main
.Lfunc_begin1:
.Lfunc_gep1:
	addis 2, 12, .TOC.-.Lfunc_gep1@ha
	addi 2, 2, .TOC.-.Lfunc_gep1@l
.Lfunc_lep1:
	.localentry	main, .Lfunc_lep1-.Lfunc_gep1
# BB#0:
	mflr 0
	std 31, -8(1)
	std 0, 16(1)
	stdu 1, -112(1)
	mr 31, 1
	li 3, 3
	li 4, 0
	stw 4, 100(31)
	extsw 3, 3
	bl .Lfunc_lep0
	nop
	extsw 3, 3
	addi 1, 1, 112
	ld 0, 16(1)
	ld 31, -8(1)
	mtlr 0
	blr
	.long	0
	.quad	0
.Lfunc_end1:
	.size	main, .Lfunc_end1-.Lfunc_begin1

	.section	.toc,"aw",@progbits
.LC0:
	.tc g_foo[TC],g_foo
	.type	g_foo,@object           # @g_foo
	.data
	.globl	g_foo
	.p2align	2
g_foo:
	.long	2                       # 0x2
	.size	g_foo, 4
