#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void	die(char *s); /* in case of error */
int	dflag;
char	*expr;
long	tokval;
char	*code;


/* Trivial lexing. */

enum Token { TEof, TNum, TAdd, TMul, TLep, TRip, NToks };

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

enum { NProds = 7, NSyms = 4, NStates = 12, MaxStack = 100 };

struct Rule {
	char	*descr;
	int	arity;
	int	head;
} grammar[NProds] =
{
 [0] = { "A -> n",      1, 0 },
 [1] = { "A -> ( C )",  3, 0 },
 [2] = { "B -> A",      1, 1 },
 [3] = { "B -> B * A",  3, 1 },
 [4] = { "C -> B",      1, 2 },
 [5] = { "C -> C + B",  3, 2 },
 [6] = { "S -> C TEof", 2, 3 },
};

long reduce(int rule, long *t)
{
	if (dflag)
		printf("\t%s\n", grammar[rule].descr);

	switch(rule) {
	case 0:
	case 2:
	case 4:
		return t[0];
	case 1:
		return t[1];
	case 3:
		if (t[1] == '*')
			return t[0] * t[2];
		else
			return t[0] / t[2];
	case 5:
		if (t[1] == '+')
			return t[0] + t[2];
		else
			return t[0] - t[2];
	default:
		abort();
	}
}

enum { Error, Accept, Shift, Reduce };

#define Sh(x) ((x << 2) | Shift)
#define Rd(x) ((x << 2) | Reduce)

#define Type(x)  (x & 3)
#define Value(x) (x >> 2)

int atbl[NStates][NToks] =
{
 [ 0] = { [TLep] = Sh(1), [TNum] = Sh(2) },
 [ 1] = { [TLep] = Sh(1), [TNum] = Sh(2) },
 [ 2] = { [TRip] = Rd(0), [TAdd] = Rd(0), [TMul] = Rd(0), [TEof] = Rd(0) },
 [ 3] = { [TRip] = Rd(2), [TAdd] = Rd(2), [TMul] = Rd(2), [TEof] = Rd(2) },
 [ 4] = { [TRip] = Rd(4), [TAdd] = Rd(4), [TMul] = Sh(7), [TEof] = Rd(4) },
 [ 5] = { [TAdd] = Sh(8), [TEof] = Accept },
 [ 6] = { [TRip] = Sh(9), [TAdd] = Sh(8) },
 [ 7] = { [TLep] = Sh(1), [TNum] = Sh(2) },
 [ 8] = { [TLep] = Sh(1), [TNum] = Sh(2) },
 [ 9] = { [TRip] = Rd(1), [TAdd] = Rd(1), [TMul] = Rd(1), [TEof] = Rd(1) },
 [10] = { [TRip] = Rd(3), [TAdd] = Rd(3), [TMul] = Rd(3), [TEof] = Rd(3) },
 [11] = { [TRip] = Rd(5), [TAdd] = Rd(5), [TMul] = Sh(7), [TEof] = Rd(5) },
};
int gtbl[NStates][NSyms] =
{
 /*        A   B   C */
 [ 0] = {  3,  4,  5 },
 [ 1] = {  3,  4,  6 },
 [ 7] = { 10,  0,  0 },
 [ 8] = {  3, 11,  0 },
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


/* Main. */

int main(int ac, char *av[])
{
	int o;
	long res;

	while ((o = getopt(ac, av, "d")) != -1) switch (o) {
	case 'd': dflag = 1; break;
	default:
		die("usage: exp [-d] [ARITHEXP]\n");
	}

	expr = optind < ac ? av[optind] : "1 + 1";

	res = parse();
	printf("%ld\n", res);
	exit(0);
}


/* Tools. */

void die(char *s)
{
	fputs(s, stderr);
	exit(1);
}
