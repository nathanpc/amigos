/**
 * ssize_t.h
 * A definition of ssize_t for platforms that lack it.
 *
 * @author Nathan Campos <nathan@innoveworkshop.com>
 */

#ifndef _SHIMS_SSIZE_T_H
#define _SHIMS_SSIZE_T_H

#ifndef ssize_t
	#ifdef _WIN32

		#include <windows.h>

		/* ssize_t */
		#ifdef LONG_PTR
			typedef LONG_PTR ssize_t;
		#else
			typedef LONG LONG_PTR;
			typedef signed __int64 ssize_t;
		#endif /* LONG_PTR */

	#endif /* _WIN32 */
#endif /* ssize_t */

#endif /* _SHIMS_SSIZE_T_H */
