#!sh
# SPDX-License-Identifier: GPL-2.0-or-later
for i in *.vert; do
	echo $i
	./makeinc_glsl.sh $i
done
for i in *.frag; do
	echo $i
	./makeinc_glsl.sh $i
done
