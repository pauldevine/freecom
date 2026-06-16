/*	$Id$

	Enable DOS/APPEND if it was enabled before.

	void appendRestore(int state)

	Enable DOS/APPEND if state & 1

	$Log$
	Revision 1.2  2004/07/12 18:48:38  skaus
	fix: appendDisable(): should disable APPEND [Eduardo Casino]

	Revision 1.1  2004/06/21 17:49:26  skaus
	fix: DIR: disable APPEND.EXE during DIR processing {Eduardo Almao}
	
 */

#include "../config.h"

#include <assert.h>
#include <dos.h>

#include "../include/misc.h"

void appendRestore(const int state)
{	if(state & 1) {		/* Re-enable DOS/APPEND */
		IREGS r;

		/* Initialize all IREGS fields to avoid garbage causing crashes */
		r.r_ax = 0xb707;		/* Set APPEND function state */
		r.r_bx = state;
		r.r_cx = 0;
		r.r_dx = 0;
		r.r_bp = 0;
		r.r_si = 0;
		r.r_di = 0;
		r.r_ds = 0;
		r.r_es = 0;
		r.r_flags = 0;
		intrpt(0x2f, &r);

		dprintf(("MUX-B7: set state 0x%04x]\n", state) );
	}
}
