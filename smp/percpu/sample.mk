# SPDX-License-Identifier: 0BSD
# smpboot_register_percpu_thread() lives in kernel/smpboot.c, which is built only
# under CONFIG_SMP; on a uniprocessor kernel the symbol is absent at link time.
SAMPLE_REQUIRED_CONFIGS := CONFIG_SMP
