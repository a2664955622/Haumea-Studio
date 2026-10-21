#!sh
# SPDX-License-Identifier: GPL-2.0-or-later
ext=${1##*.}
name=${1%.*}
(echo "const char *${name}_${ext}_src =";\
sed 's/..*/"&\\n"/' $1;\
echo ';') > ${name}_${ext}.inc
