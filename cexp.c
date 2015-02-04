#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef UDIS
#include "lib/udis86.h"
void	disas(uint8_t *, size_t); /* debug disassembler function */
#endif

void	oadd(), osub(), omul(), odiv(), oparam(long), oload(long); /* JIT */
void	die(char *s); /* in case of error */
int	dflag;
char	*expr;
long	tokval;
char	*code;


/* Trivial lexing. */

enum Token { TEof, TNum, TPrm, TAdd, TMul, TLep, TRip, NToks };

enum Token lex()
{
	expr += strspn(expr, " \t\r\n");
	if (isdigit(*expr)) {
		tokval = strtol(expr, &expr, 0);
		return TNum;
	}
	switch ((tokval = *expr++)) {
	default:
		die("lexing error\n");
	case 'x':
	case 'y':
	case 'z':
		return TPrm;
	case '+':
	case '-':
		return TAdd;
	case '*':
	case '/':
		return TMul;
	case '(':
		return TLep;
	case ')':
		return TRip;
	case 0:
		return TEof;
	}
}

/* LR Parsing. */

enum { NProds = 8, NSyms = 4, NStates = 13, MaxStack = 100 };

struct Rule {
	char	*descr;
	int	arity;
	int	head;
} grammar[NProds] =
{
 [0] = { "A -> n",      1, 0 },
 [1] = { "A -> x",      1, 0 },
 [2] = { "A -> ( C )",  3, 0 },
 [3] = { "B -> A",      1, 1 },
 [4] = { "B -> B * A",  3, 1 },
 [5] = { "C -> B",      1, 2 },
 [6] = { "C -> C + B",  3, 2 },
 [7] = { "S -> C TEof", 2, 3 },
};

long reduce(int rule, long *t)
{
	if (dflag)
		printf("\t%s\n", grammar[rule].descr);

	switch(rule) {
	case 0:
		oload(t[0]);
		break;
	case 1:
		oparam(t[0]);
		break;
	case 4:
		t[1] == '*' ? omul() : odiv();
		break;
	case 6:
		t[1] == '+' ? oadd() : osub();
		break;
	}
	return 0;
}

enum { Error, Accept, Shift, Reduce };

#define Sh(x) ((x << 2) | Shift)
#define Rd(x) ((x << 2) | Reduce)

#define Type(x)  (x & 3)
#define Value(x) (x >> 2)

int atbl[NStates][NToks] =
{
 [ 0] = { [TLep] = Sh(1), [TNum] = Sh(2), [TPrm] = Sh(3) },
 [ 1] = { [TLep] = Sh(1), [TNum] = Sh(2), [TPrm] = Sh(3) },
 [ 2] = { [TRip] = Rd(0), [TAdd] = Rd(0), [TMul] = Rd(0), [TEof] = Rd(0) },
 [ 3] = { [TRip] = Rd(1), [TAdd] = Rd(1), [TMul] = Rd(1), [TEof] = Rd(1) },
 [ 4] = { [TRip] = Rd(3), [TAdd] = Rd(3), [TMul] = Rd(3), [TEof] = Rd(3) },
 [ 5] = { [TRip] = Rd(5), [TAdd] = Rd(5), [TMul] = Sh(8), [TEof] = Rd(5) },
 [ 6] = { [TAdd] = Sh(9), [TEof] = Accept },
 [ 7] = { [TRip] = Sh(10), [TAdd] = Sh(9) },
 [ 8] = { [TLep] = Sh(1), [TNum] = Sh(2), [TPrm] = Sh(3) },
 [ 9] = { [TLep] = Sh(1), [TNum] = Sh(2), [TPrm] = Sh(3) },
 [10] = { [TRip] = Rd(2), [TAdd] = Rd(2), [TMul] = Rd(2), [TEof] = Rd(2) },
 [11] = { [TRip] = Rd(4), [TAdd] = Rd(4), [TMul] = Rd(4), [TEof] = Rd(4) },
 [12] = { [TRip] = Rd(6), [TAdd] = Rd(6), [TMul] = Sh(8), [TEof] = Rd(6) },
};
int gtbl[NStates][NSyms] =
{
 /*        A   B   C */
 [ 0] = {  4,  5,  6 },
 [ 1] = {  4,  5,  7 },
 [ 8] = { 11,  0,  0 },
 [ 9] = {  4, 12,  0 },
};

long parse()
{
	int stk[MaxStack], *s = stk;
	long toks[MaxStack], *t = toks;
	int act;
	enum Token tok;
	struct Rule *rl;

	tok = lex();
	*t = *s = 0;

	for (;;) {
		act = atbl[*s][tok];
		switch (Type(act)) {
		case Shift:
			s++; *s = Value(act);
			t++; *t = tokval;
			tok = lex();
			break;
		case Reduce:
			rl = &grammar[Value(act)];
			s -= rl->arity;
			t -= rl->arity;
			s++; *s = gtbl[*(s-1)][rl->head];
			t++; *t = reduce(Value(act), t);
			break;
		case Accept:
			return *t;
		case Error:
			die("parse error\n");
		}
	}
}


/* Code genration.
 * Reference: http://goo.gl/gjLrYt
 */

void o(char *s, int count)
{
	memcpy(code, s, count);
	code += count;
}
void oload(long n)
{
	int i;

	o("\x48\xb8", 2);             /* mov $n, %rax */
	for (i=0; i<8; i++, n /= 256)
		*code++ = n & 255;
	o("\x50", 1);                 /* push %rax */
}
void oparam(long p)
{
	switch (p) {
	default:
		die("invalid parameter\n");
	case 'x': o("\x57", 1); break; /* push %rdi */
	case 'y': o("\x56", 1); break; /* push %rsi */
	case 'z': o("\x52", 1); break; /* push %rdx */
	}
}
void oadd()
{
	o("\x58", 1);                 /* pop %rax */
	o("\x48\x01\x04\x24", 4);     /* add %rax, (%rsp) */
}
void osub()
{
	o("\x58", 1);                 /* pop %rax */
	o("\x48\x29\x04\x24", 4);     /* sub %rax, (%rsp) */
}
void omul()
{
	o("\x58", 1);                 /* pop %rax */
	o("\x48\x0f\xaf\x04\x24", 5); /* imul (%rsp), %rax */
	o("\x48\x89\x04\x24", 4);     /* mov %rax, (%rsp) */
}
void odiv()
{
	o("\x59", 1);                 /* pop %rcx */
	o("\x58", 1);                 /* pop %rax */
	o("\x33\xd2", 2);             /* xor %edx, %edx */
	o("\x48\xf7\xf9", 3);         /* idiv %rcx */
	o("\x50", 1);                 /* push %rax */
}
void oret()
{
	o("\x58", 1);                 /* pop %rax */
	o("\xc3", 1);                 /* ret */
}


/* Main. */

int main(int ac, char *av[])
{
	int o;
	long x, y, z, res;
	long (*f)(long, long, long);

	dflag = 0;
	x = y = z = 0;

	while ((o = getopt(ac, av, "dx:y:z:")) != -1) switch (o) {
	case 'd': dflag = 1; break;
	case 'x': x = strtol(optarg, 0, 0); break;
	case 'y': y = strtol(optarg, 0, 0); break;
	case 'z': z = strtol(optarg, 0, 0); break;
	default:
		die("usage: exp [-d] [-x X] [-y Y] [-z Z] [ARITHEXP]\n");
	}

	expr = optind < ac ? av[optind] : "1 + 1";

	f = mmap(0, 4096, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	code = (char *)f;
	parse(); oret();

#ifdef UDIS
	if (dflag)
		disas((uint8_t *)f, code - (char *)f);
#endif

	res = f(x, y, z);
	printf("%ld\n", res);
	exit(0);
}


/* Tools. */

void die(char *s)
{
	fputs(s, stderr);
	exit(1);
}

#ifdef UDIS
void disas(uint8_t *p, size_t count)
{
	ud_t ud;

	printf("Assembly dump:\n");
	ud_init(&ud);
	ud_set_input_buffer(&ud, p, count);
	ud_set_mode(&ud, 64);
	ud_set_syntax(&ud, UD_SYN_ATT);
	while (ud_disassemble(&ud))
		printf("\t%s\n", ud_insn_asm(&ud));
}
#endif
