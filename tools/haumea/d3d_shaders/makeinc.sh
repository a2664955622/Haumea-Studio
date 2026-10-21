#!sh
# SPDX-License-Identifier: GPL-2.0-or-later
for i in *cso; do
	(echo -n 'static '
	xxd -i $i | grep -v '_len = ') > ${i%cso}inc
done
