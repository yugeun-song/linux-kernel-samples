#!/bin/sh
# SPDX-License-Identifier: 0BSD
set -eu

sign_file=$1
key=$2
cert=$3
ko=$4

if [ ! -x "$sign_file" ]; then
	echo "sign-module: $sign_file not found -- leaving $(basename "$ko") UNSIGNED" >&2
	exit 0
fi

if [ ! -f "$key" ] || [ ! -f "$cert" ]; then
	if ! command -v openssl >/dev/null 2>&1; then
		echo "sign-module: openssl missing and no key present -- leaving $(basename "$ko") UNSIGNED" >&2
		exit 0
	fi
	echo "sign-module: generating a local signing key ($key)"
	openssl req -new -x509 -newkey rsa:2048 -nodes -days 36500 \
		-keyout "$key" -outform DER -out "$cert" \
		-subj "/CN=local module signing/" >/dev/null 2>&1
fi

"$sign_file" sha256 "$key" "$cert" "$ko"
echo "sign-module: signed $(basename "$ko") -- enroll with 'sudo mokutil --import $cert' (reboot) to load under Secure Boot"
