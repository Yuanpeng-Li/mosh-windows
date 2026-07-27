/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

#ifndef UNIWIDTH_HPP
#define UNIWIDTH_HPP

/* How many columns a character occupies.
 *
 * Same contract as wcwidth(3):
 *    2  wide (East Asian Wide and Fullwidth)
 *    1  ordinary
 *    0  combining, zero-width
 *   -1  unprintable: controls, unassigned code points, surrogates
 *
 * src/terminal/terminal.cc asserts on anything outside that set.
 *
 * Why not just call wcwidth(): MSVC does not have it, and its wchar_t is 16
 * bits, so a code point above U+FFFF could not be passed even if it did.
 *
 * Why this is compiled on every platform, not only Windows: mosh's diff
 * protocol requires the client and the server to agree on how wide each
 * character is. If they disagree, everything after the first divergent wide
 * character is placed in the wrong column, and nothing reports an error --
 * the screens simply differ. Using the platform's wcwidth() on one side and a
 * table on the other would build that disagreement in by construction.
 *
 * The table is a snapshot of glibc's wcwidth (see uniwidth_table.h and
 * scripts/gen-uniwidth.py). That does mean a peer running a much newer glibc
 * can classify recently assigned code points differently; src/tests/uniwidth
 * measures the gap against the local C library and reports it.
 */

#include <cstddef>

namespace Util {

int uniwidth( char32_t wc );

}

#endif
