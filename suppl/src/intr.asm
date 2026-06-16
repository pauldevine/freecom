; File:
;                         intr.asm
; Description:
;       Assembly implementation of calling an interrupt
;
;                    Copyright (c) 2000
;                       Steffen Kaiser
;                       All Rights Reserved
;
; This file is part of FreeDOS.
;
; FreeDOS is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation; either version
; 2, or (at your option) any later version.
;
; DOS-C is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
; the GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public
; License along with DOS-C; see the file COPYING.  If not,
; write to the Free Software Foundation, Inc.,
; 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
;

%include "model.inc"

%ifidn __OUTPUT_FORMAT__, elf 	; only for ia16-elf-gcc compilations
%define COMPILE 1

segment .text

bits 16

global intrf
global _intrf

intrf:
_intrf:

%elifidni COMPILER, WATCOM 	; and Open Watcom
%define COMPILE 1

segment _TEXT

global intrf_
global _intrf_

intrf_:
_intrf_:
%endif

%ifdef COMPILE
		push	bp			; Standard C entry
%ifidn __OUTPUT_FORMAT__, elf
		push	es			; gcc-ia16 has es caller-saved
%endif
		push	bx
		push	cx
		mov	bx, dx			; BX = REGPACK pointer (from DX via regparmcall)
		push	dx
		push	si
		push	di
		push	ds
%ifidn __OUTPUT_FORMAT__, elf
		; Save interrupt number (in AL) to code segment for later dispatch
		; This avoids self-modifying code issues
		mov	[cs:.saved_int_num], al
%endif
		mov	ah, [bx+18]		; SZAPC flags
		sahf
		mov	ax, [bx]
		mov	cx, [bx+4]
		mov	dx, [bx+6]
		mov	bp, [bx+8]
		mov	si, [bx+10]
		mov	di, [bx+12]
		push	word [bx+14]		; push ds value from REGPACK
		mov	es, [bx+16]
		mov	bx, [bx+2]
		pop	ds			; ds = REGPACK.r_ds
%ifidn __OUTPUT_FORMAT__, elf
		; Dispatch to the correct INT based on saved value
		; Use compare chain - most common interrupts first
		cmp	byte [cs:.saved_int_num], 0x21
		je	.do_int_21
		cmp	byte [cs:.saved_int_num], 0x10
		je	.do_int_10
		cmp	byte [cs:.saved_int_num], 0x2F
		je	.do_int_2f
		; Default to INT 21h for unknown (shouldn't happen in FreeCOM)
		jmp	short .do_int_21

.do_int_10:
		int	0x10
		jmp	short .after_int

.do_int_2f:
		int	0x2F
		jmp	short .after_int

.do_int_21:
		int	0x21
		; fall through to .after_int

.after_int:
		jmp	short intr_1		; skip over data

.saved_int_num:	db	0			; storage for interrupt number
%else
		int	0
%endif
intr_1:
		pushf
		push	ds
		push	bx
		mov	bx, sp
		mov	ds, [ss:bx+6]
		mov	bx, [ss:bx+12]		; address of REGPACK
		mov	[bx], ax
		pop	word [bx+2]
		mov	[bx+4], cx
		mov	[bx+6], dx
		mov	[bx+8], bp
		mov	[bx+10], si
		mov	[bx+12], di
		pop	word [bx+14]
		mov	[bx+16], es
		pop	word [bx+18]

		pop	ds
		pop	di
		pop	si
		pop	dx
		pop	cx
		pop	bx
%ifidn __OUTPUT_FORMAT__, elf
		pop	es
%endif
		pop	bp
		ret					; retf/retn model specific, see model.inc
%endif
