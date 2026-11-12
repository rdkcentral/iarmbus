#!/bin/sh
set -eu

if [ -f "${srcdir:-.}/stubs/safec_lib.h" ]; then
	root="${srcdir:-.}"
elif [ -f "${srcdir:-.}/../stubs/safec_lib.h" ]; then
	root="${srcdir:-.}/.."
else
	test_srcdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
	root="$test_srcdir/.."
fi

header="$root/stubs/safec_lib.h"
core_makefile="$root/core/Makefile.am"
coverage_script="$root/cov_build.sh"

if grep -Fq '#define SAFEC_DUMMY_API 1' "$header"; then
	exit 1
fi
grep -Fq '#if defined(SAFEC_DUMMY_API) && !defined(IARM_ALLOW_DUMMY_SAFEC)' "$header"
if grep -Fq 'SAFEC_DUMMY_API' "$core_makefile"; then
	exit 1
fi
grep -Fq -- '-DSAFEC_DUMMY_API=1 -DIARM_ALLOW_DUMMY_SAFEC=1' "$coverage_script"