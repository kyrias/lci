/* Functions that are used to "execute" terms or commands

	Copyright (C) 2004-8 Kostas Chatzikokolakis
	This file is part of LCI

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details. */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#endif
#ifdef USE_IOCTL
#include <unistd.h>
#include <sys/ioctl.h>
#include <termio.h>
#endif

#include "run.h"
#include "parser.h"
#include "termproc.h"
#include "decllist.h"


int trace;
int options[OPTNO] = {0, 0, 0, 0, 1};

#ifndef NDEBUG
extern int freeNo;
#endif
int execTerm(TERM *t) {
	int redno = -1, res = 1,
		showExec = getOption(OPT_SHOWEXEC);
	int retval = 0;
	long stime = clock();
	char c;

	trace = getOption(OPT_TRACE);

	//�������� ��� operators ���� ��� ��������
	termRemoveOper(t);

	// calculate closed flag for all sub-terms (must be done after termRemoveOper)
	termSetClosedFlag(t);
#ifndef NDEBUG
	freeNo = 0;
#endif

	switch(execSystemCmd(t)) {
	 case 1:
		//kata thn diarkeia ths ekteleshs ta shmata SIGNT energopoioyn to trace
		signal(SIGINT, sigHandler);

		//kanoyme ola ta reductions
		do {
			redno++;

			if(trace) {
#ifdef USE_READLINE
				// calling rl_prep_terminal allows to read a single character
				// from stdin (without waiting for <return>)
				rl_prep_terminal(1);
#endif
#ifdef USE_IOCTL
				//allazontas tis parametrous sto tio kanoyme thn getchar na epistrefei
				//ameses meta ton prwto xarakthra (xwris enter) kratame tis palies
				//parametrous sto oldtio gia epanafora argotera.
				struct termio tio, oldtio;
				ioctl(0, TCGETA, &tio);						//read settings
				oldtio = tio;									//save settings
				tio.c_lflag &= ~(ICANON | ECHO);			//changes settings
				tio.c_cc[VMIN] = 1;
				tio.c_cc[VTIME] = 0;
				ioctl(0, TCSETA, &tio);						//set new settings
#endif

				termPrint(t, 1);
				printf("  ?> ");
				fflush(stdout);
				do {
#ifdef USE_READLINE
					switch(c = rl_read_key()) {
#else
					switch(c = getchar()) {
#endif
					 case 'c':
						 printf("continue\n");
						 trace = 0;
						 break;
					 case 's':
						 printf("step\n");
						 break;
					 case 'a':
						 printf("abort\n");
						 break;
					 default:
						 c = '?';
						 printf("\nCommands: (s)tep, (c)ontinue, (a)bort  ?> ");
						 fflush(stdout);
					}
				} while(c == '?');

				//epanafora ry8misewn termatikoy
#ifdef USE_READLINE
				rl_deprep_terminal();
#endif
#ifdef USE_IOCTL
				ioctl(0, TCSETA, &oldtio);
#endif

				if(c == 'a') break;

			} else if(showExec) {
				termPrint(t, 1);
				printf("\n");
			}
		} while((res = termConv(t)) == 1);

		//An h metatroph teleiwse kanonika ektypwsh
		if(res == 0) {
			printf("\n");
			termPrint(t, 1);
			printf("\n(%d reductions, %.2fs CPU)\n",
				redno, (double)(clock()-stime) / CLOCKS_PER_SEC);
#ifndef NDEBUG
			printf("%d termIsFree's\n", freeNo);
#endif
		}

		//Epanafora toy default handler
		signal(SIGINT, SIG_DFL);

		break;

	 case -1:
		fprintf(stderr, "Error: Missformed sytem command. Type Help for info.\n");
		break;

	 case -2:
		// Quit command. Return 1 to exit the program
		retval = 1;
		break;

	 default:
		break;
	}

	//������������ ������
	termFree(t);
	termGC();					// call terms garbageCollector

	return retval;
}

// execSystemCmd
//
// ������� �� � ���� t ����� ������ ���������� ��� ���� ��������� ���� �������
// ��� ������. �� ������� ���������� ����� ��� ����� Cmd param1 param2 ...
// ����������
//		1	�� � ���� ��� ����� ������ ����������
//		0	�� � ���� ����� ������ ���������� ��� ����������� ��������
//		-1 �� � ���� ����� ������ ���������� ���� ������ ������ ������ ���� ��� ��������
//		-2 �� � ���� ����� ������ ���������� ��� ���������� �� ��������� (Quit)

int execSystemCmd(TERM *t) {
	TERM *stack[10], **sp = stack, *par;
	int parno = 0;

	//������ ������������� ���� ��� ������ ��� ���������� ��� ������
	while(t->type == TM_APPL) {
		*sp++ = t->rterm;
		parno++;
		t = t->lterm;
	}

	//� ������������� ���� ������ �� ����� ������ ��� �� �������������� ALIASES
	if(t->type != TM_ALIAS)
		return 1;

	if(strcmp(t->name, "DefOp") == 0) {
		// DefOp name preced assoc
		//
		// ��������� �� ������ ���� operator
		char *oper;
		int prec;
		ASS_TYPE ass;

		if(parno != 3) return -1;

		//���������� 1: ��������
		par = *--sp;
		if(par->type != TM_ALIAS) return -1;
		oper = par->name;

		//���������� 2: �������������
		par = *--sp;
		prec = termNumber(par);
		if(prec == -1) return -1;

		//���������� 3: �������������������
		par = *--sp;
		if(par->type != TM_ALIAS && par->type != TM_VAR) return -1;
		if(strcmp(par->name, "xfx") == 0)
			ass = ASS_NONE;
		else if(strcmp(par->name, "yfx") == 0)
			ass = ASS_LEFT;
		else if(strcmp(par->name, "xfy") == 0)
			ass = ASS_RIGHT;
		else
			return -1;
	
		//���������� ��� ������� ��� operator
		addOper(strdup(oper), prec, ass);

	} else if(strcmp(t->name, "ShowAlias") == 0) {
		// ShowAlias
		//
		// ������� ��� ������ ���� ��� ������������� aliases � ����
		// ������������� �� ����� �� ����������
		char *id = NULL;

		if(parno != 0 && parno != 1) return -1;
		if(parno == 1) {
			par = *--sp;
			if(par->type != TM_ALIAS) return -1;
			id = par->name;
		}

		printDeclList(id);

	} else if(strcmp(t->name, "Print") == 0) {
		// Print
		//
		// ������� ��� ��� ��� ������� �� ����������
		if(parno != 1) return -1;

		termPrint(*--sp, 1);
		printf("\n");

	} else if(strcmp(t->name, "FixedPoint") == 0) {
		// FixedPoint
		//
		// ������� ��� �������� ��� �� aliases �� �� ����� ���� fixed point combinator
		int n = 0;

		if(parno != 0) return -1;

		while(findCycle())
			n++;

		if(n > 0)
			printf("%d cycles removed using fixed point combinator Y.\n", n);
		else
			printf("No cycles found\n");

	} else if(strcmp(t->name, "Consult") == 0) {
		// Consult file
		//
		// �������� ��� ������ ��� ������� ��� �������
		if(parno != 1) return -1;

		par = *--sp;
		if(par->type != TM_ALIAS && par->type != TM_VAR) return -1;

		switch(consultFile(par->name)) {
		 case 0:
			printf("Successfully consulted %s\n", par->name);
			break;
		 case -1:
			printf("Error: cannot open %s\n", par->name);
			break;
		}

	} else if(strcmp(t->name, "Set") == 0) {
		// Set option value
		//
		// Apouhkeyei thn timh ths parametroy
		OPT opt;
		int value;

		if(parno != 2) return -1;

		par = *--sp;
		if(par->type != TM_VAR) return -1;
		if(strcmp(par->name, "trace") == 0)
			opt = OPT_TRACE;
		else if(strcmp(par->name, "showpar") == 0)
			opt = OPT_SHOWPAR;
		else if(strcmp(par->name, "greeklambda") == 0)
			opt = OPT_GREEKLAMBDA;
		else if(strcmp(par->name, "showexec") == 0)
			opt = OPT_SHOWEXEC;
		else if(strcmp(par->name, "readable") == 0)
			opt = OPT_READABLE;
		else
			return -1;

		par = *--sp;
		if(strcmp(par->name, "on") == 0)
			value = 1;
		else if(strcmp(par->name, "off") == 0)
			value = 0;
		else return -1;

		options[opt] = value;

	} else if(strcmp(t->name, "Help") == 0) {
		// Help
		//
		// �������� ��������� ��������
		printf("\nlci - A lambda calculus interpreter\n\n");
		printf("Type a lambda term to compute its normal form\n");
		printf("or enter one of the following system commands:\n\n");

		printf("FixedPoint\t\tRemoves recursion using fixed point comb. Y\n");
		printf("DefOp name prec ass\tDefines an operator\n");
		printf("ShowAlias [name]\tList the specified or all stored aliases\n");
		printf("Print term\t\tDisplays the term\n");
		printf("Consult file\t\tReads and interprets the specified file\n");
		printf("Set option (on|off)\tChanges one of the following options:\n\t\t\ttrace, showexec, showpar, greeklambda, readable\n");
		printf("Help\t\t\tDisplays this message\n");
		printf("Quit\t\t\tQuit the program (same as Ctrl-D)\n");

		printf("\nCopyright (C) 2006  Kostas Chatzikokolakis\n\n");

		printf("This program is free software; you can redistribute it and/or modify\n");
		printf("it under the terms of the GNU General Public License as published by\n");
		printf("the Free Software Foundation; either version 2 of the License, or\n");
		printf("(at your option) any later version.\n\n");

		printf("This program is distributed in the hope that it will be useful,\n");
		printf("but WITHOUT ANY WARRANTY; without even the implied warranty of\n");
		printf("MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n");
		printf("GNU General Public License for more details.\n\n");

		printf("You should have received a copy of the GNU General Public License\n");
		printf("along with this program; if not, write to the Free Software\n");
		printf("Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n\n");

	} else if(strcmp(t->name, "Quit") == 0) {
		if(parno != 0) return -1;
		return -2;

	} else
		//�� alias ��� ����� ������ ��� �� ��������������
		return 1;

	return 0;
}

// consultFile
//
// Reads and executes fname file
// Return value:
//   0   ok
//   -1  cannot open file
//   -2  syntax error

int consultFile(char *fname) {
	FILE *f;

	if(!(f = fopen(fname, "r")))
		return -1;

	//parse file
	scInputType = SC_FILE;
	scInput = f;
	getToken(NULL);

	if(parse(NULL, -1) != PAR_OK) {
		fprintf(stderr, "Error: syntax error in line %d.\n", scLineNo);
		fclose(f);
		return -2;
	}

	fclose(f);
	return 0;
}

// getOption
// 
// Epistrefei thn timh ths perametroy o
int getOption(OPT opt) {
	return options[opt]; 
}

// sigHandler
//
// Handler ��� ������� SIGINT ���� �� ������� ��� ���������.
// ����������� �� ���������� trace

void sigHandler(int sig) {
	trace = 1;

	//sta suns prin thn klhsh ths synarthshs epaneferetai o handler
	//se SIG_DFL. Etsi prepei na 3anaenergopoih8ei
	signal(SIGINT, sigHandler);
}

